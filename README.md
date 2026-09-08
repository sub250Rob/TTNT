# TTNT

Firmware for a battery-protection latch on a custom LED payload used in sea turtle
hatchling research. The payload flies on a DJI M300 RTK as a mechanical carrier and runs
entirely from its own battery pack.

---

## What it does, and why

The board monitors the payload pack's voltage. When the pack falls below a threshold, the
firmware drops the LED driver's PWM duty cycle to 1% and **holds it there permanently**
until the pack is physically disconnected.

The latch is deliberately one-way. When the pack voltage recovers — as it will, once the
load is removed — the firmware does not resume measuring and does not raise the duty cycle
back up.

The PWM pin drives a **boost-topology LED driver**. A boost converter draws more input current as its input voltage falls, so a pack that is already depleted gets loaded harder at the end of its capacity range. Clamping the pwm signal to 1% until a full power cycle is what keeps the system safer than a timer and fully automatic.

**When in doubt, the firmware fails toward staying latched.**

---

## Hardware

| | |
|---|---|
| Board | Elegoo Uno R3 (ATmega328P, 16 MHz) |
| Pack | 6S LiPo, 25.2 V at full charge, dedicated to the payload |
| MCU power | Dedicated voltage regulator from the payload pcb |
| VBAT sense | A0 (ADC0 / PC0) via a 47 kΩ / 10 kΩ divider, 100 nF from A0 to GND |
| PWM out | Pin 9 (OC1A / Timer1), 1 kHz |
| Latch indicator | Pin 13 (PB5), blinks at 1 Hz when latched |
| Serial | 9600 baud, bench builds only |

Everything shares one ground domain: pack negative = payload PCB ground = dim connector
ground = Arduino GND = divider bottom. The Arduino is never powered from the aircraft.

The 100 nF capacitor on A0 is required, not optional. The divider's Thevenin impedance is
47k ∥ 10k = 8.25 kΩ, close to the limit the ADC sample-and-hold can charge from unaided.

---

## How the detection works

```
sample every 20 ms
        |
   VBAT < threshold?  --no--> keep sampling
        |
       yes
        |
   take 10 more samples over 200 ms
        |
   at least 7 below threshold?  --no--> resume normal sampling
        |
       yes
        |
   PWM -> 1%, latch, stop sampling
```

The 7-of-10 vote debounces transient sag. Note the asymmetry: the vote can only *enter*
the latch. Once latched, no measurement can leave it — the firmware stops sampling
entirely, so no reading exists that could raise the duty cycle again.

Duty rises through a **400 ms soft-start ramp** rather than stepping to full, which spreads
the driver's inrush across many small transients instead of one large one. The ramp is
one-way: increases are rate limited, decreases are immediate.

### Clearing the latch

Only removing power clears it. A reset — the reset button, the serial monitor's DTR pulse,
a brown-out, a watchdog — preserves the latched state. See
[docs/DESIGN-NOTES.md](docs/DESIGN-NOTES.md) for how that is implemented and why.

---

## Repository layout

| File | Role |
|---|---|
| `src/config.h` | Every tunable constant and calibration value |
| `src/adc.c` | Register-level ADC driver, bandgap-based AVCC recovery |
| `src/vbat.c` | Pure conversion maths: counts → millivolts, harness compensation |
| `src/timebase.c` | Timer2 sample cadence and the ISR/main-loop handoff |
| `src/pwm.c` | Timer1 PWM output and the soft-start ramp |
| `src/latch.c` | The one-way latch and its power-cycle detection |
| `src/main.c` | Detection state machine and orchestration |
| `src/serial_shim.cpp` | C-callable wrapper so `printf()` works from C |

`src/main.c` is C, compiled as C. `setup()` and `loop()` are declared inside the
`extern "C"` block in the Arduino core's `Arduino.h`, so a `.c` file can define them.
`serial_shim` exists because the Arduino `Serial` API is C++-only.

Timer ownership: **Timer0** belongs to the Arduino core (`millis()`, `delay()`) and must not
be touched. **Timer1** drives the PWM. **Timer2** drives the sample cadence.

---

## Build and flash

```sh
pio run                 # build
pio run -t upload       # build and flash
pio device monitor      # serial monitor at 9600 baud
```

### Flight build

Set `TTNT_VERBOSE` to `0` in `src/config.h`. Every diagnostic call and its format strings
compile away — on AVR, string literals are copied into RAM at startup, so removing them
reclaims memory rather than merely silencing output.

| | RAM | Flash |
|---|---|---|
| bench (`TTNT_VERBOSE 1`) | 1195 B (58%) | 6568 B (20%) |
| flight (`TTNT_VERBOSE 0`) | 208 B (10%) | 2750 B (9%) |

Everything safety-relevant sits outside the flag. Sampling, the vote, the PWM floor, the
latch and the indicator LED compile identically in both builds, so the flight image runs
exactly the code the bench build was tested with.

---

## Before flying

1. Recalibrate for the board and harness in use — see
   [docs/CALIBRATION.md](docs/CALIBRATION.md). Constants are per-board and per-harness.
2. Verify calibration **on battery power**, not USB. The bandgap correction is designed to make
   the reading independent of the supply rail; confirm it.
3. Confirm `VBAT_THRESHOLD_MV` against the pack chemistry and the mission profile.
4. Confirm `PWM_TOP` (carrier frequency) and `PWM_DUTY_NORMAL_PCT` against the LED driver's
   datasheet.
5. Latch test on flight power: trip it, disconnect the pack, reconnect, confirm it clears.
   Then trip it and interrupt power only briefly — it should stay latched.
6. Cold-start on a marginal pack: power up below threshold and confirm the firmware latches
   during the ramp without ever reaching full duty.
7. Set `TTNT_VERBOSE` to `0` and confirm the pin 13 LED still blinks when latched. That is
   the only status output in flight.

---

## Documentation

- [docs/CALIBRATION.md](docs/CALIBRATION.md) — per-board calibration procedure and current values
- [docs/DESIGN-NOTES.md](docs/DESIGN-NOTES.md) — design decisions, hardware findings, and traps

## License

MIT. See [src/LICENSE](src/LICENSE).
