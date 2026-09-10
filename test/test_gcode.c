/* test/test_gcode.c
 *
 * Tests for grbl/gcode.c: gc_execute_line() parsing, error-checking, and
 * execution. Input lines are written the way protocol.c hands them to
 * the parser — pre-filtered, uppercased, whitespace- and comment-free —
 * since that pre-filtering lives in protocol.c, not gcode.c, and is out
 * of this harness's scope.
 *
 * mc_line()/mc_arc()/mc_dwell()/spindle_*()/coolant_*() are stubbed
 * (test/grbl_stubs.c), so these tests exercise gcode.c's own parsing,
 * validation, and modal-state bookkeeping — not the planner or real
 * motion. test_planner.c covers plan_buffer_line() directly.
 */
#include "grbl.h"
#include "grbl_stubs.h"
#include "test_framework.h"

// Runs gc_init() + gc_execute_line() against a fresh world, and hands
// back the status code. Kept short since it's used at the top of
// nearly every test case below.
static uint8_t exec(const char *line)
{
  char buf[LINE_BUFFER_SIZE];
  strncpy(buf, line, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  return gc_execute_line(buf);
}

static void fresh_parser(void)
{
  test_world_reset();
  gc_init();
}

TEST_CASE(gc_init_reads_coordinate_system_once)
{
  fresh_parser();
  TEST_ASSERT_EQ_INT(1, test_settings_read_coord_calls);
}

TEST_CASE(g21_selects_millimeters)
{
  fresh_parser();
  TEST_ASSERT_EQ_INT(STATUS_OK, exec("G21"));
  TEST_ASSERT_EQ_INT(UNITS_MODE_MM, gc_state.modal.units);
}

TEST_CASE(g20_selects_inches)
{
  fresh_parser();
  TEST_ASSERT_EQ_INT(STATUS_OK, exec("G20"));
  TEST_ASSERT_EQ_INT(UNITS_MODE_INCHES, gc_state.modal.units);
}

TEST_CASE(g91_selects_incremental_distance_mode)
{
  fresh_parser();
  TEST_ASSERT_EQ_INT(STATUS_OK, exec("G91"));
  TEST_ASSERT_EQ_INT(DISTANCE_MODE_INCREMENTAL, gc_state.modal.distance);
}

TEST_CASE(unknown_word_letter_is_rejected)
{
  fresh_parser();
  TEST_ASSERT_EQ_INT(STATUS_EXPECTED_COMMAND_LETTER, exec("@100"));
}

TEST_CASE(malformed_number_is_rejected)
{
  fresh_parser();
  TEST_ASSERT_EQ_INT(STATUS_BAD_NUMBER_FORMAT, exec("G"));
}

TEST_CASE(g1_without_any_prior_feed_rate_is_rejected)
{
  fresh_parser();
  // gc_state.feed_rate starts at 0.0 (gc_init() memsets gc_state), and
  // G94 (units-per-minute, the default) pushes that forward when no F
  // word is given in the block -- so a first-ever G1 with no F errors.
  TEST_ASSERT_EQ_INT(STATUS_GCODE_UNDEFINED_FEED_RATE, exec("G1X10Y10"));
  TEST_ASSERT_EQ_INT(0, test_mc_line_calls);
}

TEST_CASE(g1_with_explicit_feed_rate_moves_and_calls_mc_line)
{
  fresh_parser();
  TEST_ASSERT_EQ_INT(STATUS_OK, exec("G1X10Y10F100"));
  TEST_ASSERT_EQ_INT(1, test_mc_line_calls);
  TEST_ASSERT_NEAR(10.0, test_mc_line_last_target[X_AXIS], 0.0001);
  TEST_ASSERT_NEAR(10.0, test_mc_line_last_target[Y_AXIS], 0.0001);
  TEST_ASSERT_NEAR(0.0, test_mc_line_last_target[Z_AXIS], 0.0001);
  TEST_ASSERT_NEAR(10.0, gc_state.position[X_AXIS], 0.0001);
  TEST_ASSERT_NEAR(10.0, gc_state.position[Y_AXIS], 0.0001);
}

TEST_CASE(feed_rate_persists_modally_to_the_next_line)
{
  fresh_parser();
  TEST_ASSERT_EQ_INT(STATUS_OK, exec("G1X10F100"));
  // No F word this time -- G94 should push the prior 100 mm/min forward.
  TEST_ASSERT_EQ_INT(STATUS_OK, exec("G1X20"));
  TEST_ASSERT_EQ_INT(2, test_mc_line_calls);
}

TEST_CASE(two_motion_mode_words_is_an_axis_command_conflict)
{
  fresh_parser();
  // G0 and G1 are both modal group 1 (motion), but gcode.c's own
  // comments explain why this is flagged as AXIS_COMMAND_CONFLICT
  // rather than the generic MODAL_GROUP_VIOLATION: G0/G1/G2/G3/G38 are
  // themselves axis commands, and the axis_command conflict check for
  // that category runs before the shared modal-group bookkeeping does.
  TEST_ASSERT_EQ_INT(STATUS_GCODE_AXIS_COMMAND_CONFLICT, exec("G0G1X10F100"));
}

TEST_CASE(repeating_a_non_axis_motion_word_is_a_modal_group_violation)
{
  fresh_parser();
  // G80 (cancel motion mode) is modal group 1 like G0-G3, but is not
  // itself an axis command, so repeating it surfaces the generic
  // MODAL_GROUP_VIOLATION check instead of AXIS_COMMAND_CONFLICT.
  TEST_ASSERT_EQ_INT(STATUS_GCODE_MODAL_GROUP_VIOLATION, exec("G80G80"));
}

TEST_CASE(repeating_an_axis_word_is_rejected)
{
  fresh_parser();
  TEST_ASSERT_EQ_INT(STATUS_GCODE_WORD_REPEATED, exec("G1X10X20F100"));
}

TEST_CASE(g4_dwell_calls_mc_dwell_with_the_given_seconds)
{
  fresh_parser();
  TEST_ASSERT_EQ_INT(STATUS_OK, exec("G4P1.5"));
  TEST_ASSERT_EQ_INT(1, test_mc_dwell_calls);
  TEST_ASSERT_NEAR(1.5, test_mc_dwell_last_seconds, 0.0001);
}

TEST_CASE(m3_enables_spindle_cw_with_commanded_rpm)
{
  fresh_parser();
  TEST_ASSERT_EQ_INT(STATUS_OK, exec("M3S1000"));
  TEST_ASSERT_EQ_INT(SPINDLE_ENABLE_CW, gc_state.modal.spindle);
  TEST_ASSERT(test_spindle_calls >= 1);
  TEST_ASSERT_NEAR(1000.0, test_spindle_last_rpm, 0.0001);
}

TEST_CASE(m5_disables_spindle)
{
  fresh_parser();
  exec("M3S1000");
  TEST_ASSERT_EQ_INT(STATUS_OK, exec("M5"));
  TEST_ASSERT_EQ_INT(SPINDLE_DISABLE, gc_state.modal.spindle);
}

TEST_CASE(m8_enables_flood_coolant)
{
  fresh_parser();
  TEST_ASSERT_EQ_INT(STATUS_OK, exec("M8"));
  TEST_ASSERT(gc_state.modal.coolant & COOLANT_FLOOD_ENABLE);
  TEST_ASSERT(test_coolant_calls >= 1);
}

TEST_CASE(m30_resets_modal_state_to_program_end_defaults)
{
  fresh_parser();
  exec("G20");        // Switch to inches...
  exec("G91");         // ...and incremental mode...
  exec("M3S1000");     // ...spindle on...
  exec("M8");           // ...and coolant on.

  TEST_ASSERT_EQ_INT(STATUS_OK, exec("M30"));

  TEST_ASSERT_EQ_INT(MOTION_MODE_LINEAR, gc_state.modal.motion);
  TEST_ASSERT_EQ_INT(DISTANCE_MODE_ABSOLUTE, gc_state.modal.distance);
  TEST_ASSERT_EQ_INT(0, gc_state.modal.coord_select); // G54
  TEST_ASSERT_EQ_INT(SPINDLE_DISABLE, gc_state.modal.spindle);
  TEST_ASSERT_EQ_INT(COOLANT_DISABLE, gc_state.modal.coolant);
  TEST_ASSERT(test_report_feedback_calls >= 1); // MESSAGE_PROGRAM_END
}

TEST_CASE(g92_sets_coordinate_offset_without_commanding_motion)
{
  fresh_parser();
  // G92 makes the *current* position read as the commanded value in
  // the new work coordinate system: work_pos = machine_pos - offset,
  // so at the origin, "G92 X5" implies offset = 0 - 5 = -5, not +5.
  TEST_ASSERT_EQ_INT(STATUS_OK, exec("G92X5Y5"));
  TEST_ASSERT_NEAR(-5.0, gc_state.coord_offset[X_AXIS], 0.0001);
  TEST_ASSERT_NEAR(-5.0, gc_state.coord_offset[Y_AXIS], 0.0001);
  TEST_ASSERT_EQ_INT(0, test_mc_line_calls); // G92 does not move anything.
}

void run_gcode_tests(void)
{
  RUN_TEST(gc_init_reads_coordinate_system_once);
  RUN_TEST(g21_selects_millimeters);
  RUN_TEST(g20_selects_inches);
  RUN_TEST(g91_selects_incremental_distance_mode);
  RUN_TEST(unknown_word_letter_is_rejected);
  RUN_TEST(malformed_number_is_rejected);
  RUN_TEST(g1_without_any_prior_feed_rate_is_rejected);
  RUN_TEST(g1_with_explicit_feed_rate_moves_and_calls_mc_line);
  RUN_TEST(feed_rate_persists_modally_to_the_next_line);
  RUN_TEST(two_motion_mode_words_is_an_axis_command_conflict);
  RUN_TEST(repeating_a_non_axis_motion_word_is_a_modal_group_violation);
  RUN_TEST(repeating_an_axis_word_is_rejected);
  RUN_TEST(g4_dwell_calls_mc_dwell_with_the_given_seconds);
  RUN_TEST(m3_enables_spindle_cw_with_commanded_rpm);
  RUN_TEST(m5_disables_spindle);
  RUN_TEST(m8_enables_flood_coolant);
  RUN_TEST(m30_resets_modal_state_to_program_end_defaults);
  RUN_TEST(g92_sets_coordinate_offset_without_commanding_motion);
}
