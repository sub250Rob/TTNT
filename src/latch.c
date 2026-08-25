// src/latch.c
#include <avr/io.h>

#include "latch.h"
#include "config.h"

// 32 bits, chosen to be an unlikely SRAM power-up pattern -- alternating bits
// rather than 0x00000000 or 0xFFFFFFFF, which uninitialised RAM genuinely does
// produce. The odds of a real power-on matching this by chance are about one
// in four billion, and even then the failure lands in the safe direction: we
// would boot latched when we did not need to, leaving the driver dimmed.
#define LATCH_MAGIC  0x5AA5C33CUL

// Both live in .noinit, so the C startup code neither zeroes nor initialises
// them and their contents carry across a reset. Verified in the disassembly:
// the .bss clear loop stops exactly where .noinit begins.
static uint32_t magic   __attribute__((section(".noinit")));
static uint8_t  armed   __attribute__((section(".noinit")));

// Ordinary .bss -- recomputed every boot, never needs to survive one.
static uint8_t  power_on;
static uint16_t blink_counter;

void latch_init(void)
{
  if (magic == LATCH_MAGIC) {
    // RAM was preserved, so power was never removed. Whatever `armed` holds is
    // the state we were in before the reset, and we keep it.
    power_on = 0u;
  } else {
    // RAM came up with arbitrary content: a genuine power-on. This is the only
    // path that clears the latch, and it corresponds exactly to the pack being
    // disconnected for a swap or a recharge.
    power_on = 1u;
    magic    = LATCH_MAGIC;
    armed    = 0u;
  }

  // Indicator pin as an output, initially off.
  LATCH_LED_DDR  |= (1 << LATCH_LED_BIT);
  LATCH_LED_PORT &= (uint8_t)~(1 << LATCH_LED_BIT);

  blink_counter = 0u;
}

uint8_t latch_was_power_on(void)
{
  return power_on;
}

uint8_t latch_is_set(void)
{
  // Any non-zero value counts as armed, not one specific pattern. If the magic
  // ever matched by chance and `armed` held garbage, garbage is far more likely
  // to be non-zero than zero -- so the ambiguity resolves toward staying
  // latched, which is the direction that cannot hurt the pack.
  return (armed != 0u) ? 1u : 0u;
}

void latch_arm(void)
{
  armed = 1u;

  // Re-stamp the magic. If the latch is armed, the record of it must be valid
  // even if something disturbed those bytes earlier in this run.
  magic = LATCH_MAGIC;
}

void latch_service_indicator(void)
{
  blink_counter++;

  if (blink_counter >= LATCH_LED_BLINK_SAMPLES) {
    blink_counter = 0u;
    LATCH_LED_PIN = (1 << LATCH_LED_BIT);   // writing PINx toggles PORTx on AVR
  }
}
