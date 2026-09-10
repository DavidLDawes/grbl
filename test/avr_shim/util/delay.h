/* test/avr_shim/util/delay.h
 *
 * Host-build stand-in for <util/delay.h>. nuts_bolts.c's delay_ms()/
 * delay_us()/delay_sec() call _delay_ms()/_delay_us() in a loop to
 * build arbitrary-length delays out of the AVR intrinsic's
 * compile-time-constant requirement. Tests must run fast and
 * deterministically with no real hardware attached, so these are
 * no-ops rather than real sleeps — the logic under test is the loop
 * counting and the real-time check-point calls around it, not the
 * wall-clock delay itself.
 */
#ifndef GRBL_TEST_UTIL_DELAY_H
#define GRBL_TEST_UTIL_DELAY_H

static inline void _delay_ms(double ms) { (void)ms; }
static inline void _delay_us(double us) { (void)us; }

#endif
