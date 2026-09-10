# Grbl v1.1h — Development Plan

A staged plan covering toolchain setup, hardware bring-up, defect remediation,
and future work for this fork of `gnea/grbl` v1.1h (build 20190830).

## Status

**Every item in §3 "Fixes" is done except 3.10 and 4.3**, both deliberately
left open (see their entries for why — 3.10 needs a design pass, 4.3 needs
real hardware CI can't provide). Commits:

| Commit | What landed |
|---|---|
| `3a26f95` | Phase 1 (all of 1.1–1.6); Fixes 2.2, 3.3, 3.6, 3.8, 3.9 |
| `5c182fc` | Fix 4.1 (host test harness, `test/`); Fixes 3.2, 3.5, 3.7 |
| `6dff3d6` | Fixes 3.1, 2.1, 3.4 |
| *(this session)* | Fixes 4.2, 4.4 (`make size`; GitHub Actions CI) |

Firmware still builds clean on the real AVR toolchain, `make test` still
passes 39/39, and the tree still compiles with zero warnings. CI now runs
both automatically on every push/PR to `main` — see the badge at the top of
`README.md`. Current baseline: see §1.4.

**What's left** is one deferred design task (3.10), one validation task that
needs a real board (4.3), and everything in §4 "Upgrades and new features" —
which was always intended as post-fix work. Full detail and suggested order
are at the end of §3, in **Remaining work**.

---

## 1. Toolchain

### 1.1 You already have it

The AVR toolchain is **already installed on this machine**, bundled with the
Arduino IDE's AVR core. Nothing needs to be downloaded:

| Tool | Version | Location |
|---|---|---|
| `avr-gcc` (+ `avr-objcopy`, `avr-size`, `avr-objdump`) | 7.3.0 | `%LOCALAPPDATA%\Arduino15\packages\arduino\tools\avr-gcc\7.3.0-atmel3.6.1-arduino7\bin` |
| `avrdude` | 6.3.0 | `%LOCALAPPDATA%\Arduino15\packages\arduino\tools\avrdude\6.3.0-arduino17\bin` |
| `avrdude.conf` | — | `...\avrdude\6.3.0-arduino17\etc\avrdude.conf` |

`make` is already on PATH from the scoop mingw package.

**Step 1 — put the toolchain on PATH.** For a single Git Bash session:

```sh
export PATH="$LOCALAPPDATA/Arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin:$PATH"
export PATH="$LOCALAPPDATA/Arduino15/packages/arduino/tools/avrdude/6.3.0-arduino17/bin:$PATH"
avr-gcc --version   # expect: avr-gcc.exe (GCC) 7.3.0
```

To make it permanent, add both directories to your user PATH via
**Settings → System → About → Advanced system settings → Environment Variables**,
or in PowerShell:

```powershell
$avr = "$env:LOCALAPPDATA\Arduino15\packages\arduino\tools"
$new = "$env:PATH;$avr\avr-gcc\7.3.0-atmel3.6.1-arduino7\bin;$avr\avrdude\6.3.0-arduino17\bin"
[Environment]::SetEnvironmentVariable("PATH", $new, "User")
```

### 1.2 If you ever need a fresh install

Only relevant on a new machine, or if you want a newer GCC than 7.3.0.

**Windows** — pick one:

```powershell
# Option A: Arduino IDE (simplest; installs the same toolchain as above)
winget install ArduinoSA.IDE.stable

# Option B: Chocolatey
choco install avrdude
# (choco has no maintained avr-gcc package; use Option A or C for the compiler)

# Option C: Microchip AVR 8-bit Toolchain (newest GCC, standalone)
#   https://www.microchip.com/en-us/tools-resources/develop/microchip-studio/gcc-compilers
#   Unzip, add its bin\ to PATH. Pair with avrdude from Option A or B.
```

You also need `make`. You already have it via scoop; otherwise
`scoop install make` or `choco install make`.

**Linux:** `sudo apt install gcc-avr avr-libc avrdude make`
**macOS:** `brew tap osx-cross/avr && brew install avr-gcc avrdude make`

> **Note on newer GCC:** the Makefile comments warn that avr-gcc 4.8.1 emits
> `-flto` warnings. GCC 7.3.0 does not. Much newer toolchains (GCC 12+) may
> generate slightly different code sizes — re-check the `avr-size` output
> against §1.4 before flashing, because flash headroom is thin.

### 1.3 Build

```sh
cd "C:/Users/David Lyman Dawes/play/grbl"
make                # -> grbl.hex, objects in build/
make clean
```

Expected output ends with an `avr-size` summary. **The build is silent under
`-Wall`** — the tree's one warning (the `eeprom.c` checksum bug, Fix 2.1) is
fixed. If you see this pair again, something has regressed:

```
grbl/eeprom.c:133:26: warning: '<<' in boolean context, did you mean '<' ?
grbl/eeprom.c:144:26: warning: '<<' in boolean context, did you mean '<' ?
```

### 1.4 Verified baseline (stock `config.h`, ATmega328P @ 16 MHz)

Original upstream tree, before any fix in this plan:

```
   text	   data	    bss	    dec	    hex	filename
  29762	      0	   1633	  31395	   7aa3	build/main.elf
```

**Current** (every fix in §3 except 3.10 applied):

```
   text	   data	    bss	    dec	    hex	filename
  29916	      0	   1633	  31549	   7b3d	build/main.elf
```

| Resource | Used (current) | Available | Free | Headroom |
|---|---|---|---|---|
| Flash (Uno, 512 B Optiboot) | 29,916 | 32,256 | **2,340 B** | 7.3 % |
| Flash (old Nano, 2 KB bootloader) | 29,916 | 30,720 | **804 B** | 2.6 % |
| SRAM (`.bss`, before stack) | 1,633 | 2,048 | **415 B** | 20 % |

**This is the single most important constraint in this document.** All eleven
done fixes (2.1, 2.2, 3.1–3.9) together cost +154 bytes of flash net (2.1's
rewrite of the checksum loop is actually 36 bytes *smaller* than the bug it
replaced, and 2.2/3.4 cost 0 in the default build), zero SRAM. Every fix and
feature below is still annotated with its cost. On an old-bootloader Nano
there is now under 800 bytes to work with, and the 415 bytes of SRAM must
cover the entire call stack *plus* nested ISR frames.

### 1.5 Flash

Find your board's COM port (Device Manager, or `mode` in cmd), then:

```sh
# Uno / Nano (new bootloader) over USB — no external programmer needed
avrdude -C "$LOCALAPPDATA/Arduino15/packages/arduino/tools/avrdude/6.3.0-arduino17/etc/avrdude.conf" \
        -p atmega328p -c arduino -P COM3 -b 115200 -D -U flash:w:grbl.hex:i

# Old Nano bootloader: -b 57600
# Via the Makefile with an ISP programmer:
make DEVICE=atmega328p PROGRAMMER="-c avrisp2 -P usb" flash
```

> **Do not run `make fuse`** unless you are using an ISP programmer and
> understand the values. The Makefile's `FUSES` line sets `lfuse:0xff`, which
> selects an external crystal — wrong fuses on a bare chip will brick it until
> you supply an external clock. `make install` runs `flash` **and** `fuse`;
> prefer plain `make flash`.

### 1.6 First contact

Connect at **115200 8-N-1** (PuTTY, `screen`, Arduino Serial Monitor with line
ending set to Newline, or a GUI — see §2.6). You should see:

```
Grbl 1.1h ['$' for help]
```

Sanity checklist, in order: `$$` (dump settings) → `$I` (build info) → `$G`
(parser state) → `?` (status report). **Do not connect motors yet.**

---

## 2. Hardware

Grbl drives **step/direction stepper drivers** — it does not drive motor coils
itself. Everything below assumes the stock `config.h` pin map (README has the
full table).

### 2.1 Minimum viable bench setup

| Item | Recommendation | Notes |
|---|---|---|
| Controller | Arduino **Uno R3** | The officially supported target. Avoid the Uno R4 — different MCU entirely, this firmware will not run on it. |
| Breakout | **Protoneer CNC Shield v3.51** or a generic "CNC Shield v3.00" clone | Plugs straight onto the Uno, matches Grbl's pinout, has driver sockets and screw terminals. The v3.51 is also the board `DUAL_AXIS_CONFIG_PROTONEER_V3_51` targets. |
| Drivers (×3–4) | **DRV8825** (1/32 µstep, up to 2.2 A) or **TMC2209** (quiet, sensorless options) | A4988 is the classic budget choice (1/16, 2 A) but noisier. All are socket-compatible. |
| Motors | **NEMA 17**, 1.8°/step, 1.5–2 A/phase for desktop; NEMA 23 for larger gantries | Match driver current rating to the motor, not the other way round. |
| PSU | **24 V**, 5–10 A for NEMA 17 | 12 V works but caps top speed — stepper torque falls off with speed, and higher voltage lets current rise faster in the coils. |
| Limit switches | 3× mechanical microswitch (NC wiring) or inductive proximity | See §2.4. |
| E-stop | Latching mushroom button | See §2.5. |

Budget for a first build: roughly $60–120 for Uno + shield + 3 drivers + 3
NEMA 17s + PSU.

### 2.2 Wiring order (do this incrementally)

1. **Bare Uno, no shield.** Flash Grbl, confirm the welcome message and `$$`.
2. **Shield on, drivers in, no motors, no motor PSU.** Confirm Grbl still boots.
   Verify driver orientation — reversing a driver in its socket destroys it
   instantly. The EN pin marking on the driver must match the shield silkscreen.
3. **Set driver current (Vref) before connecting motors.** With the motor PSU on
   and motors *disconnected*, measure between the driver's trim pot and GND:
   - DRV8825: `Vref = current_limit / 2` → 1.5 A motor ≈ 0.75 V
   - A4988: `Vref = current_limit × 8 × R_sense` → typically `current / 2.5`
   - TMC2209: usually set in firmware/UART or by Vref depending on the board
   Start at ~70 % of the motor's rated current. Drivers get hot; heatsinks are
   not optional above ~1 A.
4. **One motor at a time.** Power down, connect X, power up, jog it. Verify
   direction and that it doesn't stall or scream. Repeat for Y, then Z.
5. **Limit switches.** Confirm with `?` — the `Pn:` field appears in the status
   report only while a pin is triggered. Trigger each switch by hand and watch
   for `Pn:X`, `Pn:Y`, `Pn:Z`.
6. **Homing.** Only after step 5 passes for every axis.
7. **Spindle/laser.** Last, and with the tool physically disconnected first.

> Motor coil pairs must be identified correctly. Twisting the wrong two wires
> together gives a motor that buzzes and doesn't turn. Use a multimeter: the two
> wires with continuity (a few ohms) are one coil.

### 2.3 Setting `$100`/`$101`/`$102` (steps/mm)

```
steps_per_mm = (motor_steps_per_rev × microsteps) / mm_per_revolution
```

With a 1.8° motor (200 steps/rev) at 1/8 microstepping (1600 µsteps/rev):

| Mechanism | mm per rev | steps/mm |
|---|---|---|
| GT2 belt, 20-tooth pulley | 40.0 | **40** |
| TR8×8 leadscrew (8 mm lead) | 8.0 | **200** |
| TR8×2 leadscrew (2 mm lead) | 2.0 | **800** |
| M8 threaded rod | 1.25 | **1280** |

**Cross-check against the 30 kHz step ceiling:** max feed in mm/min is
`30000 × 60 / steps_per_mm`. At 200 steps/mm that's 9,000 mm/min — fine. At 1280
steps/mm it's only 1,406 mm/min, and `$110` must be set at or below that or
you'll lose steps. Fix 3.2 (done) already rejects `$100`/`$101`/`$102 = 0`;
uncommenting `MAX_STEP_RATE_HZ` in `config.h` (still your choice, not changed
by 3.2 — see its entry below) additionally makes Grbl reject a steps/mm × max-rate
combination that would outrun the 30 kHz ceiling, instead of just capping speed.

Calibrate empirically: command `G91 G0 X100`, measure the actual travel, then
`new_$100 = old_$100 × (100 / measured)`.

### 2.4 Limit switches

- Wire **normally-closed (NC)** and set `$5=1` (invert limit pins). A broken
  wire then reads as a triggered limit — fail-safe — rather than as "all clear".
- Grbl uses internal pull-ups by default; switches go between the pin and GND.
- Both ends of an axis can share one pin (wire NC switches in series). Grbl
  can't tell which end tripped, but for hard limits that doesn't matter. If you
  do this, enable `LIMITS_TWO_SWITCHES_ON_AXES` in `config.h`.
- Add 0.1 µF caps pin-to-GND for debouncing, or enable
  `ENABLE_SOFTWARE_DEBOUNCE` (costs flash and claims the watchdog).
- `$21=1` enables hard limits; `$22=1` enables homing; `$20=1` enables soft
  limits (requires homing).

### 2.5 Safety — read before the first cut

- **Wire a real e-stop.** Grbl's own docs are explicit: do **not** put an e-stop
  on the limit pins, because the limit interrupt is disabled during homing.
  Wire the e-stop to the **Arduino reset pin**, and independently cut motor and
  spindle power through the same switch. Firmware is not a safety system.
- The `CONTROL_RESET_BIT` input (A0) is a soft reset, not an e-stop.
- Spindle/laser control pins are **logic-level outputs**. Never wire mains
  directly — use a properly rated relay, SSR, or VFD analog input, with
  optoisolation.
- Enabling `ENABLE_SAFETY_DOOR_INPUT_PIN` **takes over pin A1** — you get a
  safety door input *or* a feed-hold input, never both, because the 328P has no
  spare pins.
- For a laser: interlocks, correct-wavelength eyewear, and fume extraction.
  `$32=1` (laser mode) also changes motion behavior — read
  `doc/markdown/laser_mode.md` first.

### 2.6 Sender software

Grbl needs a host to stream G-code. Anything v1.1-aware:
**Universal Gcode Sender (UGS)**, **bCNC**, **Candle**, **cncjs**, or the
included `doc/script/stream.py` (character-counting protocol, the correct way to
stream) and `doc/script/simple_stream.py` (naive send-and-wait).

> v1.1 changed the status-report format. A sender written for v0.9 will
> misparse it.

---

## 3. Fixes

Fifteen defects from the code review, staged so that each phase is
independently shippable and testable. Flash costs are actual, measured deltas
against the 2,494-byte original Uno budget from §1.4, not estimates, for every
item marked done below.

### Phase 1 — Tooling and repo hygiene *(zero firmware risk, do first)* ✅ DONE

| # | Item | File | Change |
|---|---|---|---|
| 1.1 | Broken header-dependency tracking | `Makefile:106` | `-include $(BUILDDIR)/$(OBJECTS:.o=.d)` expands to `build/build/main.d` — make only prefixes the *first* word. Verified: every other `.d` resolves correctly, so only `main.c` silently misses header changes. Fixed to `-include $(OBJECTS:.o=.d)`. |
| 1.2 | Dead `disasm` target | `Makefile:99` | Depended on `main.elf`, not `$(BUILDDIR)/main.elf`. Prerequisite fixed. |
| 1.3 | Dead `.S.o` suffix rule | `Makefile:62` | Legacy suffix rule that could never fire alongside the pattern rules. Converted to a `$(BUILDDIR)/%.o: $(SOURCEDIR)/%.S` pattern rule. |
| 1.4 | No `build/` creation | `Makefile` | Previously worked only because `build/.gitignore` kept the directory in git. Added an order-only prerequisite and a `$(BUILDDIR): ; mkdir -p $@` rule. |
| 1.5 | Missing `.PHONY` | `Makefile` | Declared `all clean flash fuse install load disasm cpp test test-clean` phony. |
| 1.6 | `.gitignore` ignores `README.md` | `.gitignore:7` | Had no effect (the file was tracked) but was actively misleading. Line removed. |

**Verified:** `make clean && make` reproduces §1.4's original-baseline numbers
byte-for-byte; `touch grbl/config.h && make` now rebuilds `main.o`, confirmed
by inspecting which objects actually recompiled.

### Phase 2 — Compile-time correctness

**2.1 — EEPROM checksum uses `||` instead of `|`** ✅ DONE
`eeprom.c:133` and `eeprom.c:144` (now `:134`/`:145` after the fix). Fixed to
a real bitwise rotate:

```c
checksum = (checksum << 1) | (checksum >> 7);   // Rotate left 1 bit.
```

Verified two ways beyond the rebuild: (1) this was GCC 7.3's one and only
`-Wall` warning in the whole tree, and it's now gone — the fixed build is
silent; (2) a standalone extraction of both versions against an 8-byte buffer
with one byte corrupted showed the **old checksum missed the corruption**
(`81` == `81` for original vs. corrupted) while the **new one caught it**
(`159` vs. `175`) — concrete evidence the bug wasn't just a style nit, it
genuinely weakened corruption detection on every EEPROM read.

Shipped together with the migration hazard it requires:

> **Migration hazard, now live.** `SETTINGS_VERSION` (`settings.h:30`) was
> bumped **10 → 11**, with the reason recorded in a comment at the same
> location. On first boot after flashing this version, `read_global_settings()`
> fails its version check against whatever was previously stored, and
> `settings_init()` calls `settings_restore(SETTINGS_RESTORE_ALL)` — **every
> stored record is wiped to defaults: global `$` settings, G54–G59 work
> offsets, G28/G30 positions, startup lines, and build info.** This is the
> existing, intentional Grbl migration mechanism (the version-byte check
> exists for exactly this); nothing new was built for it. **Record `$$` and
> `$#` before flashing this version to a machine with settings that matter.**

Flash cost: **-36 bytes** (smaller, not larger — `|` compiles to one AVR
instruction; the old `||` needed extra code to synthesize proper C boolean
semantics from a bitwise-looking expression).

**2.2 — `ENABLE_DUAL_AXIS` + `STEP_PULSE_DELAY` does not compile** ✅ DONE
`stepper.c:333` and `:508` referenced `st.step_bits_dual`, which the
`stepper_t` struct never declared. **Confirmed by build before the fix:**

```
grbl/stepper.c:333:10: error: 'stepper_t' has no member named 'step_bits_dual'
grbl/stepper.c:508:27: error: 'stepper_t' has no member named 'step_bits_dual'
```

Fixed by adding the member alongside `step_bits` at `stepper.c:105`:

```c
  #ifdef STEP_PULSE_DELAY
    uint8_t step_bits;  // Stores out_bits output to complete the step pulse delay
    #ifdef ENABLE_DUAL_AXIS
      uint8_t step_bits_dual;
    #endif
  #endif
```

**Verified:** with this change the combination builds clean at 29,384 text /
1,555 bss (as measured at the time; absolute numbers have since moved with
other fixes, but the combination still builds). Cost: 1 byte SRAM, and only in
that configuration — zero cost to the default build.

### Phase 3 — Runtime correctness and safety

**3.1 — `mc_reset()` blocks for up to 254 ms inside an ISR** *(highest priority)* ✅ DONE
`mc_reset()` is called from **four** interrupt-level sites, not just the two
originally identified — traced precisely: `ISR(SERIAL_RX)` (Ctrl-X reset byte),
`ISR(LIMIT_INT_vect)` and `ISR(WDT_vect)` (hard limit, both the default and
software-debounced variants), and `ISR(CONTROL_INT_vect)` (the physical reset
pin). All four previously reached `st_go_idle()` → `delay_ms(settings.stepper_idle_lock_time)`
(`stepper.c:262`) — at the default `$1=25` that's **25 ms with global
interrupts disabled inside an interrupt handler**, ~280 dropped serial bytes
at 115200 baud, and every other interrupt source starved for the duration.

**Fix actually shipped** differs from this plan's original sketch (which
proposed a new `sys.step_control` flag and an explicit check inside
`protocol_exec_rt_system()`). Tracing the abort path in full showed that
mechanism already exists: **every** `mc_reset()` call unconditionally sets
`EXEC_RESET`, which the next `protocol_exec_rt_system()` call unconditionally
converts to `sys.abort = true`, which every wait loop in the program checks
and unwinds on, all the way back to `main()`'s system-abort reinitialization
loop — which unconditionally calls `st_reset()`, whose first line is a full,
blocking `st_go_idle()` call, in ordinary (non-interrupt) program context.
That path is unconditional and pre-existing for *every* `mc_reset()` caller,
ISR or not.

