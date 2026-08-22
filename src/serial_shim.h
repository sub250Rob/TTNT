// src/serial_shim.h
// C-callable wrappers around the Arduino core's C++-only Serial API.
#ifndef SERIAL_SHIM_H
#define SERIAL_SHIM_H

#ifdef __cplusplus
extern "C" {
#endif

// Starts the UART and wires stdout to it, so printf() works from C.
void serial_begin(unsigned long baud);

// Cheaper alternative to printf() when no format specifiers are needed.
void serial_print(const char *s);

#ifdef __cplusplus
}
#endif

#endif
