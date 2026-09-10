/* test/test_framework.h
 *
 * Minimal assertion/runner macros for the host-side Grbl test harness.
 * No external test library -- the whole point of this harness is to
 * compile the production Grbl sources natively with nothing but a host
 * C compiler and libm, so pulling in a third-party framework would work
 * against that.
 */
#ifndef GRBL_TEST_FRAMEWORK_H
#define GRBL_TEST_FRAMEWORK_H

#include <stdio.h>

extern int g_test_count;
extern int g_test_failures;

#define TEST_CASE(name) static void name(void)

#define RUN_TEST(name) \
  do { \
    int before = g_test_failures; \
    g_test_count++; \
    name(); \
    if (g_test_failures == before) { printf("  [PASS] %s\n", #name); } \
  } while (0)

#define TEST_ASSERT(cond) \
  do { \
    if (!(cond)) { \
      g_test_failures++; \
      printf("  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
  } while (0)

#define TEST_ASSERT_EQ_INT(expected, actual) \
  do { \
    long _e = (long)(expected); \
    long _a = (long)(actual); \
    if (_e != _a) { \
      g_test_failures++; \
      printf("  [FAIL] %s:%d: expected %ld, got %ld (%s)\n", __FILE__, __LINE__, _e, _a, #actual); \
    } \
  } while (0)

#define TEST_ASSERT_NEAR(expected, actual, tol) \
  do { \
    double _e = (double)(expected); \
    double _a = (double)(actual); \
    double _t = (double)(tol); \
    double _d = _e - _a; \
    if (_d < 0) { _d = -_d; } \
    if (_d > _t) { \
      g_test_failures++; \
      printf("  [FAIL] %s:%d: expected %g +/- %g, got %g (%s)\n", __FILE__, __LINE__, _e, _t, _a, #actual); \
    } \
  } while (0)

#endif
