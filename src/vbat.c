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

uint16_t vbat_compensate_harness(uint16_t measured_mv, uint8_t duty_pct)
{
#if (HARNESS_MILLIOHMS == 0u)

  (void)duty_pct;
  return measured_mv;

#else

  // Never amplify a reading that is already nonsense. If the sense path is
  // open or the pack is genuinely dead, adding a modelled drop on top would
  // turn a suspicious number into a plausible-looking one -- which is the
  // worst possible failure for a safety threshold.
  if (measured_mv < 1000u) {
    return measured_mv;
  }

  // The driver behaves as a constant-power load scaled by duty, plus a fixed
  // quiescent draw for its own control circuitry. Both terms are needed: at
  // the 1% floor the quiescent term is almost the whole current (measured
  // 80 mA, of which only ~14 mA is the LED).
  uint32_t load_mw = ((uint32_t)LOAD_POWER_MW_AT_FULL * duty_pct) / 100UL;
  uint32_t i_ma    = (uint32_t)LOAD_QUIESCENT_MA + ((load_mw * 1000UL) / measured_mv);

  // I(mA) * R(milliohm) / 1000 = drop in mV.
  uint32_t drop_mv = (i_ma * (uint32_t)HARNESS_MILLIOHMS) / 1000UL;

  // Clamp. This correction can only ever raise the reading, so too much of it
  // tells the firmware the pack is healthier than it is and delays the latch --
  // the one direction that can hurt the pack. Bounding it means a stale or
  // mistyped constant degrades accuracy instead of moving the threshold.
  if (drop_mv > (uint32_t)HARNESS_COMP_MAX_MV) {
    drop_mv = (uint32_t)HARNESS_COMP_MAX_MV;
  }

  // Uses the uncorrected reading to estimate the current. The resulting error
  // is second order -- a few percent of a sub-volt correction -- and iterating
  // would buy less than one ADC count.
  uint32_t out = (uint32_t)measured_mv + drop_mv;

  return (out > 65535UL) ? 65535u : (uint16_t)out;

#endif
}
