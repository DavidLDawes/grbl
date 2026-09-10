/* test/test_nuts_bolts.c
 *
 * Tests for grbl/nuts_bolts.c: mostly read_float(), Grbl's hand-rolled
 * G-code number parser. It's the one piece of string handling every
 * line passes through, so it's worth pinning down precisely — including
 * the documented-but-easy-to-break "drop digits past 8 significant
 * figures" overflow behavior.
 */
#include <math.h>
#include "grbl.h"
#include "test_framework.h"

static float parse(const char *text)
{
  char line[64];
  uint8_t char_counter = 0;
  float value = -12345.0f; // Sentinel: read_float() must overwrite this on success.
  strncpy(line, text, sizeof(line) - 1);
  line[sizeof(line) - 1] = 0;
  uint8_t ok = read_float(line, &char_counter, &value);
  TEST_ASSERT(ok);
  return value;
}

TEST_CASE(read_float_parses_plain_integer)
{
  TEST_ASSERT_NEAR(100.0, parse("100"), 0.0001);
}

TEST_CASE(read_float_parses_negative_decimal)
{
  TEST_ASSERT_NEAR(-5.5, parse("-5.5"), 0.0001);
}

TEST_CASE(read_float_parses_explicit_positive_sign)
{
  TEST_ASSERT_NEAR(3.2, parse("+3.2"), 0.0001);
}

TEST_CASE(read_float_parses_leading_decimal_point)
{
  TEST_ASSERT_NEAR(0.5, parse(".5"), 0.0001);
}

TEST_CASE(read_float_parses_small_fraction)
{
  TEST_ASSERT_NEAR(0.001, parse("0.001"), 0.00001);
}

TEST_CASE(read_float_parses_typical_coordinate)
{
  TEST_ASSERT_NEAR(123.456, parse("123.456"), 0.001);
}

TEST_CASE(read_float_stops_at_next_word_and_advances_counter)
{
  char line[] = "12.5X10";
  uint8_t char_counter = 0;
  float value = 0.0f;
  uint8_t ok = read_float(line, &char_counter, &value);
  TEST_ASSERT(ok);
  TEST_ASSERT_NEAR(12.5, value, 0.0001);
  TEST_ASSERT_EQ_INT(4, char_counter); // Consumed "12.5", left pointing at 'X'.
  TEST_ASSERT_EQ_INT('X', line[char_counter]);
}

TEST_CASE(read_float_rejects_no_digits)
{
  char line[] = "X10";
  uint8_t char_counter = 0;
  float value = 0.0f;
  uint8_t ok = read_float(line, &char_counter, &value);
  TEST_ASSERT(!ok);
}

TEST_CASE(read_float_drops_digits_past_eight_significant_figures)
{
  // MAX_INT_DIGITS is 8. A 9th integer digit is dropped and compensated
  // by bumping the decimal exponent, per the comment in nuts_bolts.c.
  // "123456789" -> keep "12345678", drop the trailing 9, exp+1 -> x10.
  // Tolerance is wide (not 1.0) because 123456780 has 9 significant
  // digits and `float` only carries ~7.2 -- the single remaining
  // multiply-by-10 rounds to the nearest representable float (a ULP of
  // 8 at this magnitude), landing on 123456784, not 123456780 exactly.
  // That rounding is inherent to IEEE-754 single precision, not a
  // parser defect; what this test actually pins down is the "drop the
  // 9th digit and scale" behavior, i.e. the result is close to
  // 123456780 and nothing else (not 123456789, not a wildly wrong value).
  TEST_ASSERT_NEAR(123456780.0, parse("123456789"), 10.0);
}

TEST_CASE(hypot_f_matches_pythagorean_triple)
{
  TEST_ASSERT_NEAR(5.0, hypot_f(3.0f, 4.0f), 0.0001);
}

TEST_CASE(hypot_f_zero_vector_is_zero)
{
  TEST_ASSERT_NEAR(0.0, hypot_f(0.0f, 0.0f), 0.0001);
}

TEST_CASE(convert_delta_vector_to_unit_vector_normalizes_and_returns_magnitude)
{
  float v[N_AXIS] = {3.0f, 4.0f, 0.0f};
  float magnitude = convert_delta_vector_to_unit_vector(v);
  TEST_ASSERT_NEAR(5.0, magnitude, 0.0001);
  TEST_ASSERT_NEAR(0.6, v[X_AXIS], 0.0001);
  TEST_ASSERT_NEAR(0.8, v[Y_AXIS], 0.0001);
  TEST_ASSERT_NEAR(0.0, v[Z_AXIS], 0.0001);
  // Result should itself be a unit vector.
  TEST_ASSERT_NEAR(1.0, sqrt(v[X_AXIS]*v[X_AXIS] + v[Y_AXIS]*v[Y_AXIS] + v[Z_AXIS]*v[Z_AXIS]), 0.0001);
}

TEST_CASE(limit_value_by_axis_maximum_picks_the_binding_axis)
{
  // A 3-4-0 move with per-axis rate caps of (10, 10, 10): X needs
  // magnitude/0.6 = 16.67 to hit 10 on X, Y needs magnitude/0.8 = 12.5
  // to hit 10 on Y. Y is the binding (smaller) constraint.
  float unit_vec[N_AXIS] = {0.6f, 0.8f, 0.0f};
  float max_value[N_AXIS] = {10.0f, 10.0f, 10.0f};
  float limit = limit_value_by_axis_maximum(max_value, unit_vec);
  TEST_ASSERT_NEAR(12.5, limit, 0.01);
}

void run_nuts_bolts_tests(void)
{
  RUN_TEST(read_float_parses_plain_integer);
  RUN_TEST(read_float_parses_negative_decimal);
  RUN_TEST(read_float_parses_explicit_positive_sign);
  RUN_TEST(read_float_parses_leading_decimal_point);
  RUN_TEST(read_float_parses_small_fraction);
  RUN_TEST(read_float_parses_typical_coordinate);
  RUN_TEST(read_float_stops_at_next_word_and_advances_counter);
  RUN_TEST(read_float_rejects_no_digits);
  RUN_TEST(read_float_drops_digits_past_eight_significant_figures);
  RUN_TEST(hypot_f_matches_pythagorean_triple);
  RUN_TEST(hypot_f_zero_vector_is_zero);
  RUN_TEST(convert_delta_vector_to_unit_vector_normalizes_and_returns_magnitude);
  RUN_TEST(limit_value_by_axis_maximum_picks_the_binding_axis);
}
