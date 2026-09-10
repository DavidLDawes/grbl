/* test/test_planner.c
 *
 * Tests for grbl/planner.c against the DEFAULTS_GENERIC settings (250
 * steps/mm, 500 mm/min max rate, 10 mm/sec^2 acceleration, 200 mm travel,
 * all axes) that test/grbl_stubs.c's `settings` global is initialized
 * with. Covers the planner ring buffer bookkeeping and per-block field
 * computation in plan_buffer_line() — not velocity-profile optimization
 * across multiple queued blocks (planner_recalculate() is exercised
 * indirectly, but its output isn't asserted on here).
 */
#include <math.h>
#include "grbl.h"
#include "grbl_stubs.h"
#include "test_framework.h"

static plan_line_data_t make_pl_data(float feed_rate)
{
  plan_line_data_t pl_data;
  memset(&pl_data, 0, sizeof(plan_line_data_t));
  pl_data.feed_rate = feed_rate;
  return pl_data;
}

TEST_CASE(planner_starts_empty_after_reset)
{
  test_world_reset();
  plan_reset();
  TEST_ASSERT(plan_get_current_block() == NULL);
  TEST_ASSERT_EQ_INT(BLOCK_BUFFER_SIZE - 1, plan_get_block_buffer_available());
  TEST_ASSERT(!plan_check_full_buffer());
}

TEST_CASE(planner_rejects_zero_length_move)
{
  test_world_reset();
  plan_reset();
  float target[N_AXIS] = {0.0f, 0.0f, 0.0f}; // Same as the post-reset origin.
  plan_line_data_t pl_data = make_pl_data(100.0f);
  uint8_t status = plan_buffer_line(target, &pl_data);
  TEST_ASSERT_EQ_INT(PLAN_EMPTY_BLOCK, status);
  TEST_ASSERT(plan_get_current_block() == NULL);
  TEST_ASSERT_EQ_INT(BLOCK_BUFFER_SIZE - 1, plan_get_block_buffer_available());
}

TEST_CASE(planner_queues_a_simple_positive_x_move)
{
  test_world_reset();
  plan_reset();
  float target[N_AXIS] = {10.0f, 0.0f, 0.0f};
  plan_line_data_t pl_data = make_pl_data(100.0f);
  uint8_t status = plan_buffer_line(target, &pl_data);
  TEST_ASSERT_EQ_INT(PLAN_OK, status);

  plan_block_t *block = plan_get_current_block();
  TEST_ASSERT(block != NULL);
  TEST_ASSERT_EQ_INT(BLOCK_BUFFER_SIZE - 2, plan_get_block_buffer_available());

  // 10 mm at the default 250 steps/mm -> exactly 2500 steps, no rounding.
  TEST_ASSERT_EQ_INT(2500, block->steps[X_AXIS]);
  TEST_ASSERT_EQ_INT(0, block->steps[Y_AXIS]);
  TEST_ASSERT_EQ_INT(0, block->steps[Z_AXIS]);
  TEST_ASSERT_EQ_INT(2500, block->step_event_count);

  // Positive move: direction bit must be clear ("bit enabled always means
  // direction is negative", per the comment in plan_buffer_line()).
  TEST_ASSERT_EQ_INT(0, block->direction_bits & get_direction_pin_mask(X_AXIS));

  TEST_ASSERT_NEAR(10.0, block->millimeters, 0.0001);
  TEST_ASSERT_NEAR(100.0, block->programmed_rate, 0.0001);
  TEST_ASSERT_NEAR(500.0, block->rapid_rate, 0.01); // Pure X move: unconstrained by Y/Z.

  // First block in an empty buffer always starts from rest.
  TEST_ASSERT_NEAR(0.0, block->entry_speed_sqr, 0.0001);
  TEST_ASSERT_NEAR(0.0, block->max_junction_speed_sqr, 0.0001);
}

TEST_CASE(planner_sets_direction_bit_for_negative_move)
{
  test_world_reset();
  plan_reset();
  float target[N_AXIS] = {-10.0f, 0.0f, 0.0f};
  plan_line_data_t pl_data = make_pl_data(100.0f);
  plan_buffer_line(target, &pl_data);

  plan_block_t *block = plan_get_current_block();
  TEST_ASSERT(block != NULL);
  TEST_ASSERT_EQ_INT(2500, block->steps[X_AXIS]);
  TEST_ASSERT(block->direction_bits & get_direction_pin_mask(X_AXIS));
}

