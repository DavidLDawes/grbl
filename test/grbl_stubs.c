/* test/grbl_stubs.c
 *
 * Host-build stand-ins for the modules gcode.c and planner.c depend on
 * but this harness does not compile: motion_control.c, spindle_control.c,
 * coolant_control.c, report.c, protocol.c, system.c, settings.c, jog.c,
 * and stepper.c. Also defines the three global state objects (sys,
 * sys_position, settings) that main.c owns on the real firmware.
 *
 * These are deliberately *not* a hardware simulation — they record what
 * they were called with so tests can assert on it, and otherwise do the
 * minimum needed to keep gcode.c/planner.c's control flow correct (e.g.
 * settings_read_coord_data() must behave like a real fresh-EEPROM read,
 * because gc_init() depends on its return value).
 */
#include "grbl.h"
#include "grbl_stubs.h"

// ---------------------------------------------------------------------
// Global state normally owned by main.c / settings.c
// ---------------------------------------------------------------------

system_t sys;
int32_t sys_position[N_AXIS];
int32_t sys_probe_position[N_AXIS];
volatile uint8_t sys_probe_state;
volatile uint8_t sys_rt_exec_state;
volatile uint8_t sys_rt_exec_alarm;
volatile uint8_t sys_rt_exec_motion_override;
volatile uint8_t sys_rt_exec_accessory_override;

// Mirrors settings.c's `defaults` literal field-for-field so tests run
// against the same numbers a freshly-flashed board would report on $$.
// (settings.c itself is not compiled here — it pulls in EEPROM access,
// __flash storage, and re-initializes several hardware-facing modules
// on every $ write, none of which exist on the host.)
settings_t settings = {
    .pulse_microseconds = DEFAULT_STEP_PULSE_MICROSECONDS,
    .stepper_idle_lock_time = DEFAULT_STEPPER_IDLE_LOCK_TIME,
    .step_invert_mask = DEFAULT_STEPPING_INVERT_MASK,
    .dir_invert_mask = DEFAULT_DIRECTION_INVERT_MASK,
    .status_report_mask = DEFAULT_STATUS_REPORT_MASK,
    .junction_deviation = DEFAULT_JUNCTION_DEVIATION,
    .arc_tolerance = DEFAULT_ARC_TOLERANCE,
    .rpm_max = DEFAULT_SPINDLE_RPM_MAX,
    .rpm_min = DEFAULT_SPINDLE_RPM_MIN,
    .homing_dir_mask = DEFAULT_HOMING_DIR_MASK,
    .homing_feed_rate = DEFAULT_HOMING_FEED_RATE,
    .homing_seek_rate = DEFAULT_HOMING_SEEK_RATE,
    .homing_debounce_delay = DEFAULT_HOMING_DEBOUNCE_DELAY,
    .homing_pulloff = DEFAULT_HOMING_PULLOFF,
    .flags = (DEFAULT_REPORT_INCHES << BIT_REPORT_INCHES) |
             (DEFAULT_LASER_MODE << BIT_LASER_MODE) |
             (DEFAULT_INVERT_ST_ENABLE << BIT_INVERT_ST_ENABLE) |
             (DEFAULT_HARD_LIMIT_ENABLE << BIT_HARD_LIMIT_ENABLE) |
             (DEFAULT_HOMING_ENABLE << BIT_HOMING_ENABLE) |
             (DEFAULT_SOFT_LIMIT_ENABLE << BIT_SOFT_LIMIT_ENABLE) |
             (DEFAULT_INVERT_LIMIT_PINS << BIT_INVERT_LIMIT_PINS) |
             (DEFAULT_INVERT_PROBE_PIN << BIT_INVERT_PROBE_PIN),
    .steps_per_mm[X_AXIS] = DEFAULT_X_STEPS_PER_MM,
    .steps_per_mm[Y_AXIS] = DEFAULT_Y_STEPS_PER_MM,
    .steps_per_mm[Z_AXIS] = DEFAULT_Z_STEPS_PER_MM,
    .max_rate[X_AXIS] = DEFAULT_X_MAX_RATE,
    .max_rate[Y_AXIS] = DEFAULT_Y_MAX_RATE,
    .max_rate[Z_AXIS] = DEFAULT_Z_MAX_RATE,
    .acceleration[X_AXIS] = DEFAULT_X_ACCELERATION,
    .acceleration[Y_AXIS] = DEFAULT_Y_ACCELERATION,
    .acceleration[Z_AXIS] = DEFAULT_Z_ACCELERATION,
    .max_travel[X_AXIS] = (-DEFAULT_X_MAX_TRAVEL),
    .max_travel[Y_AXIS] = (-DEFAULT_Y_MAX_TRAVEL),
    .max_travel[Z_AXIS] = (-DEFAULT_Z_MAX_TRAVEL)
};

