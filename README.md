# DCO4-REBORN

Firmware for the **classic DCO4 analog polysynth**: **4 MIDI voices × 2 oscillators**, STM32 Mainboard as analog/modulation hub, DCO3 Q15/Q24 math and slim little-endian UART. MIDI stays on the DCO.

## Purpose

- **4 voices × 2 analog DCOs** (RANGE + PW PWM on the voice board)
- Frequency on **PIO** (RP2040 / RP2350)
- Envelopes, LFOs, mod matrix, VCA/VCF/reso CVs, MCP4728 SQR/Sub, 74HC595 waves on the **STM32 Mainboard**
- DCO3-quality MIDI allocator (last-note stack, full CC map, AT, PB) on the **DCO**

## Boards

| Board | Folder | MCU | Owns |
|-------|--------|-----|------|
| DCO | `DCO/` | RP2040 / RP2350 | MIDI, voice alloc, PIO pitch, RANGE/PW, amp-comp, autotune, Character, pitch drift |
| Mainboard | `MAINBOARD-CONTROLLER/` | STM32 | EnvVCA/VCF/DCO ×4, LFO1/2, matrix, analog CVs, Input/DCO UART hub |
| Input | `INPUT-CONTROLLER/` | RP2040 | Panel scan, presets, Screen UI frames |
| Screen | `SCREEN-CONTROLLER/` | RP2040 | LVGL UI |
| Voice aux | `VOICE-AUX/` | RP2040 | Optional Dist / filter-mode helper |

Shared libraries: [`mo-lfo`](https://github.com/felipegaspari/mo-lfo) (Q15), [`ADSR_Bezier`](https://github.com/felipegaspari/ADSR_Bezier) (Q15), [`DCO_Noise`](https://github.com/felipegaspari/DCO_Noise). Topology: [`MAINBOARD-CONTROLLER/docs/MAINBOARD_REINTEGRATION.md`](MAINBOARD-CONTROLLER/docs/MAINBOARD_REINTEGRATION.md), [`DCO/docs/SYSTEM_OVERVIEW.md`](DCO/docs/SYSTEM_OVERVIEW.md).

## Architecture

| | Classic DCO4 wiring | Control model |
|--|---------------------|---------------|
| MCU (DCO) | RP2040 (2 PIO) or RP2350 | PIO pitch + MIDI |
| Voices | 4 × 2 osc | same |
| Modulation / analog CV hub | STM32 Mainboard | DCO3 Q15/Q24 bake-on-write |
| Amplitude | RANGE PWM | same |

```mermaid
flowchart LR
  MIDI["MIDI USB+DIN"] --> DCO["DCO RP2040/2350\n4x2 PIO + RANGE + PW"]
  DCO -->|"Serial2 GP20/21 2.5M\n'n'/'o'/'e'/'x'/'p'"| MB["STM32 Mainboard"]
  MB -->|"'m' Q15/Q24 + 'p'"| DCO
  Input["Input RP2040"] -->|"Serial2 GP4/5\n'a'..'d'/'p'/'q'"| MB
  Input -->|"Serial1 GP0/1"| Screen["Screen"]
  MB -->|"Serial8 PE0/PE1\n'x' 154/155"| Input
  MB --> Analog["4x VCA + 4x VCF + reso\nMCP4728 SQR/Sub\n74HC595 waves"]
```

## Build (DCO)

```bash
cd DCO
arduino-cli compile \
  --fqbn rp2040:rp2040:rpipico2:usbstack=tinyusb \
  --libraries ./_build_libs \
  .
```

Mainboard: STM32 Arduino sketch `MAINBOARD-CONTROLLER/MAINBOARD-CONTROLLER.ino`. Input: RP2040 `INPUT-CONTROLLER/INPUT-CONTROLLER.ino`.

USB bench without the panel: [`DCO/tools/dco_control/`](DCO/tools/dco_control/README.md). MIDI CC map: [`DCO/docs/MIDI_CC_MAP.md`](DCO/docs/MIDI_CC_MAP.md).
