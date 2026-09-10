# Grbl v1.1h — Development Plan

A staged plan covering toolchain setup, hardware bring-up, defect remediation,
and future work for this fork of `gnea/grbl` v1.1h (build 20190830).

Status of the tree at the time of writing: **unmodified from upstream**, and
**it builds clean** (verified — see §1.4 for the numbers).

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

Expected output ends with an `avr-size` summary. **Two warnings are expected**
until Fix 2.1 lands, both pointing at the same real bug:

```
grbl/eeprom.c:133:26: warning: '<<' in boolean context, did you mean '<' ?
grbl/eeprom.c:144:26: warning: '<<' in boolean context, did you mean '<' ?
```

Everything else compiles silently under `-Wall`.

### 1.4 Verified baseline (stock `config.h`, ATmega328P @ 16 MHz)

```
   text	   data	    bss	    dec	    hex	filename
  29762	      0	   1633	  31395	   7aa3	build/main.elf
```

| Resource | Used | Available | Free | Headroom |
|---|---|---|---|---|
| Flash (Uno, 512 B Optiboot) | 29,762 | 32,256 | **2,494 B** | 7.7 % |
| Flash (old Nano, 2 KB bootloader) | 29,762 | 30,720 | **958 B** | 3.1 % |
| SRAM (`.bss`, before stack) | 1,633 | 2,048 | **415 B** | 20 % |

**This is the single most important constraint in this document.** Every fix and
feature below is annotated with its expected flash cost. On an old-bootloader
Nano there is under 1 KB to work with, and the 415 bytes of SRAM must cover the
entire call stack *plus* nested ISR frames.

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
you'll lose steps. Uncommenting `MAX_STEP_RATE_HZ` in `config.h` makes Grbl
reject out-of-range settings instead of silently misbehaving (see Fix 3.2).

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
independently shippable and testable. Flash costs are estimates against the
2,494-byte Uno budget from §1.4.

### Phase 1 — Tooling and repo hygiene *(zero firmware risk, do first)*

| # | Item | File | Change |
|---|---|---|---|
| 1.1 | Broken header-dependency tracking | `Makefile:106` | `-include $(BUILDDIR)/$(OBJECTS:.o=.d)` expands to `build/build/main.d` — make only prefixes the *first* word. Verified: every other `.d` resolves correctly, so only `main.c` silently misses header changes. Change to `-include $(OBJECTS:.o=.d)`. |
| 1.2 | Dead `disasm` target | `Makefile:99` | Depends on `main.elf`, not `$(BUILDDIR)/main.elf`. Fix the prerequisite. |
| 1.3 | Dead `.S.o` suffix rule | `Makefile:62` | Legacy suffix rule that can never fire alongside the pattern rules. Delete, or convert to a `$(BUILDDIR)/%.o: $(SOURCEDIR)/%.S` pattern rule. |
| 1.4 | No `build/` creation | `Makefile` | Works only because `build/.gitignore` keeps the directory in git. Add an order-only prerequisite: `$(BUILDDIR)/%.o: $(SOURCEDIR)/%.c \| $(BUILDDIR)` plus a `$(BUILDDIR): ; mkdir -p $@` rule. |
| 1.5 | Missing `.PHONY` | `Makefile` | Declare `all clean flash fuse install load disasm cpp` phony. |
| 1.6 | `.gitignore` ignores `README.md` | `.gitignore:7` | No effect today (the file is tracked) but actively misleading. Delete the line. |

**Verify:** `make clean && make` reproduces §1.4 byte-for-byte; `touch
grbl/config.h && make` now rebuilds `main.o`.

### Phase 2 — Compile-time correctness

**2.1 — EEPROM checksum uses `||` instead of `|`**
`eeprom.c:133` and `eeprom.c:144`. GCC 7.3 already flags this
(`-Wint-in-bool-context`); it is the only warning in the tree.

```c
checksum = (checksum << 1) || (checksum >> 7);   // wrong: yields only 0 or 1
checksum = (checksum << 1) |  (checksum >> 7);   // intended byte rotation
```

