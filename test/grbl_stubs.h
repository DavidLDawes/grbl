/* test/grbl_stubs.h
 *
 * Test-visible hooks into the stub implementations in grbl_stubs.c.
 * Real Grbl modules that this harness doesn't compile (motion_control.c,
 * spindle_control.c, coolant_control.c, report.c, protocol.c, system.c,
 * settings.c) are replaced here by minimal stand-ins. Tests that care
 * what a stub was called with read it back through these globals rather
 * than through a mocking framework.
 */
#ifndef GRBL_TEST_STUBS_H
#define GRBL_TEST_STUBS_H

#include "grbl.h"

// Re-initializes sys/sys_position/settings to the same power-up state
// main.c would produce, and clears every call-recording hook below.
// Call this at the start of every test case that touches gcode.c or
// planner.c global state, so tests don't leak state into each other.
void test_world_reset(void);

// mc_line(): records the most recent call. mc_arc() calls back into the
// real mc_line() signature per generated segment in production, but this
// stub does not attempt to reproduce arc segmentation — it only counts
// calls and records the final target, which is what gc_execute_line()
// itself passes through unchanged for G1/G0/G28/G30.
extern uint8_t test_mc_line_calls;
extern float test_mc_line_last_target[N_AXIS];

// mc_arc(): call count only. Segment-by-segment arc geometry is
// motion_control.c's responsibility and out of scope for this harness.
extern uint8_t test_mc_arc_calls;

// mc_dwell(): records the last requested dwell time.
extern uint8_t test_mc_dwell_calls;
extern float test_mc_dwell_last_seconds;

// mc_probe_cycle(): the stub returns whatever test code sets here before
// invoking gc_execute_line(), so tests can exercise both the success and
// failure return paths without real hardware.
extern uint8_t test_mc_probe_cycle_return;

// spindle_set_state() / spindle_sync(): records the most recent state
// and rpm passed by gcode.c's spindle control handling.
extern uint8_t test_spindle_last_state;
extern float test_spindle_last_rpm;
extern uint8_t test_spindle_calls;

// coolant_set_state() / coolant_sync(): records the most recent mode.
extern uint8_t test_coolant_last_mode;
extern uint8_t test_coolant_calls;

// report_status_message() / report_feedback_message(): gcode.c only
// calls these directly in a couple of places (gc_init() setting read
// failure, M2/M30 program end feedback) — most status reporting happens
// one layer up, in protocol.c, which this harness does not compile.
extern uint8_t test_report_status_last_code;
extern uint8_t test_report_status_calls;
extern uint8_t test_report_feedback_calls;

// settings_read_coord_data(): the stub simulates a blank/erased EEPROM
// (zeroed coordinate system) and always reports success, matching
// settings.c's own fallback behavior on a real checksum failure.
extern uint8_t test_settings_read_coord_calls;

// settings_write_coord_data(): records the most recent selector/data.
extern uint8_t test_settings_write_coord_calls;
extern uint8_t test_settings_write_coord_last_select;
extern float test_settings_write_coord_last_data[N_AXIS];

// system_set_exec_state_flag(): records the bitwise-OR of every mask
// passed since the last test_world_reset().
extern uint8_t test_system_exec_state_flags;

// system_flag_wco_change(): call count only.
extern uint8_t test_system_wco_change_calls;

// jog_execute(): the stub returns this value (default STATUS_OK) and
// otherwise behaves like a successful jog — it does not queue anything
// into a (non-existent, in this harness) planner ISR pipeline.
extern uint8_t test_jog_execute_return;

// st_update_plan_block_parameters(): call count only.
extern uint8_t test_st_update_plan_block_parameters_calls;

#endif
