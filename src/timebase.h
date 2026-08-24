// src/timebase.h
// Hardware sample cadence on Timer2, replacing the delay() in loop().
//
// Timer2 rather than Timer1 on purpose: this reserves the 16-bit Timer1 for
// stage 3's PWM on pin 9, where ICR1 as TOP buys an exact 1% duty floor at any
// carrier frequency. The cost is PWM on pins 3 and 11, which nothing uses.
//
// Nothing here drives a pin. Timer2's output compare pins stay disconnected.
#ifndef TIMEBASE_H
#define TIMEBASE_H

#include <stdint.h>

// Configures Timer2 for a TIMEBASE_TICK_MS periodic interrupt and starts it.
// Call once from setup(), after the slow parts, so the first interval is
// measured from a settled system.
void timebase_init(void);

// Non-zero once per SAMPLE_INTERVAL_MS. Reading it consumes the request, so
// call it exactly once per pass through loop() and act on the result.
uint8_t timebase_sample_due(void);

// Non-zero if a sample request arrived while the previous one was still
// unconsumed -- i.e. loop() is taking longer than SAMPLE_INTERVAL_MS. Reading
// it clears it. Should never fire at 500 ms; stage 3 is where it earns its keep.
uint8_t timebase_overrun(void);

#endif
