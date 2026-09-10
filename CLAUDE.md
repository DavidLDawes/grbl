# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this is

Grbl v1.1h (build 20190830) — a G-code interpreter and real-time stepper motion
controller for the ATmega328P (Arduino Uno/Nano/Duemilanove/Micro). Bare-metal
AVR C, no RTOS, no dynamic allocation. This repo is a fork of `gnea/grbl`;
upstream is archived. **This tree now has local divergence** — eleven
correctness fixes plus a host-side test harness, all tracked in `PLAN.md`
(read its `## Status` section first for what's done vs. still open). Version
strings live in `grbl/grbl.h` (`GRBL_VERSION`, `GRBL_VERSION_BUILD`) and were
deliberately left at `1.1h`/`20190830` — the fixes are bug fixes, not new
features or protocol changes, so nothing about wire-format compatibility with
existing senders/GUIs changed. `SETTINGS_VERSION` in `settings.h`, by
contrast, **was** bumped (10 → 11, see Gotchas) — that's a separate
EEPROM-schema version, not the firmware version.

The 328P has 32 KB flash / 2 KB SRAM and the stock build uses nearly all of
both. **Flash and SRAM are the binding constraints on every change here.**
Adding a feature usually means finding something to remove.

## Build

Requires the AVR toolchain (`avr-gcc`, `avr-objcopy`, `avr-size`, `avrdude`).
**It's already installed on this machine**, bundled with the Arduino IDE's AVR
core — not on `PATH` by default, so add it per-session:

```sh
export PATH="$LOCALAPPDATA/Arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin:$PATH"
export PATH="$LOCALAPPDATA/Arduino15/packages/arduino/tools/avrdude/6.3.0-arduino17/bin:$PATH"

make                       # -> grbl.hex  (objects land in build/)
make clean
make flash                 # avrdude, needs PROGRAMMER=... DEVICE=...
make DEVICE=atmega328p PROGRAMMER="-c avrisp2 -P usb" flash
```

If those paths don't exist on a given machine, say so plainly and reason about
changes by reading instead of claiming a build passed — don't assume the
absence of one specific install means no toolchain exists anywhere; check
before concluding that. See `PLAN.md` §1 for the full toolchain story
(installed-vs-fresh-install paths, version notes) and exact current build
numbers.

The Makefile is a hand-rolled prototype, not generated. `CLOCK` is fixed at
16 MHz; `-Os -flto -ffunction-sections` plus `-Wl,--gc-sections` are load-bearing
for fitting in flash — do not drop them casually.

Most users build via the Arduino IDE instead, using
`grbl/examples/grblUpload/grblUpload.ino` after adding `grbl/` as a library.

## Layout

| Path | Role |
|---|---|
| `grbl/main.c` | Init, then the reset/abort outer loop |
| `grbl/protocol.c` | Serial line assembly, **the real-time state machine**, suspend/parking |
| `grbl/gcode.c` | RS274/NGC parser: parse → validate → execute, in that order |
| `grbl/planner.c` | Look-ahead ring buffer, junction-deviation cornering |
| `grbl/stepper.c` | Segment buffer + Bresenham/AMASS stepper ISR |
| `grbl/motion_control.c` | `mc_line`/`mc_arc`/homing/probe/`mc_reset` — gateway to planner |
| `grbl/limits.c` | Hard limits, homing cycle, dual-axis squaring, soft-limit check |
| `grbl/system.c` | `$` command dispatch, control pins, exec-flag helpers |
| `grbl/settings.c` | `$n=` settings, EEPROM load/store |
| `grbl/report.c` | All output: status reports, errors, alarms, `$$`/`$G`/`$#` |
| `grbl/serial.c` | UART ring buffers + real-time command interception in the RX ISR |
| `grbl/config.h` | **Compile-time feature switches.** Start here for behavior changes |
| `grbl/defaults.h` | Per-machine default `$` values (`DEFAULTS_GENERIC` etc.) |
| `grbl/cpu_map.h` | Pin/port/timer assignment for `CPU_MAP_ATMEGA328P` |
| `doc/markdown/` | Interface, settings, jogging, laser-mode specs |
| `doc/csv/` | Machine-readable error/alarm/setting/build-option code tables |
| `doc/script/` | Python streaming scripts (`stream.py`, `simple_stream.py`) |
| `test/` | Host-side test harness (`make test`) — see `test/README.md` and the Testing section below |
| `PLAN.md` | Fix/feature backlog with status tracking — check `## Status` before starting new work |

`grbl/grbl.h` is the single include hub — every `.c` includes only `"grbl.h"`,
and the include order inside it is order-dependent. Do not reorder it.

## Architecture rules that must not be broken

**The pipeline is one-directional:**
`serial RX ISR → line buffer → gcode.c → motion_control.c → planner.c → segment buffer → TIMER1 ISR → step pins`

1. **`TIMER1_COMPA_vect` in `stepper.c` is hard real-time.** It runs up to 30 kHz
   and must complete in well under 33 µs. No floating point, no division, no
   function calls that aren't trivial. All heavy math belongs in
   `st_prep_buffer()`, called from the main loop.
2. **Timers are spoken for.** Timer1 = stepper ISR, Timer0 = step-pulse reset
   (and pulse delay), Timer2 = spindle PWM. Watchdog is used by
   `ENABLE_SOFTWARE_DEBOUNCE`. There is no free timer.
3. **Cross-context state goes through the `sys_rt_exec_*` volatile bitflags**,
   set via the `system_set_exec_*` helpers (which do `cli()`/restore `SREG`) and
   consumed only in `protocol_exec_rt_system()`. Do not add new shared mutable
   state between ISR and main loop without this pattern.
4. **Never block in the main loop without a real-time check point.** Any wait
   loop must call `protocol_execute_realtime()` (or `protocol_exec_rt_system()`
   inside a suspend) and bail on `sys.abort`.
5. **`gcode.c` validates the entire block before mutating any state.** Keep the
   STEP 1–4 structure: no side effects until STEP 4.
6. `N_AXIS` is 3 and is *not* a free parameter — `get_step_pin_mask()`,
   `get_direction_pin_mask()`, and `get_limit_pin_mask()` in `settings.c` are
   hardcoded for X/Y/Z, and the stepper ISR unrolls `counter_x/y/z` by hand.
7. **Never call `st_go_idle()` from interrupt-level code.** It can block for
   up to 255 ms via `delay_ms(settings.stepper_idle_lock_time)`. From an ISR
   (or anything called from one), call `st_go_idle_isr()` instead — it does
   only the ISR-safe part (stop Timer1, reset its prescaler, clear `busy`)
   and returns immediately. This is safe specifically because every call path
   that reaches it is guaranteed a follow-up `st_reset()` call (which *does*
   call the full `st_go_idle()`) from `main()`'s abort-reinitialization loop,
   in ordinary non-interrupt context, a few instructions later — see the
   comment on `st_go_idle_isr()`'s definition in `stepper.c` for the traced
   proof. `mc_reset()` is the only current caller; if you add a new
   interrupt-level path that needs to stop the steppers, use
   `st_go_idle_isr()`, not `st_go_idle()`.

## Conventions

- Two-space indent, `lowercase_with_underscores`, `module_verb_noun()` naming.
  Some upstream lines are tab-indented; leave them unless you're editing them.
- **All files are CRLF.** Preserve that when editing, or diffs explode.
- Bit helpers from `nuts_bolts.h`: `bit()`, `bit_true()`, `bit_false()`,
  `bit_istrue()`, `bit_isfalse()`. Use them rather than raw shifts.
- All string literals that go to serial use `printPgmString(PSTR("..."))` so they
  stay in flash. A bare `printString("literal")` costs SRAM — don't.
- Feature code is `#ifdef`-gated on a `config.h` symbol, and mutually exclusive
  combinations are rejected by the `#error` block at the bottom of `grbl.h`.
  **If you add a feature flag, add its compatibility checks there too.**
- Internal units are **mm and mm/min** (accelerations stored as mm/min²; note
  `settings.c` case 2 multiplies by 60·60 on entry). `max_travel` is stored
  **negative**. Inch conversion happens only at report time.

## Testing

`make test` runs a host-side harness (`test/`, see `test/README.md`) that
compiles `gcode.c`, `planner.c`, and `nuts_bolts.c` — the three modules
with no hardware-register dependency — natively against a stubbed
motion_control.c/spindle_control.c/coolant_control.c/report.c/protocol.c/
system.c/settings.c/jog.c/stepper.c layer, and runs real assertions
against them. No AVR toolchain or board needed; it's independent of the
`make`/`make clean`/`make flash` targets. Run it after any change to one
of those three files, and add a test case alongside any fix to them.

Everything else — `stepper.c`'s ISR above all, plus the rest of the
hardware-facing modules — still has no automated coverage. For those,
verification is:

1. Read carefully — this is safety-relevant motion control on real hardware.
2. Compile for size (`avr-size` output from `make`) and confirm it still fits.
3. `$C` check-mode on hardware runs the parser and planner with motion blocked.
4. `doc/script/stream.py` streams a file with the character-counting protocol.

Do not claim behavior is verified unless it actually was.

## Gotchas

- `$` settings writes call `eeprom_put_char()`, which busy-waits with interrupts
  disabled for ~3.4 ms per byte. Anything that writes EEPROM mid-stream (G10 L2,
  G28.1, G30.1) will drop incoming serial bytes.
- `SETTINGS_VERSION` (`settings.h`) is currently **11**. Bumping it wipes every
  user's EEPROM on next boot — global `$` settings, work coordinate offsets,
  G28/G30 positions, startup lines, all reset to defaults — so bump it only
  alongside an EEPROM-format-changing fix (as `eeprom.c`'s
  `memcpy_to/from_eeprom_with_checksum()` change required, taking it 10 → 11),
  never casually. Record the reason in a comment at the `#define`, matching the
  existing v11 note.
- The dual-axis feature's `DUAL_LIMIT_BIT` is aliased to `Z_LIMIT_BIT` in
  `cpu_map.h` for both stock shield configs — there's no spare port bit to give
  it its own input on a stock Uno (see the comment at each `DUAL_LIMIT_BIT`
  definition for the config-specific reason why). `limits_get_state()` can't
  tell a Z-limit trip from a dual-axis-motor limit trip apart. This is flagged
  with a `#warning` when `ENABLE_DUAL_AXIS` is on, but is not fixable in
  software — don't attempt to "fix" it without freeing a physical pin first.