So the fix is smaller than planned: a new `st_go_idle_isr()` in `stepper.c`
does only the three ISR-safe register writes `st_go_idle()` used to start
with (disable Timer1, reset its prescaler, clear `busy`) and returns —
no dwell, no disable-pin write, no new flag. `mc_reset()` calls this instead
of `st_go_idle()`. The dwell and disable-pin write `st_go_idle_isr()` skips
still happen, just a few instructions later via the guaranteed `st_reset()`
call, with interrupts free to run in the meantime — same total time-to-idle,
without holding the global interrupt lock for it.

**Verified at the disassembly level** (about as far as verification goes
without a board): `avr-objdump -d` on `mc_reset()` after the fix shows zero
loop instructions and zero calls to any delay routine — straight-line code,
two trivial subroutine calls, done in a handful of cycles. `st_go_idle()`
(still used by `st_reset()` and the other non-ISR callers) still contains its
`sbiw`/`brne` busy-wait loop, confirming the dwell behavior is fully preserved
where it's safe to block.

Flash cost: **+22 bytes**. Risk: **medium-high, as planned** — this touches
the abort path and was verified by static/disassembly analysis, not on real
hardware. **Bench-test before trusting it**: Ctrl-X during a cycle, a hard
limit trip during a cycle, and a hard limit trip during homing, on an actual
board. This is now the top item in **Remaining work** below.

