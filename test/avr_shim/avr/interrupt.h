/* test/avr_shim/avr/interrupt.h
 *
 * Host-build stand-in for <avr/interrupt.h>. Provides no-op sei()/cli()
 * so the global-interrupt-disable idiom used throughout Grbl compiles
 * and behaves as a (correctly) no-op on the host, where there is no
 * hardware interrupt controller to touch. ISR() is not used by any of
 * the modules this harness compiles (gcode.c, planner.c, nuts_bolts.c),
 * but is defined here for completeness in case that set grows.
 */
#ifndef GRBL_TEST_AVR_INTERRUPT_H
#define GRBL_TEST_AVR_INTERRUPT_H

#define ISR(vector) void vector(void)

static inline void sei(void) { }
static inline void cli(void) { }

#endif
