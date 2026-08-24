// src/timebase.c
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

#include "timebase.h"
#include "config.h"

// ---- Deriving OCR2A from the requested tick --------------------------------
//
// Timer2 counts 0..OCR2A inclusive, which is OCR2A+1 ticks, then clears itself
// and fires the interrupt. Deriving the register value from TIMEBASE_TICK_MS
// rather than hard-coding 249 means the two can never disagree.
//
//   16 MHz / 128 = 125000 counter ticks per second
//   125000 * 2 ms / 1000 = 250 ticks
//   OCR2A = 250 - 1 = 249

#define TIMEBASE_PRESCALER      128UL
#define TIMEBASE_TICKS_PER_SEC  (F_CPU / TIMEBASE_PRESCALER)
#define TIMEBASE_OCR2A_VAL      (((TIMEBASE_TICKS_PER_SEC * TIMEBASE_TICK_MS) / 1000UL) - 1UL)

#if (F_CPU % TIMEBASE_PRESCALER) != 0
#error "The /128 prescaler does not divide F_CPU evenly"
#endif

#if ((TIMEBASE_TICKS_PER_SEC * TIMEBASE_TICK_MS) % 1000UL) != 0
#error "TIMEBASE_TICK_MS is not exactly achievable with the /128 prescaler"
#endif

// OCR2A is one byte. At /128 that caps the tick at 2 ms; 3 ms would need 374.
// A longer tick means moving to the /256 or /1024 prescaler.
#if TIMEBASE_OCR2A_VAL > 255UL
#error "TIMEBASE_TICK_MS too long for the /128 prescaler -- OCR2A only holds 255"
#endif

// ---- State shared with the ISR ---------------------------------------------
//
// volatile so the compiler re-reads memory instead of caching these in a
// register. That is all volatile does -- it says nothing about interrupts
// landing between two separate accesses, which is what the ATOMIC_BLOCKs below
// are for. Both are needed, and they solve different problems.

static volatile uint8_t sample_due;
static volatile uint8_t overrun;

ISR(TIMER2_COMPA_vect)
{
  // Only the ISR touches this, so it must be static to persist but must NOT be
  // volatile -- there is nothing to synchronise, and volatile would only stop
  // the compiler keeping it in a register.
  static uint16_t ticks = 0;

  if (++ticks >= SAMPLE_INTERVAL_TICKS) {
    ticks = 0;

    // The previous request was never consumed, so loop() is running slower
    // than the sample interval. Free to detect, and the alternative is the
    // cadence quietly stretching with no indication.
    if (sample_due) {
      overrun = 1;
    }

    sample_due = 1;
  }
}

// ---- Public API ------------------------------------------------------------

void timebase_init(void)
{
  // Stop the clock first. The core left Timer2 running as PWM, and configuring
  // a live timer risks a spurious compare match against a stale OCR2A.
  TCCR2B = 0;

  // ASSIGN, never OR. The Arduino core's init() has already set TCCR2A.WGM20
  // and TCCR2B.CS22 to run Timer2 as default phase-correct PWM. ORing would
  // leave WGM20 set, selecting Fast PWM instead of CTC: the timer would run,
  // the ISR would fire, and the period would be wrong with nothing to show why.
  //
  // WGM21 alone (with WGM20 and WGM22 clear) is mode 2, CTC with TOP = OCR2A.
  // The COM bits stay 0, leaving pins 11 and 3 disconnected from the timer.
  TCCR2A = (1 << WGM21);

  OCR2A = (uint8_t)TIMEBASE_OCR2A_VAL;
  TCNT2 = 0;

  // Clear any pending compare-match flag, or enabling the interrupt below
  // would fire it immediately. On AVR an interrupt flag is cleared by writing
  // a ONE to it, not a zero.
  TIFR2 = (1 << OCF2A);

  TIMSK2 = (1 << OCIE2A);

  // Start the clock last, so the first interval is a full one.
  // CS22:CS21:CS20 = 101 selects the /128 prescaler.
  TCCR2B = (1 << CS22) | (1 << CS20);

  // No sei() needed: the core's init() enables interrupts before setup() runs.
}

uint8_t timebase_sample_due(void)
{
  uint8_t due;

  // Read-then-clear is two operations. Without this block an interrupt landing
  // between them would set the flag and have it immediately erased, silently
  // dropping a sample. ATOMIC_RESTORESTATE saves SREG, disables interrupts,
  // and restores SREG on exit -- so it re-enables them only if they were
  // already on, unlike ATOMIC_FORCEON.
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    due = sample_due;
    sample_due = 0;
  }

  return due;
}

uint8_t timebase_overrun(void)
{
  uint8_t missed;

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    missed = overrun;
    overrun = 0;
  }

  return missed;
}
