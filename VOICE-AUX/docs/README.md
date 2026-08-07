# Voice-aux (RP2040)

Helper MCU for **post-filter** controls. Architecture: [`../../DCO/docs/DUAL_MCU.md`](../../DCO/docs/DUAL_MCU.md).

## Role

- **RX-only** on the Input Controller TX fanout (same 2.5 Mbaud panel stream as the DCO).
- Applies owned ParamIds; discards `'a'..'f'`, `'q'`, and unknown `'p'`/`'w'`.
- **Never** transmits on the Input bus (gap/cal stay on the RP2350 DCO).

## Owned parameters (v1)

| ParamId | Name | Action |
|--------:|------|--------|
| 52 | `PARAM_DIST_DRIVE` | Panel base; PWM after mod-matrix sum |
| 53 | `PARAM_DIST_MIX` | PWM → Dist Mix CV |
| 54 | `PARAM_FILTER_MODE` | 2 GPIO bits → AS3320 mode (DG411) |
| 60–83 | Mod matrix slots | Apply dest **6** (Dist Drive) only — see [`MOD_MATRIX.md`](../../DCO/docs/MOD_MATRIX.md) |

FX IDs 55–56 reserved in `params_def.h` (stubs only).

Each `loop()` calls `mod_matrix_apply_dist()` so LFO/ADSR stubs / local Random can move Dist Drive without a new param frame.

## Pinout

Full map: [`PINOUT.md`](PINOUT.md). PCM5102 listen: [`I2S_NOISE.md`](I2S_NOISE.md).

| Function | GPIO |
|----------|------|
| Input RX | GP1 |
| Dist Drive / Mix | GP2 / GP3 |
| Filter mode A / B | GP4 / GP5 |
| I2S BCK / RCK / DIN / XMT | GP6 / GP7 / GP8 / GP10 |

## Build

Arduino-Pico / RP2040 board. Open `VOICE-AUX/` (folder symlink `VOICE-AUX` → `.` for the IDE). Pass `--libraries ./_build_libs` so the `DCO_Noise` symlink resolves. On the DCO sketch, enable `#define ENABLE_VOICE_AUX` so Dist PWM writers stay compiled but do not claim GP9/GP26.

## Boot note

Until Input sends a full snapshot, cycle Dist/mode controls after aux power-up so values catch up.
