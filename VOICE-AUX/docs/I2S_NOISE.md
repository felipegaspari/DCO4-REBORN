# I2S noise listen (PCM5102) — voice-aux

Bench listen path on the **VOICE-AUX** RP2040: local `DCO_Noise` white/pink streamed to a PCM5102 at **true 48 kHz** (one `.next()` per stereo frame). This is **not** the DCO Character `noiseLevel[]` fleet — there is no sample link from DCO to aux.

Related: [`PINOUT.md`](PINOUT.md), [`README.md`](README.md), [`../../DCO/docs/DUAL_MCU.md`](../../DCO/docs/DUAL_MCU.md).

---

## Enable

In [`VOICE-AUX.ino`](../VOICE-AUX.ino):

```cpp
#define ENABLE_I2S_NOISE
// #define I2S_NOISE_TEST_TONE   // optional 440 Hz square DDS
```

Build with the `DCO_Noise` library on the path (symlink under `_build_libs/`):

```bash
arduino-cli compile \
  --fqbn rp2040:rp2040:rpipico \
  --libraries ./_build_libs \
  .
```

---

## Wiring

| Module | VOICE-AUX Pico | Notes |
|--------|----------------|-------|
| VIN | **5V / VBUS** | Preferred over 3V3 |
| GND | GND | Common |
| BCK | **GP6** | `I2S_BCLK_PIN` |
| RCK / LCK / LRCK | **GP7** | Must be BCK+1 |
| DIN | **GP8** | `I2S_DOUT_PIN` |
| XMT / XSMT | **GP10** | Driven HIGH in firmware |
| SCK | **GND** (+ solder bridge) | Internal PLL |
| FMT | GND / pad L | I2S |
| FLT / DEMP | GND / pad L | |

### Underside pads

| Pad | Level |
|-----|-------|
| FLT | L |
| DEMP | L |
| XSMT | H |
| FMT | L |

Output is line level (~2.1 Vrms) when unmuted.

---

## Behaviour

| Mode | Output |
|------|--------|
| Default | L = white, R = pink @ 48 kHz / 32-bit left-aligned Q15 |
| `I2S_NOISE_TEST_TONE` | Full-scale 440 Hz square DDS |

Serial (after USB CDC opens): `[I2S] begin ok BCK=6 RCK=7 DIN=8 XMT=10 32-bit`.

Dist / filter-mode / Input UART are unchanged (GP1–5).