The intended rotate collapses, degrading the checksum to roughly a plain sum.
Read and write are symmetric, so nothing currently *breaks* — but corruption
detection on settings, coordinate offsets, startup lines, and build info is far
weaker than the code claims.

> **Migration hazard.** Changing this invalidates every existing stored record.
> On first boot after flashing, `read_global_settings()` fails its checksum,
> settings silently reset to defaults, **and G54–G59 work offsets, G28/G30
> positions, and startup lines are wiped too.** Ship this together with a
> `SETTINGS_VERSION` bump (`settings.h:30`, currently 10) so the reset is
> explicit rather than mysterious, and tell users to record `$$` and `$#` first.

Flash cost: ~0. Do this in a release of its own, clearly flagged.

**2.2 — `ENABLE_DUAL_AXIS` + `STEP_PULSE_DELAY` does not compile**
`stepper.c:333` and `:508` reference `st.step_bits_dual`, which the `stepper_t`
struct never declares. **Confirmed by build:**

```
grbl/stepper.c:333:10: error: 'stepper_t' has no member named 'step_bits_dual'
grbl/stepper.c:508:27: error: 'stepper_t' has no member named 'step_bits_dual'
```

Fix — add the member alongside `step_bits` at `stepper.c:105`:

```c
  #ifdef STEP_PULSE_DELAY
    uint8_t step_bits;  // Stores out_bits output to complete the step pulse delay
    #ifdef ENABLE_DUAL_AXIS
      uint8_t step_bits_dual;
    #endif
  #endif
```

**Verified:** with this change the combination builds clean at 29,384 text /
1,555 bss. Cost: 1 byte SRAM, and only in that configuration.

Also add a matching guard to the `#error` block in `grbl.h` for any other
untested dual-axis pairing, so bad combinations fail loudly at configure time
rather than deep in `stepper.c`.

### Phase 3 — Runtime correctness and safety

**3.1 — `mc_reset()` blocks for up to 254 ms inside an ISR** *(highest priority)*
`mc_reset()` is called from the serial RX ISR and the limit-pin ISR, and reaches
`st_go_idle()` → `delay_ms(settings.stepper_idle_lock_time)` (`stepper.c:262`).
At the default `$1=25` that is **25 ms with global interrupts disabled inside an
interrupt handler** on every Ctrl-X issued during a cycle — roughly 280 dropped
bytes at 115200 baud, and every other interrupt starved.

Fix: split the stepper shutdown in two. Add `st_kill()` that only disables
`TIMSK1`/`TCCR1B` and asserts the driver-disable pin, and call *that* from
`mc_reset()`. Set a `sys.step_control` flag for a pending idle lock, and perform
the actual dwell in `protocol_exec_rt_system()` or the `main()` re-init loop,
where blocking is safe.

Risk: **medium-high** — touches the abort path. Bench-test Ctrl-X during a cycle,
during homing, and during a jog. Flash cost: ~30–60 bytes.

**3.2 — No bounds validation on `$` settings**
`settings.c:208` only rejects negatives. `$100=0` is accepted and then divides
in `plan_buffer_line()` and `system_convert_axis_steps_to_mpos()`, producing
inf/NaN machine positions. `$12=0` (arc tolerance) divides by `sqrt(0)` in
`motion_control.c:109`, then converts infinity to `uint16_t`.

Fix: add per-setting range checks in `settings_store_global_setting()`. Add
`STATUS_SETTING_VALUE_OUT_OF_RANGE 39` to `report.h` and one row to
`doc/csv/error_codes_en_US.csv` — note that `report_status_message()` just
prints the numeric code, so a new code costs **zero flash in `report.c`**.
Minimum viable version: reject `<= 0` for `steps_per_mm`, `max_rate`,
`acceleration`, and `arc_tolerance`. Also uncomment `MAX_STEP_RATE_HZ` in
`config.h` (see §2.3).

Flash cost: ~80–150 bytes depending on thoroughness.