**3.2 — No bounds validation on `$` settings** ✅ DONE
`settings.c:208` (original line) only rejected negatives. `$100=0` was
accepted and then divided in `plan_buffer_line()` and
`system_convert_axis_steps_to_mpos()`, producing inf/NaN machine positions.
`$12=0` (arc tolerance) divided by `sqrt(0)` in `motion_control.c:109`, then
converted infinity to `uint16_t`.

Fixed: `settings_store_global_setting()` now rejects `<= 0` for `steps_per_mm`,
`max_rate`, `acceleration`, and `arc_tolerance`. Added
`STATUS_SETTING_VALUE_OUT_OF_RANGE 39` to `report.h` and a matching row to
`doc/csv/error_codes_en_US.csv`. `MAX_STEP_RATE_HZ` in `config.h` was
deliberately **not** uncommented by default — that's a separate, user-facing
default-behavior decision (see §2.3), not part of this correctness fix.

Flash cost: **+100 bytes**.

**3.3 — Null dereference in the stepper ISR** ✅ DONE
`stepper.c:397` (original line, now `:420`): the segment-buffer-empty path read
`st.exec_block->is_pwm_rate_adjusted`, but `st_reset()` nulls `st.exec_block`.
Reachable when `st_wake_up()` runs before any segment is queued. On AVR,
address 0 is the register file, so it read garbage rather than faulting — the
symptom was a spurious PWM-off, not a crash.

