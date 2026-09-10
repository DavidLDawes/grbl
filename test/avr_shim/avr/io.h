/* test/avr_shim/avr/io.h
 *
 * Host-build stand-in for <avr/io.h>.
 *
 * The real header defines every AVR special function register (PORTB,
 * DDRD, TCCR1A, UCSR0A, ...) as memory-mapped I/O addresses. Those names
 * are only *referenced* by grbl/cpu_map.h and by the hardware-facing
 * modules (stepper.c, serial.c, spindle_control.c, coolant_control.c,
 * limits.c, probe.c, system.c) that this host test harness does not
 * compile. cpu_map.h's "#define STEP_DDR DDRD" style macros are harmless
 * as long as nothing in the compiled translation units actually expands
 * them, so this shim intentionally defines nothing — its only job is to
 * let "#include <avr/io.h>" succeed on a host compiler.
 */
#ifndef GRBL_TEST_AVR_IO_H
#define GRBL_TEST_AVR_IO_H
#endif