// ---------------------------------------------------------------------
// Call-recording hooks (declared in grbl_stubs.h)
// ---------------------------------------------------------------------

uint8_t test_mc_line_calls;
float test_mc_line_last_target[N_AXIS];

uint8_t test_mc_arc_calls;

uint8_t test_mc_dwell_calls;
float test_mc_dwell_last_seconds;

uint8_t test_mc_probe_cycle_return;

uint8_t test_spindle_last_state;
float test_spindle_last_rpm;
uint8_t test_spindle_calls;

uint8_t test_coolant_last_mode;
uint8_t test_coolant_calls;

uint8_t test_report_status_last_code;
uint8_t test_report_status_calls;
uint8_t test_report_feedback_calls;

uint8_t test_settings_read_coord_calls;

uint8_t test_settings_write_coord_calls;
uint8_t test_settings_write_coord_last_select;
float test_settings_write_coord_last_data[N_AXIS];

uint8_t test_system_exec_state_flags;
uint8_t test_system_wco_change_calls;

uint8_t test_jog_execute_return;

uint8_t test_st_update_plan_block_parameters_calls;

void test_world_reset(void)
{
  memset(&sys, 0, sizeof(system_t));
  sys.f_override = DEFAULT_FEED_OVERRIDE;
  sys.r_override = DEFAULT_RAPID_OVERRIDE;
  sys.spindle_speed_ovr = DEFAULT_SPINDLE_SPEED_OVERRIDE;
  memset(sys_position, 0, sizeof(sys_position));
  memset(sys_probe_position, 0, sizeof(sys_probe_position));
  sys_probe_state = 0;
  sys_rt_exec_state = 0;
  sys_rt_exec_alarm = 0;
  sys_rt_exec_motion_override = 0;
  sys_rt_exec_accessory_override = 0;

  test_mc_line_calls = 0;
  memset(test_mc_line_last_target, 0, sizeof(test_mc_line_last_target));
  test_mc_arc_calls = 0;
  test_mc_dwell_calls = 0;
  test_mc_dwell_last_seconds = 0.0f;
  test_mc_probe_cycle_return = GC_PROBE_FOUND;
  test_spindle_last_state = 0;
  test_spindle_last_rpm = 0.0f;
  test_spindle_calls = 0;
  test_coolant_last_mode = 0;
  test_coolant_calls = 0;
  test_report_status_last_code = STATUS_OK;
  test_report_status_calls = 0;
  test_report_feedback_calls = 0;
  test_settings_read_coord_calls = 0;
  test_settings_write_coord_calls = 0;
  test_settings_write_coord_last_select = 0;
  memset(test_settings_write_coord_last_data, 0, sizeof(test_settings_write_coord_last_data));
  test_system_exec_state_flags = 0;
  test_system_wco_change_calls = 0;
  test_jog_execute_return = STATUS_OK;
  test_st_update_plan_block_parameters_calls = 0;
}

// ---------------------------------------------------------------------
// motion_control.c stand-ins
// ---------------------------------------------------------------------

void mc_line(float *target, plan_line_data_t *pl_data)
{
  (void)pl_data;
  test_mc_line_calls++;
  memcpy(test_mc_line_last_target, target, sizeof(float) * N_AXIS);
}

void mc_arc(float *target, plan_line_data_t *pl_data, float *position, float *offset, float radius,
  uint8_t axis_0, uint8_t axis_1, uint8_t axis_linear, uint8_t is_clockwise_arc)
{
  (void)target; (void)pl_data; (void)position; (void)offset; (void)radius;
  (void)axis_0; (void)axis_1; (void)axis_linear; (void)is_clockwise_arc;
  test_mc_arc_calls++;
}

void mc_dwell(float seconds)
{
  test_mc_dwell_calls++;
  test_mc_dwell_last_seconds = seconds;
}

