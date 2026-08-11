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
| Control panel | `DCO-CONTROL-PANEL/` | host (Linux) | USB bench GUI; shared submodule with DCO3-MONOSYNTH |

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
  --fqbn rp2040:rp2040:rpipico2:usbstack=tinyusb,flash=4194304_524288 \
  --libraries ./_build_libs \
  .
```

`flash=…_524288` gives the DCO a 512 KB LittleFS partition for the 256-slot MCU preset
store and calibration files (see `DCO/docs/PRESET_STORE.md`). Changing the FS size
reformats it — back up calibration/presets with `DCO/tools/dco_control` first.

Mainboard: STM32 Arduino sketch `MAINBOARD-CONTROLLER/MAINBOARD-CONTROLLER.ino`.

## Build (Input)

`INPUT-CONTROLLER/` is a **shared submodule**: the exact same commit runs the
panel on both DCO3-MONOSYNTH and this project (see
[`INPUT-CONTROLLER/README.md`](INPUT-CONTROLLER/README.md)). Its
`board_model.h` default is DCO3, so **this project's builds must always pass
the DCO4 override**, or the firmware will boot with the wrong voice count and
UART wiring:

```bash
./scripts/build_input.sh          # wraps the arduino-cli call below
# or, equivalently:
arduino-cli compile --fqbn rp2040:rp2040:rpipico \
  --libraries ./INPUT-CONTROLLER/_build_libs \
  --build-property "compiler.cpp.extra_flags=-DINPUT_BOARD_MODEL=INPUT_BOARD_DCO4 -DROXMUX_FELA_SRAM_HOT=0" \
  ./INPUT-CONTROLLER
```

Never edit `INPUT_BOARD_MODEL` in the checked-out `board_model.h` — that file
is identical to DCO3-MONOSYNTH's copy and any local edit will be silently
discarded the next time the submodule is updated.

USB bench without the panel: [`DCO-CONTROL-PANEL/`](DCO-CONTROL-PANEL/README.md). MIDI CC map: [`DCO/docs/MIDI_CC_MAP.md`](DCO/docs/MIDI_CC_MAP.md).
