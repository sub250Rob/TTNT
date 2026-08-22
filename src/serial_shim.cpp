// src/serial_shim.cpp
#include <Arduino.h>
#include <stdio.h>
#include "serial_shim.h"

// avr-libc has no default stdout, so printf() needs a stream pointed at Serial.
static int serial_putchar(char c, FILE *stream) {
  (void)stream;
  if (c == '\n') Serial.write('\r');
  Serial.write(c);
  return 0;
}

static FILE serial_stdout;

extern "C" void serial_begin(unsigned long baud) {
  Serial.begin(baud);
  fdev_setup_stream(&serial_stdout, serial_putchar, NULL, _FDEV_SETUP_WRITE);
  stdout = &serial_stdout;
}

extern "C" void serial_print(const char *s) {
  Serial.print(s);
}