Fixed:

```c
if (st.exec_block != NULL && st.exec_block->is_pwm_rate_adjusted) { ... }
```

Risk: none. Flash cost: see the combined note after 3.9 below — 3.3, 3.6, 3.8,
and 3.9 shipped together and their costs aren't separable in the measurement.

**3.4 — Dual-axis limit shares the Z limit pin** *(safety, documentation fix)* ✅ DONE
`cpu_map.h`'s two dual-axis pin maps (`DUAL_AXIS_CONFIG_PROTONEER_V3_51` and
`DUAL_AXIS_CONFIG_CNC_SHIELD_CLONE`) both define `DUAL_LIMIT_BIT = Z_LIMIT_BIT`,
and `limits_get_state()` sets **both** the Z-axis limit bit and the dual-axis
limit bit (bit `N_AXIS`) from that one physical switch. During an X/Y
self-squaring homing cycle, a stray Z-limit trigger can satisfy the dual-axis
approach check and the gantry can be declared square when it isn't.

Confirmed this can't be fixed in code on an Uno for *either* stock config, for
two different reasons (traced precisely, not assumed): with
`DUAL_AXIS_CONFIG_PROTONEER_V3_51`, `VARIABLE_SPINDLE` (default on) claims
every other PORTB bit; with `DUAL_AXIS_CONFIG_CNC_SHIELD_CLONE`,
`VARIABLE_SPINDLE` is disallowed outright, but PORTB bits 6/7 are the crystal
oscillator pins on a standard Uno and aren't available as GPIO at all — so
there's still no spare bit.