uint8_t mc_probe_cycle(float *target, plan_line_data_t *pl_data, uint8_t parser_flags)
{
  (void)target; (void)pl_data; (void)parser_flags;
  return test_mc_probe_cycle_return;
}

// ---------------------------------------------------------------------
// spindle_control.c / coolant_control.c stand-ins
// ---------------------------------------------------------------------

#ifdef VARIABLE_SPINDLE
void spindle_set_state(uint8_t state, float rpm)
{
  test_spindle_calls++;
  test_spindle_last_state = state;
  test_spindle_last_rpm = rpm;
}

void spindle_sync(uint8_t state, float rpm)
{
  spindle_set_state(state, rpm);
}
#endif

void coolant_set_state(uint8_t mode)
{
  test_coolant_calls++;
  test_coolant_last_mode = mode;
}

void coolant_sync(uint8_t mode)
{
  coolant_set_state(mode);
}

// ---------------------------------------------------------------------
// report.c stand-ins
// ---------------------------------------------------------------------

void report_status_message(uint8_t status_code)
{
  test_report_status_calls++;
  test_report_status_last_code = status_code;
}

void report_feedback_message(uint8_t message_code)
{
  (void)message_code;
  test_report_feedback_calls++;
}

// ---------------------------------------------------------------------
// settings.c stand-ins
// ---------------------------------------------------------------------

uint8_t settings_read_coord_data(uint8_t coord_select, float *coord_data)
{
  (void)coord_select;
  test_settings_read_coord_calls++;
  clear_vector_float(coord_data); // Simulate a blank/erased coordinate slot.
  return true;
}

void settings_write_coord_data(uint8_t coord_select, float *coord_data)
{
  test_settings_write_coord_calls++;
  test_settings_write_coord_last_select = coord_select;
  memcpy(test_settings_write_coord_last_data, coord_data, sizeof(float) * N_AXIS);
}

// get_direction_pin_mask() is a real (not recorded) re-implementation:
// planner.c uses it to set each block's direction bits from the sign of
// the commanded move, and settings.c (not compiled here) is where the
// real one lives. The *_DIRECTION_BIT values are plain integers from
// cpu_map.h with no register dependency, so this mirrors it exactly.
uint8_t get_direction_pin_mask(uint8_t axis_idx)
{
  if (axis_idx == X_AXIS) { return (1 << X_DIRECTION_BIT); }
  if (axis_idx == Y_AXIS) { return (1 << Y_DIRECTION_BIT); }
  return (1 << Z_DIRECTION_BIT);
}

// ---------------------------------------------------------------------
// system.c stand-ins
// ---------------------------------------------------------------------

void system_set_exec_state_flag(uint8_t mask)
{
  test_system_exec_state_flags |= mask;
}

void system_flag_wco_change(void)
{
  test_system_wco_change_calls++;
}

// Real (not recorded) re-implementation, matching system.c exactly —
// gcode.c and planner.c both rely on its actual arithmetic, not just on
// it having been called, e.g. gc_sync_position() and mc_probe_cycle's
// real counterpart depend on this producing genuine mm positions.
void system_convert_array_steps_to_mpos(float *position, int32_t *steps)
{
  uint8_t idx;
  for (idx = 0; idx < N_AXIS; idx++) {
    position[idx] = steps[idx] / settings.steps_per_mm[idx];
  }
}

// ---------------------------------------------------------------------
// protocol.c stand-ins
// ---------------------------------------------------------------------

void protocol_buffer_synchronize(void)
{
  // Real Grbl blocks here until the planner buffer empties. Nothing in
  // this harness ever populates a running stepper ISR, so there is
  // nothing to wait for — the call is a synchronization point only.
}

void protocol_execute_realtime(void)
{
}

void protocol_exec_rt_system(void)
{
}

// ---------------------------------------------------------------------
// jog.c stand-in
// ---------------------------------------------------------------------

uint8_t jog_execute(plan_line_data_t *pl_data, parser_block_t *gc_block)
{
  (void)pl_data; (void)gc_block;
  return test_jog_execute_return;
}

// ---------------------------------------------------------------------
// stepper.c stand-in
// ---------------------------------------------------------------------

void st_update_plan_block_parameters(void)
{
  test_st_update_plan_block_parameters_calls++;
}
