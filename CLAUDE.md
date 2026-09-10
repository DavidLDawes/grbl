# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this is

Grbl v1.1h (build 20190830) — a G-code interpreter and real-time stepper motion
controller for the ATmega328P (Arduino Uno/Nano/Duemilanove/Micro). Bare-metal
AVR C, no RTOS, no dynamic allocation. This repo is a fork of `gnea/grbl` at
upstream master; upstream is archived and this tree currently has **no local
divergence** from it. Version strings live in `grbl/grbl.h`
(`GRBL_VERSION`, `GRBL_VERSION_BUILD`).

The 328P has 32 KB flash / 2 KB SRAM and the stock build uses nearly all of
both. **Flash and SRAM are the binding constraints on every change here.**
Adding a feature usually means finding something to remove.

## Build

Requires the AVR toolchain (`avr-gcc`, `avr-objcopy`, `avr-size`, `avrdude`).
**Neither `avr-gcc` nor `avrdude` is installed in this environment**, so you
cannot compile or flash from here — reason about changes by reading, and say so
rather than claiming a build passed.

```sh
make                       # -> grbl.hex  (objects land in build/)
make clean
make flash                 # avrdude, needs PROGRAMMER=... DEVICE=...
make DEVICE=atmega328p PROGRAMMER="-c avrisp2 -P usb" flash
```

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

There is no test suite and no host-side harness. Verification is:

1. Read carefully — this is safety-relevant motion control on real hardware.
2. Compile for size (`avr-size` output from `make`) and confirm it still fits.
3. `$C` check-mode on hardware runs the parser and planner with motion blocked.
4. `doc/script/stream.py` streams a file with the character-counting protocol.

Do not claim behavior is verified unless it actually was.

## Gotchas

- The root `.gitignore` lists `README.md`. It has no effect (the file is already
  tracked) but it is wrong and confusing.
- The Makefile's `-include $(BUILDDIR)/$(OBJECTS:.o=.d)` only prefixes the *first*
  word, so it looks for `build/build/main.d` — header-dependency tracking is
  silently broken for `main.c`. `make clean` after touching any header.
- The `disasm:` target depends on `main.elf`, not `$(BUILDDIR)/main.elf`, and
  does not work.
- `$` settings writes call `eeprom_put_char()`, which busy-waits with interrupts
  disabled for ~3.4 ms per byte. Anything that writes EEPROM mid-stream (G10 L2,
  G28.1, G30.1) will drop incoming serial bytes.
- Changing `SETTINGS_VERSION` in `settings.h` wipes users' EEPROM on next boot.
