/* test/test_main.c
 *
 * Entry point for the host-side Grbl test harness. Runs every test
 * suite and exits nonzero if anything failed, so it plugs into CI the
 * same way any other test binary would.
 *
 * Build with `make test` from the repository root (see Makefile), or
 * directly:
 *   gcc -std=c99 -Wall -I test/avr_shim -I grbl -I test \
 *       test/grbl_stubs.c test/test_main.c test/test_nuts_bolts.c \
 *       test/test_planner.c test/test_gcode.c \
 *       grbl/nuts_bolts.c grbl/planner.c grbl/gcode.c \
 *       -o test/build/grbl_test -lm
 */
#include <stdio.h>
#include "test_framework.h"

int g_test_count = 0;
int g_test_failures = 0;

void run_nuts_bolts_tests(void);
void run_planner_tests(void);
void run_gcode_tests(void);

int main(void)
{
  printf("Grbl host test harness\n");
  printf("=======================\n");

  printf("nuts_bolts.c:\n");
  run_nuts_bolts_tests();

  printf("planner.c:\n");
  run_planner_tests();

  printf("gcode.c:\n");
  run_gcode_tests();

  printf("=======================\n");
  printf("%d test case(s), %d failure(s)\n", g_test_count, g_test_failures);

  return g_test_failures ? 1 : 0;
}
