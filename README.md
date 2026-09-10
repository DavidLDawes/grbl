![Grbl Logo](https://github.com/gnea/gnea-Media/blob/master/Grbl%20Logo/Grbl%20Logo%20250px.png?raw=true)

# Grbl v1.1h

Grbl is a high-performance, open-source G-code interpreter and CNC motion
controller that runs entirely on an Arduino with an ATmega328P (Uno,
Duemilanove, Nano, Micro). It reads standards-compliant G-code over a serial
port and drives step/direction stepper outputs with full acceleration planning
and look-ahead — no PC-side real-time requirement, no parallel port.

Written in tightly optimized C against the AVR hardware directly, it sustains
**up to 30 kHz of jitter-free step pulses** while simultaneously parsing G-code,
planning motion, and servicing real-time commands. It plans up to 16 motions
ahead to deliver smooth acceleration and jerk-free cornering.

This repository is a fork of [gnea/grbl](https://github.com/gnea/grbl) at
version **1.1h (build 20190830)**, the final upstream release. Upstream is now
archived; active development continues in
[grblHAL](https://github.com/grblHAL) and
[grbl-Mega](https://github.com/gnea/grbl-Mega) for 32-bit and Mega2560 targets.

* **License:** GPLv3 — see [`COPYING`](COPYING) and the
  [licensing notes](https://github.com/gnea/grbl/wiki/Licensing).
* **Documentation:** the [Grbl Wiki](https://github.com/gnea/grbl/wiki), plus
  the specs in [`doc/markdown/`](doc/markdown) in this repo.
* **Lead developer:** Sungeun "Sonny" Jeon, Ph.D. (@chamnit), building on
  Grbl v0.6 (2011) by Simen Svale Skogsrud.

---

## Quick start

### Option A — Arduino IDE (recommended for most users)

1. In the IDE: **Sketch → Include Library → Add .ZIP Library…** and select the
   `grbl/` folder of this repository (or a zip of it).
2. Open **File → Examples → grbl → grblUpload**.
3. Select your board (an Uno or other 328P board) and serial port.
4. Click **Upload**.

To customize before uploading, edit `grbl/config.h` in place — the IDE compiles
it from wherever it stored the library.

### Option B — command line

Requires the AVR toolchain: `avr-gcc`, `avr-objcopy`, `avr-size`, and `avrdude`.

```sh
make                  # produces grbl.hex; object files go to build/
make clean

# Flash with your programmer of choice:
make DEVICE=atmega328p PROGRAMMER="-c avrisp2 -P usb" flash
```

`DEVICE` and `PROGRAMMER` are overridable on the command line; `CLOCK` is fixed
at 16 MHz. `make install` runs `flash` followed by `fuse`.

### Talking to it

Connect at **115200 baud, 8-N-1**. Grbl greets you with `Grbl 1.1h ['$' for help]`.
Type `$$` for settings, `$#` for offsets, `$G` for parser state, `$H` to home,
`$X` to clear an alarm lock. For streaming a file, use
[`doc/script/stream.py`](doc/script/stream.py) (character-counting protocol) or
[`doc/script/simple_stream.py`](doc/script/simple_stream.py) (simple send-response).

Full command and interface reference: [`doc/markdown/commands.md`](doc/markdown/commands.md)
and [`doc/markdown/interface.md`](doc/markdown/interface.md).

---

## Default pin assignment (Arduino Uno, stock `config.h`)

| Pin | Function | Pin | Function |
|---|---|---|---|
| D2 | X step | D9  | X limit |
| D3 | Y step | D10 | Y limit |
| D4 | Z step | D12 | Z limit |
| D5 | X direction | D11 | Spindle PWM / enable |
| D6 | Y direction | D13 | Spindle direction |
| D7 | Z direction | A0  | Reset / abort |
| D8 | Stepper enable (active low) | A1 | Feed hold *(or safety door)* |
| A3 | Coolant flood | A2 | Cycle start / resume |
| A4 | Coolant mist *(requires `ENABLE_M7`)* | A5 | Probe |

Z limit sits on **D12** because `VARIABLE_SPINDLE` (enabled by default) needs
D11's hardware PWM. Disabling variable spindle moves Z limit back to D11.
Enabling `ENABLE_SAFETY_DOOR_INPUT_PIN` repurposes A1 — the feed-hold input and
the safety-door input share that pin and cannot both be used. Enabling
`ENABLE_DUAL_AXIS` reassigns several pins; see `grbl/cpu_map.h` for the exact
map of each configuration.

---

## Repository layout

```
grbl/          Firmware sources
  config.h       Compile-time feature switches — start here
  defaults.h     Per-machine default $ setting values
  cpu_map.h      Pin, port, and timer assignments
  gcode.c        RS274/NGC parser
  planner.c      Look-ahead motion planner
  stepper.c      Segment buffer and the stepper ISR
  protocol.c     Real-time state machine and serial protocol
  examples/      Arduino IDE upload sketches
doc/
  markdown/      Interface, settings, jogging, and laser-mode specifications
  csv/           Machine-readable error, alarm, setting, and build-option tables
  script/        Python streaming and spindle-calibration scripts
  log/           Historical commit logs, v0.7 through v1.1
Makefile       Command-line build
build/         Build output (gitignored)
```

Firmware architecture, in one line:

```
serial RX ISR → line buffer → gcode.c → motion_control.c → planner.c
              → segment buffer → TIMER1 ISR → step/direction pins
```

`grbl/grbl.h` is the single include hub; every `.c` file includes only it.

---

## Configuration

Behavior is set two ways:

**`$` settings** (stored in EEPROM, changeable at runtime) cover steps/mm, max
rates, accelerations, travel limits, homing, spindle RPM range, and the various
invert masks. See [`doc/markdown/settings.md`](doc/markdown/settings.md) and
[`doc/csv/setting_codes_en_US.csv`](doc/csv/setting_codes_en_US.csv).

**Compile-time options** in `grbl/config.h` control features that cost flash or
change the pin map, including:

| Option | Effect |
|---|---|
| `VARIABLE_SPINDLE` | PWM spindle speed control on D11 *(default on)* |
| `ENABLE_M7` | Mist coolant on A4 |
| `ENABLE_SAFETY_DOOR_INPUT_PIN` | Safety-door input on A1 *(replaces feed hold)* |
| `PARKING_ENABLE` | Retract/park motion on safety-door hold |
| `COREXY` | CoreXY / H-bot kinematics |
| `ENABLE_DUAL_AXIS` | Second motor on X or Y with self-squaring homing |
| `ENABLE_PIECEWISE_LINEAR_SPINDLE` | Nonlinear RPM→PWM model (see `doc/script/fit_nonlinear_spindle.py`) |
| `HOMING_SINGLE_AXIS_COMMANDS` | `$HX` / `$HY` / `$HZ` |
| `ADAPTIVE_MULTI_AXIS_STEP_SMOOTHING` | AMASS step smoothing *(default on)* |

Incompatible combinations are caught at compile time by the `#error` block at
the bottom of `grbl/grbl.h`.

**Flash and SRAM are the binding constraint.** The 328P has 32 KB / 2 KB and the
stock build uses nearly all of both; enabling several optional features at once
will not fit. Check the `avr-size` output that `make` prints.

---

## Update summary for v1.1

- **IMPORTANT:** Upgrading from v1.0 or earlier wipes your EEPROM and restores
  defaults, due to two new spindle-speed `$` settings.

- **Real-time overrides.** Feed, rapid, spindle speed, spindle stop, and coolant
  toggle applied to a running job within tens of milliseconds — a feature usually
  found only on industrial controls.

- **Jogging mode.** `$J=` jog commands run independently of the G-code parser, so
  parser state is never disturbed and can't be left inconsistent after a cancel.
  Low enough latency to drive from a joystick or rotary dial. See
  [`doc/markdown/jogging.md`](doc/markdown/jogging.md).

- **Laser mode.** Moves continuously through consecutive G1/G2/G3 commands with
  spindle-speed changes instead of stopping to let a spindle spin up. Includes
  **dynamic laser power scaling with speed** (via `M4`), which compensates for
  low machine acceleration so corners don't get overburnt. Toggled with `$32`.
  See [`doc/markdown/laser_mode.md`](doc/markdown/laser_mode.md).

- **Sleep mode.** `$SLP` disables everything including the stepper drivers. Only
  a reset exits.

- **Significant interface improvements**, driven by direct feedback from GUI
  developers. *GUIs must be updated specifically for v1.1.*
  - **New status reports** — more data in fewer bytes.
  - **Error and alarm codes** — every message carries a numeric code with a
    documented meaning, shipped as parseable CSV in [`doc/csv/`](doc/csv).
  - **Extended-ASCII real-time commands** — overrides live above 0x7F so they
    can't be triggered accidentally by characters in a G-code file.
  - **Message prefixes** — every message type is identifiable without context.

- **OEM features:** safety-door parking, single-file build configuration, EEPROM
  restriction and restore controls, and stored product data.

- **Safety-door parking** as a compile option: retract, disable spindle and
  coolant, park near Z max; reverse on resume. Highly configurable.

- **Spindle min/max RPM settings** (`$30`/`$31`) to match PWM output to true
  spindle speed. Setting max to zero or below min turns D11 into a plain on/off
  enable output.

- **G28/G30 updated** from the NIST to the LinuxCNC description: with an
  intermediate motion specified, only the named axes move.

- Many minor bug fixes and refactors.

- **NOTE:** Arduino Mega2560 support moved to the separate
  [grbl-Mega](http://www.github.com/gnea/grbl-Mega/) project.

---

## Supported G-codes

```
  Non-Modal Commands: G4, G10L2, G10L20, G28, G30, G28.1, G30.1, G53, G92, G92.1
  Motion Modes: G0, G1, G2, G3, G38.2, G38.3, G38.4, G38.5, G80
  Feed Rate Modes: G93, G94
  Unit Modes: G20, G21
  Distance Modes: G90, G91
  Arc IJK Distance Modes: G91.1
  Plane Select Modes: G17, G18, G19
  Tool Length Offset Modes: G43.1, G49
  Cutter Compensation Modes: G40
  Coordinate System Modes: G54, G55, G56, G57, G58, G59
  Control Modes: G61
  Program Flow: M0, M1, M2, M30*
  Coolant Control: M7*, M8, M9
  Spindle Control: M3, M4, M5
  Valid Non-Command Words: F, I, J, K, L, N, P, R, S, T, X, Y, Z
```

Not supported: canned cycles, cutter radius compensation (G41/G42), A/B/C axes,
tool changes (M6), expressions, variables, and subprograms. Macros and canned
cycles are better expanded by the CAM tool or GUI than by a 328P.

---

### Official supporters of the Grbl CNC project

![Official Supporters](https://github.com/gnea/gnea-Media/blob/master/Contributors.png?raw=true)
