// src/latch.h
// The one-way latch, and the machinery that decides whether it survives a boot.
//
// The requirement is precise: recharging and power-cycling the payload pack
// must clear the latch, and nothing else may. A reset is NOT a power cycle --
// the reset button, the serial monitor's DTR pulse, a brown-out and a watchdog
// all wipe RAM and restart the firmware, and without help all four would
// restart it unlatched and command the driver back to full duty.
//
// MCUSR would have told us why we booted, but this board's Optiboot reads and
// clears it before our code runs (measured: MCUSR 0x00 after a reset button
// press, and the r2 value it leaves behind is not an MCUSR copy). So we detect
// power cycles from SRAM instead.
//
// A reset does not clear RAM -- only the C startup code does, by zeroing .bss
// and copying .data, and .noinit escapes both. Removing power DOES clear RAM.
// So a magic value parked in .noinit answers the question directly:
//
//   magic intact   -> RAM survived  -> this was a reset. Restore the latch.
//   magic garbage  -> RAM was lost  -> genuine power-on. Start clean.
//
// This is deliberately NOT EEPROM. EEPROM is ruled out because it would
// survive a power cycle and defeat the "recharging clears it" requirement.
// .noinit RAM has exactly the opposite property: it dies with the power. That
// is the behaviour the requirement asks for, so it is the correct storage.
#ifndef LATCH_H
#define LATCH_H

#include <stdint.h>

// Validates the .noinit magic and configures the indicator pin.
// Call once from setup(), before the state machine reads latch_is_set().
void latch_init(void);

// Non-zero if this boot followed a genuine power-on (RAM was not preserved).
uint8_t latch_was_power_on(void);

// Non-zero if the latch is armed -- either armed during this run, or inherited
// across a reset that did not remove power.
uint8_t latch_is_set(void);

// Arms the latch. One-way: there is deliberately no clear function. The only
// way out is to remove power from the board.
void latch_arm(void);

// Drives the indicator. Call once per sample while latched.
//
// Blinks rather than sitting solid: a blink proves the firmware is running AND
// latched, where a steady pin could equally mean it hung with the pin high.
void latch_service_indicator(void);

#endif