**3.3 — Null dereference in the stepper ISR**
`stepper.c:397`: the segment-buffer-empty path reads
`st.exec_block->is_pwm_rate_adjusted`, but `st_reset()` nulls `st.exec_block`.
Reachable when `st_wake_up()` runs before any segment is queued. On AVR,
address 0 is the register file, so it reads garbage rather than faulting — the
symptom is a spurious PWM-off, not a crash.

```c
if (st.exec_block != NULL && st.exec_block->is_pwm_rate_adjusted) { ... }
```

Flash cost: ~6 bytes. Risk: none.

**3.4 — Dual-axis limit shares the Z limit pin** *(safety, documentation fix)*
`cpu_map.h:173` and `:232` define `DUAL_LIMIT_BIT = Z_LIMIT_BIT`, and
`limits_get_state()` sets **both** bit 2 and bit `N_AXIS` from a single physical
switch. During an X/Y self-squaring homing cycle, a stray Z-limit trigger
satisfies the dual-axis approach check and the gantry is declared square when it
isn't.

This cannot be fixed in code on an Uno: the limit port is PORTB, and with
`VARIABLE_SPINDLE` enabled every other PORTB bit is taken (PB0 = stepper enable,
PB3 = spindle PWM, PB5 = spindle direction). The realistic fixes are:

- **Document it prominently** in `config.h` next to `ENABLE_DUAL_AXIS` and in
  the README's dual-axis notes.
- Recommend that dual-axis users free a PORTB pin (disable `VARIABLE_SPINDLE`,
  or give up spindle direction) and move `DUAL_LIMIT_BIT` to it.
- Optionally add a `#warning` when `DUAL_LIMIT_BIT == Z_LIMIT_BIT`.

**3.5 — Override arithmetic can wrap**
`protocol.c:413` and neighbours do `uint8_t -= INCREMENT` *before* clamping, and
`min()` then snaps a wrapped value to **maximum**. Safe at stock config only
because `MIN_FEED_RATE_OVERRIDE == FEED_OVERRIDE_COARSE_INCREMENT == 10`. Any
config where MIN < the increment reintroduces exactly the bug upstream commit
`5967839` fixed. Fix by accumulating in `int16_t` and clamping once.

Flash cost: ~20 bytes.

**3.6 — `eeprom_put_char()` unconditionally re-enables interrupts**
`eeprom.c:124` calls `sei()` instead of restoring the saved `SREG`. Safe only
because every current caller runs with interrupts already on. Save/restore
`SREG` to match the pattern used everywhere else in `system.c`.

**3.7 — Tiny-radius arcs are undefined behavior**
`motion_control.c:109`: when `radius < arc_tolerance/2`, `sqrt()` of a negative
gives NaN, and `(uint16_t)NaN` is UB. Degrades to a straight line in practice.
Guard with an explicit radius check and clamp `segments` to a sane maximum
(upstream's own comment says it shouldn't exceed ~2000).

**3.8 — `serial_get_rx_buffer_count()` off-by-one**
`serial.c:51` uses `RX_BUFFER_SIZE` on a ring of `RX_BUFFER_SIZE+1`. Deprecated
and unused unless classic status reports are enabled — fix or delete.

**3.9 — `clear_vector_float` passes a float to `memset`**
`nuts_bolts.h:54`: `memset(a, 0.0, ...)` — the fill argument is an `int`. Works,
but change `0.0` to `0` for clarity.

**3.10 — `serial_write()` busy-waits without pumping the segment buffer**
`serial.c:93`, an acknowledged upstream TODO. A long report against a full TX
buffer can starve motion.

> **Do not fix this naively.** `serial_write()` is called from report functions
> that are themselves called from `protocol_exec_rt_system()`; calling
> `st_prep_buffer()` from inside `serial_write()` risks re-entrancy. Treat this
> as a design task, not a one-liner, and defer it behind the rest of Phase 3.

### Phase 4 — Validation