Shipped as three documentation/warning changes, no behavior change:
- Expanded the existing one-line NOTE at each `DUAL_LIMIT_BIT` definition in
  `cpu_map.h` into a full explanation of the risk and the config-specific
  reason no spare pin exists, plus the fix (free a PORTB pin and move
  `DUAL_LIMIT_BIT`) for anyone whose machine needs accurate squaring.
- Added a second `WARNING:` paragraph to the dual-axis feature's doc comment
  block in `config.h`, next to `ENABLE_DUAL_AXIS` itself, pointing to the full
  explanation.
- Added a non-fatal `#warning` in `grbl.h`'s `ENABLE_DUAL_AXIS` compatibility
  block, firing whenever `DUAL_LIMIT_BIT == Z_LIMIT_BIT` — covers a future or
  custom dual-axis pin map too, not just the two stock ones. **Verified it
  fires** by building a scratch copy with `ENABLE_DUAL_AXIS` on, and that the
  default (dual-axis-off) build is completely unaffected (0 byte size change).

**3.5 — Override arithmetic can wrap** ✅ DONE
`protocol.c` (feed and spindle override handling) did `uint8_t -= INCREMENT`
*before* clamping, and `min()` then snapped a wrapped value to **maximum**.
Safe at stock config only because `MIN_FEED_RATE_OVERRIDE ==
FEED_OVERRIDE_COARSE_INCREMENT == 10`. Any config where MIN exceeds the
increment reintroduced exactly the bug upstream commit `5967839` fixed.

