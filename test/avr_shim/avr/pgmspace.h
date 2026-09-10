/* test/avr_shim/avr/pgmspace.h
 *
 * Host-build stand-in for <avr/pgmspace.h>. On the AVR target this
 * lets strings live in flash instead of SRAM; on the host there is only
 * one address space, so every one of these collapses to its ordinary
 * equivalent. None of gcode.c, planner.c, or nuts_bolts.c currently use
 * PROGMEM/PSTR (that's report.c/print.c/settings.c), but the header
 * must still be includable via grbl.h's unconditional include chain.
 */
#ifndef GRBL_TEST_AVR_PGMSPACE_H
#define GRBL_TEST_AVR_PGMSPACE_H

#include <string.h>

#define PROGMEM
#define PSTR(s) (s)
#define pgm_read_byte_near(addr) (*(const unsigned char *)(addr))
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#define memcpy_P(dest, src, n) memcpy((dest), (src), (n))
#define strncpy_P(dest, src, n) strncpy((dest), (src), (n))

#endif