**4.1 — Host-side test harness.** *(highest value-per-effort item in this
document.)* `gcode.c`, `planner.c`, and `nuts_bolts.c` are nearly
AVR-independent. A thin shim for `avr/pgmspace.h` and the register accesses lets
them compile and run natively under a normal `gcc` — which is already installed.
That buys the first real regression tests this codebase has ever had, and makes
Phase 3 verifiable instead of merely plausible. Build it *before* attempting
Phase 5.

**4.2 — Size regression check.** Add a `make size` target that fails if `text`
exceeds a threshold. With 958 bytes of headroom on an old-bootloader Nano, an
unnoticed 1 KB growth is a broken release.

**4.3 — Hardware smoke test.** Document a fixed sequence — home, jog each axis,
run a known square, feed hold, resume, Ctrl-X mid-cycle, probe — to run against
every firmware change before flashing a machine that has a tool in it.

### Suggested ordering

```
Phase 1  (tooling)        — no risk, immediate
Phase 3.3, 3.6, 3.8, 3.9  — trivial, no behavior change
Phase 2.2                 — verified fix, contained
Phase 4.1                 — build the harness
Phase 3.2, 3.5, 3.7       — now testable
Phase 3.1                 — riskiest; needs bench time
Phase 2.1 + 3.4           — ship as a flagged release (EEPROM reset + safety doc)
```

Total estimated flash cost of all fixes: **~200–300 bytes** of the 2,494
available on an Uno. Comfortable there; tight on an old Nano.

---

## 4. Upgrades and new features

Ordered by ratio of value to risk. Everything here is **after** Phases 1–4.

### 4.1 Cheap wins

| Feature | Effort | Flash | Notes |
|---|---|---|---|
| **Cache `1/steps_per_mm`** | Low | ~+12 B SRAM, likely **net flash saving** | Software float division is the expensive operation on AVR. It appears 3× per planned block in `planner.c`, plus once per axis per status report in `system_convert_axis_steps_to_mpos()` — ~30 divisions/sec at 10 Hz reporting alone. Compute reciprocals once in `settings_init()` and on `$` change. |
| **Cache the override scale factor** | Low | ~0 | `plan_compute_profile_nominal_speed()` recomputes `0.01 * sys.f_override` for every block. Compute on override change instead. |
| **Precompute `2*acceleration`, `0.5/acceleration` per block** | Low | small | `planner_recalculate()` and `st_prep_buffer()` each redo these on every pass. |
| **`$` setting bounds table** | Low | ~100 B | Already scoped as Fix 3.2; generalizing it to a full table is a natural follow-on. |

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
- **Restore `$` settings without an EEPROM wipe.** A `$RST=` variant that
  preserves work offsets would take the sting out of Fix 2.1.

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
matches it. The 328P is at 92 % flash and 80 % SRAM with the *stock* feature
set. Anything ambitious in §4.2 or §4.3 argues for moving:

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

make clean && make          # expect 29762 text / 1633 bss, 2 eeprom.c warnings
avr-size --format=berkeley build/main.elf
avr-objdump -d build/main.elf | less     # after Fix 1.2, `make disasm`

# Flash an Uno on COM3
avrdude -C "$LOCALAPPDATA/Arduino15/packages/arduino/tools/avrdude/6.3.0-arduino17/etc/avrdude.conf" \
        -p atmega328p -c arduino -P COM3 -b 115200 -D -U flash:w:grbl.hex:i
```

| Doc | Contents |
|---|---|
| `README.md` | Overview, pin map, configuration options |
| `CLAUDE.md` | Architecture rules and conventions for code changes |
| `doc/markdown/commands.md` | `$` command reference |
| `doc/markdown/settings.md` | `$n=` setting reference |
| `doc/markdown/interface.md` | Status report and message format |
| `doc/markdown/jogging.md` | `$J=` jogging protocol |
| `doc/markdown/laser_mode.md` | `$32` laser mode behavior |
| `doc/csv/` | Parseable error, alarm, setting, and build-option tables |
