# DCO3 monosynth — remaining migration TODO

Living checklist for finishing the **1 voice × 3 oscillators** migration (plus later paraphonic / 2-filter plans).  
Last updated: 2026-07-28.

Status key: `[x]` done · `[ ]` todo · `[~]` partial

---

## Done (baseline)

### DCO (`DCO/`)
- [x] `NUM_VOICES_TOTAL = 1`, `NUM_OSCILLATORS = 3`
- [x] PIO0/1/2 SM0 freq; OSC1↔OSC2 sync; OSC3 free-running
- [x] RANGE PWM amp-comp ×3; shared PW voice
- [x] OSC3 ParamIds **33–35** + handlers
- [x] ADSR3→osc select **0–4** (OSC1 / OSC2 / both / OSC3 / all)
- [x] Autotune: PW once on voice 0, amp-comp per osc 0..2
- [x] Float/fixed engines kept; unused alt voice paths excised
- [x] Pico 2 float-default build verified
- [x] Core DCO docs refreshed for 1×3 (`REFERENCE_AI`, `FILE_INDEX`, `AUTOTUNE`, `ENGINE_OPTIONS`)

### Mainboard (`Mainboard/`) — logic pass
- [x] `NUM_VOICES = 1`, `NUM_FILTERS = 2`
- [x] OSC3 ParamIds **33–35** store + forward to DCO (no OSC3 CV hardware)
- [x] ADSR3 select documented/forwarded as 0–4
- [x] Manual cal stage = osc index 0..2; OSC3 has no mux/SQR path
- [x] Unused voice mux slots forced off; unused timer CVs parked
- [x] Unused-code cleanup: dead serial/helpers/orphans excised to `Mainboard/_removed/` (kept ADSR release curves, soft timers, main debug/DETUNE1/randomness)

---

## Firmware still needed

### Mainboard (follow-ups)
- [x] Refresh Mainboard README (`NUM_VOICES 1`, `NUM_FILTERS 2`, OSC3 forward-only, pointer to `DCO/docs/SYSTEM_OVERVIEW.md`)
- [ ] Refresh Mainboard deep docs (`FILE_INDEX`, `REFERENCE_AI`, `MODULATION_PIPELINE` — still may say DCO4 / 4-voice)
- [ ] Decide final MCP4728 / SQR level mapping for monosynth (today still DCO4 V1–V4 OSC1/OSC2 comments; OSC3 has no DAC channel)
- [ ] True **paraphonic 2-filter** behavior (today both filter outs get the same voice-0 VCF CV)
- [ ] STM32 build/smoke test on hardware after pinout freeze
- [ ] Verify Mainboard↔Input UART path for cal/offset frames (known DCO4-era RX discrepancy risk)

### Input controller (`INPUT-CONTROLLER/`)
- [ ] `NUM_VOICES` → **1** (or keep UI poly stubs intentionally)
- [ ] `NUM_OSCILLATORS` → **3** (today `NUM_VOICES * 2` → 8)
- [ ] Add ParamIds **33–35** to `params_def.h` (match DCO/Mainboard)
- [ ] Encoder actions + map for OSC3 interval / detune / LFO2→OSC3
- [ ] Preset storage: persist/load OSC3 fields
- [ ] ADSR3→osc select wrap/UI: **0–4** (today wraps at 2)
- [ ] Manual cal: stage range + `manualCalibrationInitAmpCompOffset[3]` (today `[8]`, stages assume 8 DCOs)
- [ ] Voice-mode / unison UI copy for monosynth (POLY/UNISON may be misleading)
- [ ] Docs refresh

### Screen controller (`SCREEN-CONTROLLER/`)
- [ ] Add ParamIds **33–35** to `params_def.h`
- [ ] Labels/display for OSC3 interval, detune, LFO2→OSC3
- [ ] ADSR3→osc select labels: OSC3 + ALL (today OSC1/OSC2/BOTH only)
- [ ] Level meters / pages: add OSC3 where OSC1/OSC2 appear
- [ ] Cal UI stage count for 3 oscillators
- [ ] Docs refresh

