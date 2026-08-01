# DCO3 monosynth — remaining migration TODO

Living checklist for finishing the **1 voice × 3 oscillators** migration (plus later paraphonic / 2-filter plans).  
Last updated: 2026-07-29.

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
- [x] Mainboard absorption Phases 0–5 (hub default; CV flags opt-in)

### Archived Mainboard (`_archived/Mainboard/`)
- [x] Logic pass for monosynth (`NUM_VOICES 1`, `NUM_FILTERS 2`, OSC3 forward-only)
- [x] Archived after DCO hub cutover (legacy link code since deleted — no escape hatch)

---

## Firmware still needed

### Input controller (`INPUT-CONTROLLER/`)
- [x] `NUM_VOICES` → **1**, `NUM_OSCILLATORS` → **3**
- [x] Add ParamIds **33–35** to `params_def.h`
- [x] Encoder actions + map for OSC3 interval / detune / LFO2→OSC3
- [x] Preset storage: persist/load OSC3 fields
- [x] ADSR3→osc select wrap/UI: **0–4**
- [x] Manual cal: stage range **0–5** + `manualCalibrationInitAmpCompOffset[3]`
- [ ] Voice-mode / unison UI copy for monosynth (POLY/UNISON may be misleading)
- [ ] Docs refresh (`FILE_INDEX` still mentions NUM_VOICES 4)

### Screen controller (`SCREEN-CONTROLLER/`)
- [x] Add ParamIds **33–35** to `params_def.h`
- [x] Labels/display for OSC3 interval, detune, LFO2→OSC3
- [x] ADSR3→osc select labels: OSC3 + ALL
- [x] Cal UI stage count for 3 oscillators (clamp 0–5; TRI stages 1,5)
- [ ] Level meters / pages: add OSC3 where OSC1/OSC2 appear (no OSC3 SQR level ParamId yet)
- [ ] Docs refresh

### Cross-board / protocol
- [x] ParamIds 33–35 synced on DCO / Input / Screen
- [ ] End-to-end smoke: Input encoder → DCO for OSC3 params (hub path)
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
- [x] Per-osc wave mux: OSC1–3 × Saw/Pulse/Tri via dual 595 + DG411 ([`DCO/docs/WAVE_MUX.md`](DCO/docs/WAVE_MUX.md))
- [ ] Wire **2 independent filters** for paraphonic plan (CV already reserved as `NUM_FILTERS 2`)
- [ ] VCA / resonance / cutoff assignments for mono + 2-filter layout
- [ ] Shared PW path (one PW voice) on PCB
- [ ] Power / fix-rail (GPIO24 etc.) vs new board
- [ ] Calibration sense routing for all 3 oscillators (mux or sequential probe)

### Bring-up / validation
- [ ] Flash DCO + Input + Screen with matching ParamIds (hub)
- [ ] Pitch: OSC1/2/3 tracking, interval, detune, LFO2→OSC3
- [ ] Sync modes OSC1↔OSC2; confirm OSC3 free-running
- [ ] Amp-comp / autotune tables for 3 oscs persist + reload
- [ ] PW center/limits (shared) still sane on hardware
- [ ] Filter CV1/CV2 both respond when `ENABLE_CV_OUTS`
- [ ] EMI / timing: Pico 2 @ 225 MHz sysClock vs analog

### Future (out of scope for “mono ship”)
- [ ] True 3-voice paraphonic note allocation → osc ownership
- [ ] Independent filter envelopes per paraphonic note
- [x] OSC3 analog level + wave select (level PWM + WAVE_MUX)
- [ ] Rename sketch/product strings on Input/Screen away from “DCO4” if desired

---

## Suggested order of work

1. ~~**Input + Screen ParamIds 33–35 + ADSR3 0–4**~~ — **done**
2. **Bench bring-up** on 3-board hub (pitch, OSC3 params, cal)
3. **Hardware pinout freeze** ([`DCO/docs/PINOUT.md`](DCO/docs/PINOUT.md)) → update DCO `globals.h` → enable CV HW flags
4. **Docs** (Input/Screen FILE_INDEX; optional OSC3 level meter if hardware exists)
5. **Paraphonic 2-filter behavior** when hardware and product rules are defined

---

## Quick reference — OSC3 ParamIds

| ID | Symbol | Boards |
|----|--------|--------|
| 33 | `PARAM_OSC3_INTERVAL` | DCO ✓ · Input ✓ · Screen ✓ |
| 34 | `PARAM_OSC3_DETUNE_VAL` | DCO ✓ · Input ✓ · Screen ✓ |
| 35 | `PARAM_LFO2_TO_DETUNE3` | DCO ✓ · Input ✓ · Screen ✓ |

ADSR3→osc (`PARAM_ADSR3_TO_OSC_SELECT` = 10): **0–4** on DCO / Input / Screen (OSC1 / OSC2 / both / OSC3 / all).
