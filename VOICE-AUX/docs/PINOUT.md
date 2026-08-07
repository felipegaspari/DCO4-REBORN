# Voice-aux pinout (provisional)

**Status:** Breadboard / bring-up map. Freeze on PCB later. Live constants: [`../globals.h`](../globals.h).

**MCU:** RP2040 (Arduino-Pico). Architecture: [`../../DCO/docs/DUAL_MCU.md`](../../DCO/docs/DUAL_MCU.md).  
**Not** the DCO solo-B Dist map (GP9/GP26).

Related: [`README.md`](README.md), [`I2S_NOISE.md`](I2S_NOISE.md).

---

## Occupancy summary

| GPIO | Role | Notes |
|------|------|-------|
| 0 | UART0 TX | **Do not wire** to Input bus |
| 1 | Input RX | HW `UART0` / `Serial1` ← Input TX @ 2 500 000 |
| 2 | Dist Drive PWM | wrap `DIV_COUNTER_CV` (4095) |
| 3 | Dist Mix PWM | wrap 4095 |
| 4 | Filter mode A | bit 0 → AS3320 stage-1 SPDT (+ inverter off-board) |
| 5 | Filter mode B | bit 1 → AS3320 stage-2 SPDT |
| 6 | I2S BCK | PCM5102 when `ENABLE_I2S_NOISE` |
| 7 | I2S RCK / LRCK | = BCK+1 (Arduino-Pico `I2S` requirement) |
| 8 | I2S DIN | PCM5102 data |
| 9 | *(free)* | |
| 10 | I2S XMT | Driven HIGH in `i2s_noise_init()` (soft unmute) |
| 11–28 | *(free)* | Spares for later FX / digitals |

---

## UART (Input fanout)

| Role | Peripheral | Pin | Baud |
|------|------------|-----|------|
| Input RX | HW UART0 / `Serial1` | **GP1** | 2 500 000 |
| Input TX | UART0 TX | GP0 | **Unwired** — aux never drives the Input bus |

DCO gap/cal TX stays on the RP2350. Aux is RX-only on this link.

---

## Distortion / filter mode

| Function | GPIO | Block |
|----------|------|-------|
| Dist Drive | **GP2** | PWM |
| Dist Mix | **GP3** | PWM |
| Filter mode A | **GP4** | GPIO out |
| Filter mode B | **GP5** | GPIO out |

---

## PCM5102 I2S (`ENABLE_I2S_NOISE`)

| Module | GPIO | Constant |
|--------|------|----------|
| BCK | **GP6** | `I2S_BCLK_PIN` |
| RCK / LCK / LRCK | **GP7** | implied BCLK+1 |
| DIN | **GP8** | `I2S_DOUT_PIN` |
| XMT / XSMT | **GP10** | `I2S_XMT_PIN` |

Also: VIN→**5V/VBUS**, SCK→**GND** (+ solder bridge), FMT/FLT/DEMP pads as in [`I2S_NOISE.md`](I2S_NOISE.md). Local white/pink gens @ 48 kHz — not the DCO Character fleet.

---

## Feature flags

```text
ENABLE_I2S_NOISE       // GP6/7/8/10 PCM5102 listen — on by default in VOICE-AUX.ino
I2S_NOISE_TEST_TONE   // optional 440 Hz square instead of noise
ENABLE_VOICE_AUX       // on DCO sketch: skip Dist PWM writers so they do not fight this board
```