### Cross-board / protocol
- [ ] Keep ParamId numbers stable; sync all three `params_def.h` copies with DCO
- [ ] End-to-end smoke: Input encoder → Mainboard → DCO for OSC3 params
- [ ] End-to-end smoke: ADSR3 select values 3 and 4
- [ ] End-to-end smoke: auto + manual cal with 3 oscs / voice 0 notes only
- [ ] Optional: fixed-engine (`#undef USE_FLOAT_ENGINE`) link smoke on DCO

### DCO (optional polish)
- [ ] Final PCB pinout replace provisional RESET/RANGE/PW maps in `globals.h`
- [ ] Paraphonic note→osc ownership remap (scaffolding kept; not wired)
- [ ] Any remaining stale comments (`sm1N` names, etc.) if desired — not required for function

---

## Hardware still needed

### PCB / analog (monosynth)
- [ ] Finalize Pico 2 DCO pinout (RESET ×3, RANGE ×3, PW ×1, cal sense, UART, MIDI, board rails)
- [ ] Confirm OSC1–3 analog front-ends (saw/pulse/mix) match firmware assumptions
- [ ] Confirm **no Mainboard OSC3 SQR/wave mux** is intentional (logic-only OSC3) — or add hardware later
- [ ] Wire **2 independent filters** for paraphonic plan (CV already reserved as `NUM_FILTERS 2`)
- [ ] VCA / resonance / cutoff assignments for mono + 2-filter layout
- [ ] Shared PW path (one PW voice) on PCB
- [ ] Power / fix-rail (GPIO24 etc.) vs new board
- [ ] Calibration sense routing for all 3 oscillators (mux or sequential probe)

### Bring-up / validation
- [ ] Flash DCO + Mainboard + Input + Screen with matching ParamIds
- [ ] Pitch: OSC1/2/3 tracking, interval, detune, LFO2→OSC3
- [ ] Sync modes OSC1↔OSC2; confirm OSC3 free-running
- [ ] Amp-comp / autotune tables for 3 oscs persist + reload
- [ ] PW center/limits (shared) still sane on hardware
- [ ] Filter CV1/CV2 both respond; unused DCO4 outs stay quiet
- [ ] EMI / timing: Pico 2 @ 225 MHz sysClock vs analog

### Future (out of scope for “mono ship”)
- [ ] True 3-voice paraphonic note allocation → osc ownership
- [ ] Independent filter envelopes per paraphonic note
- [ ] OSC3 analog level / wave select on Mainboard (if PCB grows a 3rd mixer path)
- [ ] Rename sketch/product strings on Mainboard/Input/Screen away from “DCO4” if desired

---

## Suggested order of work

1. **Input + Screen ParamIds 33–35 + ADSR3 0–4** (unblocks panel/UI control of OSC3)
2. **Input cal/preset sizing to 3 oscs**
3. **Hardware pinout freeze → update DCO `globals.h`**
4. **Bench bring-up** (pitch, amp-comp, PW, filters)
5. **Mainboard/Input/Screen docs**
6. **Paraphonic 2-filter behavior** when hardware and product rules are defined

---

## Quick reference — OSC3 ParamIds

| ID | Symbol | Boards that must know it |
|----|--------|---------------------------|
| 33 | `PARAM_OSC3_INTERVAL` | DCO ✓ · Mainboard ✓ · Input ✗ · Screen ✗ |
| 34 | `PARAM_OSC3_DETUNE_VAL` | DCO ✓ · Mainboard ✓ · Input ✗ · Screen ✗ |
| 35 | `PARAM_LFO2_TO_DETUNE3` | DCO ✓ · Mainboard ✓ · Input ✗ · Screen ✗ |

ADSR3→osc (`PARAM_ADSR3_TO_OSC_SELECT` = 10): DCO+Mainboard accept 0–4; Input/Screen still 0–2 UI.
