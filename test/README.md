# Grbl host-side test harness

Compiles a slice of the real Grbl firmware sources with a **host** C
compiler (not `avr-gcc`) and runs them natively, with no board attached.
This is the harness sketched out in `PLAN.md` Phase 4.1: `gcode.c`,
`planner.c`, and `nuts_bolts.c` turned out to be "nearly AVR-independent"
in practice too — none of the three touch a hardware register directly,
so they compile against a thin shim instead of needing a simulated
ATmega328P.

## Running it

```sh
make test         # builds test/build/grbl_test and runs it
make test-clean    # removes the test build artifacts
```

No AVR toolchain required — `make test` uses `gcc` (or `$HOSTCC`, e.g.
`make test HOSTCC=clang`) and is independent of the `make` / `make clean`
/ `make flash` targets and the `build/` directory those use.

A run looks like:

```
Grbl host test harness
=======================
nuts_bolts.c:
  [PASS] read_float_parses_plain_integer
  ...
=======================
39 test case(s), 0 failure(s)
```

Exit code is `0` if everything passed, `1` otherwise — safe to wire into
CI as a normal test-command step.

## What's actually compiled

`grbl/nuts_bolts.c`, `grbl/planner.c`, and `grbl/gcode.c`, unmodified,
straight from the working tree — compiling the real production sources,
not a reimplementation of their logic. The test binary links them
against:

- **`avr_shim/`** — minimal stand-ins for `<avr/io.h>`, `<avr/interrupt.h>`,
  `<avr/pgmspace.h>`, `<avr/wdt.h>`, and `<util/delay.h>`, just enough for
  `grbl.h`'s unconditional include chain to compile on a host. None of
  the three target files reference an AVR register by name, so `io.h`'s
  shim is intentionally empty — see the comment in that file for why
  that's safe.
- **`grbl_stubs.c`** — defines the three globals main.c/settings.c
  normally own (`sys`, `sys_position`, `settings`, the last initialized
  to the exact same numbers as `DEFAULTS_GENERIC`), and stands in for
  every function these three files call into modules the harness
  doesn't compile: `motion_control.c`, `spindle_control.c`,
  `coolant_control.c`, `report.c`, `protocol.c`, `system.c` (except one
  real re-implementation, see below), `settings.c`, `jog.c`, and
  `stepper.c`. Most stubs just record what they were called with into a
  global a test can read back (`test/grbl_stubs.h` declares all of
  them); a couple (`get_direction_pin_mask()`,
  `system_convert_array_steps_to_mpos()`) are genuine re-implementations
  because `planner.c` depends on their actual arithmetic, not just on
  them having been called.
- **`test_framework.h`** — a deliberately tiny assert/runner macro set.
  No external test library, in keeping with the harness's whole point:
  a host C compiler and libm, nothing else.

## What this does *not* cover

Everything hardware-facing: `stepper.c` (the ISR is hard real-time by
design — see `CLAUDE.md`), `motion_control.c`, `serial.c`,
`spindle_control.c`/`coolant_control.c`'s actual pin toggling, `report.c`'s
output formatting, `system.c`'s pin/interrupt handling, `settings.c`'s
EEPROM access, and `protocol.c`'s line assembly and real-time state
machine. `mc_line()`/`mc_arc()`/`mc_dwell()` are stubbed to just record
their call, so gcode tests exercise gcode.c's own parsing, validation,
and modal-state bookkeeping — not real motion planning or execution.
`test_planner.c` calls `plan_buffer_line()` directly to cover that.

## Adding a test

Pick the right `test_*.c` file for the module under test (or add a new
one — mirror `run_nuts_bolts_tests()`'s pattern in `test_main.c`), write
a `TEST_CASE(name) { ... }` using the `TEST_ASSERT*` macros from
`test_framework.h`, and register it in that file's `run_*_tests()`. If a
test needs a clean starting state, call `test_world_reset()` (and
`gc_init()`, for gcode tests) at the top — see any existing test case for
the pattern.

## A note on what this catches

While writing the initial suite, four of the first-draft test
*expectations* turned out to be wrong, not the Grbl code under test:
a float-precision tolerance that was tighter than a `float` can actually
hold at that magnitude, a status-code test that didn't account for
`gcode.c`'s axis-command-conflict check running before the generic
modal-group check, and a `G92` offset sign that was simply backwards
(the real semantics are `work_pos = machine_pos - offset`). All four
were caught by actually running the tests, not by reasoning about the
code — which is the case for this harness's existence: it was also
verified to fail correctly by temporarily injecting a real one-line
regression into `gcode.c` (swapping which int maps to which units mode)
and confirming the harness reported it before the fix was reverted.