TEST_CASE(planner_diagonal_move_scales_rapid_rate_by_unit_vector)
{
  test_world_reset();
  plan_reset();
  float target[N_AXIS] = {10.0f, 10.0f, 0.0f};
  plan_line_data_t pl_data = make_pl_data(100.0f);
  plan_buffer_line(target, &pl_data);

  plan_block_t *block = plan_get_current_block();
  TEST_ASSERT(block != NULL);
  TEST_ASSERT_EQ_INT(2500, block->steps[X_AXIS]);
  TEST_ASSERT_EQ_INT(2500, block->steps[Y_AXIS]);
  // Equal X/Y max rates (500) at 45 degrees -> binding rate is 500*sqrt(2).
  TEST_ASSERT_NEAR(500.0 * sqrt(2.0), block->rapid_rate, 0.1);
  TEST_ASSERT_NEAR(hypot_f(10.0f, 10.0f), block->millimeters, 0.001);
}

TEST_CASE(planner_rapid_motion_uses_rapid_rate_as_programmed_rate)
{
  test_world_reset();
  plan_reset();
  float target[N_AXIS] = {10.0f, 0.0f, 0.0f};
  plan_line_data_t pl_data = make_pl_data(0.0f); // Feed rate is ignored for rapids.
  pl_data.condition |= PL_COND_FLAG_RAPID_MOTION;
  plan_buffer_line(target, &pl_data);

  plan_block_t *block = plan_get_current_block();
  TEST_ASSERT(block != NULL);
  TEST_ASSERT_NEAR(block->rapid_rate, block->programmed_rate, 0.0001);
  TEST_ASSERT_NEAR(500.0, block->programmed_rate, 0.01);
}

TEST_CASE(planner_discard_current_block_empties_single_block_buffer)
{
  test_world_reset();
  plan_reset();
  float target[N_AXIS] = {10.0f, 0.0f, 0.0f};
  plan_line_data_t pl_data = make_pl_data(100.0f);
  plan_buffer_line(target, &pl_data);
  TEST_ASSERT(plan_get_current_block() != NULL);

  plan_discard_current_block();
  TEST_ASSERT(plan_get_current_block() == NULL);
  TEST_ASSERT_EQ_INT(BLOCK_BUFFER_SIZE - 1, plan_get_block_buffer_available());
}

TEST_CASE(planner_queues_multiple_blocks_in_order)
{
  test_world_reset();
  plan_reset();
  float target1[N_AXIS] = {10.0f, 0.0f, 0.0f};
  float target2[N_AXIS] = {10.0f, 10.0f, 0.0f};
  plan_line_data_t pl_data = make_pl_data(100.0f);

  plan_buffer_line(target1, &pl_data);
  plan_buffer_line(target2, &pl_data);
  TEST_ASSERT_EQ_INT(BLOCK_BUFFER_SIZE - 3, plan_get_block_buffer_available());

  // First block queued is still the current (oldest/head-of-line) block.
  plan_block_t *block = plan_get_current_block();
  TEST_ASSERT(block != NULL);
  TEST_ASSERT_NEAR(10.0, block->millimeters, 0.001); // target1's distance from origin.

  plan_discard_current_block();
  block = plan_get_current_block();
  TEST_ASSERT(block != NULL);
  TEST_ASSERT_NEAR(10.0, block->millimeters, 0.001); // target2's distance from target1 (pure Y move).
}

void run_planner_tests(void)
{
  RUN_TEST(planner_starts_empty_after_reset);
  RUN_TEST(planner_rejects_zero_length_move);
  RUN_TEST(planner_queues_a_simple_positive_x_move);
  RUN_TEST(planner_sets_direction_bit_for_negative_move);
  RUN_TEST(planner_diagonal_move_scales_rapid_rate_by_unit_vector);
  RUN_TEST(planner_rapid_motion_uses_rapid_rate_as_programmed_rate);
  RUN_TEST(planner_discard_current_block_empties_single_block_buffer);
  RUN_TEST(planner_queues_multiple_blocks_in_order);
}
