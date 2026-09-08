# Calibration

Three of the constants in `src/config.h` are properties of physical parts, not of the
firmware. They must be measured on the hardware in use.

| Constant | Belongs to | Recalibrate when |
|---|---|---|
| `BANDGAP_MV` | the ATmega die | the microcontroller changes |
| `DIVIDER_RATIO_X1000` | the two divider resistors | the divider is rebuilt |
| `HARNESS_MILLIOHMS` | the pack-to-payload wiring | the harness changes |

Nominal values are wrong by enough to matter. The 47k/10k divider's nominal divisor is
5.700; the parts on board #1 measure **5.594**, a 1.9% error — roughly 0.4 V at a 21 V
threshold.

---

## Current values, board #1

| Constant | Value | Nominal | Source |
|---|---|---|---|
| `BANDGAP_MV` | 1082 | 1100 | `bg_sum` 3408 against a DMM reading of 5.20 V at the 5 V pin |
| `DIVIDER_RATIO_X1000` | 5594 | 5700 | least squares through the origin, three DMM-verified points |
| `ADC_MUX_SETTLE_US` | 2000 | — | see [DESIGN-NOTES](DESIGN-NOTES.md#adc-multiplexer-settling) |
| `ADC_MUX_DISCARD_SAMPLES` | 4 | — | one discard was not enough |

Measured behaviour on USB power: AVCC ≈ **5.201 V**, `bg_sum` stable at **3408**,
**35.2 counts per volt**, **28.4 mV per ADC count** at the pack, saturation at **29.1 V**.
The divider's real bottom leg is ≈ 10.23 kΩ, 2.3% high and inside 5% tolerance.

Verification residuals, all under 0.04% and far inside a single count:

| DMM | raw | a0_mv | reported vbat_mv | error |
|---|---|---|---|---|
| 17.99 V | 633 | 3215 | 17 984 | −0.03% |
| 20.99 V | 739 | 3753 | 20 994 | +0.02% |
| 24.00 V | 845 | 4291 | 24 003 | +0.01% |

`HARNESS_MILLIOHMS` is currently set for a bench rig using alligator clips (546 mΩ) and
**must be remeasured for the flight harness.** A flight build will refuse to compile while
it holds a bench-sized value.

---

## Procedure

Run these in order. Each step depends on the one before it.

### 1. Linearity sweep

Before calibrating anything, confirm the hardware is sound. Step the supply from ~15 V to
~26 V in 1 V increments and record `raw` and `bg_sum` at each point.

- **`raw` must increase at every step.** A stall or a backward jump means a bad joint.
- **Counts per volt must stay constant** across the range. If it shrinks at the top, the
  sample-and-hold is not charging — check that the 100 nF cap is fitted properly and all other solder joints.

- **`bg_sum` must not move.** The bandgap is a fixed internal reference with no electrical
  path to the supply. If it drifts as the input rises, the ADC multiplexer is not settling
  and nothing downstream can be trusted. See
  [DESIGN-NOTES](DESIGN-NOTES.md#adc-multiplexer-settling).

Expect roughly 35 counts per volt with a 47k/10k divider.

### 2. `BANDGAP_MV`

Measure the actual 5 V rail at the Arduino's 5 V pin with a digital multimeter, then read `bg_sum` from
the firmware output.

```
BANDGAP_MV = round( (bg_sum / ADC_AVG_SAMPLES) × AVCC_mV / 1024 )
```

The internal reference is only specified to 1.0–1.2 V, so this will not be 1100. After
setting it, the firmware's printed `avcc_mv` should agree with the DMM.

### 3. `DIVIDER_RATIO_X1000`

Set the supply near the **threshold voltage** (~21V), not at full charge — single-point calibration
puts its smallest error at the calibration point, and the threshold is where accuracy
affects the safety decision.

Without changing the supply settings or turning it off between readings, record:

- the **DMM across the divider input** (top of the 47k to ground), in millivolts
- the firmware's **`a0_mv`** from the same output line

```
DIVIDER_RATIO_X1000 = round( 1000 × Vin_mV / a0_mv )
```

**Calibrate against the firmware's `a0_mv`, not against a DMM reading at A0.** Because
`a0_mv = V_A0 × (BANDGAP_stored / BANDGAP_true)` with AVCC cancelling exactly, this single
constant absorbs any residual bandgap error as a fixed scale factor — and that correction
stays valid when the supply rail changes, for example: moving off the bench and mounting to an aircraft.

### 4. `HARNESS_MILLIOHMS`

With the LED driver at full PWM duty, compare the DMM at the pack terminals against the
firmware's `divider` column, and note the load current.

```
HARNESS_MILLIOHMS = drop_mV × 1000 / load_mA
```

If the result is under about 10 mΩ, set it to 0 — below that you are compensating less than
half an ADC count, and can be considered negligable.

The correction models `I × R`, not a fixed offset, because the driver behaves as a
roughly constant-power load: as the pack sags it draws more current, so the drop grows
exactly where the threshold decision is made. `LOAD_POWER_MW_AT_FULL` and
`LOAD_QUIESCENT_MA` describe that load and should be re-derived from two measured current
points (full duty and the 1% floor) if the driver changes.

The correction only ever **raises** the reading, so an over-large value makes the firmware
believe the pack is healthier than it is and latch late. `HARNESS_COMP_MAX_MV` clamps it,
and a flight build will not compile with a bench-sized value.

### 5. Verify

Three DMM-verified points spread across the operating range (Reccomended: 21V, 23V, 25V). Record the residuals.

Read the **shape** of the error, not just its size:

- **A flat offset** means a scale constant is wrong.
- **A tilt** — error growing with input voltage — means the reference is being corrupted.
  Go back to step 1 and check `bg_sum`.

Residuals should land inside one ADC count (28 mV).

---

## Repeat on battery power

The bandgap correction exists so that calibration survives a change of supply rail. Verify
that it does: repeat step 5 with the board running from its buck regulator rather than USB.
`bg_sum` will shift because AVCC has changed. `vbat_mv` should not.
