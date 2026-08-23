// src/vbat.c
#include "vbat.h"
#include "config.h"

// Deliberately two steps rather than one expression.
//
// Folding these together overflows uint32_t as soon as the divider ratio is
// parameterised for calibration: 1023 * 5000 * 5700 = 2.9e10, well past the
// 4.29e9 ceiling. Splitting keeps every intermediate in range, and it hands
// the caller the A0 millivolt value -- which is exactly what gets compared
// against the DMM during calibration, and the only way to tell which of the
// two stages is responsible when a reading is off.
//
// Precision lost to the intermediate integer division is under 6 mV at the
// pack, against a 27.83 mV LSB. Both results fit uint16_t (ceiling 28471 mV
// at 1023 counts), so they print with %u.

uint16_t vbat_counts_to_adc_mv(uint16_t counts, uint16_t avcc_mv)
{
  // counts = 1024 * V_A0 / AVCC, rearranged for V_A0.
  return (uint16_t)(((uint32_t)counts * avcc_mv) / 1024UL);
}

uint16_t vbat_adc_mv_to_pack_mv(uint16_t adc_mv)
{
  return (uint16_t)(((uint32_t)adc_mv * DIVIDER_RATIO_X1000) / 1000UL);
}
