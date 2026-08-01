# Voice-aux (RP2040)

Helper MCU for **post-filter** controls. Architecture: [`../../DCO/docs/DUAL_MCU.md`](../../DCO/docs/DUAL_MCU.md).

## Role

- **RX-only** on the Input Controller TX fanout (same 2.5 Mbaud panel stream as the DCO).
- Applies owned ParamIds; discards `'a'..'f'`, `'q'`, and unknown `'p'`/`'w'`.
- **Never** transmits on the Input bus (gap/cal stay on the RP2350 DCO).

## Owned parameters (v1)

| ParamId | Name | Action |
|--------:|------|--------|
| 52 | `PARAM_DIST_DRIVE` | PWM → Dist Drive CV |
| 53 | `PARAM_DIST_MIX` | PWM → Dist Mix CV |
| 54 | `PARAM_FILTER_MODE` | 2 GPIO bits → AS3320 mode (DG411) |

FX IDs 55+ reserved in `params_def.h` (stubs only).

## Provisional breadboard pins

Freeze on PCB later — **not** the DCO solo-B Dist map (GP9/GP26).

| Function | GPIO | Notes |
|----------|------|-------|
| Dist Drive PWM | **GP2** | wrap 4095 |
| Dist Mix PWM | **GP3** | wrap 4095 |
| Filter mode A | **GP4** | bit 0 → stage-1 SPDT (+ inverter off-board) |
| Filter mode B | **GP5** | bit 1 → stage-2 SPDT |
| Input RX | **GP1** | UART0 / `Serial1` RX ← Input TX |
| Input TX | GP0 | **Do not wire** to Input bus |

## Build

Arduino-Pico / RP2040 board. Open `VOICE-AUX/` (folder symlink `VOICE-AUX` → `.` for the IDE). On the DCO sketch, enable `#define ENABLE_VOICE_AUX` so Dist PWM writers stay compiled but do not claim GP9/GP26.

## Boot note

Until Input sends a full snapshot, cycle Dist/mode controls after aux power-up so values catch up.
