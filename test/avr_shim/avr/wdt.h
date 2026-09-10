/* test/avr_shim/avr/wdt.h
 *
 * Host-build stand-in for <avr/wdt.h>. Only ENABLE_SOFTWARE_DEBOUNCE
 * (limits.c) touches the watchdog, which this harness does not compile;
 * this header exists only so grbl.h's unconditional include succeeds.
 */
#ifndef GRBL_TEST_AVR_WDT_H
#define GRBL_TEST_AVR_WDT_H

static inline void wdt_reset(void) { }
static inline void wdt_disable(void) { }

#endif