Fixed by accumulating both `new_f_override` and `last_s_override` in `int16_t`
instead of `uint8_t`, clamping once at the end. (`new_r_override`, the rapid
override, was already safe — it only ever gets direct assignments, never an
increment/decrement, so it can't underflow.) **Verified with a standalone
extraction** of the exact arithmetic under a non-default config
(`MIN=20, INCREMENT=10`): the old code took three "decrease" requests from 20
and landed on **200** (the max) instead of clamping at the 20% floor — a real
scenario where trying to *slow down* silently sped the machine up to double
feed rate. The fixed version correctly lands on 20.

Flash cost: **+32 bytes**.

**3.6 — `eeprom_put_char()` unconditionally re-enables interrupts** ✅ DONE
`eeprom.c:124` called `sei()` instead of restoring the saved `SREG`. Safe only
because every then-current caller ran with interrupts already on. Fixed to
save/restore `SREG`, matching the pattern used everywhere else in `system.c`.

**3.7 — Tiny-radius arcs are undefined behavior** ✅ DONE
`motion_control.c:109` (original line): when `radius < arc_tolerance/2`,
`sqrt()` of a negative value gave NaN, and `(uint16_t)NaN` is undefined
behavior in C — not merely "degrades to a straight line," as first assessed.

Fixed by guarding the segment-count formula: only run it when
`2*radius > settings.arc_tolerance`; otherwise fall back to `segments = 0`
(a direct line to the target — the same code path an arc this small relative
to its own tolerance would produce anyway). **Verified two ways**: (1) a
standalone extraction confirmed the old formula's `sqrt()` argument really did
go negative for a degenerate case and that `(uint16_t)NaN` produced `0` on
this host compiler *via undefined behavior*, with no portability guarantee —
the new guarded version reaches the same `0` through well-defined logic
instead; (2) confirmed **zero regression** — identical segment counts before
and after the fix across four realistic arc sizes (default and loose
tolerances, small and large radii).

Flash cost: **+30 bytes**.

**3.8 — `serial_get_rx_buffer_count()` off-by-one** ✅ DONE
`serial.c:51` used `RX_BUFFER_SIZE` on a ring of `RX_BUFFER_SIZE+1`. Fixed to
`RX_RING_BUFFER`. Deprecated and unused unless classic status reports are
enabled in `config.h`.

**3.9 — `clear_vector_float` passes a float to `memset`** ✅ DONE
`nuts_bolts.h:54`: `memset(a, 0.0, ...)` — the fill argument is an `int`.
Worked regardless, but changed `0.0` to `0` for clarity (and did the same for
the adjacent commented-out `clear_vector_long`).

Combined flash cost for 3.3 + 3.6 + 3.8 + 3.9 together (measured, not
separable — all four shipped in one build): **+6 bytes**.

**3.10 — `serial_write()` busy-waits without pumping the segment buffer** — not done, by design
`serial.c:92` (the `while (next_head == serial_tx_buffer_tail)` loop in
`serial_write()`), an acknowledged upstream TODO. A long report against a full TX
buffer can starve motion.

> **Do not fix this naively.** `serial_write()` is called from report functions
> that are themselves called from `protocol_exec_rt_system()`; calling
> `st_prep_buffer()` from inside `serial_write()` risks re-entrancy. This is a
> design task, not a one-liner — see **Remaining work** below for where it
> fits relative to everything else still open.

### Phase 4 — Validation

**4.1 — Host-side test harness.** ✅ DONE (`test/`, see `test/README.md`)
`gcode.c`, `planner.c`, and `nuts_bolts.c` turned out to be nearly
AVR-independent in practice too — none touch a hardware register directly.
Compiles all three natively (host `gcc`, no AVR toolchain needed) against a
minimal shim (`test/avr_shim/`) and a stub layer (`test/grbl_stubs.c`)
standing in for the modules they call into. 39 test cases, run via `make test`.

**Verified the harness actually catches regressions**, not just that it
passes: a real one-line regression (swapped which G-code number maps to which
units mode) was deliberately injected into `gcode.c`, confirmed the harness
failed 2 tests with exit code 1, then reverted. Along the way, writing the
first draft of the suite also caught **4 bugs in the test expectations
themselves** (a float-precision tolerance tighter than `float` can hold at
that magnitude, a status-code assumption that didn't match `gcode.c`'s actual
check ordering, and a `G92` offset sign that was backwards) — none were Grbl
defects, but it's a concrete demonstration of why running real assertions
beats reasoning from memory.

This made Fixes 3.2, 3.5, and 3.7 verifiable rather than merely plausible
(§3.5 and §3.7's standalone-extraction verifications above follow the same
"don't just read it, run it" discipline this harness established, even though
`protocol.c` and `motion_control.c` aren't in the harness's own compiled set).

**4.2 — Size regression check.** ✅ DONE
Added `make size`: builds `$(BUILDDIR)/main.elf` if needed, parses
`avr-size`'s berkeley-format output, and fails (nonzero exit) if `text`
exceeds `MAX_FLASH_BYTES` (default 30,500 — 220 bytes below the hard
old-Nano ceiling of 30,720, 584 bytes above the current 29,916 baseline) or
`bss` exceeds `MAX_SRAM_BYTES` (default 1,800). Both are overridable on the
command line for a deliberate, confirmed-fits-your-board size increase.
**Verified it actually fails**: ran `make size MAX_FLASH_BYTES=29000
MAX_SRAM_BYTES=1000` against the real current build and confirmed both
checks fire with a nonzero exit code, not just that the default (passing)
case looks right. Wired into CI (see below) so it runs on every push and PR.

**4.3 — Hardware smoke test.** Not done, and still the most important open item.
Document a fixed sequence — home, jog each axis, run a known square, feed
hold, resume, **Ctrl-X mid-cycle, a hard limit trip mid-cycle, a hard limit
trip during homing** (the three scenarios Fix 3.1 needs bench-verified), probe
— to run against every firmware change before flashing a machine that has a
tool in it. CI (below) cannot substitute for this: it proves the firmware
compiles and the parser/planner logic is correct, not that a real board
behaves correctly. See Remaining work for why this still outranks everything
else that's left.

**4.4 — Continuous integration.** ✅ DONE (`.github/workflows/ci.yml`)
Two jobs, both on `push`/`pull_request` to `main` and manually triggerable:
- **Build (AVR)**: installs `gcc-avr`/`avr-libc`/`binutils-avr` via `apt`,
  runs `make`, then `make size` (Fix 4.2, above) as a hard gate, then
  uploads `grbl.hex` as a build artifact.
- **Host test harness**: runs `make test` — no AVR toolchain needed, since
  it's a native host build (Fix 4.1).

Deliberately **not** a substitute for Fix 4.3: nothing here touches real
hardware, so it proves the firmware compiles, fits its flash/SRAM budget,
and the parser/planner logic passes its assertions — not that motion,
homing, or the abort path behave correctly on an actual board. A green CI
run and a bench-tested Fix 3.1 are two different kinds of confidence; both
matter, neither substitutes for the other.

Added a CI status badge to `README.md`, linking to the workflow.

## Remaining work

Everything below is open. Suggested order, with reasoning. (Numbers below refer
to the Phase 3/Phase 4 item numbers above, e.g. "Fix 4.3" — not to the
separately-numbered subsections of the "Upgrades and new features" section,
which are referred to by name to avoid confusion between the two.)

CI (Fix 4.4, `.github/workflows/ci.yml`) is live: `make` and `make size`
(Fix 4.2) run on every push/PR to `main`, plus `make test` (Fix 4.1). That
closes the two items that used to top this list. What's left:

1. **Bench-test Fix 3.1 on real hardware — this doubles as Fix 4.3 (hardware
   smoke test).** This is the only fix in the whole plan verified by static
   and disassembly analysis alone, specifically because it touches the abort
   path — this plan flagged it medium-high risk from the start, and that risk
   hasn't been retired by reasoning, only reduced. **CI cannot do this part**
   — it builds and tests on a GitHub-hosted runner with no board attached, so
   a compile-clean, test-passing run says nothing about real interrupt timing
   or actual stepper/limit-switch behavior. Do this before trusting 3.1 on a
   machine with a workpiece in it: Ctrl-X during a cycle, a hard limit trip
   during a cycle, and a hard limit trip during homing. Write the sequence
   down while doing it — home, jog each axis, a known square, feed hold,
   resume, probe, plus the three abort-path cases above — and Fix 4.3 is done
   at the same time.
2. **Fix 3.10, the `serial_write()` design task.** No longer urgent-by-omission
   now that everything else in Phase 3 is closed, but it's the last known
   correctness gap and was explicitly deferred rather than declined. Needs a
   design pass (see its entry for the re-entrancy hazard to avoid), not a
   quick patch — budget real time for it, ideally after the host test harness
   (Fix 4.1) is extended enough to cover `protocol.c`/`serial.c` interaction
   if that's feasible, since this is exactly the kind of subtle
   interrupt/main-loop interaction the harness exists to catch.
3. **The "Upgrades and new features" section below**, in the order already
   laid out there: Cheap wins first, informed by whether `make test` coverage
   can be extended alongside each one; Moderate features next; read Things
   that look small but aren't before proposing anything in that category; and
   treat Change of target as the actual answer for anything beyond
   incremental fixes to this tree.

---

## 4. Upgrades and new features

Ordered by ratio of value to risk. Everything here is **after** Phases 1–4.

### 4.1 Cheap wins

| Feature | Effort | Flash | Notes |
|---|---|---|---|
| **Cache `1/steps_per_mm`** | Low | ~+12 B SRAM, likely **net flash saving** | Software float division is the expensive operation on AVR. It appears 3× per planned block in `planner.c`, plus once per axis per status report in `system_convert_axis_steps_to_mpos()` — ~30 divisions/sec at 10 Hz reporting alone. Compute reciprocals once in `settings_init()` and on `$` change. |
| **Cache the override scale factor** | Low | ~0 | `plan_compute_profile_nominal_speed()` recomputes `0.01 * sys.f_override` for every block. Compute on override change instead. |
| **Precompute `2*acceleration`, `0.5/acceleration` per block** | Low | small | `planner_recalculate()` and `st_prep_buffer()` each redo these on every pass. |
| **Full `$` setting bounds table** | Low | ~100 B | Fix 3.2 (done) only rejects `<= 0` for `steps_per_mm`, `max_rate`, `acceleration`, and `arc_tolerance`. Generalizing that to a per-setting min/max table covering the rest of the `$` settings is a natural follow-on. |

These are the only optimizations worth doing before profiling. All trade SRAM
for cycles, and both are scarce — measure with `avr-size` before and after.

### 4.2 Moderate features

- **`%` program start/end.** Already scaffolded as a TODO in `protocol.c`, which
  also explains why it would fix the resume-semantics issues around functions
  that need to empty the planner buffer.
- **G64 continuous / blended path mode.** The math already exists inside
  `plan_buffer_line()`'s junction-deviation block — the code comment states
  plainly that only the 328P's CPU budget blocks it. A good candidate *after* a
  32-bit port, not before.
- **Block delete (`/`).** `protocol.c` already parses and discards the
  character; wiring it to a toggle is small.
- **M6 tool-change pause.** Can be implemented as an M0-style suspend without
  any tool-changer support.
- **Restore `$` settings without a full EEPROM wipe.** A `$RST=` variant that
  preserves work offsets would take the sting out of version bumps like Fix
  2.1's (done) for any future EEPROM-format-changing fix.

### 4.3 Things that look small but aren't

- **A 4th axis.** Not a small change. `N_AXIS` is load-bearing;
  `get_step_pin_mask()`, `get_direction_pin_mask()`, and `get_limit_pin_mask()`
  in `settings.c` are hardcoded for X/Y/Z; the stepper ISR unrolls
  `counter_x/y/z` by hand; and the Uno has no free pins on the right ports.
  Realistically this means changing target, not editing this tree.
- **Backlash compensation.** Upstream explicitly declined it in a
  `motion_control.c` comment, with reasoning about why a post-processor does it
  better. Read that comment before re-proposing it.
- **Canned cycles, cutter compensation (G41/G42), expressions, variables.**
  Deliberately out of scope upstream; the CAM tool or sender expands them. There
  is no flash budget for any of them regardless.

### 4.4 Change of target — the real upgrade path

Upstream `gnea/grbl` is **archived**; v1.1h is the last release and this tree
matches it. The 328P is at 92.7 % flash and 80 % SRAM with the *stock* feature
set (§1.4's current numbers), after every correctness fix in §3 and still
before any of the features below. Anything ambitious in Moderate features or
Things that look small but aren't argues for moving:

| Target | What it buys |
|---|---|
| **[grblHAL](https://github.com/grblHAL)** | Hardware-abstracted Grbl for ARM (STM32, RP2040, ESP32, Teensy, SAMD). Up to 6 axes, plugins, networking, SD card. The mainstream successor. |
| **[grbl-Mega](https://github.com/gnea/grbl-Mega)** | 2560-based, 4-axis, minimal porting effort from here. Also archived. |
| **[FluidNC](https://github.com/bdring/FluidNC)** | ESP32, config-file driven (no recompile to change pins), WiFi, web UI. |

**Recommendation:** treat this repo as a maintained, well-understood v1.1h — fix
the defects in §3, keep it building for real Uno hardware, and start new
feature-heavy work on grblHAL rather than fighting a 32 KB flash ceiling.

---

## 5. Quick reference

```sh
# One-time per shell
export PATH="$LOCALAPPDATA/Arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin:$PATH"
export PATH="$LOCALAPPDATA/Arduino15/packages/arduino/tools/avrdude/6.3.0-arduino17/bin:$PATH"

make clean && make          # expect 29916 text / 1633 bss, zero warnings
make size                   # same numbers, plus a pass/fail against the flash/SRAM budget
make disasm | less          # or: avr-objdump -d build/main.elf | less

make test                   # host test harness: expect 39/39, no AVR toolchain needed

# Flash an Uno on COM3
avrdude -C "$LOCALAPPDATA/Arduino15/packages/arduino/tools/avrdude/6.3.0-arduino17/etc/avrdude.conf" \
        -p atmega328p -c arduino -P COM3 -b 115200 -D -U flash:w:grbl.hex:i
```

CI (`.github/workflows/ci.yml`) runs `make` + `make size` and `make test` as
two parallel jobs on every push/PR to `main` — same commands as above, on a
fresh Ubuntu runner with `gcc-avr`/`avr-libc`/`binutils-avr` from `apt`
instead of this machine's Arduino-IDE-bundled toolchain. `gh run list` /
`gh run watch` (if you have `gh` installed and authenticated) shows live
status without leaving the terminal.

| Doc | Contents |
|---|---|
| `README.md` | Overview, pin map, configuration options |
| `CLAUDE.md` | Architecture rules and conventions for code changes |
| `test/README.md` | Host test harness: scope, what's stubbed, how to add a test |
| `.github/workflows/ci.yml` | CI: build + size gate + host test harness on every push/PR |
| `doc/markdown/commands.md` | `$` command reference |
| `doc/markdown/settings.md` | `$n=` setting reference |
| `doc/markdown/interface.md` | Status report and message format |
| `doc/markdown/jogging.md` | `$J=` jogging protocol |
| `doc/markdown/laser_mode.md` | `$32` laser mode behavior |
| `doc/csv/` | Parseable error, alarm, setting, and build-option tables |
