#ifndef __AUTOTUNE_H__
#define __AUTOTUNE_H__

#include "../include_all.h"
#include "autotune_constants.h"
#include "autotune_measurement.h"
#include "autotune_context.h"

// Global flags controlling calibration routines.
//  - calibrationFlag: a calibration process is currently running.
//  - manualCalibrationFlag: manual calibration mode is active.
//  - firstTuneFlag: true on the very first calibration run after boot/flash.
bool calibrationFlag = false;
bool manualCalibrationFlag = false;
bool firstTuneFlag = false;

// Cancel request for a running auto-calibration. Set on core 0 by
// apply_param_calibration_flag(0) (PARAM_CALIBRATION_FLAG = 0) while
// DCO_calibration() blocks core 1; the calibration loops poll it and unwind,
// skipping the FS persist of whatever stage was interrupted. Cleared by
// DCO_calibration() on entry.
volatile bool calibrationCancelRequested = false;

// Which stage(s) the next DCO_calibration() run performs; carried by the value
// of PARAM_CALIBRATION_FLAG and mirroring the Screen's calibration menu tabs
// (AUTO / PW / FULL). PW and amp-comp are independent: an amp-only run drives
// the pulse from the PW center already stored in the filesystem.
enum CalibrationScope : uint8_t {
  CAL_SCOPE_AMP  = 1,  // amp-comp tables only
  CAL_SCOPE_PW   = 2,  // PW center + low/high limits only
  CAL_SCOPE_FULL = 3,  // PW stage, then amp-comp
};
uint8_t calibrationScope = CAL_SCOPE_FULL;

static inline const char *calibration_scope_name(uint8_t s) {
  if (s == CAL_SCOPE_AMP) return "AMP";
  if (s == CAL_SCOPE_PW)  return "PW";
  return "FULL";
}

static inline bool calibration_scope_runs_pw(uint8_t s) {
  return s == CAL_SCOPE_PW || s == CAL_SCOPE_FULL;
}

static inline bool calibration_scope_runs_amp(uint8_t s) {
  return s == CAL_SCOPE_AMP || s == CAL_SCOPE_FULL;
}

// How carefully the next run measures, also carried by the value of
// PARAM_CALIBRATION_FLAG: 1/2/3 run a scope at NORMAL, 5/6/7 the same scope at
// FINE, 9/10/11 at FAST. NORMAL builds a table from scratch as fast as the
// hardware allows; FINE skips the model-building entirely and re-measures the
// frequency of every pair already in the stored table (refine_DCO_amp_table);
// FAST is NORMAL's build with cheaper readings and the structural shortcuts
// gated on it in calibrate_DCO_freq_trace() - a table for testing, quickly.
enum CalPrecision : uint8_t {
  CAL_PRECISION_NORMAL = 0,
  CAL_PRECISION_FINE   = 1,
  CAL_PRECISION_FAST   = 2,
};
uint8_t calibrationPrecision = CAL_PRECISION_NORMAL;

// The measurement settings the calibration code should use right now. Every
// speed/quality knob is read through this, so nothing has to know which mode
// is active.
static inline const CalPrecisionProfile &cal_precision() {
  if (calibrationPrecision == CAL_PRECISION_FINE) return kCalPrecisionFine;
  if (calibrationPrecision == CAL_PRECISION_FAST) return kCalPrecisionFast;
  return kCalPrecisionNormal;
}

static inline const char *calibration_precision_name(uint8_t p) {
  if (p == CAL_PRECISION_FINE) return "FINE";
  if (p == CAL_PRECISION_FAST) return "FAST";
  return "NORMAL";
}

// How the amp-comp-0 endpoint (the lowest reachable frequency, pair 0 of the
// table) is obtained. MEASURE runs the live hunt (band scan + bounded search);
// CALC skips the hunt entirely and stores the least-squares fit through the
// lowest measured rungs (amp0_fit_freq()). CALC exists because on hardware
// whose pulse dies before the duty reaches 50% the hunt can never accept a
// measurement anyway, and the fit is what the rejection branch would store -
// minus the probe timeouts spent proving it. Runtime-only A/B via
// PARAM_DEBUG_COMMAND 40/41. Lives here with the other calibration-run flags
// (scope, precision, method, search).
#ifndef AUTOTUNE_AMP0_MODE_DEFAULT
#define AUTOTUNE_AMP0_MODE_DEFAULT 0
#endif
enum AutotuneAmp0Mode : uint8_t {
  AMP0_MODE_MEASURE = 0,
  AMP0_MODE_CALC    = 1,
};
uint8_t autotuneAmp0Mode = (uint8_t)AUTOTUNE_AMP0_MODE_DEFAULT;

static inline const char *autotune_amp0_mode_name(uint8_t m) {
  return (m == AMP0_MODE_CALC) ? "CALC" : "MEASURE";
}

// Amp-comp calibration method (A/B via cmds 34/35; see docs/AUTOTUNE.md).
// 0 CLASSIC: per-note range-PWM search (calibrate_DCO).
// 1 FREQ_TRACE: fixed-PWM frequency bisection from the manual 440 Hz anchor
//   (calibrate_DCO_freq_trace). Boot default from AUTOTUNE_AMP_METHOD_DEFAULT
//   in DCO.ino; fallback here if that is unset.
#ifndef AUTOTUNE_AMP_METHOD_DEFAULT
#define AUTOTUNE_AMP_METHOD_DEFAULT 0
#endif
enum AutotuneAmpMethod : uint8_t {
  AMP_METHOD_CLASSIC    = 0,
  AMP_METHOD_FREQ_TRACE = 1,
};
uint8_t autotuneAmpMethod = (uint8_t)AUTOTUNE_AMP_METHOD_DEFAULT;

static inline const char *autotune_amp_method_name(uint8_t m) {
  return (m == AMP_METHOD_FREQ_TRACE) ? "FREQ_TRACE" : "CLASSIC";
}

// How the frequency search closes in once it has the answer bracketed
// (A/B via cmds 37/38/39; see docs/AUTOTUNE.md).
// 0 BISECT: geometric midpoint every time. Only the sign of a reading matters,
//   so a noisy magnitude cannot move the probe. Slowest, most robust.
// 1 INTERP: Illinois secant in log-frequency. Two or three probes instead of
//   six, at the cost of believing the size of a reading.
// 2 GATED: INTERP where both bracket readings are clearly above the
//   measurement's own noise, BISECT where they are not.
#ifndef AUTOTUNE_SEARCH_MODE_DEFAULT
#define AUTOTUNE_SEARCH_MODE_DEFAULT 1
#endif
enum AutotuneSearchMode : uint8_t {
  SEARCH_BISECT = 0,
  SEARCH_INTERP = 1,
  SEARCH_GATED  = 2,
};
uint8_t autotuneSearchMode = (uint8_t)AUTOTUNE_SEARCH_MODE_DEFAULT;

static inline const char *autotune_search_mode_name(uint8_t m) {
  switch (m) {
    case SEARCH_BISECT: return "BISECT";
    case SEARCH_GATED:  return "GATED";
    default:            return "INTERP";
  }
}

// Measure at FINE quality for the rest of the enclosing scope, whatever the run
// asked for, and put the run's own precision back on the way out (including when
// the calibration is cancelled mid-probe).
//
// For the top of the range: that pair is where every note above the last rung is
// played from, and it sits right below the frequency at which the pulse
// collapses, so it is worth more readings than a coarse run would give it - and
// up there a reading is a couple of milliseconds, so they are nearly free.
struct CalPrecisionOverride {
  uint8_t saved;
  explicit CalPrecisionOverride(uint8_t p = CAL_PRECISION_FINE)
      : saved(calibrationPrecision) {
    calibrationPrecision = p;
  }
  ~CalPrecisionOverride() { calibrationPrecision = saved; }
};

#ifndef NUM_PW_CHANNELS
#define NUM_PW_CHANNELS NUM_OSCILLATORS
#endif

// PW channel that belongs to oscillator `osc`.
// DCO3: PW arrays are NUM_OSCILLATORS and only [0] is wired, so osc 1/2
// hit PW_PIN_UNASSIGNED and are skipped. DCO4: two oscillators share one
// PW channel (NUM_PW_CHANNELS == NUM_VOICES_TOTAL), so this is osc / 2.
static inline uint8_t cal_pw_channel(uint8_t osc) {
  if (NUM_PW_CHANNELS == NUM_OSCILLATORS) return osc;
  return (uint8_t)(osc / (NUM_OSCILLATORS / NUM_PW_CHANNELS));
}

// Manual DCO calibration workflow state and per-oscillator manual offsets
// that are added on top of automatic amp compensation.
uint8_t manualCalibrationStage;
int8_t manualCalibrationOffset[NUM_OSCILLATORS] = { 0, 0, 0 };

// Oscillator under trim. Packed DCO4 walk is not stage/3.
static inline uint8_t cal_stage_to_osc(uint8_t stage) {
  return cal_stage_to_osc_n(stage, NUM_OSCILLATORS);
}
static inline CalStageKind cal_stage_kind(uint8_t stage) {
  return cal_stage_kind_n(stage, NUM_OSCILLATORS);
}
static inline bool cal_stage_is_440(uint8_t stage) {
  return cal_stage_is_440_n(stage, NUM_OSCILLATORS);
}
static inline bool cal_stage_is_saw(uint8_t stage) {
  return cal_stage_is_saw_n(stage, NUM_OSCILLATORS);
}
static inline bool cal_stage_is_tri(uint8_t stage) {
  return cal_stage_is_tri_n(stage, NUM_OSCILLATORS);
}
static inline bool cal_stage_is_pw_edit(uint8_t stage) {
  return cal_stage_is_pw_edit_n(stage, NUM_OSCILLATORS);
}
static inline bool cal_stage_is_square(uint8_t stage) {
  return cal_stage_is_square_n(stage, NUM_OSCILLATORS);
}
static inline uint8_t cal_manual_osc() {
  uint8_t osc = cal_stage_to_osc(manualCalibrationStage);
  if (osc >= NUM_OSCILLATORS) osc = NUM_OSCILLATORS - 1;
  return osc;
}

static inline uint8_t cal_stage_max() {
  return cal_stage_max_n(NUM_OSCILLATORS);
}

// Manual calibration step (PARAM_MANUAL_CALIBRATION_STEP): 0 = trimpot stage
// at the low starting note, 1 = 440 Hz amp-set stage (adjust ampComp440 until
// duty = 50%). Reset to 0 on every manual-cal entry.
uint8_t manualCalibrationStep = 0;

// Per-oscillator amp-comp (range PWM) value at 440 Hz, set during manual
// calibration step 1 and persisted in LittleFS ("AmpComp440"). 0 = never set;
// FREQ_TRACE refuses to run without it (it is the curve anchor).
uint16_t ampComp440[NUM_OSCILLATORS] = { 0, 0, 0 };

// Per-oscillator duty target trim (PARAM_AMP_COMP_DUTY_OFFSET), in hundredths
// of a percent of duty, persisted in LittleFS ("AmpCompDutyOffset").
// The calibration sense pin is a digital input with its own thresholds, so the
// duty it calls 50% can be a fixed offset away from the 50% a scope sees on
// the analog pulse output. Both amp-comp methods aim at 50% + this trim, so
// dialling it once per oscillator (scope on the pulse output) makes every
// stored point land at a true 50%. 0 = untrimmed, the historical behaviour.
int16_t ampCompDutyOffset[NUM_OSCILLATORS] = { 0, 0, 0 };

// Gap (in microseconds) that corresponds to the trimmed duty target at freqHz.
// From duty - 0.5 = gap / (2T): gapTarget = 2T * offsetFraction. Searches
// compare their measured gap against this instead of against zero.
static inline float duty_trim_gap_us(uint8_t osc, float freqHz) {
  if (osc >= NUM_OSCILLATORS || freqHz <= 0.0f || ampCompDutyOffset[osc] == 0) {
    return 0.0f;
  }
  const float offsetFraction = (float)ampCompDutyOffset[osc] / 10000.0f;
  return 2.0f * (1.0e6f / freqHz) * offsetFraction;
}

/************************************************/
/****************** DCO calibration ******************/

// Temporary buffer used during calibration to build [frequency, range-PWM]
// pairs for a single DCO. Persisted via update_FS_voice() when an osc is done.
uint32_t calibrationData[chanLevelVoiceDataSize];

// --- Per-run calibration report -------------------------------------------
// Where each table pair came from and which duty error was achieved when it
// was measured. Filled by whichever amp-comp method built the table and
// printed by print_calibration_report() once the oscillator is done, so a bad
// point is visible without re-measuring anything.
enum CalPointSource : uint8_t {
  CAL_SRC_NONE = 0,       // never written (should not survive a complete run)
  CAL_SRC_RUNG,           // ladder rung (FREQ_TRACE) / per-note search (classic)
  CAL_SRC_ANCHOR,         // the 440 Hz manual operating point
  CAL_SRC_ENDPOINT_FULL,  // top endpoint at full amp comp
  CAL_SRC_ENDPOINT_AMP0,  // bottom endpoint at amp comp 0
  CAL_SRC_MANUAL,         // trimpot header pair, written without measuring
  CAL_SRC_FILLED,         // interpolated or estimated, never measured
  CAL_SRC_SENTINEL,       // 20000000 padding above the top endpoint
  CAL_SRC_REFINED,        // stored pair re-measured by the fine pass
};

// Number of [freq, amp comp] pairs in a calibration table.
static constexpr int kCalReportPairs = (int)(chanLevelVoiceDataSize / 2);

// Marks a pair with no measurement behind it in calPointDutyErrPct[].
static constexpr float kCalDutyErrUnknown = 1e9f;

// Signed duty error per pair, in percentage points. Sign follows the search
// convention (measure_gap_for_amp / measure_duty_at_freq): + = amplitude too
// low, i.e. the pulse spends less than half the period high.
float   calPointDutyErrPct[kCalReportPairs];
uint8_t calPointSource[kCalReportPairs];

// FREQ_TRACE ladder shape of the last run, for the report header
// (0 / -1 = not applicable, e.g. after a classic run).
int calReportLadderInterval = 0;
int calReportAnchorPair     = -1;

// What the current oscillator's amp-comp stage has cost so far: duty
// measurements taken and wall-clock time since cal_report_reset(). These are the
// two numbers the search-mode A/B (autotuneSearchMode, cmds 37-39) is judged on,
// so they are printed on the report footer. The PW stage is not counted; it does
// not go through the frequency search.
uint32_t      calRunProbes  = 0;
unsigned long calRunStartMs = 0;

// Duty error in percentage points from a measured gap in microseconds, using
// |duty - 0.5| = |gap| / (2 * period). Keeps the caller's sign.
static inline float duty_err_pct_from_gap(float gapUs, float freqHz) {
  if (gapUs == kGapTimeoutSentinel || freqHz <= 0.0f) {
    return kCalDutyErrUnknown;
  }
  return 100.0f * gapUs * freqHz / 2.0e6f;
}

// Calibration logs: 3 decimal Hz. The stored table stays freq × 100 integers.
static inline String fmt_freq(float hz) {
  return String(hz, 3);
}

// Verification-sweep request (PARAM_DEBUG_COMMAND 36). Set on core 0; loop1
// runs the sweep because every probe blocks on a duty measurement.
volatile bool calibrationVerifyRequested = false;

// PW CV probe request (PARAM_DEBUG_COMMAND 46). Set on core 0 while manual
// calibration is running; loop1 runs it between two manual passes because each
// step blocks on a duty measurement.
volatile bool pwCvProbeRequested = false;

// Manual calibration solos one oscillator by stopping every other state machine,
// which a synced pair cannot survive: under hard sync the master's sideset owns
// the slave's RESET pin, under soft sync the slave polls the master's pin, so a
// stopped partner leaves the soloed oscillator unable to reset itself and it goes
// silent. Manual cal walks with a neutral topology and puts the operator's choice
// back on exit; these hold it meanwhile, and also absorb a sync change arriving
// mid-walk (a preset load) so it cannot re-arm sync under the solo.
uint8_t manualCalSavedSyncMode = 0;
uint8_t manualCalSavedSoftSyncChunks = 0;
// Rebuilding the topology touches PIO, so core 0 only asks: loop1's manual-cal
// branch runs it, since that branch never reaches pio_defer_service().
volatile bool calSyncNeutralRequested = false;

// --- Implemented in autotune_impl.h ---

// The calibration run itself, driven from core 1 while calibrationFlag is set.
void DCO_calibration();

void run_calibration_verify_sweep();
void run_pw_cv_probe();
void cal_report_reset();
void cal_report_set_pair(int pair, float dutyErrPct, uint8_t src);
void cal_report_set_pair_from_gap(int pair, float gapUs, float freqHz, uint8_t src);
void print_calibration_report(uint8_t dcoIndex, const uint32_t *data);

// Prepare the next oscillator's calibration run: seed its table header, reset
// the per-DCO state and drive the start note.
void restart_DCO_calibration();

// Undo what a calibration run did to the oscillators: PW centers back, every SM
// started same-cycle, voices retriggered. Manual cal reaches it from core 0
// through pio_defer_request_cal_restore(), so it needs a declaration this early.
static void restore_voice_engine_after_calibration();

// PW calibration stages, called from the param handlers and the manual
// calibration workflow. mode selects which note/voice the center search runs on.
void find_PW_center(uint8_t mode);

// One-shot diagnostics for the calibration sense path (PARAM_DEBUG_COMMAND).
void DCO_calibration_debug();

// Index of the DCO currently being calibrated.
uint8_t currentDCO;

// millis() timestamp when the current calibration pass started. Used by the
// PW search phases for their 60 s safety timeouts.
unsigned long DCOCalibrationStart;

// Current range-PWM value used during calibration for the active DCO.
volatile uint16_t ampCompCalibrationVal;

// Frequency override (Hz) consumed by voice_task_autotune() mode 4 during the
// highest-frequency search (replaces the old PID_v1 PIDOutput coupling).
float calibrationFreqHz = 0.0f;

// When > 0, find_gap() gates edge intervals against this frequency instead of
// note_to_freq(DCO_calibration_current_note). Set by measure_duty_at_freq()
// while probing arbitrary frequencies; 0 = fall back to the current note.
float gapGateFreqHz = 0.0f;

// Frequency (Hz) the oscillator is currently running at, so the next probe
// knows how far it has to move: measure_duty_at_freq() sizes its stability
// checks from that distance. 0 = nothing is running (a cold start), which
// counts as the largest possible move.
float g_lastDrivenFreqHz = 0.0f;

// Baseline manual amp-comp starting value (PWM counts). 35 was measured at
// wrap 14000; scale so analog duty stays the same if RANGE_PWM_WRAP changes.
static constexpr uint16_t initManualAmpCompCalibrationValPreset =
    (uint16_t)(35u * DIV_COUNTER / 14000u);
// Per-oscillator baseline manual amp-comp starting values. Filled from the
// preset on first use so the array size can follow NUM_OSCILLATORS (3 on
// DCO3, 8 on DCO4) without a brace list that only covers the first three.
uint16_t initManualAmpCompCalibrationVal[NUM_OSCILLATORS];

static inline void autotune_fill_init_manual_amp() {
  static bool filled = false;
  if (filled) return;
  for (int i = 0; i < NUM_OSCILLATORS; ++i) {
    initManualAmpCompCalibrationVal[i] = initManualAmpCompCalibrationValPreset;
  }
  filled = true;
}
// Range-PWM value stored as the "lowest frequency" anchor in the calibration
// table header (also persisted by FS.ino when seeding fake tables). 10 was
// measured at wrap 14000.
volatile uint16_t ampCompLowestFreqVal = (uint16_t)(10u * DIV_COUNTER / 14000u);

// Note from which DCO calibration starts, in the offset convention described at
// manual_cal_reference_note below (note 29 -> sNotePitches[17] = F0, 21.83 Hz).
static constexpr uint8_t DCO_calibration_start_note = 29;
// Interval in semitones between successive calibration notes.
static constexpr uint8_t calibration_note_interval = 5;
// Starting note used for the PW-centered calibration passes.
static constexpr uint8_t manual_DCO_calibration_start_note = DCO_calibration_start_note - 5;
// Reference note for the manual trim stage: A4, exactly 440 Hz. Duty feedback
// refreshes ~27x faster than at the low trim note, and this operating point
// becomes the anchor of the FREQ_TRACE curve.
// PW calibration deliberately stays at manual_DCO_calibration_start_note.
//
// 81, not the 69 an A4 usually is: note_to_freq() below reads
// sNotePitches[midiNote - 12], and that table starts at C-1 (8.18 Hz, standard
// MIDI 0), so every note number here names a pitch an octave below the MIDI
// note of the same number. 81 - 12 = 69 = NOTE_A4. The whole autotune path
// shares this convention (voice_task_autotune() looks up VOICE_NOTES[0] - 12
// the same way), so the numbers are consistent; only this one was chosen as if
// they were not, which had manual step 2 trimming at 220 Hz while the panel
// said 440.
static constexpr uint8_t manual_cal_reference_note = 81;

// Current note used during calibration.
uint8_t DCO_calibration_current_note;

// Global debug verbosity level for autotune routines.
byte autotuneDebug = 1;

// Convert a calibration note number to its frequency in Hz. sNotePitches[]
// starts at C-1 (standard MIDI 0), so the -12 makes every note number here name
// a pitch an octave below the MIDI note of the same number - see the convention
// note at manual_cal_reference_note above.
static inline float note_to_freq(uint8_t midiNote) {
  return sNotePitches[midiNote - 12];
}

// Period-proportional settle delay before a duty measurement: two waveform
// periods, floored at 4 ms (replaces the old fixed delay(10) which was too
// short at low frequencies and needlessly long at high ones).
static inline void settle_for_freq(double freqHz) {
  uint32_t settleMs = 4;
  if (freqHz > 0.0) {
    double twoPeriodsMs = 2000.0 / freqHz;
    if (twoPeriodsMs > (double)settleMs) {
      settleMs = (uint32_t)(twoPeriodsMs + 0.999);
    }
  }
  delay(settleMs);
}

// --- Implemented in autotune_search_impl.h ---

// Allowed |gap| in microseconds for a frequency and duty-error fraction.
double compute_gap_tolerance_for_freq(double freqHz, double dutyErrorFraction);

// Main DCO amp-comp calibration routine (search-based).
// dutyErrorFraction specifies the allowed duty-cycle error (e.g. 0.005 = 0.5%).
void calibrate_DCO(DCOCalibrationContext& ctx, double dutyErrorFraction);

// Curve-fit helpers shared by the calibration searches and the table builders.
float    quadraticInterpolation(float x0, float y0, float x1, float y1,
                                float x2, float y2, float x);
uint16_t logarithmicInterpolation(float x0, float y0, float x1, float y1, float x);
float    linearInterpolation(float x0, float y0, float x1, float y1, float x);
double   expInterpolationSolveY(double x, double x0, double x1,
                                double y0, double y1);

// Highest usable frequency at full range PWM (returns Hz * 100). pairsFilled is
// how much of ctx's table already holds measured points to model from.
float find_highest_freq(DCOCalibrationContext& ctx, int pairsFilled);

// Estimated lowest reachable frequency at amp comp = 0 (returns Hz * 100).
float find_lowest_freq();

// Hard frequency limits for one search. Outside them there is either nothing to
// find or nothing the caller may store, so a search that reaches an edge without
// a signal stops there instead of spending 100 ms timeouts past it. This type
// lives in the header for the same reason the PW ones below do: the Arduino
// builder's generated prototypes have to compile.
struct FreqSearchBounds {
  float loHz;
  float hiHz;
};

// Measured lowest usable frequency at amp comp = 0: scan bounds for a pair of
// readings that bracket 50% duty, then search between them with the amp fixed at
// 0. freqSeedHz is the fallback when nothing in the band pulses.
// Returns Hz, or 0 if no signal.
float measure_lowest_freq_at_amp0(float freqSeedHz, const FreqSearchBounds *bounds);

// Overwrite the table's amp-comp-0 anchor (entry [0..1]) with a measured point
// (classic method only). Keeps the previous estimate when there is no pulse at
// amp 0 anywhere in the band below the first real pair, or when the point found
// there is not close enough to 50% duty to be the one we were looking for.
void apply_measured_lowest_freq(DCOCalibrationContext& ctx);

// Probe the duty error at an arbitrary frequency with a fixed range PWM.
// Positive result = amplitude too low (frequency too high for this PWM);
// kGapTimeoutSentinel on timeout. hiRes averages twice as many segments.
float measure_duty_at_freq(float freqHz, uint16_t amp, bool hiRes = false);

// Search the frequency at a fixed range PWM at which duty = 50%. Measures
// freqGuess first and then steps outward until it brackets the answer, expecting
// it within freqGuess * [1/windowRatio, windowRatio]. refine adds hi-res probes,
// a larger probe budget and an averaged confirmation of the converged point.
// bounds, when given, is where the search may look at all - which also lets it
// spend its whole probe budget inside a band it has been told holds the answer,
// instead of stopping at the timeout allowance an unbounded search gets.
// Returns the best frequency in Hz, or 0 if no usable signal was seen.
float find_freq_for_duty50(uint16_t amp, float freqGuess, float windowRatio,
                           bool refine = false,
                           const FreqSearchBounds *bounds = nullptr);

// FREQ_TRACE amp-comp calibration: trace the freq(PWM) curve outward from the
// 440 Hz manual anchor. Returns false if the resulting table failed the
// monotonicity check (caller must then skip persisting it).
bool calibrate_DCO_freq_trace(DCOCalibrationContext& ctx);

// Fine pass (calibrationPrecision == CAL_PRECISION_FINE): keep every amp comp
// value of the stored table and re-measure the frequency each one really sits
// at. Returns false when there is no usable stored table or the result is not
// monotonic (caller must then skip persisting it).
bool refine_DCO_amp_table(DCOCalibrationContext& ctx);

// --- PW target-duty search (autotune_impl.h) ---
// These types live in this header (rather than beside the definitions) because
// the search-phase helpers are declared here.

// Maximum number of valid samples remembered by a PW search.
static constexpr int kPWMaxSamples = 40;

// State shared by the PW target-duty search phases (coarse scan, bisection,
// fine scan, candidate selection). All gap differences are relative to the
// target gap (gap - gapTarget), so "0" always means "exactly on target duty".
struct PWSearchState {
  uint16_t validPW[kPWMaxSamples];       // PW of each stored valid sample
  double   validGapDiff[kPWMaxSamples];  // gap - gapTarget for each sample
  int      validCount;
  int      inToleranceCount;  // valid samples measured within targetGap
  bool     haveBest;          // at least one valid sample was seen
  double   bestGapAbs;        // smallest |gap - gapTarget| seen so far
  uint16_t bestPW;            // PW that produced bestGapAbs
  bool     haveBracket;       // sign-change bracket found during coarse scan
  uint16_t pwLow, pwHigh;     // bracket bounds
  double   gapLow;            // raw gap measured at pwLow
};

// How a sample should enter the valid-samples table.
enum PWRecordMode {
  PW_RECORD_NO_TABLE,       // update best/in-tolerance counters only
  PW_RECORD_APPEND,         // append while there is room
  PW_RECORD_REPLACE_WORST,  // append, or replace the worst entry when full
};

// --- PW limit search (autotune_impl.h) ---

// Direction selector for the unified PW limit search.
enum PWLimitDir {
  PW_LIMIT_LOW,
  PW_LIMIT_HIGH
};

// Result structure used by the PW-limit search helpers.
struct PWLimitSearchResult {
  bool     ok;                  // true if at least one valid sample was found
  uint16_t limitPW;             // PW value chosen as limit
  double   finalDutyPercent;    // measured duty at limitPW in percent, or < 0 if unknown
};

// Low-level search routine that assumes the DCO is already configured for
// PW calibration on the desired note/voice. It scans from centerPW toward
// the requested direction and returns the PW that best matches targetDuty.
PWLimitSearchResult search_PW_limit_from_center(
  uint8_t     voiceIdx,
  uint16_t    centerPW,
  PWLimitDir  dir,
  double      periodUs,
  double      targetDuty
);

// High-level wrapper that configures the calibration context and commits the
// found limit (LOW or HIGH) to the filesystem and runtime tables.
void find_PW_limit_v2(PWLimitDir dir);


#endif
#ifndef __AUTOTUNE_CONSTANTS_H__
#define __AUTOTUNE_CONSTANTS_H__

#include <stdint.h>

// Common constants used by the DCO/VCO autotune and measurement code.
// Kept here to avoid magic numbers scattered across the implementation.

// Sentinel value returned by gap-measurement routines to indicate
// a timeout or invalid measurement.
constexpr float kGapTimeoutSentinel = 1.16999f;

// PARAM_GAP_FROM_DCO payload (duty error [%] * 100) when find_gap times out.
// Distinct from a real near-zero trim so USB/Screen never look "perfect".
constexpr int32_t kManualGapTimeoutDutyErrTimes100 = 99999;

// Time without seeing an edge before a measurement is considered timed out, and
// (being the same thing from the other side) the longest segment find_gap() will
// accept as part of a waveform.
//
// 100 ms is generous for most of the range and not generous at all at the bottom
// of it: one period at 12 Hz is 83 ms, and amp comp 0 - the one operating point
// where the pulse is deliberately as lopsided as the oscillator can make it -
// can easily put a single segment past 100 ms there. So the deadline is a floor
// rather than a constant: kGapTimeoutPeriods periods of whatever is being probed
// when that is longer, up to kGapTimeoutMaxUs. The cap matters because this is
// also what a dead oscillator costs per probe.
constexpr unsigned long kGapTimeoutUs     = 100000UL;
constexpr unsigned long kGapTimeoutMaxUs  = 400000UL;
constexpr double        kGapTimeoutPeriods = 2.5;

// Minimum time (in microseconds) between detected edges to treat
// them as valid (simple debounce).
constexpr unsigned long kEdgeDebounceMinUs = 20UL;

// Accepted low/high segments averaged per find_gap() measurement in the
// classic path (specialMode 0). The hi-res modes (2 = PW search, 3 = frequency
// probe) take their segment counts from the precision profile below.
constexpr uint16_t kGapSamplesDefault = 6;
constexpr uint16_t kGapSamplesHiRes   = 12;

// Segment floor for hi-res readings below kSearchStepVeryLowHz (30 Hz). Down
// there the profile's measurement window fits nothing at a 50+ ms period, so a
// reading otherwise sits on the profile's 6-segment floor - and pair 1 of the
// table was noise-limited to ~0.2% duty by exactly that. Twice the segments
// costs ~150 ms per reading at 20 Hz and drops the swing by ~1/sqrt(2). The
// gapMaxWindowMs cap still bounds it, which is what keeps the amp-0 scan at
// 8 Hz from paying for this.
constexpr uint16_t kGapSamplesVeryLowMin = 12;

// --- Calibration precision profiles ----------------------------------------
//
// Everything that trades run time against measurement quality lives here, in
// three sets: NORMAL builds a table from scratch quickly, FINE re-measures an
// existing one as accurately as the hardware allows, FAST is NORMAL cut down
// to produce a testing table as quickly as possible. The value of
// PARAM_CALIBRATION_FLAG picks the set (1/2/3 normal, 5/6/7 fine, 9/10/11
// fast); see calibrationPrecision in autotune.h and docs/AUTOTUNE.md.
struct CalPrecisionProfile {
  uint16_t gapSamplesMin;     // find_gap modes 2/3: floor on averaged segments
  uint16_t gapSamplesMax;     // ... and ceiling
  uint32_t gapWindowMs;       // measurement window the segment count aims at
  uint32_t gapMaxWindowMs;    // ... and the longest one reading may ever take
  float    settlePeriods;     // wait after the last frequency write, in periods
  uint16_t settleMinMs;       // ... floored at this many milliseconds
  double   bisectDutyTol;     // search acceptance, as a fraction of duty
  double   bisectGapFloorUs;  // ... floored at this many microseconds of gap
  int      bisectIters;       // probes allowed per search
  int      bisectWindows;     // travel allowance, in caller windows
  int      confirmReads;      // readings averaged at the converged frequency
  int      confirmRounds;     // corrections allowed if that average misses
  int      anchorTries;       // FREQ_TRACE 440 Hz anchor corrections
  int      rungRetries;       // FREQ_TRACE per-rung corrections
  uint8_t  settleMaxChecks;   // re-readings allowed after a large move
  float    settleStableMult;  // x acceptance tolerance = "reading has settled"
};

// A frequency move smaller than this needs no stability check: the late
// iterations of a bisection move by well under a cent, and the analog core has
// nothing to follow.
constexpr float kSettleSkipCents = 5.0f;

// A move of this size or more gets the profile's full stability budget; smaller
// ones (but above kSettleSkipCents) get a single confirming reading.
constexpr float kSettleBigMoveCents = 100.0f;

// Window ratios for the fixed-amp probes that are not on the derived ladder: how
// far from its seed each one expects the answer to be. The manual trim point is a
// known note, and both endpoints are seeded by a model built from ~20 measured
// points, so all of them can search tightly.
//
// windowRatio also sizes the search's first step (a quarter of it), which is why
// the two endpoints differ. The top one is seeded by a power law anchored on the
// nearest measured point and lands within ~10 cents. The bottom one extrapolates
// to amp comp 0, where the log-log form has no anchor at all, so its seed can be
// most of an octave out.
constexpr float kManualNoteWindowRatio     = 1.15f;
constexpr float kTopEndpointWindowRatio    = 1.05f;
constexpr float kBottomEndpointWindowRatio = 1.25f;
constexpr float kAnchorWindowRatio         = 1.15f;

// The exception: the very first anchor probe, which is the one search in a run
// with no model behind it at all. Its amp comp comes from the panel slider, and
// a hand-dialled value can be a long way from a 440 Hz operating point - after
// the reference note moved from 220 Hz to 440 Hz, a value stored by an earlier
// firmware is a whole octave off. An octave either way is wide enough to find
// where that amp really sits; the re-anchor loop then walks the amp to 440 Hz
// and persists it, and every later probe uses the tight window above.
constexpr float kAnchorAcquireWindowRatio  = 2.0f;

// Where the bootstrap probes sit relative to the 440 Hz anchor, in semitones of
// amp comp: amp = anchorAmp * 2^(n/12), which lands the frequencies near
// 440 * 2^(n/12) - about 311, 370, 523 and 622 Hz. These four points are what
// the model has to work with before the ladder starts, so they have to straddle
// the anchor (one above, one below, twice over) and be far enough apart to say
// something about the curve's shape. The inner pair comes first, so that a run
// that loses the outer probes still has a straddle. They only ever feed the
// model - none of them is written to the table.
constexpr int kBootstrapSemitones[4] = { 3, -3, 6, -6 };

// The power-law and quadratic seeds for the full-amp endpoint disagreeing by more
// than this means the curve is bending near the ceiling and the quadratic is
// extrapolating through the bend. Take the lower of the two when that happens:
// overshooting lands in the collapse, and a timeout carries no information about
// where the answer is.
constexpr float kEndpointSeedAgreeCents = 50.0f;

// Largest single step the frequency search may take, by range, in cents. It
// steps out from its seed until it brackets the answer and then interpolates,
// so this only bounds one probe's move - but that bound is what keeps the
// bottom of the range readable, where a probe is a handful of periods long and
// a jump of several hundred cents lands on a waveform that is still moving.
// The very-low range exists for the amp-comp-0 hunt: down there a 50-cent
// step barely moves the duty, so the search walks without closing. A semitone
// (100 cents) is enough to change the reading and still half of the 200-cent
// mid-range cap; the denser pre-scan is what finds the sign change, not creeps.
constexpr float kSearchStepVeryLowHz     = 30.0f;
constexpr float kSearchStepLowHz         = 100.0f;
constexpr float kSearchStepHighHz        = 440.0f;
constexpr float kSearchStepCentsVeryLow  = 100.0f;  // below kSearchStepVeryLowHz
constexpr float kSearchStepCentsLow      = 100.0f;  // kSearchStepVeryLowHz .. LowHz
constexpr float kSearchStepCentsMid      = 200.0f;  // kSearchStepLowHz .. HighHz
constexpr float kSearchStepCentsHigh     = 400.0f;  // above kSearchStepHighHz

// Smallest frequency move the search will make, and the "same frequency" test.
// A 2-cent stop at 8 Hz is ~0.01 Hz — finer than the measurement — and was
// treating opposite-sign readings at one frequency as a finished bracket.
constexpr float kMinFreqStepHz = 0.1f;

// ... and, tighter than any of those, what the latest reading itself implies.
// The duty error moves ~3-4% per 100 cents across the whole range, so a seed
// that reads -0.07% is a few cents from the answer and a 100-cent jump away
// from it is pure waste (measured: 15 probes for a rung whose seed was already
// within noise). Dividing the error by a deliberately flat slope (about half
// the flattest measured) makes the step overshoot the true distance ~2x, so
// it still brackets in one hop; the floor guarantees progress when the error
// is within noise of zero. Timeouts have no magnitude and keep the range cap.
constexpr float kSearchSlopeMinPctPer100Cents = 1.5f;
constexpr float kSearchStepFloorCents         = 3.0f;

// ... except while a search with explicit bounds has yet to measure a single
// pulse, when the caps above protect nothing (there is no reading to spoil) and
// only slow the walk out of a region the oscillator cannot produce. Half an
// octave per step: enough to cross a dead zone in a few probes, small enough not
// to step over a narrow band of usable frequencies on the way. Bounded only,
// because striding away from a seed with no limit to stop at is how an unbounded
// search overshoots instead of arriving.
constexpr float kHuntStepMaxCents = 600.0f;

// Where the amp-comp-0 endpoint is allowed to be: from just under the first
// measured pair down to a fraction of it. Both amp-comp methods extrapolate the
// point before measuring it, and that extrapolation has no anchor under it, so
// the band has to be wide enough to hold the answer wherever it really is - a
// measured table puts it at pair 1 / 2.2, so a band of one ladder rung (which is
// what this used to be) looks for it about an octave above where it is.
//
// The floor is the frequency below which a reading stops meaning anything:
// kGapTimeoutPeriods periods of it are longer than the kGapTimeoutMaxUs deadline,
// so a lopsided pulse there cannot be told from silence, and each probe that
// tries costs the full cap.
constexpr float kAmp0BandRatio = 2.5f;
constexpr float kAmp0MinFreqHz = 5.0f;

// The freq(amp) curve is measured to be nearly linear at the bottom of the
// range (pair-to-pair slopes agree within ~1%), so the amp-0 intercept comes
// from a least-squares line through this many of the lowest-amp measured
// points. The 3-point quadratic that used to be extrapolated there amplified
// the noise of exactly the noisiest points and swung by whole octaves between
// runs (-2.87, 4.18, 4.68 Hz on the same hardware); the line is stable.
constexpr int kAmp0FitPoints = 5;

// When the amp-0 endpoint cannot be measured (on hardware whose pulse dies
// before the duty reaches 50%, i.e. practically always), the fit above is what
// gets stored as entry 0 - it is an interpolation anchor for the runtime
// lookup, not a producible frequency, so it is not clamped to the search band.
// Only a sanity floor applies.
constexpr float kAmp0StoreFloorHz = 2.0f;

// The endpoint is scanned for before the search proper: this many frequencies,
// log-spaced across the band, one quick reading each after a wait of at least
// this long (or one period, whichever is longer - 20 ms at 8 Hz is a seventh of
// a period, and what comes back describes the previous frequency). A single
// modelled probe down here tells the search nothing except that it saw nothing,
// whereas a scan finds which frequencies pulse at all and, better, brackets the
// answer between two readings of opposite sign for the search to close on.
constexpr int      kAmp0ScanPoints   = 10;
constexpr uint32_t kAmp0ScanSettleMs = 20;

// How far the measured period (avgLow + avgHigh) may deviate from the ideal
// one before the reading is not describing the requested waveform at all. A
// healthy reading lands within ~0.5% of the ideal period; a degenerate one can
// be wildly off - at amp comp 0 near 6 Hz the pin was observed toggling at
// ~58% of the requested period (a double-trigger of the comparator), whose
// near-symmetric sub-segments read ~50% duty no matter what the frequency is.
// The 1%..99%-of-period segment gate cannot catch that; only the sum can.
constexpr float kGapPeriodTolRatio = 0.15f;

// How close to 50% the duty of the amp-comp-0 endpoint must come for it to count
// as measured, in percentage points. The rungs land within 0.05%, but this point
// is different in kind: at amp comp 0 the pulse may die before the duty ever
// reaches 50%, in which case the search converges on the border of the dead zone
// and returns a frequency whose duty is nowhere near the target. Storing that as
// pair 0 is worse than storing the extrapolation it was seeded with.
constexpr float kEndpointAcceptDutyPct = 0.5f;

// Timeouts in a row an unbounded search may spend before giving up with its best
// reading. A timeout costs at least kGapTimeoutUs (100 ms, more at the bottom of
// the range), doubled after a large move by the retry in measure_duty_at_freq(),
// so a search hunting inside a region the oscillator cannot produce at all is
// the most expensive way to learn nothing. Six is enough to walk out of a dead
// zone or converge onto its edge. In a row: a good reading resets the count,
// because a search that is still producing them is converging rather than lost.
constexpr int kMaxSearchTimeouts = 6;

// How long one reading takes is gapWindowMs by construction - the segment count
// is the window divided by a half period - so the two clamps around it are what
// actually decide the cost at the ends of the range. At the bottom the floor
// wins (at 16 Hz a segment is 30 ms, so 12 of them are 367 ms), which is what
// gapMaxWindowMs bounds. At the top the ceiling still wins, and how much
// averaging it allows is the accuracy/speed trade at that end: a fast build
// stops at 64 segments (16 ms at 2 kHz), a fine one lets the window govern.
//
// Fast build. The segment floor of 6 (instead of 12) is what speeds up the
// bottom of the range, where one segment is already tens of milliseconds, and
// the looser acceptance lets a good probe leave the search early. Only one
// stability check is allowed, so a from-scratch run does not pay for settling
// twenty times per pair.
constexpr CalPrecisionProfile kCalPrecisionNormal = {
  /* gapSamplesMin    */ 6,
  /* gapSamplesMax    */ 64,
  /* gapWindowMs      */ 25,
  // 300 rather than 200 so the cap admits kGapSamplesVeryLowMin (12) segments
  // at ~20 Hz, where the lowest measured pair sits; at 8 Hz it still holds a
  // reading to ~5 segments, so the amp-0 scan keeps its cost.
  /* gapMaxWindowMs   */ 300,
  /* settlePeriods    */ 1.0f,
  /* settleMinMs      */ 3,
  /* bisectDutyTol    */ 0.0005,
  /* bisectGapFloorUs */ 0.5,
  /* bisectIters      */ 24,
  /* bisectWindows    */ 2,
  /* confirmReads     */ 2,
  // Two rounds, not one: rungs can drift a few hundredths of a percent between
  // the search and the confirm (the DCO still creeping at a fresh operating
  // point), and with a single round the confirm can measure that miss but not
  // correct it. The second round runs only when the first average misses the
  // acceptance, so well-behaved rungs pay nothing.
  /* confirmRounds    */ 2,
  /* anchorTries      */ 2,
  /* rungRetries      */ 1,
  /* settleMaxChecks  */ 1,
  /* settleStableMult */ 3.0f,
};

// Fine tuning. Used by the refine pass over a stored table, by the verification
// sweep and by the top-of-range endpoint whatever the run's own precision; the
// anchor/rung fields are unused in the refine pass (no ladder is built) but stay
// filled in so a fine full build is still coherent. What a reading rests on
// after a frequency change is settlePeriods plus the stability checks: the
// frequency is written once and then left alone until whole periods have come
// out of it.
constexpr CalPrecisionProfile kCalPrecisionFine = {
  /* gapSamplesMin    */ 12,
  /* gapSamplesMax    */ 256,
  /* gapWindowMs      */ 60,
  // Matches NORMAL's 300: with 200 the cap clipped a 20 Hz fine reading to 8
  // segments, below what a normal reading now averages there.
  /* gapMaxWindowMs   */ 300,
  /* settlePeriods    */ 1.0f,
  /* settleMinMs      */ 4,
  /* bisectDutyTol    */ 0.0002,
  /* bisectGapFloorUs */ 0.25,
  /* bisectIters      */ 32,
  /* bisectWindows    */ 3,
  /* confirmReads     */ 5,
  /* confirmRounds    */ 2,
  /* anchorTries      */ 3,
  /* rungRetries      */ 1,
  /* settleMaxChecks  */ 3,
  /* settleStableMult */ 2.0f,
};

// Fastest usable build - a table for testing, not for keeps. NORMAL's
// structure with every knob turned toward speed: half the measurement window
// (a reading is noisier by ~sqrt(2), still well inside what the runtime
// interpolation smooths over), a 2x looser acceptance (0.1% duty, far below
// an audible amp error), a single confirm reading with a single round, no
// anchor or rung corrections. The gapMaxWindowMs cap of 200 also clips the
// kGapSamplesVeryLowMin floor at the very bottom, so pair 1 gets noisier -
// and the amp-0 fit through the lowest rungs absorbs it. The structural
// shortcuts (amp-0 fit instead of the live hunt, 2 bootstrap probes instead
// of 4, no forced-FINE top endpoint) are gated on CAL_PRECISION_FAST in
// autotune_search_impl.h, not expressed here.
constexpr CalPrecisionProfile kCalPrecisionFast = {
  /* gapSamplesMin    */ 4,
  /* gapSamplesMax    */ 32,
  /* gapWindowMs      */ 12,
  /* gapMaxWindowMs   */ 200,
  /* settlePeriods    */ 1.0f,
  /* settleMinMs      */ 2,
  /* bisectDutyTol    */ 0.0010,
  /* bisectGapFloorUs */ 1.0,
  /* bisectIters      */ 16,
  /* bisectWindows    */ 2,
  /* confirmReads     */ 1,
  /* confirmRounds    */ 1,
  /* anchorTries      */ 1,
  /* rungRetries      */ 0,
  /* settleMaxChecks  */ 1,
  /* settleStableMult */ 4.0f,
};

// Target duty fractions for PW calibration:
//  - Center:  50% duty
//  - Low:      2% duty (user-adjustable if desired)
//  - High:    98% duty (user-adjustable if desired)
constexpr double kPWCenterDutyFraction = 0.5;
constexpr double kPWLowDutyFraction    = 0.02;
constexpr double kPWHighDutyFraction   = 0.98;

// Polarity of the digital calibration signal on DCO_calibration_pin.
// If your hardware inverts the waveform (so the pin is high when the
// actual DCO output is low, and vice versa), set this to true. All duty
// measurements (find_gap / measure_gap) will automatically compensate.
constexpr bool kGapPolarityInverted    = false;  // true if cal pin is inverted vs DCO output

// Duty tolerance used when validating PW low/high limits and PW center lock-in.
// A sample whose duty is within ±kPWLimitDutyTolerance of the target
// low/center/high duty is considered "in tolerance".
constexpr double kPWLimitDutyTolerance = 0.01;  // ±1% duty

#endif  // __AUTOTUNE_CONSTANTS_H__


#ifndef __AUTOTUNE_CONTEXT_H__
#define __AUTOTUNE_CONTEXT_H__

#include <stdint.h>

// Lightweight context for DCO calibration routines.
// For now this simply groups references/pointers to existing global state
// so that functions like calibrate_DCO() can be written against a single
// parameter without changing behaviour.
struct DCOCalibrationContext {
  // Reference to the global currentDCO index.
  uint8_t& dcoIndex;
  // Reference to the global DCO_calibration_current_note.
  uint8_t& currentNote;
  // Pointer to the per-DCO calibration buffer (calibrationData).
  uint32_t* calibrationData;
  // Pointer to the per-osc manual calibration offsets (±20 counts).
  int8_t* manualOffsetByOsc;
  // Pointer to the per-osc initial manual amp-comp values. These are RANGE PWM
  // counts scaled from DIV_COUNTER, so they do not fit in a byte.
  uint16_t* initManualAmpByOsc;

  DCOCalibrationContext(
    uint8_t& dcoIndexRef,
    uint8_t& currentNoteRef,
    uint32_t* calibrationDataPtr,
    int8_t* manualOffsetPtr,
    uint16_t* initManualAmpPtr
  )
    : dcoIndex(dcoIndexRef),
      currentNote(currentNoteRef),
      calibrationData(calibrationDataPtr),
      manualOffsetByOsc(manualOffsetPtr),
      initManualAmpByOsc(initManualAmpPtr) {}
};

#endif  // __AUTOTUNE_CONTEXT_H__


#ifndef __AUTOTUNE_IMPL_H__
#define __AUTOTUNE_IMPL_H__

#include "../include_all.h"

// =============================================================================
// autotune_impl.h — DCO calibration orchestration, PW center/limit searches and
// the edge-timing duty measurement core (find_gap).
//
// Definitions, not declarations: include this exactly once per sketch, from a
// .ino shim so the merge order of the sketch's translation unit is unchanged
// (DCO/autotune.ino). The declarations are in autotune.h.
//
// The per-note amplitude-compensation search (calibrate_DCO) and its helpers
// live in autotune_search_impl.h.
// =============================================================================

// File-scope helpers called before the line that defines them. While this code
// lived in a .ino the Arduino builder generated these prototypes; a header gets
// none, so they are written out here.
static void reset_pw_to_DIV_COUNTER_PW();
static inline void apply_pw_center(uint8_t ch) {
  if (ch >= NUM_PW_CHANNELS || PW_PINS[ch] == PW_PIN_UNASSIGNED) return;
  pwm_set_chan_level(PW_PWM_SLICES[ch], pwm_gpio_to_channel(PW_PINS[ch]), PW_CENTER[ch]);
  PW[ch] = PW_CENTER[ch];
}

// Stored center on the measured channel, 0 on all the others — the same rule
// the manual walk follows (voice_task_autotune), and for the same reason: the
// pulse is the one wave with no analog switch, so a channel left anywhere but 0
// is an open pulse on a voice nothing is measuring. Applies only; no search can
// move a stored center from here.
//
// Only the soloed channel is tracked in PW[]: the muted ones would write PW[0],
// which the play path reads as the panel's pulse width (apply_param_pw_value).
static void apply_pw_center_solo(uint8_t soloCh) {
  for (uint8_t ch = 0; ch < NUM_PW_CHANNELS; ++ch) {
    if (PW_PINS[ch] == PW_PIN_UNASSIGNED) continue;
    if (ch == soloCh) {
      apply_pw_center(ch);
      continue;
    }
    pwm_set_chan_level(PW_PWM_SLICES[ch], pwm_gpio_to_channel(PW_PINS[ch]), 0);
  }
}

// What a PW channel is actually driving, read back from the slice's compare
// register. The diagnostics want this rather than a tracked copy: PW[] is not a
// hardware mirror (the play path's voice_write_pw() does not update it, and
// PW[0] doubles as the panel's pulse width), and the tracker this replaces was
// only touched by the PW search — it reported 0 through whole amp runs that were
// in fact sitting at max wrap.
static uint16_t pw_level_readback(uint8_t ch) {
  if (ch >= NUM_PW_CHANNELS || PW_PINS[ch] == PW_PIN_UNASSIGNED) return 0;
  const uint32_t cc = pwm_hw->slice[PW_PWM_SLICES[ch]].cc;
  return (pwm_gpio_to_channel(PW_PINS[ch]) == PWM_CHAN_A) ? (uint16_t)(cc & 0xFFFFu)
                                                          : (uint16_t)(cc >> 16);
}

// For debug logging and duty computation in gap measurement: the duty
// target/period assumed by the current PW search routine, and the most
// recently measured period from find_gap(). The PW in force is not tracked
// here — pw_level_readback() asks the hardware instead.
static double   g_gapLogCurrentPeriodUs = 0.0;
static double   g_gapLogTargetDutyFraction = 0.5;  // default 50%

// Helper: turn off all oscillators and set their RANGE outputs to a known
// state, while charging their timing capacitors using the original
// PIO+GPIO sequence. This preserves the analogue behaviour you rely on.
static void disable_all_oscillators_and_range_pwm() {
  for (int i = 0; i < NUM_OSCILLATORS; i++) {
    PIO     pioN      = pio[VOICE_TO_PIO[i]];
    uint8_t smN = VOICE_TO_SM[i];

    // Original "park" frequency used to pre-charge the caps.
    uint32_t clk_div1 = 200;

    // Run the DCO SM at a known slow rate while driving the RANGE PWM.
    pio_sm_set_enabled(pioN, smN, true);
    pio_sm_put(pioN, smN, clk_div1);
    pio_sm_exec(pioN, smN, pio_encode_pull(false, false));

    delay(200);

    // Stop the SM and hold the RANGE pin high as a plain GPIO output.
    pio_sm_set_enabled(pioN, smN, false);
#ifdef RANGE0_PIO_DITHER_TEST
    range_pio_set_level((uint8_t)i, DIV_COUNTER);  // full-on via PIO; do not steal RANGE pin
    continue;
#endif
    gpio_init(RANGE_PINS[i]);
    gpio_set_dir(RANGE_PINS[i], GPIO_OUT);
    gpio_put(RANGE_PINS[i], 1);
  }

  // After all RANGE caps are charged, park every PW PWM at max wrap so the
  // centre search can start from a known state. (Matches original behaviour.)
  //
  // Max wrap is a rail, not a usable operating point: there the comparator has
  // no crossing to make and the sense pin never toggles. Only the PW searches
  // may leave it here, since they program PW on every probe. Any stage that
  // measures without writing PW has to put the stored centre back first —
  // restart_DCO_calibration() does that for the amp-comp stage.
  reset_pw_to_DIV_COUNTER_PW();

  // Nothing is oscillating any more, so the next probe is a cold start.
  g_lastDrivenFreqHz = 0.0f;
}



// Helper: park every assigned PW PWM at max wrap (DIV_COUNTER_PW). Called from
// disable_all_oscillators_and_range_pwm(). Unassigned pins are skipped.
static void reset_pw_to_DIV_COUNTER_PW() {
  for (int i = 0; i < NUM_PW_CHANNELS; i++) {
    if (PW_PINS[i] == PW_PIN_UNASSIGNED) continue;
    pwm_set_chan_level(PW_PWM_SLICES[i], pwm_gpio_to_channel(PW_PINS[i]), DIV_COUNTER_PW);
  }
}

// After auto-cal / verify: the disable helper left every voice SM stopped
// (except the last probe, parked at an inaudible amp-0 frequency). Playback
// only writes dividers into those SMs, so there is no sound until something
// like Send all hits PARAM_SYNC_MODE and calls start_voice_sms(). Restore
// that here — already on core 1.
static void restore_voice_engine_after_calibration() {
  for (uint8_t ch = 0; ch < NUM_PW_CHANNELS; ++ch) {
    apply_pw_center(ch);
  }
  start_voice_sms();
  for (int i = 0; i < NUM_VOICES_TOTAL; i++) {
    note_on_flag[i] = 1;
  }
}

/*************************************************************************************/
/*************************************************************************************/
/*************************************************************************************/
// Main DCO auto-calibration entry point; the stage(s) it runs come from
// calibrationScope (value of PARAM_CALIBRATION_FLAG: 1 amp, 2 PW, 3 full).
// PW stage: once per assigned PW channel (center + low/high limits).
// Amp stage: per oscillator, calibrate_DCO()/calibrate_DCO_freq_trace() build a
// [freq -> range PWM] table persisted via update_FS_voice().
void DCO_calibration() {
  autotune_fill_init_manual_amp();

  // A fresh run always starts un-cancelled; PARAM_CALIBRATION_FLAG = 0 (core 0)
  // raises the request while this function blocks core 1.
  calibrationCancelRequested = false;

  // Stage selection comes from the value of PARAM_CALIBRATION_FLAG.
  const uint8_t scope = calibrationScope;
  const bool runPW  = calibration_scope_runs_pw(scope);
  const bool runAmp = calibration_scope_runs_amp(scope);
  const bool fine = (calibrationPrecision == CAL_PRECISION_FINE);
  Serial.println((String)"[DCO_CAL] scope: " + calibration_scope_name(scope) +
                 " precision: " + calibration_precision_name(calibrationPrecision) +
                 " (param 150 value: 1=amp-comp, 2=PW, 3=full;" +
                 " 5/6/7 = the same in fine mode, 9/10/11 in fast mode)");

  // Make the active amp-comp method visible up front: a stored 440 Hz anchor
  // is only used when FREQ_TRACE is selected (panel buttons / debug cmds 34-35,
  // boot default from AUTOTUNE_AMP_METHOD_DEFAULT). The fine pass measures the
  // stored table whatever built it, so the method does not apply there.
  if (runAmp && fine) {
    Serial.println("[DCO_CAL] amp-comp stage: refining the stored tables "
                   "(amp comp values kept, frequencies re-measured)");
  } else if (runAmp) {
    Serial.println((String)"[DCO_CAL] amp-comp method: " +
                   autotune_amp_method_name(autotuneAmpMethod) +
                   " (select via PARAM_DEBUG_COMMAND 34=CLASSIC / 35=FREQ_TRACE)");
  }
  if (runAmp) {
    Serial.println((String)"[DCO_CAL] freq search: " +
                   autotune_search_mode_name(autotuneSearchMode) +
                   " (37=BISECT / 38=INTERP / 39=GATED; compare probes= and"
                   " elapsed= on the report footer)");
    Serial.println((String)"[DCO_CAL] amp-0 endpoint: " +
                   autotune_amp0_mode_name(autotuneAmp0Mode) +
                   " (40=MEASURE live hunt / 41=CALC bottom-rung fit)");
  }

  // TURN OFF ALL OSCILLATORS and park PW channels.
  disable_all_oscillators_and_range_pwm();

  // PW is per channel (DCO3: one wired pin; DCO4: one per voice, two oscs
  // sharing it) and independent of the amp-comp stage. An amp-only run reuses
  // the PW centers already stored in the FS, applied per oscillator by
  // restart_DCO_calibration(). Each distinct assigned channel is calibrated
  // once, driving the first oscillator that maps to it.
  if (runPW) {
    uint8_t lastCh = 0xFF;
    for (uint8_t osc = 0; osc < NUM_OSCILLATORS && !calibrationCancelRequested; ++osc) {
      const uint8_t ch = cal_pw_channel(osc);
      if (ch == lastCh) continue;
      if (PW_PINS[ch] == PW_PIN_UNASSIGNED) continue;
      lastCh = ch;
      currentDCO = osc;
      restart_DCO_calibration();
      DCO_calibration_current_note = manual_DCO_calibration_start_note;
      VOICE_NOTES[0] = DCO_calibration_current_note;
      find_PW_center(0);
      if (!calibrationCancelRequested) find_PW_limit_v2(PW_LIMIT_LOW);
      if (!calibrationCancelRequested) find_PW_limit_v2(PW_LIMIT_HIGH);
    }
  }

  for (int i = 0; runAmp && i < NUM_OSCILLATORS && !calibrationCancelRequested; i++) {
    currentDCO = i;

    restart_DCO_calibration();

    ampCompCalibrationVal = initManualAmpCompCalibrationVal[currentDCO] + manualCalibrationOffset[currentDCO];
    write_range_pwm(currentDCO, ampCompCalibrationVal);

    DCO_calibration_current_note = DCO_calibration_start_note;
    VOICE_NOTES[0] = DCO_calibration_current_note;

    // Build a small context for this DCO and run the calibration routine.
    DCOCalibrationContext ctx(
      currentDCO,
      DCO_calibration_current_note,
      calibrationData,
      manualCalibrationOffset,
      initManualAmpCompCalibrationVal
    );
    // Amp-comp stage: method A (classic per-note PWM search) or method B
    // (fixed-PWM frequency tracing), selected via PARAM_DEBUG_COMMAND 34/35.
    bool tableOk = true;
    cal_report_reset();
    if (fine) {
      tableOk = refine_DCO_amp_table(ctx);
    } else if (autotuneAmpMethod == AMP_METHOD_FREQ_TRACE) {
      tableOk = calibrate_DCO_freq_trace(ctx);
    } else {
      // The classic path inherits the two header pairs written by
      // restart_DCO_calibration(): the amp-comp-0 placeholder (measured later
      // by apply_measured_lowest_freq) and the trimpot operating point.
      cal_report_set_pair(0, kCalDutyErrUnknown, CAL_SRC_FILLED);
      cal_report_set_pair(1, kCalDutyErrUnknown, CAL_SRC_MANUAL);
      // Desired duty-cycle error tolerance as a fraction (e.g. 0.005 = 0.5%).
      double dutyErrorFraction = 0.001;
      calibrate_DCO(ctx, dutyErrorFraction);
    }

    // The classic method leaves an extrapolated (or placeholder) amp-comp-0
    // anchor in entry [0..1]; measure it for real before the table is
    // printed/persisted. FREQ_TRACE measures its own bottom endpoint, and the
    // fine pass re-measures whatever pair 0 already holds. In CALC mode the
    // table keeps what find_lowest_freq() computed - which is already the
    // least-squares fit through the bottom of the table - with no live hunt.
    if (!fine && autotuneAmpMethod != AMP_METHOD_FREQ_TRACE &&
        !calibrationCancelRequested && tableOk) {
      // FAST runs always skip the hunt, like the FREQ_TRACE bottom endpoint.
      if (autotuneAmp0Mode == AMP0_MODE_CALC ||
          calibrationPrecision == CAL_PRECISION_FAST) {
        Serial.println((String)"[LOWEST_FREQ] DCO=" + currentDCO +
                       " amp-0 hunt skipped (" +
                       ((calibrationPrecision == CAL_PRECISION_FAST)
                          ? "FAST run" : "CALC") +
                       "); keeping the calculated " +
                       ((float)calibrationData[0] / 100.0f) + " Hz");
      } else {
        apply_measured_lowest_freq(ctx);
      }
    }

    for (int j = 0; j < chanLevelVoiceDataSize; j++) {
      Serial.println(calibrationData[j]);
    }

    if (!calibrationCancelRequested) {
      print_calibration_report(currentDCO, calibrationData);
    }

    if (calibrationCancelRequested) {
      // Interrupted mid-osc: discard this table; previously finished
      // oscillators keep the tables already persisted.
      Serial.println((String)"[DCO_CAL] DCO=" + currentDCO +
                     " interrupted; keeping previous calibration");
    } else if (tableOk) {
      update_FS_voice(currentDCO);
    } else {
      Serial.println((String)"[FREQ_TRACE_ERROR] DCO=" + currentDCO +
                     " table rejected; keeping previous calibration");
    }

    Serial.println((String) "DCO " + currentDCO + (String) " calibration finished.");
  }
  if (calibrationCancelRequested) {
    Serial.println("[DCO_CAL] cancelled by user");
    calibrationCancelRequested = false;
  } else {
    Serial.println((String)"[DCO_CAL] " + calibration_scope_name(scope) + " calibration done");
  }
  calibrationFlag = false;
  init_FS();

  // Rebuild amp-comp tables for the active engine.
  precompute_amp_comp_for_engine();
  restore_voice_engine_after_calibration();
}
/*************************************************************************************/
/*************************************************************************************/
/*************************************************************************************/

// --- Calibration report ----------------------------------------------------

// Clear the per-pair provenance/duty bookkeeping before an oscillator runs.
void cal_report_reset() {
  for (int p = 0; p < kCalReportPairs; ++p) {
    calPointDutyErrPct[p] = kCalDutyErrUnknown;
    calPointSource[p]     = CAL_SRC_NONE;
  }
  calReportLadderInterval = 0;
  calReportAnchorPair     = -1;
  calRunProbes            = 0;
  calRunStartMs           = millis();
}

// Record what a table pair is and how well it landed. dutyErrPct is signed
// (+ = duty above 50%); pass kCalDutyErrUnknown when there is no measurement.
void cal_report_set_pair(int pair, float dutyErrPct, uint8_t src) {
  if (pair < 0 || pair >= kCalReportPairs) {
    return;
  }
  calPointDutyErrPct[pair] = dutyErrPct;
  calPointSource[pair]     = src;
}

// Same, converting a measured gap in microseconds at freqHz into duty error.
void cal_report_set_pair_from_gap(int pair, float gapUs, float freqHz, uint8_t src) {
  cal_report_set_pair(pair, duty_err_pct_from_gap(gapUs, freqHz), src);
}

// Right-align a field so the [CAL_REPORT] table stays readable in a terminal.
static String cal_pad_left(const String& s, int width) {
  String out = s;
  while ((int)out.length() < width) {
    out = " " + out;
  }
  return out;
}

static const char *cal_point_source_name(uint8_t src) {
  switch (src) {
    case CAL_SRC_RUNG:          return "rung";
    case CAL_SRC_ANCHOR:        return "anchor";
    case CAL_SRC_ENDPOINT_FULL: return "endpoint-full";
    case CAL_SRC_ENDPOINT_AMP0: return "endpoint-amp0";
    case CAL_SRC_MANUAL:        return "manual";
    case CAL_SRC_FILLED:        return "filled";
    case CAL_SRC_SENTINEL:      return "sentinel";
    case CAL_SRC_REFINED:       return "refined";
    default:                    return "-";
  }
}

// Print the finished table for one oscillator: every pair with the duty error
// it was measured at, plus the endpoints, the span and the worst point.
// data is the [freq*100, amp comp] table that is about to be persisted.
void print_calibration_report(uint8_t dcoIndex, const uint32_t *data) {
  if (autotuneDebug < 1) {
    return;
  }

  String header = (String)"[CAL_REPORT] DCO=" + dcoIndex +
                  " method=" +
                  ((calibrationPrecision == CAL_PRECISION_FINE)
                     ? "REFINE"
                     : autotune_amp_method_name(autotuneAmpMethod)) +
                  " precision=" + calibration_precision_name(calibrationPrecision) +
                  " search=" + autotune_search_mode_name(autotuneSearchMode);
  if (calReportLadderInterval > 0) {
    header += (String)" ladder=" + calReportLadderInterval + " semitones";
  }
  if (calReportAnchorPair >= 0) {
    header += (String)" anchorPair=" + calReportAnchorPair;
  }
  Serial.println(header);
  Serial.println("[CAL_REPORT] pair    freqHz  ampComp  dutyErr%     gapUs    1cnt%  src");

  float    errSum      = 0.0f;
  int      errCount    = 0;
  float    worstErr    = -1.0f;
  int      worstPair   = -1;
  int      measured    = 0;
  int      highestPair = -1;

  for (int p = 0; p < kCalReportPairs; ++p) {
    const float    freqHz = (float)data[2 * p] / 100.0f;
    const uint32_t amp    = data[2 * p + 1];
    const uint8_t  src    = calPointSource[p];
    const bool     isSent = (src == CAL_SRC_SENTINEL);
    const float    err    = calPointDutyErrPct[p];
    const bool     hasErr = (fabsf(err) < 1e8f);

    if (!isSent) {
      highestPair = p;
    }
    if (src == CAL_SRC_RUNG || src == CAL_SRC_ANCHOR ||
        src == CAL_SRC_ENDPOINT_FULL || src == CAL_SRC_ENDPOINT_AMP0 ||
        src == CAL_SRC_REFINED) {
      ++measured;
    }

    String line = "[CAL_REPORT] " + cal_pad_left(String(p), 4);
    line += cal_pad_left(isSent ? String("-") : fmt_freq(freqHz), 10);
    line += cal_pad_left(String(amp), 9);

    if (hasErr) {
      const float absErr = fabsf(err);
      errSum += absErr;
      ++errCount;
      if (absErr > worstErr) {
        worstErr  = absErr;
        worstPair = p;
      }
      // gapUs is the inverse of duty_err_pct_from_gap() at this frequency.
      const float gapUs = (freqHz > 0.0f) ? (err * 20000.0f / freqHz) : 0.0f;
      line += cal_pad_left(String(err, 3), 10);
      line += cal_pad_left(String(gapUs, 2), 10);
    } else {
      line += cal_pad_left("-", 10);
      line += cal_pad_left("-", 10);
    }

    // Duty change caused by one count of amp comp, to first order
    // (duty - 0.5 scales with the relative amplitude error, so one count of
    // 'amp' is 50/amp percentage points). This is the floor for that point:
    // a dutyErr% already below it cannot be improved by more averaging.
    if (!isSent && amp > 0) {
      line += cal_pad_left(String(50.0f / (float)amp, 3), 9);
    } else {
      line += cal_pad_left("-", 9);
    }
    line += (String)"  " + cal_point_source_name(src);
    Serial.println(line);
  }

  const float lowestHz  = (float)data[0] / 100.0f;
  const float highestHz = (highestPair >= 0) ? ((float)data[2 * highestPair] / 100.0f) : 0.0f;
  String span = "-";
  if (lowestHz > 0.0f && highestHz > lowestHz) {
    span = String(log2f(highestHz / lowestHz), 2);
  }
  Serial.println((String)"[CAL_REPORT] DCO=" + dcoIndex +
                 " lowest=" + lowestHz + " Hz highest=" + highestHz +
                 " Hz span=" + span + " octaves measured=" + measured +
                 "/" + kCalReportPairs);

  if (errCount > 0 && worstPair >= 0) {
    Serial.println((String)"[CAL_REPORT] DCO=" + dcoIndex +
                   " dutyErr avg=" + String(errSum / (float)errCount, 3) +
                   "% worst=" + String(worstErr, 3) +
                   "% at pair " + worstPair +
                   " (" + String((float)data[2 * worstPair] / 100.0f, 2) + " Hz)");
  } else {
    Serial.println((String)"[CAL_REPORT] DCO=" + dcoIndex +
                   " dutyErr: no measured points");
  }

  // What this oscillator cost. Together with the dutyErr line above, this is the
  // whole A/B: same table quality in fewer probes and less time is a better
  // search mode (cmds 37-39), and a probe count far below the number of pairs
  // means something converged on its first reading rather than measuring.
  const unsigned long elapsedMs = millis() - calRunStartMs;
  Serial.println((String)"[CAL_REPORT] DCO=" + dcoIndex +
                 " search=" + autotune_search_mode_name(autotuneSearchMode) +
                 " probes=" + calRunProbes +
                 " elapsed=" + String(elapsedMs / 1000.0f, 1) + " s" +
                 " (" + String((float)elapsedMs / (float)max(calRunProbes, 1u), 1) +
                 " ms/probe)");
}

// --- Verification sweep ----------------------------------------------------

// Semitones between probes of the verification sweep.
static constexpr uint8_t kCalVerifyNoteStep = 3;

// Read-only pass over the finished tables, measuring what the engine would
// actually produce: for every oscillator walk the playable range and take the
// amp comp from the *runtime* lookup (interpolated), not from the table row,
// so interpolation error and table error are both visible.
// Reading the result: a constant error is a frame-of-reference offset between
// the sense pin and the real output (that is what the duty trim is for), error
// peaking between breakpoints is interpolation, error growing toward the low
// notes is the one-count floor printed as 1cnt=.
void run_calibration_verify_sweep() {
  Serial.println((String)"[CAL_VERIFY] start: step=" + kCalVerifyNoteStep +
                 " semitones, amp from the runtime lookup (" +
                 amp_comp_method_name(amp_comp_method) + ")");

  // One probe per note, so the fine profile's long averaging costs almost
  // nothing here and the numbers are worth trusting.
  const uint8_t precisionBefore = calibrationPrecision;
  calibrationPrecision = CAL_PRECISION_FINE;

  calibrationCancelRequested = false;
  calibrationFlag = true;  // keeps the main voice task off this core's oscillators

  disable_all_oscillators_and_range_pwm();

  for (uint8_t osc = 0; osc < NUM_OSCILLATORS && !calibrationCancelRequested; ++osc) {
    currentDCO = osc;
    restart_DCO_calibration();

    // Top of the useful range: the frequency where the table saturates at full
    // amp comp. Without a plateau, fall back to the amp-comp table's own limit.
    float topHz = (plateauStartFreqQ[osc] > 0)
                    ? ((float)plateauStartFreqQ[osc] / (float)(1 << FREQ_FRAC_BITS))
                    : (float)AMP_COMP_MAX_HZ;

    float errSum   = 0.0f;
    int   errCount = 0;
    float worstErr = -1.0f;
    float worstHz  = 0.0f;

    for (uint8_t note = manual_DCO_calibration_start_note;
         note < 120 && !calibrationCancelRequested;
         note += kCalVerifyNoteStep) {
      const float freqHz = note_to_freq(note);
      if (freqHz > topHz) {
        break;
      }
      const uint16_t amp = get_chan_level_for_engine(freqHz, osc);

      DCO_calibration_current_note = note;
      VOICE_NOTES[0] = note;
      const float gapUs = measure_duty_at_freq(freqHz, amp, true);

      String line = (String)"[CAL_VERIFY] DCO=" + osc + " note=" + note +
                    " freq=" + fmt_freq(freqHz) + " amp=" + amp;
      if (gapUs == kGapTimeoutSentinel) {
        Serial.println(line + " dutyErr=- gapUs=- (no signal)");
        continue;
      }
      const float errPct = duty_err_pct_from_gap(gapUs, freqHz);
      errSum += fabsf(errPct);
      ++errCount;
      if (fabsf(errPct) > worstErr) {
        worstErr = fabsf(errPct);
        worstHz  = freqHz;
      }
      line += " dutyErr=" + String(errPct, 3) + "% gapUs=" + String(gapUs, 2);
      if (amp > 0) {
        line += " 1cnt=" + String(50.0f / (float)amp, 3) + "%";
      }
      Serial.println(line);
    }

    if (errCount > 0) {
      Serial.println((String)"[CAL_VERIFY] DCO=" + osc +
                     " points=" + errCount +
                     " dutyErr avg=" + String(errSum / (float)errCount, 3) +
                     "% worst=" + String(worstErr, 3) +
                     "% at " + String(worstHz, 2) + " Hz");
    } else {
      Serial.println((String)"[CAL_VERIFY] DCO=" + osc + " no usable points");
    }
  }

  disable_all_oscillators_and_range_pwm();
  calibrationFlag = false;
  calibrationPrecision = precisionBefore;
  restore_voice_engine_after_calibration();
  if (calibrationCancelRequested) {
    Serial.println("[CAL_VERIFY] cancelled by user");
    calibrationCancelRequested = false;
  } else {
    Serial.println("[CAL_VERIFY] done");
  }
}

/*************************************************************************************/

// Reset per-DCO calibration state and header entries in calibrationData.
// This is called once for the PW pass and again before calibrating each DCO.
void restart_DCO_calibration() {
  autotune_fill_init_manual_amp();

  VOICE_NOTES[0] = DCO_calibration_start_note;
  DCO_calibration_current_note = DCO_calibration_start_note;

  // Table header:
  //  [0..1] "lowest frequency" anchor (freq placeholder 0, PWM ampCompLowestFreqVal)
  //  [2..3] manual starting point, one interval below the first calibrated note.
  calibrationData[0] = 0;
  calibrationData[1] = ampCompLowestFreqVal;
  calibrationData[2] = (uint32_t)(note_to_freq(DCO_calibration_current_note - calibration_note_interval) * 100);
  calibrationData[3] = initManualAmpCompCalibrationVal[currentDCO] + manualCalibrationOffset[currentDCO];

  // Reference for the 60 s safety timeouts used by the PW search phases.
  DCOCalibrationStart = millis();

  // TURN OFF ALL OSCILLATORS for a clean restart, and pre-charge the
  // RANGE capacitors using the legacy helper.
  disable_all_oscillators_and_range_pwm();

  // IMPORTANT: disable_all_oscillators_and_range_pwm() leaves RANGE_PINS[]
  // as plain GPIO outputs driven HIGH. Before starting calibration for the
  // currentDCO we must restore its RANGE pin back to PWM function so that
  // voice_task_autotune() and subsequent RANGE PWM writes actually appear
  // on the physical pin.
#ifndef RANGE0_PIO_DITHER_TEST
  gpio_set_function(RANGE_PINS[currentDCO], GPIO_FUNC_PWM);
#endif

  PIO pioN = pio[VOICE_TO_PIO[currentDCO]];
  uint8_t sm1N = VOICE_TO_SM[currentDCO];
  pio_sm_set_enabled(pioN, sm1N, true);

  // Undo the PW park above for this oscillator: the amp-comp stage never writes
  // PW itself (voice_task_autotune's auto branch only touches the divider and
  // RANGE), so whatever is left here is what it measures at — and the rail the
  // park leaves gives the comparator no crossing at all, hence a whole run of
  // [GAP_TIMEOUT] with edges=0. The PW searches overwrite this immediately.
  apply_pw_center_solo(cal_pw_channel(currentDCO));

  // This oscillator starts from nothing, so its first probe gets the full
  // settle budget instead of one sized against the previous one's frequency.
  g_lastDrivenFreqHz = 0.0f;

  delay(100);
}

/*************************************************************************************/
/*************************************************************************************/
/*************************************************************************************/
// PW search — shared low-level helpers
/*************************************************************************************/

// Helper: program a PW value on the given PW channel, keep PW[] and the debug
// tracker in sync, wait for the waveform to settle and measure the gap.
// This replaces the "set PWM, delay, measure" blocks that used to be
// copy-pasted throughout the PW search code.
//
// pwCh is a PW channel, `cal_pw_channel(osc)` — not an oscillator index. They
// coincide on DCO3 (one channel, index 0) and do not on DCO4 (two oscillators
// per channel), which is why the centre search's callers pass it explicitly
// instead of assuming 0: they used to sweep voice 0 whatever was calibrated.
static GapMeasurement set_pw_and_measure(uint8_t pwCh, uint16_t pw) {
  pwm_set_chan_level(PW_PWM_SLICES[pwCh],
                     pwm_gpio_to_channel(PW_PINS[pwCh]),
                     pw);
  PW[pwCh] = pw;
  delay(30);
  return measure_gap(2);
}

// PWSearchState / PWRecordMode are defined in autotune.h so the Arduino
// builder's auto-generated prototypes for these helpers can see the types.

static void pw_search_state_init(PWSearchState& st) {
  st.validCount       = 0;
  st.inToleranceCount = 0;
  st.haveBest         = false;
  st.bestGapAbs       = 1e12;
  st.bestPW           = 0;
  st.haveBracket      = false;
  st.pwLow            = 0;
  st.pwHigh           = 0;
  st.gapLow           = 0.0;
}

// Record one valid (non-timeout) measurement into the search state.
static void pw_record_sample(PWSearchState& st, uint16_t pw, double gapDiff,
                             double targetGap, PWRecordMode mode) {
  double absGapDiff = fabs(gapDiff);

  if (absGapDiff <= targetGap) {
    st.inToleranceCount++;
  }
  if (!st.haveBest || absGapDiff < st.bestGapAbs) {
    st.haveBest   = true;
    st.bestGapAbs = absGapDiff;
    st.bestPW     = pw;
  }

  if (mode == PW_RECORD_NO_TABLE) {
    return;
  }
  if (st.validCount < kPWMaxSamples) {
    st.validPW[st.validCount]      = pw;
    st.validGapDiff[st.validCount] = gapDiff;
    st.validCount++;
  } else if (mode == PW_RECORD_REPLACE_WORST) {
    int worstIdx = 0;
    double worstAbs = fabs(st.validGapDiff[0]);
    for (int vi = 1; vi < st.validCount; ++vi) {
      double curAbs = fabs(st.validGapDiff[vi]);
      if (curAbs > worstAbs) {
        worstAbs = curAbs;
        worstIdx = vi;
      }
    }
    if (absGapDiff < worstAbs) {
      st.validPW[worstIdx]      = pw;
      st.validGapDiff[worstIdx] = gapDiff;
    }
  }
}

// Phase 1: coarse scan over [pwMin, pwMax] looking for a sign-change bracket
// around the target duty. When a bracket is found, one extra sample at the
// linearly interpolated crossing point is measured and stored, then the scan
// stops.
static void pw_coarse_scan(PWSearchState& st,
                           double gapTarget, double targetGap,
                           uint16_t pwMin, uint16_t pwMax, uint16_t coarseStep,
                           double periodUs, double toleranceDutyPercent) {
  bool     havePrev    = false;
  double   prevGapDiff = 0.0;
  uint16_t prevPW      = 0;

  for (uint16_t pw = pwMin; pw <= pwMax; pw = (uint16_t)(pw + coarseStep)) {

    if (calibrationCancelRequested) break;
    if (millis() - DCOCalibrationStart > 60000) {
      Serial.println("PW coarse scan timeout (60s)");
      break;
    }

    GapMeasurement gm = set_pw_and_measure(cal_pw_channel(currentDCO), pw);
    if (gm.timedOut) {
      continue;  // no usable signal at this PW
    }

    double gap     = (double)gm.value;
    double gapDiff = gap - gapTarget;

    if (autotuneDebug >= 2 && periodUs > 0.0) {
      double dutyPercent = (0.5 + gap / (2.0 * periodUs)) * 100.0;
      Serial.println((String)"[PW_CENTER_COARSE] note=" + DCO_calibration_current_note +
                     (String)" DCO=" + currentDCO +
                     (String)" PW_raw=" + pw +
                     (String)" gap=" + gap +
                     (String)"us duty=" + dutyPercent +
                     (String)"% target=50% tol≈" + toleranceDutyPercent + "%");
    }

    pw_record_sample(st, pw, gapDiff, targetGap, PW_RECORD_REPLACE_WORST);

    if (havePrev &&
        ((gapDiff > 0.0 && prevGapDiff < 0.0) || (gapDiff < 0.0 && prevGapDiff > 0.0))) {
      st.haveBracket = true;
      st.pwLow  = prevPW;
      st.gapLow = prevGapDiff + gapTarget;  // raw gap at pwLow
      st.pwHigh = pw;

      // With two samples straddling the target, probe the crossing point
      // estimated by linear interpolation between them.
      double denom = fabs(prevGapDiff) + fabs(gapDiff);
      if (denom > 0.0) {
        double t = fabs(prevGapDiff) / denom;  // weight towards the closer side
        uint16_t pwEst = (uint16_t)((double)prevPW + ((double)(pw - prevPW) * t));
        if (pwEst >= pwMin && pwEst <= pwMax) {
          GapMeasurement gmEst = set_pw_and_measure(cal_pw_channel(currentDCO), pwEst);
          if (!gmEst.timedOut) {
            pw_record_sample(st, pwEst, (double)gmEst.value - gapTarget,
                             targetGap, PW_RECORD_APPEND);
          }
        }
      }
      break;
    }

    havePrev    = true;
    prevGapDiff = gapDiff;
    prevPW      = pw;
  }
}

// Phase 2a (bracket found): bisection search within the sign-change bracket.
// Midpoint samples refine the best candidate but are not added to the valid
// table (same as the original implementation).
static void pw_bisect_bracket(PWSearchState& st,
                              double gapTarget, double targetGap,
                              double periodUs, double toleranceDutyPercent) {
  uint16_t pwLow  = st.pwLow;
  uint16_t pwHigh = st.pwHigh;
  double   gapLow = st.gapLow;

  for (int iter = 0; iter < 14; ++iter) {
    if (calibrationCancelRequested) break;
    if (millis() - DCOCalibrationStart > 60000) {
      Serial.println("PW bisection timeout (60s)");
      break;
    }

    uint16_t pwMid = (uint16_t)((pwLow + pwHigh) / 2);
    GapMeasurement gm = set_pw_and_measure(cal_pw_channel(currentDCO), pwMid);
    if (gm.timedOut) {
      // No valid data at this midpoint; try again on the next iteration.
      if (autotuneDebug >= 2) {
        Serial.println("PW center: timeout during bisection, skipping midpoint.");
      }
      continue;
    }

    double gapMid     = (double)gm.value;
    double gapDiffMid = gapMid - gapTarget;

    pw_record_sample(st, pwMid, gapDiffMid, targetGap, PW_RECORD_NO_TABLE);

    if (autotuneDebug >= 2 && periodUs > 0.0) {
      double dutyPercent = (0.5 + gapMid / (2.0 * periodUs)) * 100.0;
      Serial.println((String)"[PW_CENTER_BISECT] note=" + DCO_calibration_current_note +
                     (String)" DCO=" + currentDCO +
                     (String)" PW_raw=" + pwMid +
                     (String)" gap=" + gapMid +
                     (String)"us duty=" + dutyPercent +
                     (String)"% target=50% tol≈" + toleranceDutyPercent + "%");
    }

    // Maintain the sign-change bracket.
    if ((gapDiffMid > 0.0 && (gapLow - gapTarget) > 0.0) ||
        (gapDiffMid < 0.0 && (gapLow - gapTarget) < 0.0)) {
      pwLow  = pwMid;
      gapLow = gapMid;
    } else {
      pwHigh = pwMid;
    }

    if (pwHigh - pwLow <= 1) {
      break;  // can't refine further in integer PW space
    }
  }
}

// Phase 2b (no bracket): local fine scan around the best coarse candidate so
// that we still gather several near-target samples before deciding.
static void pw_fine_scan_around_best(PWSearchState& st,
                                     double gapTarget, double targetGap,
                                     uint16_t pwMin, uint16_t pwMax,
                                     uint16_t coarseStep) {
  if (autotuneDebug >= 1) {
    Serial.println("PW center: no sign-change bracket found, running local fine scan.");
  }

  uint16_t startPW = (st.haveBest && st.bestPW >= pwMin && st.bestPW <= pwMax)
                       ? st.bestPW
                       : (uint16_t)((pwMin + pwMax) / 2);
  uint16_t span = (coarseStep > 0) ? coarseStep * 2 : 4;
  uint16_t fineMin = (startPW > span) ? (startPW - span) : pwMin;
  uint16_t fineMax = (startPW + span < pwMax) ? (startPW + span) : pwMax;
  if (fineMax < fineMin) {
    uint16_t tmp = fineMin;
    fineMin = fineMax;
    fineMax = tmp;
  }
  uint16_t fineStep = (fineMax > fineMin) ? ((fineMax - fineMin) / 16) : 1;
  if (fineStep == 0) fineStep = 1;

  for (uint16_t pw = fineMin; pw <= fineMax; pw = (uint16_t)(pw + fineStep)) {
    if (calibrationCancelRequested) break;
    if (millis() - DCOCalibrationStart > 60000) {
      Serial.println("PW local fine scan timeout (60s)");
      break;
    }

    GapMeasurement gm = set_pw_and_measure(cal_pw_channel(currentDCO), pw);
    if (gm.timedOut) {
      continue;
    }
    pw_record_sample(st, pw, (double)gm.value - gapTarget, targetGap, PW_RECORD_APPEND);
  }
}

// Lock-in: demand 3 consecutive measurements within targetGap of gapTarget at
// the given PW (up to 8 tries). On success, writes the last locked gap to
// lockedGapOut and returns true.
static bool pw_lock_in(uint8_t voiceIdx, uint16_t pw,
                       double gapTarget, double targetGap,
                       double periodUs, double& lockedGapOut) {
  const int kMaxLockInTries = 8;
  int consecutiveOk = 0;

  for (int li = 0; li < kMaxLockInTries; ++li) {
    if (calibrationCancelRequested) return false;
    GapMeasurement gm = set_pw_and_measure(voiceIdx, pw);
    if (gm.timedOut || periodUs <= 0.0) {
      consecutiveOk = 0;
      continue;
    }

    double gap = (double)gm.value;
    if (fabs(gap - gapTarget) <= targetGap) {
      consecutiveOk++;
      if (consecutiveOk >= 3) {
        lockedGapOut = gap;
        return true;
      }
    } else {
      consecutiveOk = 0;
    }
  }
  return false;
}

// Phase 3: pick the best candidate from the valid-samples table (smallest gap
// to target first), demanding a lock-in at each candidate. A locked candidate
// is then refined locally (PW-2..PW+2, each with its own mini lock-in).
// Returns true and writes the final PW to chosenPWOut on success; false if
// every candidate failed lock-in or the best gap was hopelessly large.
static bool pw_select_and_lock(PWSearchState& st,
                               double gapTarget, double targetGap,
                               uint16_t pwMin, uint16_t pwMax,
                               double periodUs, uint16_t& chosenPWOut) {
  // Try candidates from best gap to worse. After each failed lock-in the
  // candidate's gap difference is inflated so it won't be chosen again.
  for (int attempt = 0; attempt < st.validCount; ++attempt) {
    if (calibrationCancelRequested) return false;
    int    bestIdx = -1;
    double bestAbs = 1e12;
    int    inTolForThisPass = 0;

    for (int vi = 0; vi < st.validCount; ++vi) {
      double curAbs = fabs(st.validGapDiff[vi]);
      if (curAbs <= targetGap) {
        inTolForThisPass++;
      }
      if (curAbs < bestAbs) {
        bestAbs = curAbs;
        bestIdx = vi;
      }
    }

    if (bestIdx < 0) {
      break;
    }

    // If the best gap is still extremely large compared to the allowed gap
    // (e.g. > 10x), abort early and keep the previous PW center.
    if (bestAbs > targetGap * 10.0) {
      if (autotuneDebug >= 1) {
        Serial.println((String)"[PW_CENTER_ABORT] note=" + DCO_calibration_current_note +
                       (String)" DCO=" + currentDCO +
                       (String)" bestGap=" + bestAbs +
                       (String)"us (> " + targetGap * 10.0 +
                       (String)"us); keeping PW_center=" + PW_CENTER[cal_pw_channel(currentDCO)]);
      }
      return false;
    }

    uint16_t chosenPW  = st.validPW[bestIdx];
    double   chosenGap = gapTarget + st.validGapDiff[bestIdx];

    double lockedGap = 0.0;
    if (pw_lock_in(cal_pw_channel(currentDCO), chosenPW, gapTarget, targetGap,
                   periodUs, lockedGap)) {
      chosenGap = lockedGap;

      // Local refinement: probe a small neighbourhood around the locked-in PW
      // (PW-2..PW+2). Each candidate must pass its own mini lock-in before it
      // can replace the current choice.
      uint16_t bestLocalPW     = chosenPW;
      double   bestLocalGapAbs = bestAbs;

      for (int16_t off = -2; off <= 2; ++off) {
        int32_t testPW32 = (int32_t)chosenPW + off;
        if (testPW32 < (int32_t)pwMin || testPW32 > (int32_t)pwMax) continue;
        uint16_t testPW = (uint16_t)testPW32;

        double gapLocal = 0.0;
        if (pw_lock_in(cal_pw_channel(currentDCO), testPW, gapTarget, targetGap,
                       periodUs, gapLocal)) {
          double absGapDiffLocal = fabs(gapLocal - gapTarget);
          if (absGapDiffLocal < bestLocalGapAbs) {
            bestLocalGapAbs = absGapDiffLocal;
            bestLocalPW     = testPW;
            chosenGap       = gapLocal;
          }
        }
      }

      chosenPW = bestLocalPW;
      double chosenDutyPercent = 0.0;
      if (periodUs > 0.0) {
        chosenDutyPercent = (0.5 + chosenGap / (2.0 * periodUs)) * 100.0;
      }

      if (autotuneDebug >= 1) {
        Serial.println((String)"[PW_CENTER_RESULT] note=" + DCO_calibration_current_note +
                       (String)" DCO=" + currentDCO +
                       (String)" PW_center=" + chosenPW +
                       (String)" duty≈" + chosenDutyPercent +
                       (String)"% bestGap=" + bestLocalGapAbs +
                       (String)"us inTolSamples=" + inTolForThisPass +
                       (String)" totalValid=" + st.validCount);
      }
      chosenPWOut = chosenPW;
      return true;
    }

    // This candidate failed lock-in; inflate its gap diff so we try the next
    // best one on the following attempt.
    st.validGapDiff[bestIdx] = targetGap * 20.0;
    if (autotuneDebug >= 1) {
      Serial.println((String)"[PW_CENTER_LOCKIN_REJECT] note=" + DCO_calibration_current_note +
                     (String)" DCO=" + currentDCO +
                     (String)" PW=" + chosenPW +
                     (String)" could not get 3 consecutive in-band readings; trying next candidate.");
    }
  }

  if (autotuneDebug >= 1) {
    Serial.println((String)"[PW_CENTER_ABORT] note=" + DCO_calibration_current_note +
                   (String)" DCO=" + currentDCO +
                   (String)" all candidates failed lock-in; keeping PW_center=" +
                   PW_CENTER[cal_pw_channel(currentDCO)]);
  }
  return false;
}

// Shared search routine used by PW calibration (currently the center search).
// It looks for the PW value whose duty cycle is closest to targetDutyFraction
// at the current calibration note. targetGap is the allowed absolute gap (in
// microseconds) from the ideal duty at that note. On failure the caller's
// fallbackPW is returned unchanged.
//
// Phases: coarse scan → bisection (bracket) or local fine scan (no bracket)
// → candidate selection with lock-in and local refinement.
static uint16_t find_PW_for_target_duty(double targetDutyFraction,
                                        uint16_t targetGap,
                                        uint16_t pwMin,
                                        uint16_t pwMax,
                                        uint16_t fallbackPW) {

  double freqHz   = (double)note_to_freq(DCO_calibration_current_note);
  double periodUs = (freqHz > 0.0) ? (1000000.0 / freqHz) : 0.0;

  // Update global logging context for gap measurements during this search.
  g_gapLogCurrentPeriodUs    = periodUs;
  g_gapLogTargetDutyFraction = targetDutyFraction;

  double toleranceDutyPercent = 0.0;
  double gapTarget = 0.0;
  if (periodUs > 0.0) {
    // Ideal gap for a target HIGH-duty p: gap = avgHigh - avgLow = T*(2p - 1).
    // (Zero for the 50% center target, positive above, negative below.)
    gapTarget = periodUs * (2.0 * targetDutyFraction - 1.0);
    toleranceDutyPercent = ((double)targetGap / (2.0 * periodUs)) * 100.0;
  }

  // Coarse step: use smaller steps for low/high limit searches (target duty
  // far from 50%) and larger steps for the center search.
  uint16_t coarseDiv  = (fabs(targetDutyFraction - 0.5) < 0.05) ? 16 : 32;
  uint16_t coarseStep = (pwMax > pwMin) ? ((pwMax - pwMin) / coarseDiv) : 1;
  if (coarseStep == 0) coarseStep = 1;

  PWSearchState st;
  pw_search_state_init(st);

  pw_coarse_scan(st, gapTarget, (double)targetGap, pwMin, pwMax, coarseStep,
                 periodUs, toleranceDutyPercent);

  if (st.haveBracket) {
    pw_bisect_bracket(st, gapTarget, (double)targetGap, periodUs, toleranceDutyPercent);
  } else {
    pw_fine_scan_around_best(st, gapTarget, (double)targetGap, pwMin, pwMax, coarseStep);
  }

  if (calibrationCancelRequested) {
    return fallbackPW;
  }

  if (st.validCount == 0) {
    // No valid samples at all in the searched range: keep the caller's PW
    // and log the situation so the user can investigate.
    if (autotuneDebug >= 1) {
      Serial.println("PW search: no valid samples found; keeping current PW.");
    }
    return fallbackPW;
  }

  uint16_t chosenPW = fallbackPW;
  if (pw_select_and_lock(st, gapTarget, (double)targetGap, pwMin, pwMax,
                         periodUs, chosenPW)) {
    return chosenPW;
  }
  return fallbackPW;
}

// Locate PW center for the current DCO's voice by minimizing duty-cycle error
// at a reference note. Mode 0 = low note, mode 1 = higher note refinement.
void find_PW_center(uint8_t mode) {

  DCO_calibration_current_note = manual_DCO_calibration_start_note;
  VOICE_NOTES[0] = DCO_calibration_current_note;
  ampCompCalibrationVal = initManualAmpCompCalibrationVal[currentDCO] + manualCalibrationOffset[currentDCO];

  uint16_t targetGap;
  uint8_t voiceTaskMode;

  if (mode == 0) {
    targetGap = compute_gap_tolerance_for_freq(note_to_freq(DCO_calibration_current_note), 0.005);
    voiceTaskMode = 2;
  } else {
    DCO_calibration_current_note = 76;
    VOICE_NOTES[0] = DCO_calibration_current_note;
    targetGap = 5;
    voiceTaskMode = 3;
  }

  DCOCalibrationStart = millis();

  const uint8_t pwCh = cal_pw_channel(currentDCO);
  if (PW_PINS[pwCh] == PW_PIN_UNASSIGNED) {
    Serial.println((String)"[DCO_CAL] PW channel " + pwCh +
                   " unassigned; skipping center search for DCO=" + currentDCO);
    return;
  }

  // Starting PW: middle of the range on the very first tune, otherwise the
  // previously stored center.
  if (firstTuneFlag == true) {
    PW[pwCh] = DIV_COUNTER_PW / 2;
    PW_CENTER[pwCh] = DIV_COUNTER_PW / 2;
  } else {
    PW[pwCh] = PW_CENTER[pwCh];
  }
  uint16_t startPW = PW[pwCh];

  // Apply the starting PW to the PW PWM channel before configuring the DCO.
  pwm_set_chan_level(PW_PWM_SLICES[pwCh], pwm_gpio_to_channel(PW_PINS[pwCh]), startPW);

  voice_task_autotune(voiceTaskMode, ampCompCalibrationVal);

  uint16_t centerPW = find_PW_for_target_duty(
    kPWCenterDutyFraction,
    targetGap,
    0,
    DIV_COUNTER_PW,
    startPW
  );
  if (calibrationCancelRequested) {
    Serial.println("[DCO_CAL] PW center search interrupted; keeping previous center");
    return;
  }
  Serial.println("PW center found !!!");
  update_FS_PWCenter(pwCh, centerPW);
  PW_CENTER[pwCh] = centerPW;

  // Apply the newly found PW center immediately to the hardware so that the
  // effect is visible on the pulse waveform as soon as calibration finishes.
  pwm_set_chan_level(PW_PWM_SLICES[pwCh],
                     pwm_gpio_to_channel(PW_PINS[pwCh]),
                     centerPW);
  PW[pwCh] = centerPW;
}


// -----------------------------------------------------------------------------
// PW limit search
// -----------------------------------------------------------------------------

PWLimitSearchResult search_PW_limit_from_center(
  uint8_t     voiceIdx,
  uint16_t    centerPW,
  PWLimitDir  dir,
  double      periodUs,
  double      targetDuty
) {
  PWLimitSearchResult result;
  result.ok                  = false;
  result.limitPW             = centerPW;
  result.finalDutyPercent    = -1.0;

  if (periodUs <= 0.0) {
    return result;
  }

  // Hard bounds convention:
  //  - LOW  side scans from center down to 0
  //  - HIGH side scans from center up to DIV_COUNTER_PW
  uint16_t minPW = (dir == PW_LIMIT_LOW)  ? 0           : centerPW;
  uint16_t maxPW = (dir == PW_LIMIT_LOW)  ? centerPW    : DIV_COUNTER_PW;

  // Coarse step size for scanning from center toward the limit.
  uint16_t step = DIV_COUNTER_PW / 64;
  if (step == 0) step = 1;

  bool     haveBest   = false;
  uint16_t bestPW     = centerPW;
  double   bestDelta  = 1e12;
  double   bestDuty   = -1.0;   // duty (0..1) at bestPW when known

  unsigned long searchStartMs = millis();

  // Coarse scan: walk from center toward the requested side, tracking the
  // PW that gets closest to the target duty. We stop when we reach the
  // boundary, run out of time, or find a value within tolerance.
  for (uint16_t pw = centerPW; ; ) {
    if (calibrationCancelRequested) break;
    if (millis() - searchStartMs > 60000UL) {
      // Safety timeout.
      break;
    }

    if (pw < minPW) pw = minPW;
    if (pw > maxPW) pw = maxPW;

    GapMeasurement gm = set_pw_and_measure(voiceIdx, pw);
    if (!gm.timedOut) {
      double gap           = (double)gm.value;
      double dutyErrorFrac = gap / (2.0 * periodUs);
      double duty          = 0.5 + dutyErrorFrac;

      double delta = fabs(duty - targetDuty);
      if (!haveBest || delta < bestDelta) {
        haveBest  = true;
        bestDelta = delta;
        bestPW    = pw;
        bestDuty  = duty;
      }

      if (autotuneDebug >= 2) {
        const char *scanTag =
          (dir == PW_LIMIT_LOW) ? "[PW_LOW_SCAN_V2]" : "[PW_HIGH_SCAN_V2]";
        Serial.println((String)scanTag +
                       (String)" note=" + DCO_calibration_current_note +
                       (String)" DCO=" + currentDCO +
                       (String)" PW_raw=" + pw +
                       (String)" duty=" + (duty * 100.0) + "%" +
                       (String)" targetDuty=" + (targetDuty * 100.0) + "%");
      }

      // If we are already within tolerance, we can stop the coarse scan early.
      if (delta <= kPWLimitDutyTolerance) {
        break;
      }
    }

    // Step toward the boundary.
    if (dir == PW_LIMIT_LOW) {
      if (pw <= minPW + step) {
        break;
      }
      pw = (uint16_t)(pw - step);
    } else {  // PW_LIMIT_HIGH
      if (pw >= maxPW - step) {
        break;
      }
      pw = (uint16_t)(pw + step);
    }
  }

  if (!haveBest) {
    // Never saw a valid measurement; caller should keep previous limit.
    return result;
  }

  // Fine refinement around bestPW: search with step = 1 in a relatively
  // tight window around the best coarse candidate. This keeps the search
  // local so we do not wander too far from the best-known PW.
  uint16_t refineRadius = step / 2;
  if (refineRadius < 4)  refineRadius = 4;
  if (refineRadius > 32) refineRadius = 32;

  uint16_t startPW;
  if (bestPW > refineRadius) {
    startPW = bestPW - refineRadius;
  } else {
    startPW = minPW;
  }
  // Enforce the same [minPW, maxPW] bounds used in the coarse scan so that
  // the refinement phase never crosses to the other side of center.
  if (startPW < minPW) startPW = minPW;

  uint16_t endPW = bestPW + refineRadius;
  if (endPW > maxPW) {
    endPW = maxPW;
  }

  int consecutiveTimeouts = 0;
  for (uint16_t pw = startPW; pw <= endPW; ++pw) {
    if (calibrationCancelRequested) break;
    GapMeasurement gm = set_pw_and_measure(voiceIdx, pw);
    if (gm.timedOut) {
      // If we are stepping deeper into the "edge" side and accumulate several
      // consecutive timeouts, stop refining in that direction to avoid
      // spending a long time in a region with no measurable signal.
      ++consecutiveTimeouts;
      bool goingDeeperLow  = (dir == PW_LIMIT_LOW)  && (pw < bestPW);
      bool goingDeeperHigh = (dir == PW_LIMIT_HIGH) && (pw > bestPW);
      if ((goingDeeperLow || goingDeeperHigh) && consecutiveTimeouts >= 4) {
        break;
      }
      continue;
    }
    consecutiveTimeouts = 0;

    double gap           = (double)gm.value;
    double dutyErrorFrac = gap / (2.0 * periodUs);
    double duty          = 0.5 + dutyErrorFrac;

    double delta = fabs(duty - targetDuty);
    if (delta < bestDelta) {
      bestDelta = delta;
      bestPW    = pw;
      bestDuty  = duty;
    }
  }

  // Final result: start from the best sample seen during coarse+fine.
  result.ok      = true;
  result.limitPW = bestPW;

  if (bestDuty >= 0.0) {
    result.finalDutyPercent = bestDuty * 100.0;
  }

  // Check whether the target duty is actually reachable within tolerance.
  double currentDutyFrac = result.finalDutyPercent / 100.0;
  if (result.finalDutyPercent <= 0.0 ||
      fabs(currentDutyFrac - targetDuty) > kPWLimitDutyTolerance) {
    // Not within tolerance: push all the way to the hardware boundary for
    // this side and treat that as the "best possible" limit. This matches
    // the specification that the target is considered unreachable only after
    // trying the maximum/minimum PW value.
    uint16_t boundaryPW = (dir == PW_LIMIT_LOW) ? minPW : maxPW;

    GapMeasurement gmEdge = set_pw_and_measure(voiceIdx, boundaryPW);
    if (!gmEdge.timedOut) {
      double gap           = (double)gmEdge.value;
      double dutyErrorFrac = gap / (2.0 * periodUs);
      double duty          = 0.5 + dutyErrorFrac;
      result.limitPW        = boundaryPW;
      result.finalDutyPercent = duty * 100.0;
    } else {
      // If even the boundary cannot be measured reliably, we still honour the
      // boundary PW as the limit but leave finalDutyPercent as-is.
      result.limitPW = boundaryPW;
    }
  }

  return result;
}

void find_PW_limit_v2(PWLimitDir dir) {
  uint8_t voiceTaskMode = 2;

  // Configure the calibration context the same way as the PW center search
  // so that both phases operate on the same note and amplitude.
  DCO_calibration_current_note = manual_DCO_calibration_start_note;
  VOICE_NOTES[0] = DCO_calibration_current_note;
  ampCompCalibrationVal =
    initManualAmpCompCalibrationVal[currentDCO] + manualCalibrationOffset[currentDCO];

  DCOCalibrationStart = millis();

  double freqHz   = (double)note_to_freq(DCO_calibration_current_note);
  double periodUs = (freqHz > 0.0) ? (1000000.0 / freqHz) : 0.0;

  uint8_t  voiceIdx = cal_pw_channel(currentDCO);
  uint16_t centerPW = PW_CENTER[voiceIdx];

  // Direction-dependent target HIGH-duty:
  //  - Low limit:  kPWLowDutyFraction  (≈ 2% HIGH)
  //  - High limit: kPWHighDutyFraction (≈98% HIGH)
  double targetDuty = (dir == PW_LIMIT_LOW)
                      ? kPWLowDutyFraction
                      : kPWHighDutyFraction;

  // Update global logging context for gap measurements during PW-limit search.
  g_gapLogCurrentPeriodUs    = periodUs;
  g_gapLogTargetDutyFraction = targetDuty;

  // Configure the DCO for PW calibration mode.
  voice_task_autotune(voiceTaskMode, ampCompCalibrationVal);
  delay(100);

  PWLimitSearchResult res =
    search_PW_limit_from_center(voiceIdx, centerPW, dir, periodUs, targetDuty);

  if (calibrationCancelRequested) {
    Serial.println((String)"[DCO_CAL] PW " +
                   ((dir == PW_LIMIT_LOW) ? "low" : "high") +
                   " limit search interrupted; keeping previous limit");
    return;
  }

  if (!res.ok) {
    if (autotuneDebug >= 1) {
      const char *abortTag =
        (dir == PW_LIMIT_LOW) ? "[PW_LOW_ABORT_NO_SIGNAL_V2]" : "[PW_HIGH_ABORT_NO_SIGNAL_V2]";
      uint16_t keepPW =
        (dir == PW_LIMIT_LOW) ? PW_LOW_LIMIT[voiceIdx] : PW_HIGH_LIMIT[voiceIdx];
      Serial.println((String)abortTag +
                     (String)" note=" + DCO_calibration_current_note +
                     (String)" DCO=" + currentDCO +
                     (String)" keeping_PW=" + keepPW);
    }
    return;
  }

  // Log result and commit it.
  double targetDutyPercent =
    (dir == PW_LIMIT_LOW)
      ? (kPWLowDutyFraction * 100.0)
      : ((1.0 - kPWHighDutyFraction) * 100.0);
  double targetHighDutyPercent = kPWHighDutyFraction * 100.0;

  if (autotuneDebug >= 1) {
    const char *resultTag =
      (dir == PW_LIMIT_LOW) ? "[PW_LOW_RESULT_V2]" : "[PW_HIGH_RESULT_V2]";
    Serial.println((String)resultTag +
                   (String)" note=" + DCO_calibration_current_note +
                   (String)" DCO=" + currentDCO +
                   (String)" PW_LIMIT=" + res.limitPW +
                   (String)" duty≈" + res.finalDutyPercent + "%" +
                   (String)" targetDuty=" + targetDutyPercent + "%" +
                   (dir == PW_LIMIT_LOW
                      ? (String)""
                      : (String)" targetHighDuty=" + targetHighDutyPercent + "%"));
  }

  if (dir == PW_LIMIT_LOW) {
    Serial.println("--------------------------------");
    Serial.println("PW low limit (v2) found !!!");
    Serial.println(
      (String)" PW_LIMIT=" + res.limitPW +
      (String)" duty≈" + res.finalDutyPercent + "%" +
      (String)" targetDuty=" + (kPWLowDutyFraction * 100.0) + "%");
    Serial.println("--------------------------------");
    update_FS_PW_Low_Limit(voiceIdx, res.limitPW);
    PW_LOW_LIMIT[voiceIdx] = res.limitPW;
  } else {
    Serial.println("--------------------------------");
    Serial.println("PW high limit (v2) found !!!");
    Serial.println(
      (String)" PW_LIMIT=" + res.limitPW +
      (String)" duty≈" + res.finalDutyPercent + "%" +
      (String)" targetDuty=" + ((1.0 - kPWHighDutyFraction) * 100.0) + "%" +
      (String)" targetHighDuty=" + (kPWHighDutyFraction * 100.0) + "%");
    Serial.println("--------------------------------");
    update_FS_PW_High_Limit(voiceIdx, res.limitPW);
    PW_HIGH_LIMIT[voiceIdx] = res.limitPW;
  }
}

//////////////////////////////////////////////////////////////////////////////
// Raw cal-sense probe (no period gate): sample digital level / edge rate so a
// TIMEOUT can be split into "pin stuck" vs "edges exist but find_gap rejects".
// Throttled to ~2 Hz. Called from DCO_calibration_debug on gap timeout.
static void cal_sense_probe_log() {
  static uint32_t lastPrintMs = 0;
  const uint32_t nowMs = millis();
  if ((nowMs - lastPrintMs) < 500u) {
    return;
  }
  lastPrintMs = nowMs;

  constexpr uint32_t kWindowUs = 40000u;  // 40 ms
  const uint32_t t0 = micros();
  bool lastRaw = digitalRead(DCO_calibration_pin);
  uint32_t edges = 0;
  uint32_t minDt = 0xFFFFFFFFu;
  uint32_t maxDt = 0;
  uint32_t lastEdgeUs = t0;
  bool haveEdge = false;

  while ((micros() - t0) < kWindowUs) {
    const bool raw = digitalRead(DCO_calibration_pin);
    if (raw != lastRaw) {
      const uint32_t nowUs = micros();
      const uint32_t dt = nowUs - lastEdgeUs;
      if (haveEdge) {
        if (dt < minDt) {
          minDt = dt;
        }
        if (dt > maxDt) {
          maxDt = dt;
        }
      }
      lastEdgeUs = nowUs;
      haveEdge = true;
      edges++;
      lastRaw = raw;
    }
  }

  const bool rawNow = digitalRead(DCO_calibration_pin);
  double expectHz = 0.0;
  if (DCO_calibration_current_note >= 12) {
    expectHz = (double)note_to_freq(DCO_calibration_current_note);
  }

  Serial.print((String)"[CAL_SENSE] pin=" + DCO_calibration_pin +
               (String)" raw=" + (int)rawNow +
               (String)" edges=" + edges);
  if (edges >= 2 && minDt != 0xFFFFFFFFu) {
    Serial.print((String)" minDt=" + minDt + (String)" maxDt=" + maxDt);
  } else {
    Serial.print(" minDt=- maxDt=-");
  }
  Serial.println((String)" pullup=1 invert=" + (int)kGapPolarityInverted +
                 (String)" note=" + DCO_calibration_current_note +
                 (String)" expectHz≈" + expectHz);
}

//////////////////////////////////////////////////////////////////////////////
// Measure duty-cycle error on DCO_calibration_pin by timing rising/falling
// edges. Returns avgHighUs - avgLowUs (0 when duty is ≈50%), or
// kGapTimeoutSentinel on timeout. All measurement state is local; callers
// normally use the measure_gap() wrapper from autotune_measurement.h.
float find_gap(byte specialMode) {
  // Estimate the ideal period so we can reject obviously invalid edge
  // intervals (e.g. very short glitches) that do not match the DCO's actual
  // frequency. When gapGateFreqHz is set (arbitrary-frequency probes), gate
  // against the probe frequency instead of the current calibration note.
  double freqHz = (gapGateFreqHz > 0.0f)
                    ? (double)gapGateFreqHz
                    : (double)note_to_freq(DCO_calibration_current_note);
  double idealPeriodUs = (freqHz > 0.0) ? (1000000.0 / freqHz) : 0.0;

  // Number of accepted low/high segments per measurement. Mode 2 (PW search)
  // and mode 3 (FREQ_TRACE frequency probe) average over the precision
  // profile's time window instead, so the segment count scales with the
  // frequency and with how careful this run is meant to be.
  uint16_t samplesTarget = kGapSamplesDefault;
  if (specialMode == 2 || specialMode == 3) {
    const CalPrecisionProfile &prec = cal_precision();
    samplesTarget = prec.gapSamplesMin;
    const double halfPeriodMs = idealPeriodUs / 2000.0;  // one segment
    if (halfPeriodMs > 0.0) {
      long n = lround((double)prec.gapWindowMs / halfPeriodMs);
      if (n > (long)prec.gapSamplesMax) n = (long)prec.gapSamplesMax;
      if (n > (long)samplesTarget)      samplesTarget = (uint16_t)n;
      // Below ~30 Hz the window fits nothing, so without a higher floor a
      // reading down there averages only the profile's minimum segments and
      // its noise (~±0.2% duty at 20 Hz) becomes the accuracy limit of the
      // lowest measured pair. Raise the floor; the gapMaxWindowMs cap below
      // still bounds it, so the amp-0 scan around 8 Hz does not slow down.
      if (freqHz < (double)kSearchStepVeryLowHz &&
          samplesTarget < kGapSamplesVeryLowMin) {
        samplesTarget = kGapSamplesVeryLowMin;
      }
      // Bound how long one reading may take. Without this the segment floor
      // governs at the bottom of the range, where a segment is tens of
      // milliseconds, and every reading below ~27 Hz costs more than the
      // window asked for - a fine run spends most of its time down there.
      // Four segments is the least that still averages a rising and a falling
      // one of each polarity.
      long cap = lround((double)prec.gapMaxWindowMs / halfPeriodMs);
      if (cap < 4) cap = 4;
      if ((long)samplesTarget > cap) samplesTarget = (uint16_t)cap;
    }
  }
  // The deadline for one edge, and with it the longest segment that can be part
  // of a waveform rather than evidence of a dead one. See kGapTimeoutUs: at the
  // bottom of the range a single period outlasts the fixed 100 ms.
  unsigned long timeoutUs = kGapTimeoutUs;
  if (idealPeriodUs > 0.0) {
    const double scaled = idealPeriodUs * kGapTimeoutPeriods;
    if (scaled > (double)timeoutUs) {
      timeoutUs = (scaled > (double)kGapTimeoutMaxUs) ? kGapTimeoutMaxUs
                                                      : (unsigned long)scaled;
    }
  }

  double dtMinUs = 0.0;
  double dtMaxUs = 0.0;
  if (idealPeriodUs > 0.0) {
    // Accept any segment between ~1% and ~99% of the ideal period. This covers
    // extreme duty cycles (2%/98%) while rejecting very short/high-frequency
    // glitches that are clearly not the fundamental.
    dtMinUs = idealPeriodUs * 0.01;
    dtMaxUs = idealPeriodUs * 0.99;
    if (dtMinUs < (double)kEdgeDebounceMinUs) {
      dtMinUs = (double)kEdgeDebounceMinUs;
    }
    if (dtMaxUs > (double)timeoutUs) {
      dtMaxUs = (double)timeoutUs;
    }
  }

  // Segments are timed in CPU cycles, not microseconds. What this function
  // measures is the difference between two segment lengths, and micros() (a 1 us
  // timer) quantises each edge to 1 us: at 4 kHz a period is 250 us, so a single
  // count of quantisation is already 0.2% of duty - ten times the tolerance a
  // fine run asks for, and irreducible by averaging since the timer is what is
  // coarse, not the waveform. The cycle counter is the SysTick counter the core
  // already runs, extended to 32 bits (rp2040.getCycleCount()); at 125 MHz that
  // is 8 ns, so one reading resolves what micros() could not reach at all.
  // The timeout stays on micros(): 100 ms does not need 8 ns. The extension is
  // not trusted for segments longer than one 24-bit wrap - see the wrap
  // reconstruction at the edge handler below.
  const uint32_t cyclesPerUs   = (uint32_t)(rp2040.f_cpu() / 1000000);
  const double   usPerCycle    = 1.0 / (double)cyclesPerUs;
  const uint32_t debounceCycles = (uint32_t)kEdgeDebounceMinUs * cyclesPerUs;
  const uint32_t dtMinCycles    = (uint32_t)(dtMinUs * (double)cyclesPerUs);
  const uint32_t dtMaxCycles    = (uint32_t)(dtMaxUs * (double)cyclesPerUs);

  // Local edge-timing state (was global before the cleanup).
  int      pulseCount        = 0;
  uint16_t acceptedSamples   = 0;
  uint64_t risingSumCycles   = 0;
  uint64_t fallingSumCycles  = 0;
  bool     lastVal           = 0;
  uint16_t risingCount       = 0;
  uint16_t fallingCount      = 0;
  // Diagnostics: debounced edges vs period-gate rejects (TIMEOUT localization).
  uint16_t edgesSeen     = 0;
  uint16_t edgesRejected = 0;

  unsigned long lastEdgeTime   = micros();
  uint32_t      lastEdgeCycles = rp2040.getCycleCount();
  // How late an edge is noticed is one pass of this loop, so the loop carries as
  // little as possible: a register read of the pin and nothing else. The timeout
  // is checked once every 64 passes instead of on every one, which is still far
  // more often than a 100 ms deadline needs.
  uint8_t pollTick = 0;

  while (acceptedSamples < samplesTarget) {

    const bool rawVal = (bool)gpio_get(DCO_calibration_pin);
    // Compensate for hardware polarity if needed so that 'val == 1' always
    // represents the same logical DCO level for duty measurements.
    const bool val = kGapPolarityInverted ? !rawVal : rawVal;

    if (val == lastVal && ((++pollTick & 0x3F) != 0)) {
      continue;  // nothing happened; not time to look at the clock either
    }

    const uint32_t nowCycles = rp2040.getCycleCount();
    const unsigned long nowUs = micros();

    if ((nowUs - lastEdgeTime) > timeoutUs) {
      const bool rawAtTimeout = digitalRead(DCO_calibration_pin);

      // Manual cal: log at debug >= 1. Auto-cal keeps the quieter >= 3 threshold.
      if (autotuneDebug >= 3 || (manualCalibrationFlag && autotuneDebug >= 1)) {
        Serial.println((String)"[GAP_TIMEOUT] note=" + DCO_calibration_current_note +
                       (String)" freq=" + fmt_freq((float)freqHz) +
                       (String)" DCO=" + currentDCO +
                       (String)" raw=" + (int)rawAtTimeout +
                       (String)" edges=" + edgesSeen +
                       (String)" rejected=" + edgesRejected +
                       (String)" accepted=" + acceptedSamples +
                       (String)" TidealUs≈" + (uint32_t)idealPeriodUs +
                       (String)" timeoutUs=" + (uint32_t)timeoutUs +
                       (String)" PW_raw=" + pw_level_readback(cal_pw_channel(currentDCO)) +
                       (String)" ampComp=" + ampCompCalibrationVal);
      }

      return kGapTimeoutSentinel;
    }

    if (val != lastVal) {
      // The cycle counter is the 24-bit SysTick, and on this core its 32-bit
      // software extension does not advance: a segment longer than 2^24 cycles
      // (67 ms at 250 MHz) comes back short by whole wraps. That is how the
      // amp-0 hunt once rejected every reading of a waveform the scope showed
      // was clean - below ~7.5 Hz both halves outlast a wrap, and the mangled
      // period failed the off-period gate at every probe. The wall clock
      // (micros(), already read for the timeout) recovers the lost wraps: it
      // picks the multiple of 2^24, the counter keeps the fine 4 ns bits. The
      // low 24 bits of the delta are wrap-proof by construction, and micros()
      // jitter is four orders of magnitude below half a wrap, so the rounded
      // wrap count cannot come out wrong. The longest correctable segment is
      // the 400 ms gap deadline = 100 M cycles, comfortably inside uint32.
      const uint32_t fine24     = (nowCycles - lastEdgeCycles) & 0x00FFFFFFu;
      const uint64_t wallCycles = (uint64_t)(nowUs - lastEdgeTime) * cyclesPerUs;
      const int64_t  lostWraps  =
        ((int64_t)wallCycles - (int64_t)fine24 + (int64_t)(1u << 23)) >> 24;
      const uint32_t dtCycles =
        (lostWraps > 0) ? (fine24 + (uint32_t)((uint64_t)lostWraps << 24))
                        : fine24;

      if (dtCycles >= debounceCycles) {

        lastVal = val;
        edgesSeen++;

        // Re-align so counting starts on a rising edge.
        if (pulseCount == 1 && val == 0) {
          pulseCount = 0;
        }
        if (pulseCount > 2) {
          const uint32_t dt = dtCycles;  // cycles
          bool intervalOk = true;
          if (idealPeriodUs > 0.0) {
            // Reject intervals that are incompatible with the ideal period.
            // This prevents very short spurious edges from corrupting the
            // duty measurement at low frequencies.
            if (dt < dtMinCycles || dt > dtMaxCycles) {
              intervalOk = false;
            }
          }

          // NOTE: segment attribution follows the legacy convention (the
          // segment ending on a falling edge goes into the "falling" sum).
          // The overall sign chain (kGapPolarityInverted here plus the flip
          // in measure_gap_for_amp) is field-validated; keep them in sync if
          // this is ever changed.
          if (intervalOk) {
            if (val == 0) {
              fallingSumCycles += dt;
              fallingCount++;
            } else {
              risingSumCycles += dt;
              risingCount++;
            }
            acceptedSamples++;
          } else {
            edgesRejected++;
          }
        }
        lastEdgeTime   = nowUs;
        lastEdgeCycles = nowCycles;
        pulseCount++;
      }
    }
  }

  // A reading where only one polarity of segment survived the gate is not a
  // duty measurement: the duty is pegged at 0% or 100% (the other side's blips
  // were shorter than the segment floor) and avgHigh - avgLow then measures the
  // blips, not the waveform - at the bottom of the range that number is tiny
  // against the ideal period and converts to a duty error near zero, which is
  // how a pegged waveform once scored -0.72% and closed a fake amp-0 bracket.
  // Only the frequency-search probes (mode 3) get the sentinel: the classic
  // search and the PW limit search read extreme duties on purpose.
  if (specialMode == 3 && (risingCount == 0 || fallingCount == 0)) {
    if (autotuneDebug >= 2) {
      Serial.println((String)"[GAP_ONESIDED] freq=" + fmt_freq((float)freqHz) +
                     (String)" DCO=" + currentDCO +
                     (String)" highs=" + risingCount +
                     (String)" lows=" + fallingCount +
                     (String)" duty pegged; reading discarded");
    }
    return kGapTimeoutSentinel;
  }

  // Compute average low and high segment durations directly from the number
  // of segments we actually accumulated. Averaging in cycles and converting
  // once keeps the 8 ns resolution all the way to the result.
  float avgLowUs  = (fallingCount > 0)
                      ? (float)((double)fallingSumCycles * usPerCycle / (double)fallingCount)
                      : 0.0f;
  float avgHighUs = (risingCount > 0)
                      ? (float)((double)risingSumCycles * usPerCycle / (double)risingCount)
                      : 0.0f;

  // Derived period and direct HIGH-duty estimate based purely on measured
  // low/high portions (duty cycle = fraction of the period spent HIGH).
  float measuredPeriodUs = avgLowUs + avgHighUs;
  float dutyMeasuredFrac = (measuredPeriodUs > 0.0f) ? (avgHighUs / measuredPeriodUs) : 0.0f;

  // A reading whose segments do not sum to the requested period is not a duty
  // measurement of the requested waveform: near the bottom of the range at
  // amp comp 0 the pin can toggle roughly twice per requested cycle, and those
  // near-symmetric sub-segments read ~50% duty at any frequency - which is how
  // the amp-0 search once chased a fake 50% crossing the scope could not see.
  // Mode 3 only, like the one-sided rule above and for the same reason.
  if (specialMode == 3 && idealPeriodUs > 0.0 &&
      fabsf(measuredPeriodUs - (float)idealPeriodUs) >
        kGapPeriodTolRatio * (float)idealPeriodUs) {
    if (autotuneDebug >= 2) {
      Serial.println((String)"[GAP_OFFPERIOD] freq=" + fmt_freq((float)freqHz) +
                     (String)" DCO=" + currentDCO +
                     (String)" Tmeas=" + measuredPeriodUs +
                     (String)" Tideal=" + (float)idealPeriodUs +
                     (String)" not the requested waveform; reading discarded");
    }
    return kGapTimeoutSentinel;
  }

  // Positive result means the HIGH segment is longer than LOW (duty > 50%);
  // negative means LOW is longer (duty < 50%). This keeps the relation:
  //   duty_high - 0.5 = diff / (2 * periodUs)
  float diffUs = avgHighUs - avgLowUs;

  if (autotuneDebug >= 2) {
    // Log raw gap measurement with context: which mode, note/DCO, the
    // current amplitude compensation value, the last PW we explicitly set,
    // and the inferred duty/target duty if a period is available. freq= is the
    // frequency actually being driven, which during an arbitrary-frequency
    // probe has nothing to do with note= (that one is left at whatever note
    // the calibration last set).

    // Duty estimate using the same "diff vs ideal period" method used by
    // the PW search code.
    double dutyPercentIdeal = 0.0;
    double targetDutyPercent = g_gapLogTargetDutyFraction * 100.0;
    if (g_gapLogCurrentPeriodUs > 0.0) {
      double dutyErrorFrac = (double)diffUs / (2.0 * g_gapLogCurrentPeriodUs);
      dutyPercentIdeal = (0.5 + dutyErrorFrac) * 100.0;
    }

    // Direct duty estimate based only on measured low/high times.
    double dutyPercentMeasured = dutyMeasuredFrac * 100.0;

    Serial.println((String)"[GAP_MEASURE] mode=" + specialMode +
                   (String)" note=" + DCO_calibration_current_note +
                   (String)" freq=" + fmt_freq((float)freqHz) +
                   (String)" DCO=" + currentDCO +
                   (String)" AMP=" + ampCompCalibrationVal +
                   (String)" PW_raw=" + pw_level_readback(cal_pw_channel(currentDCO)) +
                   (String)" diff=" + diffUs +
                   (String)" avgLowUs=" + avgLowUs +
                   (String)" avgHighUs=" + avgHighUs +
                   (String)" T_meas=" + measuredPeriodUs +
                   (String)" duty_meas≈" + dutyPercentMeasured + "%" +
                   (String)" duty_ideal≈" + dutyPercentIdeal + "%" +
                   (String)" targetDuty=" + targetDutyPercent + "%");
  }

  return diffUs;
}

/*************************************************************************************/
/*************************************************************************************/
/*************************************************************************************/

// --- PW CV probe ------------------------------------------------------------

// PW raw levels walked by the probe: both rails plus three points across the
// span, so a comparator that only reacts near its center still shows movement.
static const uint16_t kPWProbeLevels[] = {
  0, DIV_COUNTER_PW / 4, DIV_COUNTER_PW / 2,
  (DIV_COUNTER_PW * 3) / 4, DIV_COUNTER_PW - 1
};

// Duty span (percentage points) above which a channel counts as having moved
// the pulse. Measurement noise on a good board is a fraction of a point.
static constexpr float kPWProbeMovedPct = 5.0f;

// Prove whether a PW CV write reaches the pulse comparator at all, and whether
// it reaches the voice the firmware believes it does (PARAM_DEBUG_COMMAND 46,
// run from loop1 while manual calibration is active).
//
// Manual cal is required because only then is a single oscillator soloed onto
// the cal-sense pin: the duty measured there is the only witness that the CV
// arrived. Every PW channel is walked, not just the calibrated one, so a duty
// that follows some other channel means PW_PINS does not match the wiring,
// while a duty that follows nothing means there is no CV path to this
// oscillator's pulse and no firmware change can mute it.
//
// Leaves PW clobbered on purpose: the next manual-cal pass in loop1 rewrites
// every channel from the current substage.
void run_pw_cv_probe() {
  const uint8_t osc       = cal_manual_osc();
  const uint8_t expectCh  = cal_pw_channel(osc);
  const double  freqHz    = (double)note_to_freq(DCO_calibration_current_note);
  const double  periodUs  = (freqHz > 0.0) ? (1000000.0 / freqHz) : 0.0;

  Serial.println((String)"[PW_PROBE] start: osc=" + osc +
                 " expected ch=" + expectCh +
                 " (pin GP" + PW_PINS[expectCh] + ")" +
                 " stage=" + manualCalibrationStage +
                 " note=" + DCO_calibration_current_note +
                 " freq=" + fmt_freq((float)freqHz));

  if (periodUs <= 0.0) {
    Serial.println("[PW_PROBE] no note driven; nothing to measure");
    return;
  }

  uint8_t bestCh    = 0;
  float   bestSpan  = -1.0f;
  float   expectSpan = 0.0f;
  bool    anyRead   = false;

  for (uint8_t ch = 0; ch < NUM_PW_CHANNELS; ++ch) {
    if (PW_PINS[ch] == PW_PIN_UNASSIGNED) {
      Serial.println((String)"[PW_PROBE] ch=" + ch + " not wired, skipped");
      continue;
    }

    // Only the channel under test carries a CV, so a duty that moves anyway
    // belongs to whatever channel is actually feeding this oscillator.
    for (uint8_t z = 0; z < NUM_PW_CHANNELS; ++z) {
      if (z != ch && PW_PINS[z] != PW_PIN_UNASSIGNED) {
        pwm_set_chan_level(PW_PWM_SLICES[z], pwm_gpio_to_channel(PW_PINS[z]), 0);
        PW[z] = 0;
      }
    }

    float dutyMin = 0.0f, dutyMax = 0.0f;
    uint8_t reads = 0;
    const uint8_t levels = (uint8_t)(sizeof(kPWProbeLevels) / sizeof(kPWProbeLevels[0]));

    for (uint8_t li = 0; li < levels && !calibrationCancelRequested; ++li) {
      const uint16_t pw = kPWProbeLevels[li];
      GapMeasurement gm = set_pw_and_measure(ch, pw);

      String line = (String)"[PW_PROBE] ch=" + ch + " pin=GP" + PW_PINS[ch] +
                    " PW_raw=" + pw;
      if (gm.timedOut) {
        Serial.println(line + " TIMEOUT");
        continue;
      }
      const float dutyPct = (float)((0.5 + (double)gm.value / (2.0 * periodUs)) * 100.0);
      Serial.println(line + " gapUs=" + gm.value +
                     " duty≈" + String(dutyPct, 2) + "%");

      if (reads == 0 || dutyPct < dutyMin) dutyMin = dutyPct;
      if (reads == 0 || dutyPct > dutyMax) dutyMax = dutyPct;
      ++reads;
      anyRead = true;
    }

    const float span = (reads > 0) ? (dutyMax - dutyMin) : 0.0f;
    Serial.println((String)"[PW_PROBE] ch=" + ch + " pin=GP" + PW_PINS[ch] +
                   " reads=" + reads + "/" + levels +
                   " span≈" + String(span, 2) + "pp" +
                   (ch == expectCh ? "  <-- expected for this oscillator" : ""));

    if (ch == expectCh) expectSpan = span;
    if (span > bestSpan) {
      bestSpan = span;
      bestCh   = ch;
    }
    if (calibrationCancelRequested) break;
  }

  apply_pw_center(expectCh);

  if (calibrationCancelRequested) {
    Serial.println("[PW_PROBE] cancelled by user");
    calibrationCancelRequested = false;
    return;
  }

  if (!anyRead) {
    Serial.println("[PW_PROBE] every read timed out: the cal-sense pin sees no pulse "
                   "at all, so this says nothing about the PW CV");
    cal_sense_probe_log();
    return;
  }
  if (expectSpan >= kPWProbeMovedPct) {
    Serial.println((String)"[PW_PROBE] done: expected ch=" + expectCh +
                   " moves the duty by " + String(expectSpan, 2) +
                   "pp, so the CV is live; look downstream in the mix");
    return;
  }
  if (bestSpan >= kPWProbeMovedPct) {
    Serial.println((String)"[PW_PROBE] done: the duty follows ch=" + bestCh +
                   " (GP" + PW_PINS[bestCh] + ", span " + String(bestSpan, 2) +
                   "pp) instead of the expected ch=" + expectCh +
                   ": PW_PINS does not match the wiring");
    return;
  }
  Serial.println((String)"[PW_PROBE] done: no channel moves the duty (widest " +
                 String(bestSpan, 2) + "pp on ch=" + bestCh +
                 "): the PW CV does not reach this oscillator's pulse, "
                 "so it cannot be muted from firmware");
}

// Debug helper used during manual calibration: measure and report the
// duty-cycle difference from the target duty (normally 50%) for the
// current note/DCO. The result is sent to the Input board as a 32-bit
// PARAM_GAP_FROM_DCO value, which it relays to the screen as "GAP".
void DCO_calibration_debug() {
  // Reuse the main gap-measurement path (which already handles polarity,
  // debouncing, and timeouts) so manual calibration sees the same notion
  // of "gap" as the automatic routines.
  GapMeasurement gm = measure_gap(0);  // target is 50% duty

  // Osc under trim comes from the stage walk (not currentDCO, which only moves
  // during auto-cal). The stage counts substages, so it is not the osc index.
  uint8_t reportDCO = cal_manual_osc();
  if (reportDCO >= NUM_OSCILLATORS) {
    reportDCO = NUM_OSCILLATORS - 1;
  }

  // Compute duty error relative to the center target (0.5) using the
  // *ideal* period for the current note. For manual trimming this is
  // sufficient and keeps the math simple.
  int32_t dutyErrorPercentTimes100 = 0;  // duty error [%] * 100

  if (!gm.timedOut) {
    double freqHz = (double)note_to_freq(DCO_calibration_current_note);
    if (freqHz > 0.0) {
      double periodUs = 1000000.0 / freqHz;
      // gm.value is avgHighUs - avgLowUs (same sign as find_gap).
      // For a perfect 50% duty, high and low are equal, so gm.value == 0.
      // Duty error fraction from 50% is:
      //   duty_high - 0.5 = (avgHighUs - avgLowUs) / (2 * periodUs)
      // The oscillator's duty trim is subtracted so that manual trimming and
      // auto-cal aim at the same target: "0" on screen means the same duty the
      // searches converge to.
      double gapUs = (double)gm.value - (double)duty_trim_gap_us(reportDCO, (float)freqHz);
      double dutyErrorFrac = gapUs / (2.0 * periodUs);
      double dutyErrorPercent = dutyErrorFrac * 100.0;
      // Scale by 100 for two decimal digits of resolution on the screen.
      dutyErrorPercentTimes100 = (int32_t)(dutyErrorPercent * 100.0);
    }
  } else {
    // On timeout, propagate a large sentinel so the UI/Serial never look
    // like a near-perfect 50% trim.
    dutyErrorPercentTimes100 = kManualGapTimeoutDutyErrTimes100;
  }

  if (autotuneDebug >= 1) {
    if (gm.timedOut) {
      Serial.println((String)"[MANUAL_GAP] note=" + DCO_calibration_current_note +
                     (String)" DCO=" + reportDCO +
                     (String)" AMP=" + ampCompCalibrationVal +
                     (String)" TIMEOUT");
      // Raw cal-sense window (no period gate) — separates stuck pin from rejected freq.
      cal_sense_probe_log();
    } else {
      Serial.println((String)"[MANUAL_GAP] note=" + DCO_calibration_current_note +
                     (String)" DCO=" + reportDCO +
                     (String)" AMP=" + ampCompCalibrationVal +
                     (String)" gapUs=" + gm.value +
                     (String)" dutyTrim=" + (ampCompDutyOffset[reportDCO] / 100.0) + "%" +
                     (String)" dutyErr(%)≈" + (dutyErrorPercentTimes100 / 100.0));
    }
  }

  // Send as a 32-bit PARAM_GAP_FROM_DCO value through the standard
  // param protocol: Serial2 → Mainboard, which relays it to the Screen.
  // force: do not drop when Serial2 DMA is busy — this is the live GAP UI.
  serialSendParam32(PARAM_GAP_FROM_DCO, (uint32_t)dutyErrorPercentTimes100, true);
}

#endif  // __AUTOTUNE_IMPL_H__
#ifndef __AUTOTUNE_MEASUREMENT_H__
#define __AUTOTUNE_MEASUREMENT_H__

#include "autotune_constants.h"

// Forward declaration so this header can be included before the definition
// in autotune_impl.h.
float find_gap(byte specialMode);

// Simple wrapper around find_gap() that interprets the timeout sentinel and
// returns a structured result instead of a raw float.
struct GapMeasurement {
  bool timedOut;
  float value;
};

inline GapMeasurement measure_gap(byte specialMode) {
  float v = find_gap(specialMode);
  GapMeasurement result;
  result.timedOut = (v == kGapTimeoutSentinel);
  result.value = v;
  return result;
}

#endif  // __AUTOTUNE_MEASUREMENT_H__


#ifndef __AUTOTUNE_SEARCH_IMPL_H__
#define __AUTOTUNE_SEARCH_IMPL_H__

#include "../include_all.h"

// =============================================================================
// autotune_search_impl.h — search-based DCO amplitude-compensation calibration.
//
// This file holds the per-note search that builds each oscillator's
// [frequency -> range PWM] table (calibrate_DCO), the highest/lowest
// frequency estimators used when the table reaches the top of the PWM range,
// and the interpolation helpers shared by those routines.
//
// Definitions, not declarations: include this exactly once per sketch, from a
// .ino shim (DCO/autotune_search.ino), after autotune_impl.h — the file-scope
// statics of the two are visible to each other in that order.
//
// Orchestration (DCO_calibration) and the PW center/limit searches live in
// autotune_impl.h; the edge-timing measurement core (find_gap) lives there too.
// =============================================================================

// Signed duty error (in microseconds, + = amplitude too low) measured at the
// frequency find_freq_for_duty50() last returned; kGapTimeoutSentinel when the
// search never saw a usable signal. The FREQ_TRACE logs report it per stored
// pair and the calibration report converts it to a duty percentage, so the
// achieved precision is visible.
static float g_lastFreqBisectGapUs = kGapTimeoutSentinel;

// Duty probes spent by the last find_freq_for_duty50() call (bisection plus
// refinement). Printed as probes= so an implausibly fast run is visible.
static int g_lastFreqBisectProbes = 0;

// Extra readings the last search spent waiting for the waveform to stop moving
// after a frequency change (see measure_duty_at_freq). Printed as settle=, so
// an oscillator that needs a long time to follow a jump is visible in the logs
// rather than silently biasing the readings.
static int g_lastSettleChecks = 0;

// Last measure_duty_at_freq() at amp 0 never got two readings to agree.
// find_freq_for_duty50() then treats the probe as sign-only (no INTERP).
static bool g_lastDutyUnsettled = false;

// Secant seed from the last amp0_prescan() that found a sign change, or 0.
// FREQ_TRACE stores this when the endpoint search is rejected, instead of
// the model intercept that can sit below the pulse floor.
static float g_lastAmp0ScanSeedHz = 0.0f;

// Compute allowed |gap| (in microseconds) for a given frequency (Hz) and
// duty-cycle error fraction (e.g. 0.005 = 0.5% duty error).
// From duty_high - 0.5 = gap / (2*T): |gap|max = 2 * epsilon * T.
double compute_gap_tolerance_for_freq(double freqHz, double dutyErrorFraction) {
  if (freqHz <= 0.0) {
    return 1e6;  // Very loose tolerance if frequency is invalid.
  }
  double periodUs = 1e6 / freqHz;
  return 2.0 * dutyErrorFraction * periodUs;
}

// Return true if the two values have opposite signs (simple sign change test).
// Used by calibrate_DCO() to detect when the duty-cycle error has crossed
// through zero between successive measurements (indicating we've passed the
// ideal PWM point and should probe neighbours more carefully).
static bool did_sign_change(float previous, float current) {
  return (previous > 0.0f && current < 0.0f) ||
         (previous < 0.0f && current > 0.0f);
}

// Helper: set the current DCO amplitude, wait for the waveform to settle,
// and return the measured duty-cycle gap (or timeout sentinel value).
// IMPORTANT: We normalize the sign here so that a *positive* value means
// "amplitude too low" and a *negative* value means "amplitude too high".
static float measure_gap_for_amp(uint16_t ampPwm) {
  const float freqHz = note_to_freq(DCO_calibration_current_note);
  voice_task_autotune(0, ampPwm);
  settle_for_freq((double)freqHz);
  ++calRunProbes;
  GapMeasurement gm = measure_gap(0);

  // Preserve the timeout sentinel exactly so downstream code can reliably
  // detect "no signal" vs a real small error.
  if (gm.timedOut) {
    return kGapTimeoutSentinel;
  }

  // find_gap() returns avgHighUs - avgLowUs; flip the sign so the search
  // moves the PWM in the correct direction regardless of edge polarity, and
  // aim at the trimmed duty target instead of a bare 50%.
  return -(gm.value - duty_trim_gap_us(currentDCO, freqHz));
}

// Helper: evaluate neighbour measurements (lower/higher) around the current
// PWM and update closestToZero / bestAmpComp if any of them are better.
// The caller passes in the measurements taken one step below and above the
// current PWM value; this routine picks the best candidate among those and
// the current PWM, based purely on closeness of the duty error to zero.
static void update_best_from_neighbours(
  int rangeSamples,
  const float* lowerMeasurements,
  const uint16_t* lowerVoltages,
  const float* higherMeasurements,
  const uint16_t* higherVoltages,
  float avgValue,
  float& closestToZero,
  uint16_t& bestAmpComp,
  uint16_t currentAmpCompCalibrationVal
) {
  for (int i = 0; i < rangeSamples; i++) {
    if (abs(lowerMeasurements[i]) < abs(closestToZero)) {
      closestToZero = lowerMeasurements[i];
      bestAmpComp = lowerVoltages[i];
    }
    if (abs(higherMeasurements[i]) < abs(closestToZero)) {
      closestToZero = higherMeasurements[i];
      bestAmpComp = higherVoltages[i];
    }
  }

  // Check the current voltage again
  if (abs(avgValue) < abs(closestToZero)) {
    closestToZero = avgValue;
    bestAmpComp = currentAmpCompCalibrationVal;
  }
}

// Helper: PWM step for the next probe based on the current error.
// For large errors step by 2; once close to the target (within tolerance * 20)
// step by 1 to avoid overshooting. Sign follows the error direction.
static int step_amp_from_error(float avgValue, double tolerance) {
  int magnitude = (abs(avgValue) < tolerance * 20) ? 1 : 2;
  return (avgValue > 0) ? magnitude : -magnitude;
}

// Helper: compute the initial amplitude (range PWM) guess for a given table
// index j and note, using the same interpolation strategy as the original code:
//  - j == 4: manual preset scaled by 1.35,
//  - j == 6: logarithmic interpolation between the first two entries,
//  - else : quadratic interpolation based on the previous three calibration points.
static uint16_t compute_initial_amp_for_note(
  const DCOCalibrationContext& ctx,
  int j
) {
  if (j == 4) {
    return (ctx.initManualAmpByOsc[ctx.dcoIndex] + ctx.manualOffsetByOsc[ctx.dcoIndex]) * 1.35;
  } else if (j == 6) {
    return logarithmicInterpolation(
      ctx.calibrationData[2],
      ctx.calibrationData[3],
      ctx.calibrationData[4],
      ctx.calibrationData[5],
      note_to_freq(ctx.currentNote) * 100
    );
  } else {
    return quadraticInterpolation(
      ctx.calibrationData[j - 6],
      ctx.calibrationData[j - 5],
      ctx.calibrationData[j - 4],
      ctx.calibrationData[j - 3],
      ctx.calibrationData[j - 2],
      ctx.calibrationData[j - 1],
      note_to_freq(ctx.currentNote) * 100
    );
  }
}

// Helper: store the final calibration pair for the current note into the
// calibration table and print a short summary to Serial.
static void store_note_result(
  DCOCalibrationContext& ctx,
  int j,
  uint16_t bestAmpComp,
  float closestToZero
) {
  ctx.calibrationData[j]     = note_to_freq(ctx.currentNote) * 100;
  ctx.calibrationData[j + 1] = bestAmpComp;

  // closestToZero keeps its 50000 initializer when no probe ever succeeded.
  cal_report_set_pair_from_gap(
    j / 2,
    (fabsf(closestToZero) < 40000.0f) ? closestToZero : kGapTimeoutSentinel,
    note_to_freq(ctx.currentNote),
    CAL_SRC_RUNG);

  Serial.print("DCO_calibration_current_note ");
  Serial.println(ctx.currentNote);
  Serial.print("Best calibration voltage: ");
  Serial.println(bestAmpComp);
  Serial.print("Closest measurement to zero: ");
  Serial.println(closestToZero);
}

// Frequency ratio spanned by one calibration note interval (2^(n/12)).
static inline float calibration_interval_ratio() {
  return powf(2.0f, (float)calibration_note_interval / 12.0f);
}

// How far a frequency change moves the oscillator, in cents. Returns a huge
// value when there is nothing to compare against (cold start), which makes the
// callers treat it as the largest possible move.
static float freq_move_cents(float fromHz, float toHz) {
  if (fromHz <= 0.0f || toHz <= 0.0f) {
    return 1e9f;
  }
  return fabsf(1200.0f * log2f(toHz / fromHz));
}

// Wait a number of waveform periods, floored at a minimum. Used for the wait
// between writing a frequency and reading it; the values come from the
// precision profile, and they can be short because nothing here is trusted
// until measure_duty_at_freq() sees two readings agree.
static void wait_periods(float freqHz, float periods, uint32_t minUs) {
  uint32_t us = minUs;
  if (freqHz > 0.0f) {
    const uint32_t p = (uint32_t)(periods * 1000000.0f / freqHz + 0.999f);
    if (p > us) {
      us = p;
    }
  }
  if (us >= 1000u) {
    delay(us / 1000u);
  }
  const uint32_t rem = us % 1000u;
  if (rem) {
    delayMicroseconds(rem);
  }
}

// Write a probe frequency and remember it. The move is made in one go: walking
// there in small steps only means the divider changes again before a full
// waveform has come out at the previous one, which is not settling, it is a
// frequency ramp. One write followed by a wait long enough to produce whole
// periods is what the measurement actually needs.
static void drive_freq(float freqHz, uint16_t amp) {
  calibrationFreqHz  = freqHz;
  voice_task_autotune(4, amp);
  g_lastDrivenFreqHz = freqHz;
}

// Probe the duty error at an arbitrary frequency with a fixed range PWM. Sets
// the frequency (drive_freq), gates find_gap() against it and then keeps
// reading until two readings agree - the wait that actually matters is not a
// constant, it is however long this oscillator needs, so the fixed wait stays
// short and two readings that agree within the search's own acceptance prove
// nothing is moving any more. Sign convention matches measure_gap_for_amp():
// positive = amplitude too low (i.e. the frequency is too high for this PWM).
// hiRes averages the precision profile's window instead of kGapSamplesDefault.
// Returns kGapTimeoutSentinel on timeout.
float measure_duty_at_freq(float freqHz, uint16_t amp, bool hiRes) {
  const CalPrecisionProfile &prec = cal_precision();
  const float movedCents = freq_move_cents(g_lastDrivenFreqHz, freqHz);
  const bool  bigMove    = (movedCents >= kSettleBigMoveCents);

  gapGateFreqHz = freqHz;
  drive_freq(freqHz, amp);
  wait_periods(freqHz, prec.settlePeriods, prec.settleMinMs * 1000u);

  const uint8_t mode = hiRes ? 3 : 0;

  // First reading. A timeout normally means "frequency too high" (the pulse
  // collapsed), but right after a large move it can also be the core still
  // catching up, so that case is given one more chance before believing it.
  ++g_lastFreqBisectProbes;
  ++calRunProbes;
  GapMeasurement gm = measure_gap(mode);
  if (gm.timedOut && bigMove) {
    ++g_lastFreqBisectProbes;
    ++g_lastSettleChecks;
    ++calRunProbes;
    gm = measure_gap(mode);
  }
  if (gm.timedOut) {
    gapGateFreqHz = 0.0f;
    return kGapTimeoutSentinel;
  }

  // How much settling this probe may pay for, from how far it just moved: a
  // late bisection iteration moves by well under a cent and needs nothing, a
  // semitone or more gets the full budget.
  int checks = 0;
  if (movedCents >= kSettleSkipCents) {
    checks = bigMove ? (int)prec.settleMaxChecks : 1;
  }
  if (amp == 0 && checks < 3) {
    checks = 3;
  }

  // "Settled" = two readings that agree closely enough that more waiting could
  // not change what the search decides.
  double stableTol = compute_gap_tolerance_for_freq(freqHz, prec.bisectDutyTol);
  if (stableTol < prec.bisectGapFloorUs) {
    stableTol = prec.bisectGapFloorUs;
  }
  stableTol *= (double)prec.settleStableMult;

  float value    = gm.value;
  bool  settled  = (checks == 0);
  g_lastDutyUnsettled = false;
  for (int c = 0; c < checks; ++c) {
    if (calibrationCancelRequested) {
      break;
    }
    ++g_lastFreqBisectProbes;
    ++g_lastSettleChecks;
    ++calRunProbes;
    GapMeasurement again = measure_gap(mode);
    if (again.timedOut) {
      // The valid reading in hand already proved the waveform exists; a
      // re-read discarded by the gap gates (one-sided, off-period, a genuine
      // glitch) does not refute it. Marginal waveforms flicker between clean
      // and glitchy readings, and returning the sentinel here is how a probe
      // that had measured the far side of the crossing once became a "no
      // pulse" wall that stopped the pair-1 search 1% short of the answer.
      // The check is consumed; if none are left, the valid reading stands.
      if (autotuneDebug >= 2) {
        Serial.println((String)"[FREQ_SETTLE] f=" + fmt_freq(freqHz) + " amp=" + amp +
                       " re-read discarded; keeping the valid reading");
      }
      continue;
    }
    if (fabsf(again.value - value) <= (float)stableTol) {
      value   = 0.5f * (value + again.value);  // both are good; average them
      settled = true;
      break;
    }
    if (amp == 0) {
      // Keep the reading closer to 50% (smaller |gap|). The newer one used
      // to throw away a 49.90% settle in favour of a later 3% swing.
      if (fabsf(again.value) < fabsf(value)) {
        value = again.value;
      }
    } else {
      value = again.value;  // still moving: the newer reading is the better one
    }
  }

  if (!settled && autotuneDebug >= 2) {
    Serial.println((String)"[FREQ_SETTLE] f=" + fmt_freq(freqHz) + " amp=" + amp +
                   " moved=" + movedCents + " cents; no two readings within " +
                   stableTol + " us after " + checks + " checks");
  }
  if (!settled && amp == 0) {
    g_lastDutyUnsettled = true;
  }

  gapGateFreqHz = 0.0f;
  // Aim at 50% + the oscillator's duty trim (0 by default).
  return -(value - duty_trim_gap_us(currentDCO, freqHz));
}

// How fast the outward step grows while the search is still hunting for a
// bracket, and how far inside the bracket an interpolated candidate must stay
// (as a fraction of the bracket, in log-frequency) to be worth measuring.
static constexpr float  kSearchStepGrowth = 1.6f;
static constexpr double kBracketEdgeGuard = 0.05;
// Narrower than this and there is nothing left to resolve: the noise between
// two readings of the same point is bigger than what moving inside the bracket
// could change. Without a floor the search re-probes the same frequency until
// the budget runs out (seen at the amp-0 endpoint: ~20 probes at 6.29 Hz).
static constexpr double kBracketMinWidthCents = 3.0;

// Snap a candidate so the probe actually moves by at least kMinFreqStepHz.
static double snap_min_freq_step(double from, double to) {
  if (fabs(to - from) >= (double)kMinFreqStepHz) {
    return to;
  }
  return (to >= from) ? (from + (double)kMinFreqStepHz)
                      : (from - (double)kMinFreqStepHz);
}

// Largest step one probe of the frequency search may take at this frequency:
// 400 cents above 440 Hz, 200 from 100 Hz up, 100 below that (including the
// amp-0 hunt under 30 Hz). See kSearchStepCents* in autotune_constants.h.
static float search_step_cap_cents(float freqHz) {
  if (freqHz >= kSearchStepHighHz)    return kSearchStepCentsHigh;
  if (freqHz >= kSearchStepLowHz)     return kSearchStepCentsMid;
  if (freqHz >= kSearchStepVeryLowHz) return kSearchStepCentsLow;
  return kSearchStepCentsVeryLow;
}

// Next frequency to probe inside a bracket, per autotuneSearchMode (cmds 37-39).
//
// The bracket is [fLo, fHi] with gLo < 0 < gHi, so the answer is between them
// whatever this returns; the mode only decides how fast it closes and how much
// it is willing to believe.
//   BISECT: the geometric midpoint, which halves the bracket in cents. Only the
//     sign of a reading is used, so a magnitude thrown off by noise cannot move
//     the probe.
//   INTERP: an Illinois secant step in log-frequency. Duty error against
//     log-frequency is nearly straight over a small bracket, so this usually
//     lands inside the acceptance in one or two moves.
//   GATED: INTERP only where both readings are clearly bigger than the noise
//     the measurement admits to (noiseGapUs, the disagreement between two
//     readings of one point that measure_duty_at_freq() is willing to accept),
//     BISECT where they are not - fast up high, sign-only at the bottom.
// edgeFromTimeout means one of the edges is a probe that found no pulse and so
// has no magnitude to interpolate against, which forces the midpoint whatever
// the mode. So does an interpolated candidate that lands within
// kBracketEdgeGuard of an edge: it would measure a frequency we have
// effectively already measured.
static double next_probe_in_bracket(double fLo, double fHi, double gLo, double gHi,
                                    bool edgeFromTimeout, double noiseGapUs) {
  const double lLo = log(fLo);
  const double lHi = log(fHi);

  bool interpolate = (autotuneSearchMode != SEARCH_BISECT) &&
                     !edgeFromTimeout && gLo < 0.0 && gHi > 0.0;
  if (interpolate && autotuneSearchMode == SEARCH_GATED) {
    interpolate = (-gLo > noiseGapUs) && (gHi > noiseGapUs);
  }

  double next = 0.0;
  if (interpolate) {
    next = exp(lLo + (lHi - lLo) * (gLo / (gLo - gHi)));
    const double guardLo = exp(lLo + (lHi - lLo) * kBracketEdgeGuard);
    const double guardHi = exp(lHi - (lHi - lLo) * kBracketEdgeGuard);
    if (!(next > guardLo && next < guardHi)) {
      next = 0.0;
    }
  }
  return (next > 0.0) ? next : sqrt(fLo * fHi);
}

// Find the frequency at which a fixed range PWM produces ~zero duty error (the
// 50% duty point of the freq(PWM) calibration curve).
//
// At a fixed PWM, a positive duty error ("amplitude too low") means the
// frequency is too high for the oscillator to reach full amplitude, so the
// answer is below this probe; a negative error means headroom, so it is above.
//
// A timeout means there is no pulse to measure at all, and which way that points
// depends on where the probe is. Above the range the amplitude has collapsed
// below the comparator threshold, so the answer is lower; at the very bottom the
// duty goes so lopsided that a single segment outlasts the reading deadline, so
// it is higher. The search decides from evidence where it has any - a timeout
// below a frequency that did produce a signal can only be the bottom - and
// otherwise reads it as "too high", which is the collapse and by far the common
// case. The amp-comp-0 search, the one that lives at the bottom of the range, is
// handed a measured bracket by amp0_prescan() so that it starts with the evidence
// instead of a guess about it.
//
// freqGuess is measured first - every caller passes a modelled seed, so that is
// the probe most likely to be the answer - and then the search walks outward in
// the indicated direction until it has the answer bracketed, in steps of at most
// search_step_cap_cents() and starting at a quarter of the caller's window, so a
// good seed is probed finely. Once bracketed, next_probe_in_bracket() closes in.
// windowRatio is how far the caller expects the answer to be from the seed;
// travelling more than a few times that means the seed was wrong and the search
// gives up with its best reading. Without bounds, spending kMaxSearchTimeouts
// probes on frequencies with no pulse ends it the same way; with bounds, the
// edges of the band are the terminator instead - a search that has been told
// where the answer must be is allowed to spend its probes getting there.
// With refine = true (FREQ_TRACE, the fine pass
// and both endpoints) the probes average a longer window, the probe budget and
// the acceptance come from the precision profile, and the converged frequency is
// confirmed by averaging before it is returned.
// Returns the best frequency found in Hz, or 0 if no usable signal was seen.
float find_freq_for_duty50(uint16_t amp, float freqGuess, float windowRatio,
                           bool refine, const FreqSearchBounds *bounds) {
  if (windowRatio < 1.05f) {
    windowRatio = 1.05f;
  }
  if (freqGuess <= 0.0f) {
    return 0.0f;
  }
  const double boundLo = (bounds != nullptr) ? (double)bounds->loHz : 0.0;
  const double boundHi = (bounds != nullptr) ? (double)bounds->hiHz : 0.0;
  const bool   bounded = (boundLo > 0.0 && boundHi > boundLo);
  if (bounded) {
    if (freqGuess < (float)boundLo) freqGuess = (float)boundLo;
    if (freqGuess > (float)boundHi) freqGuess = (float)boundHi;
  }
  // Keep the global in sync so [GAP_TIMEOUT]/debug logs report the probed PWM.
  ampCompCalibrationVal  = amp;
  g_lastFreqBisectGapUs  = kGapTimeoutSentinel;
  g_lastFreqBisectProbes = 0;
  g_lastSettleChecks     = 0;

  const CalPrecisionProfile &prec = cal_precision();
  const int   maxProbes   = refine ? prec.bisectIters : 24;
  const float windowCents = 1200.0f * log2f(windowRatio);
  // Allowance for the hunt: the caller's window plus one per retry it would
  // have spent shifting that window, which is how far the search used to be
  // able to reach. Spending it means the seed was outside the range the caller
  // promised, so there is nothing to gain from walking further.
  const int   windows      = refine ? prec.bisectWindows : 2;
  const float travelBudget = windowCents * (float)(windows + 1);

  float bestFreq      = 0.0f;
  float bestAbsGap    = 1e9f;
  float bestSignedGap = 0.0f;  // same measurement, sign kept for the report

  // Bracket: the highest probe that read "too low" and the lowest that read
  // "too high". Duty error rises with frequency, so the answer lies between
  // them. 0 = that side has not been seen yet.
  double fLo = 0.0, fHi = 0.0;
  double gLo = 0.0, gHi = 0.0;    // their readings
  bool   hiFromTimeout = false;   // no usable gap on the high edge
  bool   loFromTimeout = false;   // ... or on the low one
  int    lastSide      = 0;       // which edge the previous probe replaced
  int    timeouts      = 0;       // probes in a row that found no pulse at all

  // Open at a quarter of the window: the seed is a model prediction, so the
  // first move should be sized to the error expected of it, not to the whole
  // range the caller allows. The 1.6x growth still reaches the window edge in
  // three steps, so nothing loses reach.
  double f         = (double)freqGuess;
  float  stepCents = fminf(search_step_cap_cents(freqGuess), 0.25f * windowCents);
  float  travelled = 0.0f;
  // The frequencies that did produce a signal bound the region worth probing:
  // the answer cannot be outside them by more than the collapse itself, and a
  // timeout beyond either one says which side the dead zone is on.
  double lowGoodFreq  = 0.0;
  double highGoodFreq = 0.0;
  double tol          = prec.bisectGapFloorUs;

  for (int probe = 0; probe < maxProbes; ++probe) {
    if (calibrationCancelRequested) {
      return bestFreq;  // best-so-far; callers poll the cancel flag themselves
    }

    const float diff      = measure_duty_at_freq((float)f, amp, refine);
    const bool  timedOut  = (diff == kGapTimeoutSentinel);
    const bool  signOnly  = timedOut || (amp == 0 && g_lastDutyUnsettled);

    if (!timedOut) {
      if (f > highGoodFreq) {
        highGoodFreq = f;
      }
      if (lowGoodFreq == 0.0 || f < lowGoodFreq) {
        lowGoodFreq = f;
      }
      if (fabsf(diff) < bestAbsGap) {
        bestAbsGap    = fabsf(diff);
        bestSignedGap = diff;
        bestFreq      = (float)f;
      }

      if (autotuneDebug >= 2) {
        Serial.println((String)"[FREQ_BISECT] amp=" + amp +
                       (String)" f=" + fmt_freq((float)f) + (String)" gap=" + diff +
                       (String)" dutyErr=" +
                       duty_err_pct_from_gap(diff, (float)f) + "%");
      }

      // Acceptance from the precision profile (NORMAL 0.05% / 0.5 us, FINE
      // 0.02% / 0.25 us). The tight one is only meaningful because the hi-res
      // probes average a long time window (find_gap).
      tol = compute_gap_tolerance_for_freq(f, prec.bisectDutyTol);
      if (tol < prec.bisectGapFloorUs) tol = prec.bisectGapFloorUs;
      if (fabsf(diff) <= tol) {
        break;
      }
    }

    // Which edge of the bracket this probe becomes. A reading says so itself; a
    // timeout has to be placed, and getting that wrong is what used to send the
    // bottom-endpoint search marching further down into silence.
    int side;
    if (!timedOut) {
      side     = (diff > 0.0f) ? +1 : -1;
      timeouts = 0;  // the allowance below is for being lost, not for bad luck
    } else {
      ++timeouts;
      if (lowGoodFreq > 0.0 && f < lowGoodFreq) {
        side = -1;  // below a frequency that worked: the dead zone is the bottom
      } else if (highGoodFreq > 0.0 && f > highGoodFreq) {
        side = +1;  // above one: the pulse has collapsed
      } else {
        side = +1;  // nothing measured yet: assume the collapse
      }
    }

    if (side > 0) {
      fHi           = f;
      gHi           = timedOut ? 0.0 : (double)diff;
      hiFromTimeout = signOnly;
    } else {
      fLo           = f;
      gLo           = timedOut ? 0.0 : (double)diff;
      loFromTimeout = signOnly;
    }
    // Illinois: when the same edge is replaced twice running, halve the stale
    // edge's error so the interpolation stops creeping in from one side.
    if (side == lastSide) {
      if (side > 0) gLo *= 0.5;
      else          gHi *= 0.5;
    }
    lastSide = side;

    // Probing a region the oscillator cannot produce is the most expensive way
    // to learn nothing: 100 ms per probe, more at the bottom of the range and
    // doubled after a large move. Spend a fixed allowance of those in a row and
    // then stop, bracketed or not - in a row, because a search that keeps
    // producing readings between the dead probes is converging, not lost. A
    // bounded search is exempt: it cannot wander, so let it walk the whole band
    // looking for the pulse and stop at the edge instead of at a probe count.
    if (!bounded && timeouts >= kMaxSearchTimeouts) {
      Serial.println((String)"[FREQ_BISECT] amp=" + amp +
                     (String)" gave up after " + timeouts +
                     " probes in a row with no pulse (last " + fmt_freq((float)f) +
                     " Hz, seed " + fmt_freq(freqGuess) + "); keeping best=" +
                     fmt_freq(bestFreq));
      break;
    }

    if (fLo > 0.0 && fHi > 0.0) {
      // A bracket narrower than the smallest probe move is exhausted. A
      // 3-cent floor used to stop with a 3% duty error still on the table;
      // only stop on cents when the best reading is already inside tolerance.
      const double widthHz    = fHi - fLo;
      const double widthCents = 1200.0 * log2(fHi / fLo);
      if (widthHz < (double)kMinFreqStepHz ||
          (widthCents < kBracketMinWidthCents && bestAbsGap <= tol)) {
        if (autotuneDebug >= 1) {
          Serial.println((String)"[FREQ_BISECT] amp=" + amp +
                         (String)" bracket exhausted at " + fmt_freq((float)fLo) + ".." +
                         fmt_freq((float)fHi) + " Hz (" + (float)widthCents +
                         " cents); keeping best=" + fmt_freq(bestFreq));
        }
        break;
      }
      const double prevF = f;
      f = next_probe_in_bracket(fLo, fHi, gLo, gHi,
                                hiFromTimeout || loFromTimeout,
                                tol * (double)prec.settleStableMult);
      f = snap_min_freq_step(prevF, f);
      if (f <= fLo) f = fLo + (double)kMinFreqStepHz;
      if (f >= fHi) f = fHi - (double)kMinFreqStepHz;
      if (f <= fLo || f >= fHi) {
        if (bestAbsGap <= tol) {
          break;
        }
        // Nowhere left to put a distinct probe; keep the best reading.
        break;
      }
      // An edge that timed out puts that end of the bracket inside a dead zone,
      // and the answer is at its border, not in its middle. Keep the next probe
      // within half a step of the nearest frequency that did produce a signal,
      // so the search closes on the border from the side that can be measured
      // instead of spending 100 ms at a time inside the silence.
      if (hiFromTimeout && highGoodFreq > 0.0) {
        const double ceilingHz =
          highGoodFreq * pow(2.0, 0.5 * (double)search_step_cap_cents((float)highGoodFreq) / 1200.0);
        if (f > ceilingHz && ceilingHz > fLo) {
          f = ceilingHz;
        }
      }
      if (loFromTimeout && lowGoodFreq > 0.0) {
        const double floorHz =
          lowGoodFreq * pow(2.0, -0.5 * (double)search_step_cap_cents((float)lowGoodFreq) / 1200.0);
        if (f < floorHz && floorHz < fHi) {
          f = floorHz;
        }
      }
      continue;
    }

    // Not bracketed yet: step outward, bounded, growing until the sign flips.
    if (travelled >= travelBudget) {
      if (autotuneDebug >= 1) {
        Serial.println((String)"[FREQ_BISECT] amp=" + amp +
                       (String)" no bracket within " + travelBudget +
                       " cents of " + fmt_freq(freqGuess) + " Hz; keeping best=" +
                       fmt_freq(bestFreq));
      }
      break;
    }
    // A real reading says how far the crossing is. Cap the step at that
    // distance (assuming a conservatively flat slope, so the cap overshoots
    // ~2x and still brackets in one hop) instead of jumping the full range cap
    // away from a seed that already read near zero. See
    // kSearchSlopeMinPctPer100Cents in autotune_constants.h.
    if (!timedOut) {
      const float propCents = fabsf(duty_err_pct_from_gap(diff, (float)f)) *
                              (100.0f / kSearchSlopeMinPctPer100Cents);
      stepCents = fminf(stepCents, fmaxf(propCents, kSearchStepFloorCents));
    }
    const double prev = f;
    f = f * pow(2.0, (side > 0 ? -1.0 : 1.0) * (double)stepCents / 1200.0);
    f = snap_min_freq_step(prev, f);
    if (bounded) {
      if (f < boundLo) f = boundLo;
      if (f > boundHi) f = boundHi;
      // Standing on an edge and being told to go further means the answer is
      // not inside the band: either nothing in it pulsed at all, or every
      // reading kept pointing past this edge. Say which - they mean different
      // things (a silent band has no signal; readings pointing past the edge
      // mean the 50% point sits outside what the caller allows).
      if (f == prev) {
        if (highGoodFreq > 0.0) {
          Serial.println((String)"[FREQ_BISECT] amp=" + amp +
                         (String)" readings keep pointing " +
                         ((prev <= boundLo) ? "below" : "above") +
                         " the band " + fmt_freq((float)boundLo) + ".." +
                         fmt_freq((float)boundHi) +
                         " Hz; keeping best=" + fmt_freq(bestFreq));
        } else {
          Serial.println((String)"[FREQ_BISECT] amp=" + amp +
                         (String)" no pulse anywhere in " + fmt_freq((float)boundLo) + ".." +
                         fmt_freq((float)boundHi) + " Hz (" + timeouts +
                         " timeouts); keeping best=" + fmt_freq(bestFreq));
        }
        break;
      }
    }
    travelled += stepCents;

    // How big the next step may be. The per-range cap is there so a reading is
    // not taken straight after a jump the waveform has not settled from - but a
    // probe that timed out produced no reading to protect, so a bounded search
    // that has yet to see a single pulse strides instead, and crosses its band in
    // a few probes rather than a dozen. kHuntStepMaxCents keeps even that from
    // jumping clean over a narrow band of usable frequencies. Only a bounded
    // search: it has been told where the answer is, so striding can only bring it
    // closer, whereas an unbounded hunt striding away from a seed it cannot check
    // is how the top endpoint used to overshoot.
    const bool nothingMeasuredYet = (highGoodFreq == 0.0);
    stepCents = fminf(stepCents * kSearchStepGrowth,
                      (bounded && nothingMeasuredYet)
                        ? kHuntStepMaxCents
                        : search_step_cap_cents((float)f));
    // Feeling for the border of a dead zone from a frequency that works: halve
    // the step so the last probe before the silence is a close one.
    if (timedOut && !nothingMeasuredYet) {
      stepCents *= 0.5f;
    }
  }

  if (bestAbsGap >= 1e9f) {
    Serial.println((String)"[FREQ_BISECT] amp=" + amp +
                   (String)" no valid signal around " + fmt_freq(freqGuess) + " Hz");
    return 0.0f;
  }

  if (!refine) {
    g_lastFreqBisectGapUs = bestSignedGap;
    return bestFreq;
  }

  // Confirm: the search stops on one probe, which noise can bias by a step, so
  // average confirmReads readings at the frequency it settled on. If that
  // average still misses the acceptance, the probe that ended the search was
  // lucky rather than right: feed the average back into the bracket, take one
  // more step and confirm again.
  //
  // This replaces measuring a grid of five candidates +/-0.05% and +/-0.1% away
  // (under 2 cents) and keeping whichever read smallest. That was a minimum over
  // five noisy readings, which reported its own luck as the achieved error;
  // averaging the same number of readings at one frequency cuts the noise by
  // sqrt(n) with no bias, and correcting through the bracket is what actually
  // moves the answer when the frequency really is off.
  const int confirmReads  = (prec.confirmReads  < 1) ? 1 : prec.confirmReads;
  const int confirmRounds = (prec.confirmRounds < 1) ? 1 : prec.confirmRounds;

  float bestConfirmedFreq = 0.0f;
  float bestConfirmedGap  = 1e9f;
  float bestConfirmedSign = 0.0f;
  f = (double)bestFreq;  // reuse the search's probe cursor

  for (int round = 0; round < confirmRounds; ++round) {
    if (calibrationCancelRequested) {
      break;
    }

    float sum = 0.0f;
    int   n   = 0;
    for (int read = 0; read < confirmReads; ++read) {
      const float d = measure_duty_at_freq((float)f, amp, true);
      if (d == kGapTimeoutSentinel) {
        n = 0;  // unusable point: nothing to average
        break;
      }
      sum += d;
      ++n;
    }
    if (n == 0) {
      break;
    }

    const float avg = sum / (float)n;
    if (fabsf(avg) < bestConfirmedGap) {
      bestConfirmedGap  = fabsf(avg);
      bestConfirmedSign = avg;
      bestConfirmedFreq = (float)f;
    }

    const double acceptFrac = (amp == 0)
      ? ((double)kEndpointAcceptDutyPct / 100.0)
      : prec.bisectDutyTol;
    double roundTol = compute_gap_tolerance_for_freq(f, acceptFrac);
    if (roundTol < prec.bisectGapFloorUs) roundTol = prec.bisectGapFloorUs;
    if (fabsf(avg) <= roundTol) {
      break;
    }

    // Correct through the bracket the search already built. Without one (the
    // seed was accepted on the first probe) there is nothing to interpolate
    // against, so the averaged reading stands — do not accept just because
    // confirmRounds is exhausted.
    if (avg > 0.0f) {
      fHi = f; gHi = (double)avg; hiFromTimeout = false;
    } else {
      fLo = f; gLo = (double)avg;
    }
    if (!(fLo > 0.0 && fHi > 0.0)) {
      break;
    }
    const double next = next_probe_in_bracket(fLo, fHi, gLo, gHi, hiFromTimeout,
                                              roundTol * (double)prec.settleStableMult);
    if (!(next > 0.0)) {
      break;
    }
    if (autotuneDebug >= 2) {
      Serial.println((String)"[FREQ_BISECT] amp=" + amp +
                     (String)" confirm " + fmt_freq((float)f) + " avg=" + avg +
                     (String)" over " + n + " reads; correcting to " +
                     fmt_freq((float)next));
    }
    f = snap_min_freq_step(f, next);
  }

  if (bestConfirmedGap >= 1e9f) {
    g_lastFreqBisectGapUs = bestSignedGap;
    return bestFreq;  // every confirmation reading timed out; keep the search result
  }

  const float searchDuty  = fabsf(duty_err_pct_from_gap(bestAbsGap, bestFreq));
  const float confirmDuty = fabsf(duty_err_pct_from_gap(bestConfirmedGap, bestConfirmedFreq));
  const float acceptPct   = (amp == 0)
    ? kEndpointAcceptDutyPct
    : (float)(prec.bisectDutyTol * 100.0);
  if (searchDuty <= acceptPct && confirmDuty > acceptPct) {
    if (autotuneDebug >= 2) {
      Serial.println((String)"[FREQ_BISECT] amp=" + amp +
                     (String)" confirm " + fmt_freq(bestConfirmedFreq) +
                     " dutyErr=" + confirmDuty +
                     "% worse than search " + fmt_freq(bestFreq) +
                     " dutyErr=" + searchDuty + "%; keeping search");
    }
    g_lastFreqBisectGapUs = bestSignedGap;
    return bestFreq;
  }

  if (autotuneDebug >= 2) {
    Serial.println((String)"[FREQ_BISECT] amp=" + amp +
                   (String)" confirmed " + fmt_freq(bestFreq) + " -> " +
                   fmt_freq(bestConfirmedFreq) +
                   (String)" gap=" + bestConfirmedGap + " (was " + bestAbsGap + ")" +
                   (String)" dutyErr=" +
                   duty_err_pct_from_gap(bestConfirmedGap, bestConfirmedFreq) + "%");
  }
  g_lastFreqBisectGapUs = bestConfirmedSign;
  return bestConfirmedFreq;
}

// Model helpers shared with the curve tracer, defined further down with the rest
// of the FREQ_TRACE model.
static float  freq_trace_power_seed(const float *freqs, const float *amps,
                                    int count, float ampTarget);
static String freq_trace_quality(float gapUs, float freqHz, int probes,
                                 int settleChecks);

// Search the highest usable DCO frequency at full range PWM (returns Hz*100).
// Called from calibrate_DCO() when the table reaches the top of the PWM range,
// which makes this the classic method's top-of-range endpoint: the last pair in
// the table, and therefore the one every note above the last calibrated note is
// played from. pairsFilled is how many pairs of ctx.calibrationData the per-note
// search has already measured.
//
// Those pairs are the seed. A power law fitted to the top of the measured curve
// lands within a few cents of the answer, so the search opens with a small step
// and needs a couple of probes; the old seed was note_to_freq() of
// DCO_calibration_current_note, which the classic loop never advances past the
// *start* note, so it was the bottom of the range - an octave or more below, with
// no averaging (refine = false) and a fallback that simply invented a frequency a
// calibration interval lower. Probes are hi-res and FINE-quality whatever the
// run asked for (except FAST, which keeps its own cheaper readings), which up
// here costs a couple of milliseconds each.
float find_highest_freq(DCOCalibrationContext& ctx, int pairsFilled) {
  CalPrecisionOverride fineForEndpoint(
    (calibrationPrecision == CAL_PRECISION_FAST) ? calibrationPrecision
                                                 : CAL_PRECISION_FINE);

  float knownFreq[kCalReportPairs];
  float knownAmp[kCalReportPairs];
  int   knownCount = 0;
  if (pairsFilled > kCalReportPairs) {
    pairsFilled = kCalReportPairs;
  }
  // Pair 0 is the amp-comp-0 estimate, not a measurement, so it is skipped.
  for (int p = 1; p < pairsFilled; ++p) {
    const float f = (float)ctx.calibrationData[2 * p] / 100.0f;
    const float a = (float)ctx.calibrationData[2 * p + 1];
    if (f <= 0.0f || f >= 100000.0f || a <= 0.0f) {
      continue;  // empty or sentinel (20000000 = 200 kHz)
    }
    knownFreq[knownCount] = f;
    knownAmp[knownCount]  = a;
    ++knownCount;
  }

  float fSeed = freq_trace_power_seed(knownFreq, knownAmp, knownCount,
                                      (float)DIV_COUNTER);
  float windowRatio = kTopEndpointWindowRatio;
  if (fSeed <= 0.0f) {
    // Nothing measured to extrapolate from (the table topped out immediately):
    // fall back to the legacy seed and window.
    fSeed       = note_to_freq(DCO_calibration_current_note);
    windowRatio = calibration_interval_ratio();
  }

  float bestFreq = find_freq_for_duty50(DIV_COUNTER, fSeed, windowRatio, true);
  const float lastFreq = (knownCount > 0) ? knownFreq[knownCount - 1] : 0.0f;
  // A tight window is only safe with a second chance: retry from the highest
  // measured pair, one calibration interval up, which is a seed built from a
  // measurement rather than an extrapolation.
  if (bestFreq <= lastFreq && knownCount > 0 && !calibrationCancelRequested) {
    const float fRetry = lastFreq * calibration_interval_ratio();
    Serial.println((String)"[HIGHEST_FREQ] retry from " + fRetry +
                   " Hz (seed " + fSeed + " gave " + bestFreq + ")");
    bestFreq = find_freq_for_duty50(DIV_COUNTER, fRetry,
                                    calibration_interval_ratio(), true);
  }
  if (bestFreq <= 0.0f) {
    // Keep the table monotonic: the endpoint has to sit above the last measured
    // pair, and one interval up is where the search was looking for it.
    bestFreq = (lastFreq > 0.0f)
                 ? lastFreq * calibration_interval_ratio()
                 : note_to_freq(DCO_calibration_current_note);
    Serial.println((String)"[HIGHEST_FREQ] no valid signal in search window; using " + bestFreq);
  }

  Serial.println((String)"Highest freq found: " + bestFreq +
                 freq_trace_quality(g_lastFreqBisectGapUs, bestFreq,
                                    g_lastFreqBisectProbes, g_lastSettleChecks) +
                 " (seed=" + fSeed + ")");

  // Report the nearest note at/below the found frequency.
  constexpr int kNoteCount = (int)(sizeof(sNotePitches) / sizeof(sNotePitches[0]));
  for (int i = 0; i < kNoteCount - 1; i++) {
    if (bestFreq >= sNotePitches[i] && bestFreq < sNotePitches[i + 1]) {
      Serial.println((String)"Highest note found: " + i + (String)" - Note freq: " + sNotePitches[i]);
      break;
    }
  }

  return bestFreq * 100.0f;
}

// Defined further down with the rest of the FREQ_TRACE model helpers, used
// here and by calibrate_DCO_freq_trace().
static float amp0_fit_freq(const float *amps, const float *freqs, int count);

// Estimate the lowest reachable frequency for the current DCO using the
// latest [freq -> PWM] calibration data, assuming an amp compensation
// (range PWM) of 0. This is conceptually symmetric to find_highest_freq(),
// but instead of a live search we derive the estimate from the stored table.
//
// The estimate is a least-squares line through the lowest table pairs
// (amp0_fit_freq(): the bottom of the curve is measured to be linear), with
// the historical 3-point quadratic kept only as the fallback when too few
// distinct pairs exist for a fit.
//
// Return value: estimated lowest frequency * 100 (same units as
// calibrationData[] entries and find_highest_freq()).
float find_lowest_freq() {
  // Use amp compensation (range PWM) = 0 as requested.
  ampCompCalibrationVal = 0;

  // We require at least three calibration points (six entries). The layout of
  // calibrationData is:
  //   [0]  reserved / lowestFreq placeholder
  //   [1]  reserved
  //   [2]  freq0 * 100
  //   [3]  pwm0
  //   [4]  freq1 * 100
  //   [5]  pwm1
  //   [6]  freq2 * 100
  //   [7]  pwm2
  //   ...
  //
  // If we don't have enough data, just return 0.
  if (chanLevelVoiceDataSize < 8) {
    return 0.0f;
  }

  // Collect the bottom pairs for the fit. The pairs are stored ascending, so
  // the first ones are the lowest; the helper picks the lowest-amp ones and
  // applies its own spread rule. The top sentinel (20 MHz) never enters
  // because collection stops well before it, and synthetic amp-0 entries are
  // skipped explicitly.
  float fitAmps[kAmp0FitPoints + 3];
  float fitFreqs[kAmp0FitPoints + 3];
  int   fitCount = 0;
  for (int j = 2;
       j + 1 < chanLevelVoiceDataSize &&
       fitCount < (int)(sizeof(fitAmps) / sizeof(fitAmps[0]));
       j += 2) {
    const float fHz = (float)calibrationData[j] / 100.0f;
    const float amp = (float)calibrationData[j + 1];
    if (!(fHz > 0.0f) || !(amp > 0.0f)) continue;
    fitAmps[fitCount]  = amp;
    fitFreqs[fitCount] = fHz;
    ++fitCount;
  }
  const float fitHz = amp0_fit_freq(fitAmps, fitFreqs, fitCount);
  if (fitHz > 0.0f) {
    Serial.println((String)"[LOWEST_FREQ_EST] DCO=" + currentDCO +
                   (String)" estFreq*100=" + (fitHz * 100.0f) +
                   (String)" from least-squares fit");
    return fitHz * 100.0f;
  }

  float f0 = (float)calibrationData[2];  // already freq * 100
  float p0 = (float)calibrationData[3];
  float f1 = (float)calibrationData[4];
  float p1 = (float)calibrationData[5];
  float f2 = (float)calibrationData[6];
  float p2 = (float)calibrationData[7];

  // Guard against degenerate cases where the PWMs are identical.
  if (p0 == p1 || p1 == p2 || p0 == p2) {
    // Fall back to a simple linear extrapolation using the first segment.
    float y = linearInterpolation(p0, f0, p1, f1, 0.0f);
    return y;
  }

  // No usable fit: fall back to the quadratic in the space PWM -> (freq * 100)
  // evaluated at PWM = 0.
  float estFreqTimes100 = quadraticInterpolation(
    p0, f0,
    p1, f1,
    p2, f2,
    0.0f
  );

  // Clamp to a sensible minimum to avoid negative or zero frequencies
  // from extreme extrapolation.
  if (estFreqTimes100 < 0.0f) {
    estFreqTimes100 = 0.0f;
  }

  Serial.println((String)"[LOWEST_FREQ_EST] DCO=" + currentDCO +
                 (String)" estFreq*100=" + estFreqTimes100 +
                 (String)" using PWM points {" + p0 + "," + p1 + "," + p2 + "}");

  return estFreqTimes100;
}

// Search window for the amp-0 hunt: the seed is an extrapolation rather than a
// measurement, so allow roughly an octave in either direction. The caller's
// bounds are what really contain the search; this only sizes its first step.
static constexpr float kLowestFreqWindowRatio = 2.0f;

// Where the amp-comp-0 point may be: under the first measured pair, down to
// kAmp0BandRatio below it, and never under kAmp0MinFreqHz. Both amp-comp methods
// bound the scan, the search and what they are willing to store to this.
static FreqSearchBounds amp0_search_band(float firstPairHz) {
  const float hi = firstPairHz * 0.99f;
  float       lo = firstPairHz / kAmp0BandRatio;
  if (lo < kAmp0MinFreqHz) {
    lo = kAmp0MinFreqHz;
  }
  if (!(lo < hi)) {
    // A first pair this low is already at the edge of what a duty reading can
    // resolve; leave a sliver of band rather than an empty one.
    lo = hi / 1.05f;
  }
  return { lo, hi };
}

// One quick duty reading at freqHz: write the frequency, wait, take a single
// averaged reading. This is measure_duty_at_freq() without the adaptive settle -
// deliberately, because a scan wants one number per point, not the two or three
// re-readings that function spends proving a point has stopped moving. The wait
// is still at least a whole period: down here kAmp0ScanSettleMs alone is a
// fraction of one, and what comes back then describes the previous frequency.
// Same sign and duty-trim convention as measure_duty_at_freq().
// Returns kGapTimeoutSentinel when nothing pulsed.
static float scan_duty_at_freq(float freqHz, uint16_t amp) {
  // Keep the logging global in sync, as find_freq_for_duty50() does: without
  // this the [GAP_MEASURE]/[GAP_TIMEOUT] lines of the scan report whatever amp
  // the previous stage drove (the top endpoint's 14000), not the scan's own.
  ampCompCalibrationVal = amp;
  gapGateFreqHz = freqHz;
  drive_freq(freqHz, amp);
  uint32_t settleMs = kAmp0ScanSettleMs;
  if (freqHz > 0.0f) {
    const uint32_t onePeriodMs = (uint32_t)(1000.0f / freqHz) + 1;
    if (onePeriodMs > settleMs) {
      settleMs = onePeriodMs;
    }
  }
  delay(settleMs);
  ++calRunProbes;
  const GapMeasurement gm = measure_gap(3);
  gapGateFreqHz = 0.0f;
  if (gm.timedOut) {
    return kGapTimeoutSentinel;
  }
  return -(gm.value - duty_trim_gap_us(currentDCO, freqHz));
}

// Walk kAmp0ScanPoints frequencies down through the band at amp comp 0, looking
// for two readings of opposite sign: that pair brackets the answer, and a
// bracket is worth far more to the search than any single seed - it can only
// interpolate inward from there, and no probe can wander into a region already
// known to be silent. Returns the bracket (the whole band when only one sign
// showed up) and, through seedOut, where the search should start: the secant
// crossing of the bracket when there is one, else the point that read closest to
// 50%, else the caller's fallback.
//
// Descending, because the top of the band is the most likely to pulse and
// because the first point that reads "too low" is the lower edge - the duty only
// grows further down, so nothing under it can close the bracket. A silent point,
// on the other hand, is no evidence about what is under it and does not stop the
// descent: the pulse can be lost at either end (above by the amplitude collapsing
// under the comparator threshold, below by the duty going so lopsided that a
// segment outlasts the reading deadline).
static FreqSearchBounds amp0_prescan(FreqSearchBounds band, float fallbackHz,
                                     float *seedOut) {
  *seedOut = fallbackHz;
  g_lastAmp0ScanSeedHz = 0.0f;
  if (!(band.loHz > 0.0f && band.hiHz > band.loHz) || kAmp0ScanPoints < 2) {
    return band;
  }

  const double ratio = pow((double)band.loHz / (double)band.hiHz,
                           1.0 / (double)(kAmp0ScanPoints - 1));
  float  bestFreq = 0.0f;
  float  bestGap  = 0.0f;
  bool   found    = false;
  // Tightest bracket seen so far: aboveFreq read "frequency too high" (gap > 0),
  // belowFreq read "too low", so the answer is between them.
  float  aboveFreq = 0.0f, aboveGap = 0.0f;
  float  belowFreq = 0.0f, belowGap = 0.0f;
  double f         = (double)band.hiHz;

  for (int i = 0; i < kAmp0ScanPoints; ++i, f *= ratio) {
    if (calibrationCancelRequested) {
      break;
    }
    const float gap = scan_duty_at_freq((float)f, 0);
    if (gap == kGapTimeoutSentinel) {
      // The sentinel covers a real timeout but also a reading the gap gates
      // discarded (one-sided, off-period) - the pin may well have pulsed.
      Serial.println((String)"[AMP0_SCAN] f=" + fmt_freq((float)f) + " no usable reading");
      continue;
    }
    Serial.println((String)"[AMP0_SCAN] f=" + fmt_freq((float)f) + " dutyErr=" +
                   String(duty_err_pct_from_gap(gap, (float)f), 2) + "%");
    if (!found || fabsf(gap) < fabsf(bestGap)) {
      bestFreq = (float)f;
      bestGap  = gap;
      found    = true;
    }
    if (gap > 0.0f) {
      // Too high: the tightest such point is the bracket's upper edge so far.
      aboveFreq = (float)f;
      aboveGap  = gap;
      continue;
    }
    // Too low, and the scan is descending, so every point under this one reads
    // the same way: this is the lower edge and there is no reason to go on.
    belowFreq = (float)f;
    belowGap  = gap;
    break;
  }

  if (belowFreq > 0.0f && aboveFreq > belowFreq && aboveGap > belowGap) {
    // Secant crossing in log frequency: duty error against log f is close enough
    // to a straight line over a bracket this small that this is usually within a
    // few cents of the answer - and unlike bestFreq it is a point the search has
    // not already measured.
    const double t = (double)(-belowGap) / (double)(aboveGap - belowGap);
    *seedOut = (float)((double)belowFreq *
                       pow((double)aboveFreq / (double)belowFreq, t));
    g_lastAmp0ScanSeedHz = *seedOut;
    Serial.println((String)"[AMP0_SCAN] bracketed " + fmt_freq(belowFreq) + ".." +
                   fmt_freq(aboveFreq) + " Hz; seeding the search at " +
                   fmt_freq(*seedOut) + " Hz");
    return { belowFreq, aboveFreq };
  }

  if (!found) {
    Serial.println((String)"[AMP0_SCAN] no usable reading anywhere in " +
                   fmt_freq(band.loHz) + ".." + fmt_freq(band.hiHz) +
                   " Hz; seeding the search at " + fmt_freq(fallbackHz));
    return band;
  }
  *seedOut = bestFreq;
  Serial.println((String)"[AMP0_SCAN] no sign change in " + fmt_freq(band.loHz) + ".." +
                 fmt_freq(band.hiHz) + " Hz; seeding the search at " + fmt_freq(bestFreq) +
                 " Hz (dutyErr=" +
                 String(duty_err_pct_from_gap(bestGap, bestFreq), 2) + "%)");
  return band;
}

// Measure (instead of extrapolating) the lowest usable frequency: fix the
// amp comp at 0 and search the frequency at which the duty is 50%, within
// bounds. Every amp-comp-0 point in the firmware comes through here, so the scan
// that finds where the oscillator pulses at all happens once, here: it hands the
// search a bracket to work inside and a seed on the line between its two edges,
// which is also why the search needs no hint about what a timeout means down
// here. freqSeedHz is only what is left if the scan finds no pulse at all.
// Returns the frequency in Hz, or 0 when amp 0 gives no usable signal at all.
float measure_lowest_freq_at_amp0(float freqSeedHz, const FreqSearchBounds *bounds) {
  if (freqSeedHz <= 0.0f) {
    return 0.0f;
  }
  if (bounds == nullptr) {
    return find_freq_for_duty50(0, freqSeedHz, kLowestFreqWindowRatio, true);
  }
  float                  seedHz  = freqSeedHz;
  const FreqSearchBounds bracket = amp0_prescan(*bounds, freqSeedHz, &seedHz);
  return find_freq_for_duty50(0, seedHz, kLowestFreqWindowRatio, true, &bracket);
}

// Replace the table's amp-comp-0 anchor (entry [0..1]) with a measured point.
// Classic method only: FREQ_TRACE measures its own bottom endpoint. The
// classic top-out path leaves an extrapolated frequency there, and a classic
// run that never reaches full amp comp leaves the restart_DCO_calibration()
// placeholder (freq 0, amp comp ampCompLowestFreqVal).
// Seed and result are both held inside amp0_search_band() below the first real
// pair, for the reasons given at the FREQ_TRACE bottom endpoint: below the
// oscillator's pulse floor there is nothing to measure at any frequency, and an
// extrapolation is a better entry 0 than a frequency that cannot be produced.
// That also keeps the table monotonic.
void apply_measured_lowest_freq(DCOCalibrationContext& ctx) {
  const float prevHz = (float)ctx.calibrationData[0] / 100.0f;
  if (ctx.calibrationData[2] == 0) {
    Serial.println((String)"[LOWEST_FREQ] DCO=" + ctx.dcoIndex +
                   " no first pair to bound the search; keeping entry 0 as is");
    return;
  }

  const float            firstPairHz = (float)ctx.calibrationData[2] / 100.0f;
  const FreqSearchBounds bounds      = amp0_search_band(firstPairHz);
  const float            floorHz     = bounds.loHz;
  const float            ceilHz      = bounds.hiHz;

  // Seed with the estimate the method left behind when it is inside the band,
  // otherwise one interval below the first pair. find_lowest_freq()'s quadratic
  // extrapolation to PWM 0 is not used as a seed: it is fitted to the three
  // lowest points and, aimed past all of them, is exactly where it is least
  // reliable.
  float seedHz = prevHz;
  if (!(seedHz >= floorHz && seedHz <= ceilHz)) {
    const float clamped = (seedHz < floorHz) ? floorHz : ceilHz;
    Serial.println((String)"[LOWEST_FREQ] DCO=" + ctx.dcoIndex +
                   " seed " + seedHz + " -> " + clamped +
                   " Hz (band " + floorHz + ".." + ceilHz + ")");
    seedHz = clamped;
  }

  const float foundHz = measure_lowest_freq_at_amp0(seedHz, &bounds);
  if (foundHz <= 0.0f) {
    Serial.println((String)"[LOWEST_FREQ] DCO=" + ctx.dcoIndex +
                   " no pulse at amp 0 anywhere in " + fmt_freq(floorHz) + ".." +
                   fmt_freq(ceilHz) +
                   " Hz (seed=" + fmt_freq(seedHz) + "); keeping estimate " +
                   fmt_freq(prevHz));
    return;
  }

  const float    foundErr      = duty_err_pct_from_gap(g_lastFreqBisectGapUs, foundHz);
  const uint32_t foundTimes100 = (uint32_t)(foundHz * 100.0f);
  if (!(foundHz >= floorHz && foundHz <= ceilHz) || foundTimes100 == 0 ||
      fabsf(foundErr) > kEndpointAcceptDutyPct) {
    Serial.println((String)"[LOWEST_FREQ] DCO=" + ctx.dcoIndex +
                   " rejected: best " + fmt_freq(foundHz) + " Hz dutyErr=" +
                   String(foundErr, 2) + "% (band " + fmt_freq(floorHz) + ".." +
                   fmt_freq(ceilHz) +
                   ", accept " + kEndpointAcceptDutyPct +
                   "%); keeping estimate " + fmt_freq(prevHz));
    return;
  }

  ctx.calibrationData[0] = foundTimes100;
  ctx.calibrationData[1] = 0;
  cal_report_set_pair_from_gap(0, g_lastFreqBisectGapUs, foundHz,
                               CAL_SRC_ENDPOINT_AMP0);
  Serial.println((String)"[LOWEST_FREQ] DCO=" + ctx.dcoIndex +
                 " amp=0 freq=" + fmt_freq(foundHz) +
                 " gapUs=" + g_lastFreqBisectGapUs +
                 " probes=" + g_lastFreqBisectProbes +
                 " settle=" + g_lastSettleChecks +
                 " (seed=" + fmt_freq(seedHz) + ", was " + fmt_freq(prevHz) + ")");
}

// Build the [frequency -> amplitude PWM] calibration table for the DCO in ctx.
// For each calibration note it:
//  - Picks an initial PWM guess (via interpolation),
//  - Searches locally for the PWM that makes the duty error closest to zero,
//  - Stores the best PWM together with the note frequency in ctx.calibrationData.
// dutyErrorFraction controls how much duty-cycle error (e.g. 0.005 = 0.5%)
// is tolerated before the search stops for each note.
void calibrate_DCO(DCOCalibrationContext& ctx, double dutyErrorFraction) {

  const int rangeSamples = 2;  // Number of neighbour voltages to probe around a sign change.
  const int numPresetVoltages = chanLevelVoiceDataSize;  // Size of the [freq, pwm] table.

  // Per-note search guards: a dead oscillator or an unreachable tolerance
  // must not hang the whole calibration run.
  const int           kMaxSearchIterations   = 300;
  const unsigned long kMaxNoteSearchMs       = 30000;
  const int           kMaxConsecutiveTimeouts = 20;

  for (int j = 4; j < numPresetVoltages; j += 2) {  // Start from the 3rd preset voltage

    if (calibrationCancelRequested) {
      return;  // caller (DCO_calibration) discards the partial table
    }

    ctx.currentNote = DCO_calibration_start_note + (calibration_note_interval * (j - 4) / 2);
    VOICE_NOTES[0] = ctx.currentNote;
    uint16_t currentAmpCompCalibrationVal = compute_initial_amp_for_note(ctx, j);

    if (currentAmpCompCalibrationVal > DIV_COUNTER * 0.98) {
      // When we hit the top of the usable PWM range, stop the table here.
      // Record the highest reachable frequency at the current PWM, and also
      // estimate the lowest reachable frequency at PWM=0 so that the first
      // table entry remains a true "lowest note" anchor.
      // j/2 pairs are measured at this point, and they are what seeds the
      // endpoint search.
      float highestFreqFound = find_highest_freq(ctx, j / 2);  // Hz * 100
      float lowestFreqCalc   = find_lowest_freq();   // Hz * 100, at PWM=0

      // Store the highest reachable point at this index.
      ctx.calibrationData[j]     = (uint32_t)highestFreqFound;
      ctx.calibrationData[j + 1] = DIV_COUNTER;
      cal_report_set_pair_from_gap(j / 2, g_lastFreqBisectGapUs,
                                   highestFreqFound / 100.0f,
                                   CAL_SRC_ENDPOINT_FULL);

      // Ensure entry 0 continues to represent the lowest frequency at PWM=0.
      ctx.calibrationData[0] = (uint32_t)lowestFreqCalc;
      ctx.calibrationData[1] = 0;

      for (int i = j + 2; i < numPresetVoltages; i += 2) {
        ctx.calibrationData[i] = 20000000;
        ctx.calibrationData[i + 1] = DIV_COUNTER;
        cal_report_set_pair(i / 2, kCalDutyErrUnknown, CAL_SRC_SENTINEL);
      }
      break;
    }

    const uint16_t minAmpComp = currentAmpCompCalibrationVal * 0.8;  // Lower limit for this note.
    const uint16_t maxAmpComp = currentAmpCompCalibrationVal * 1.3;  // Upper limit for this note.

    const double freqHz = note_to_freq(VOICE_NOTES[0]);
    double tolerance = compute_gap_tolerance_for_freq(freqHz, dutyErrorFraction);

    // For debugging, report the effective duty-cycle tolerance in percent.
    const double periodUs = (freqHz > 0.0) ? (1000000.0 / freqHz) : 0.0;
    double toleranceDutyPercent = 0.0;
    if (periodUs > 0.0) {
      toleranceDutyPercent = (tolerance / (2.0 * periodUs)) * 100.0;
    }

    Serial.println((String) "Current DCO: " + ctx.dcoIndex);
    Serial.println((String) "Calibration note: " + VOICE_NOTES[0]);
    Serial.println((String) "Calibration note freq: " + freqHz);
    Serial.println((String) "Calibration note amplitude: " + currentAmpCompCalibrationVal);
    Serial.println((String) "Tolerance (us): " + tolerance);
    Serial.println((String) "Tolerance duty approx (%): " + toleranceDutyPercent);
    Serial.println((String) "MinAmpComp: " + minAmpComp);
    Serial.println((String) "MaxAmpComp: " + maxAmpComp);

    voice_task_autotune(0, currentAmpCompCalibrationVal);  // Send the preset voltage
    delay(10);

    uint16_t bestAmpComp = currentAmpCompCalibrationVal;  // Best PWM found so far for this note.
    float closestToZero = 50000;   // Smallest absolute duty error seen so far.
    float previousAvgValue = 0.0;  // Duty error from the previous iteration (for sign-change detection).

    float lowerMeasurements[rangeSamples];   // Duty errors measured at lower neighbour PWMs.
    float higherMeasurements[rangeSamples];  // Duty errors measured at higher neighbour PWMs.
    uint16_t lowerVoltages[rangeSamples];    // PWM values used for lowerMeasurements[].
    uint16_t higherVoltages[rangeSamples];   // PWM values used for higherMeasurements[].

    int flipCounter = 0;  // Count of successive sign changes; used to relax tolerance if the search oscillates.
    int consecutiveTimeouts = 0;
    unsigned long noteSearchStartMs = millis();

    for (int iteration = 0;; ++iteration) {
      if (calibrationCancelRequested) {
        break;  // note loop head returns to the caller
      }
      if (iteration >= kMaxSearchIterations ||
          (millis() - noteSearchStartMs) > kMaxNoteSearchMs) {
        Serial.println((String)"[DCO_AMP_GUARD] note=" + ctx.currentNote +
                       (String)" DCO=" + ctx.dcoIndex +
                       (String)" search guard tripped after " + iteration +
                       (String)" iterations; keeping best AMP=" + bestAmpComp);
        break;
      }

      float avgValue = measure_gap_for_amp(currentAmpCompCalibrationVal);

      // Optional debug: report current duty and tolerance when enabled.
      // Treat timeout sentinel specially so we don't fake a 50% duty reading.
      if (autotuneDebug >= 2 && periodUs > 0.0) {
        if (avgValue == kGapTimeoutSentinel) {
          Serial.println((String)"[DCO_AMP_SCAN] note=" + ctx.currentNote +
                         (String)" DCO=" + ctx.dcoIndex +
                         (String)" AMP=" + currentAmpCompCalibrationVal +
                         (String)" gap=TIMEOUT" +
                         (String)" duty=NA target=50% tol≈" + toleranceDutyPercent + "%");
        } else {
          // avgValue sign convention: positive => amplitude too low.
          double dutyErrorFrac = (double)avgValue / (2.0 * periodUs);
          double dutyPercent   = (0.5 + dutyErrorFrac) * 100.0;
          Serial.println((String)"[DCO_AMP_SCAN] note=" + ctx.currentNote +
                         (String)" DCO=" + ctx.dcoIndex +
                         (String)" AMP=" + currentAmpCompCalibrationVal +
                         (String)" gap=" + avgValue +
                         (String)"us duty=" + dutyPercent +
                         (String)"% target=50% tol≈" + toleranceDutyPercent + "%");
        }
      }

      // Timeout: no usable signal at this PWM. The most common cause is an
      // amplitude too low for the calibration comparator, so nudge the PWM up
      // one step and measure again. previousAvgValue is deliberately left
      // untouched so the sentinel cannot fake a sign change, and the sentinel
      // is never allowed into the best-candidate tracking below.
      if (avgValue == kGapTimeoutSentinel) {
        ++consecutiveTimeouts;
        if (consecutiveTimeouts >= kMaxConsecutiveTimeouts) {
          Serial.println((String)"[DCO_AMP_GUARD] note=" + ctx.currentNote +
                         (String)" DCO=" + ctx.dcoIndex +
                         (String)" too many consecutive timeouts; keeping best AMP=" + bestAmpComp);
          break;
        }
        if (currentAmpCompCalibrationVal < maxAmpComp) {
          currentAmpCompCalibrationVal += 1;
        }
        continue;
      }
      consecutiveTimeouts = 0;

      // Update best candidate if this measurement is closer to zero.
      if (abs(avgValue) < abs(closestToZero)) {
        closestToZero = avgValue;
        bestAmpComp = currentAmpCompCalibrationVal;
      }

      // Detect sign change
      if (did_sign_change(previousAvgValue, avgValue)) {
        // Store measurements around the current voltage
        for (int i = 0; i < rangeSamples; i++) {
          uint16_t lowerVoltage = currentAmpCompCalibrationVal - (i + 1);
          uint16_t higherVoltage = currentAmpCompCalibrationVal + (i + 1);

          lowerMeasurements[i] = measure_gap_for_amp(lowerVoltage);
          lowerVoltages[i] = lowerVoltage;

          higherMeasurements[i] = measure_gap_for_amp(higherVoltage);
          higherVoltages[i] = higherVoltage;
        }

        update_best_from_neighbours(
          rangeSamples,
          lowerMeasurements,
          lowerVoltages,
          higherMeasurements,
          higherVoltages,
          avgValue,
          closestToZero,
          bestAmpComp,
          currentAmpCompCalibrationVal
        );

        // Break the loop if the closest value is within tolerance
        if (abs(closestToZero) <= tolerance) {
          break;
        } else {
          tolerance = tolerance * 1.2;
        }
        flipCounter++;
        if (flipCounter >= 3 && abs(closestToZero) <= tolerance * 2) {
          break;
        } else {
          tolerance = tolerance * 1.5;
        }
      }

      // Step the PWM toward the target and enforce the allowed search window.
      // Stepping is done in int32 so it cannot wrap below zero.
      int32_t nextAmp = (int32_t)currentAmpCompCalibrationVal + step_amp_from_error(avgValue, tolerance);
      if (nextAmp < (int32_t)minAmpComp) nextAmp = (int32_t)minAmpComp;
      if (nextAmp > (int32_t)maxAmpComp) nextAmp = (int32_t)maxAmpComp;

      if ((uint16_t)nextAmp == currentAmpCompCalibrationVal &&
          (nextAmp == (int32_t)minAmpComp || nextAmp == (int32_t)maxAmpComp)) {
        // Stuck at a search bound with the error still pushing outward:
        // the target is not reachable inside the window; keep the best found.
        Serial.println((String)"[DCO_AMP_GUARD] note=" + ctx.currentNote +
                       (String)" DCO=" + ctx.dcoIndex +
                       (String)" stuck at bound AMP=" + currentAmpCompCalibrationVal +
                       (String)"; keeping best AMP=" + bestAmpComp);
        break;
      }
      currentAmpCompCalibrationVal = (uint16_t)nextAmp;

      previousAvgValue = avgValue;
    }

    store_note_result(ctx, j, bestAmpComp, closestToZero);
  }
}

// =============================================================================
// FREQ_TRACE amp-comp calibration (method B, PARAM_DEBUG_COMMAND 35).
//
// The calibration table is one monotonic curve: freq(amp comp) at 50% duty.
// Instead of fixing a note frequency and hunting the integer amp comp (classic
// method above), fix the amp comp and bisect the frequency — the PIO divider gives
// near-continuous frequency resolution, so every stored pair is exact.
// =============================================================================

// Extrapolate y at targetX from up to three known curve points, ordered
// nearest-first (x1/y1 is the closest to the target). Mirrors the classic
// initial-guess strategy: 1 point -> proportional scaling (freq and amp are
// roughly proportional), 2 -> logarithmic, 3 -> quadratic. Works in either
// direction of the freq(amp) curve, so x/y can be freq/amp or amp/freq.
static float extrapolate_amp_for_freq(
  int nPoints,
  float x1, float y1,
  float x2, float y2,
  float x3, float y3,
  float targetX
) {
  if (nPoints >= 3) {
    return quadraticInterpolation(x3, y3, x2, y2, x1, y1, targetX);
  }
  if (nPoints == 2) {
    return (float)logarithmicInterpolation(x2, y2, x1, y1, targetX);
  }
  return (x1 > 0.0f) ? y1 * (targetX / x1) : y1;
}

// Minimum relative separation (in x) between the points chosen by
// freq_trace_guess(); closer candidates are treated as one point.
static constexpr float kGuessMinSpread = 0.10f;

// Least-squares quadratic through 4+ points, evaluated at targetX. An exact
// fit through 4 points would be a cubic - worse behaved than the quadratic it
// replaces when extrapolating - so the 4th point is used as redundancy
// instead: the fit averages the noise of any single measurement rather than
// reproducing it exactly in the coefficients, the way the exact 3-point
// quadratic does.
//
// Fitted in u = x - targetX so the intercept c0 is directly y(targetX), and in
// double throughout: the normal equations carry sums up to u^4, and with amps
// reaching 14000 those overflow what a float sum can resolve. Returns NAN on a
// degenerate system (x values nearly collinear after centering); the caller
// falls back to the exact 3-point path.
static float lsq_quadratic(const float *xs, const float *ys,
                           const int *idx, int n, float targetX) {
  double s0 = (double)n;
  double s1 = 0.0, s2 = 0.0, s3 = 0.0, s4 = 0.0;
  double t0 = 0.0, t1 = 0.0, t2 = 0.0;
  for (int k = 0; k < n; ++k) {
    const double u = (double)xs[idx[k]] - (double)targetX;
    const double y = (double)ys[idx[k]];
    const double u2 = u * u;
    s1 += u;
    s2 += u2;
    s3 += u2 * u;
    s4 += u2 * u2;
    t0 += y;
    t1 += u * y;
    t2 += u2 * y;
  }

  // Normal equations for y = c0 + c1*u + c2*u^2, solved by Cramer's rule:
  //   | s0 s1 s2 | | c0 |   | t0 |
  //   | s1 s2 s3 | | c1 | = | t1 |
  //   | s2 s3 s4 | | c2 |   | t2 |
  const double det = s0 * (s2 * s4 - s3 * s3)
                   - s1 * (s1 * s4 - s3 * s2)
                   + s2 * (s1 * s3 - s2 * s2);
  // Scale-aware degeneracy test: det has units of u^6, so compare it against
  // the point spread at the same power rather than a fixed epsilon.
  const double scale = s2 / (double)n;  // ~ mean squared spread of u
  if (!(det > 1e-9 * scale * scale * scale)) {
    return NAN;
  }

  const double c0 = (t0 * (s2 * s4 - s3 * s3)
                   - s1 * (t1 * s4 - s3 * t2)
                   + s2 * (t1 * s3 - s2 * t2)) / det;

  if (autotuneDebug >= 2) {
    Serial.println((String)"[GUESS_LSQ] x=" + targetX + " y=" + (float)c0 +
                   " points=" + n);
  }
  return (float)c0;
}

// Interpolate/extrapolate y(x) through the up-to-4 known points nearest to x.
// xs/ys hold every point measured so far in the current FREQ_TRACE run; the
// helper is used in both directions: amp-for-freq when targeting a ladder
// frequency, and freq-for-amp when seeding a fixed-amp bisection (bootstrap
// probes, downward trace). Points closer than kGuessMinSpread to an already
// chosen one are skipped, which also covers duplicate x values from integer
// PWM quantization (a quadratic fit would divide by zero on those).
// With 4 qualifying points the answer is a least-squares quadratic
// (lsq_quadratic() above); with 3 or fewer the historical exact paths apply
// (quadratic / log / proportional).
static float freq_trace_guess(const float *xs, const float *ys, int count, float x) {
  if (count <= 0) {
    return 0.0f;
  }

  int idx[4];
  int n = 0;

  // A quadratic through three nearly coincident points is meaningless outside
  // the cluster: right after the bootstrap the 4 probes sit within +/-6% of the
  // anchor, and fitting them alone sent the first ladder guesses ~25% off.
  // Points must therefore be spread by at least kGuessMinSpread in x.
  auto far_enough = [&](int cand) {
    for (int k = 0; k < n; ++k) {
      const float a = xs[cand], b = xs[idx[k]];
      const float ref = fmaxf(fabsf(a), fabsf(b));
      if (ref <= 0.0f || fabsf(a - b) < kGuessMinSpread * ref) {
        return false;
      }
    }
    return true;
  };

  // side: -1 = only points below x, +1 = only above, 0 = either. Nearest wins.
  auto pick = [&](int side) {
    int best = -1;
    for (int i = 0; i < count; ++i) {
      bool used = false;
      for (int k = 0; k < n; ++k) {
        if (idx[k] == i) used = true;
      }
      if (used) continue;
      if (side < 0 && xs[i] > x) continue;
      if (side > 0 && xs[i] < x) continue;
      if (!far_enough(i)) continue;
      if (best < 0 || fabsf(xs[i] - x) < fabsf(xs[best] - x)) {
        best = i;
      }
    }
    return best;
  };

  // Bracket the target first (interpolating beats extrapolating), then fill up
  // to four points with the nearest qualifying ones.
  int cand = pick(-1);
  if (cand >= 0) idx[n++] = cand;
  cand = pick(+1);
  if (cand >= 0) idx[n++] = cand;
  while (n < 4) {
    cand = pick(0);
    if (cand < 0) break;
    idx[n++] = cand;
  }
  if (n == 0) {
    return 0.0f;
  }

  // Nearest-first order: the exact paths below take the closest points, and
  // it matters for the single-point proportional case.
  for (int i = 1; i < n; ++i) {
    for (int j = i; j > 0 && fabsf(xs[idx[j]] - x) < fabsf(xs[idx[j - 1]] - x); --j) {
      const int t = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = t;
    }
  }

  // Four points: least-squares quadratic. On a degenerate fit fall through to
  // the exact quadratic over the three nearest, exactly as with 3 points.
  if (n >= 4) {
    const float fit = lsq_quadratic(xs, ys, idx, n, x);
    if (!isnan(fit)) {
      return fit;
    }
    n = 3;
  }

  return extrapolate_amp_for_freq(
    n,
    xs[idx[0]], ys[idx[0]],
    (n > 1) ? xs[idx[1]] : 0.0f, (n > 1) ? ys[idx[1]] : 0.0f,
    (n > 2) ? xs[idx[2]] : 0.0f, (n > 2) ? ys[idx[2]] : 0.0f,
    x);
}

// Frequency at amp comp 0, from a least-squares line through the lowest-amp
// measured points. The bottom of the freq(amp) curve is measured to be linear
// (pair-to-pair slopes agree within ~1%), and a line through up to
// kAmp0FitPoints of it makes the intercept stable run to run - the 3-point
// quadratic that freq_trace_guess() extrapolates there amplified the noise of
// exactly the noisiest points and swung by whole octaves between runs. The
// intercept also matters more than it looks: entry 0 anchors the runtime
// interpolation for every note below pair 1, and only the fitted intercept
// reproduces the measured slope of that segment.
//
// amps/freqs hold measured points in any order; points closer in amp than
// kGuessMinSpread to one already chosen are skipped (same rule as
// freq_trace_guess()). Returns 0 when no trustworthy fit exists - fewer than
// 2 usable points, non-positive slope, or an intercept at or above the lowest
// fitted frequency - and the caller falls back to the old model.
static float amp0_fit_freq(const float *amps, const float *freqs, int count) {
  int idx[kAmp0FitPoints];
  int n = 0;

  // Pick the lowest-amp points, spread apart: the fit describes the bottom of
  // the curve, and the slight upward bend further up would tilt the intercept.
  while (n < kAmp0FitPoints) {
    int best = -1;
    for (int i = 0; i < count; ++i) {
      bool usable = (amps[i] > 0.0f && freqs[i] > 0.0f);
      for (int k = 0; k < n && usable; ++k) {
        if (idx[k] == i) {
          usable = false;
        } else {
          const float ref = fmaxf(amps[i], amps[idx[k]]);
          if (fabsf(amps[i] - amps[idx[k]]) < kGuessMinSpread * ref) {
            usable = false;
          }
        }
      }
      if (!usable) continue;
      if (best < 0 || amps[i] < amps[best]) {
        best = i;
      }
    }
    if (best < 0) break;
    idx[n++] = best;
  }
  if (n < 2) {
    return 0.0f;
  }

  // Ordinary least squares of freq against amp; the intercept is f(amp = 0).
  double sumA = 0.0, sumF = 0.0, sumAA = 0.0, sumAF = 0.0;
  float  lowestFreq = freqs[idx[0]];
  for (int k = 0; k < n; ++k) {
    const double a = (double)amps[idx[k]];
    const double f = (double)freqs[idx[k]];
    sumA  += a;
    sumF  += f;
    sumAA += a * a;
    sumAF += a * f;
    if (freqs[idx[k]] < lowestFreq) lowestFreq = freqs[idx[k]];
  }
  const double det = (double)n * sumAA - sumA * sumA;
  if (det <= 0.0) {
    return 0.0f;
  }
  const double slope     = ((double)n * sumAF - sumA * sumF) / det;
  const double intercept = (sumF - slope * sumA) / (double)n;
  if (slope <= 0.0 || !(intercept > 0.0) || intercept >= (double)lowestFreq) {
    return 0.0f;
  }

  Serial.println((String)"[AMP0_FIT] DCO=" + currentDCO +
                 (String)" f0=" + fmt_freq((float)intercept) +
                 (String)" Hz slope=" + (float)(1.0 / slope) +
                 (String)" cnt/Hz points=" + n);
  return (float)intercept;
}

// Anchor refinement: the stored ampComp440 is a seed, not the truth. Correct
// it until the 50% duty frequency is within kAnchorToleranceHz of 440 Hz.
// Always try at least one amp correction after acquire when it is off by more
// than that. How many corrections are allowed comes from the precision profile.
static constexpr float kAnchorToleranceHz = 0.1f;

// Bounds (in semitones) for the ladder spacing derived at runtime.
static constexpr int kLadderIntervalMin = 3;
static constexpr int kLadderIntervalMax = 12;

// A rung is stored where it was measured, but a rung that lands far from its
// target frequency makes the ladder uneven. Correct the amp (as many times as
// the precision profile allows) and keep the closest measurement.
static constexpr float kRungToleranceCents = 25.0f;

// How the bisection behind a stored pair went, kept next to the pair so the
// logs and the report describe the measurement that was actually kept.
struct FreqTraceProbeInfo {
  float gapUs;
  int   probes;
  int   settleChecks;
};

// Common tail of the trace log lines: the achieved error in microseconds and
// in duty percent (the unit the scope reads), the probes it took, and how many
// of those went into waiting for the waveform to settle.
static String freq_trace_quality(float gapUs, float freqHz, int probes,
                                 int settleChecks) {
  return (String)" gapUs=" + gapUs +
         " dutyErr=" + duty_err_pct_from_gap(gapUs, freqHz) + "%" +
         " probes=" + probes +
         " settle=" + settleChecks;
}

// Local log-log slope d(log freq) / d(log amp comp) near freqRef, from the two
// nearest known points. Charge current (hence frequency) is roughly
// proportional to amp comp, so 1.0 is the expected value; the clamp keeps a
// noisy pair of points from producing a wild correction.
static float freq_trace_local_slope(const float *freqs, const float *amps,
                                    int count, float freqRef) {
  if (freqRef <= 0.0f) {
    return 1.0f;
  }
  int i1 = -1, i2 = -1;
  for (int i = 0; i < count; ++i) {
    if (freqs[i] <= 0.0f || amps[i] <= 0.0f) continue;
    const float d = fabsf(logf(freqs[i] / freqRef));
    if (i1 < 0 || d < fabsf(logf(freqs[i1] / freqRef))) {
      i2 = i1; i1 = i;
    } else if (i2 < 0 || d < fabsf(logf(freqs[i2] / freqRef))) {
      i2 = i;
    }
  }
  if (i1 < 0 || i2 < 0 || amps[i1] == amps[i2] || freqs[i1] == freqs[i2]) {
    return 1.0f;
  }
  const float s = logf(freqs[i2] / freqs[i1]) / logf(amps[i2] / amps[i1]);
  if (!(s > 0.5f)) return 0.5f;  // also catches NaN
  if (s > 2.0f)    return 2.0f;
  return s;
}

// Frequency a given amp comp should land at, extrapolated as a power law from
// the top of the measured curve (freq ~ amp^s, s from the two highest points).
//
// Used to seed the full-amp endpoint, which is the one probe that sits outside
// the measured range. freq_trace_guess() fits a quadratic in linear (amp, freq)
// space, and evaluated outside its data that fit is ill-conditioned: on a real
// table its three terms came to 4489 - 13593 + 13004 for a 17-cent error, where
// this power law was 6 cents off and stable. The physical curve is much closer
// to a power law than to a parabola.
// Returns 0 when there is nothing to extrapolate from.
static float freq_trace_power_seed(const float *freqs, const float *amps,
                                   int count, float ampTarget) {
  if (ampTarget <= 0.0f) {
    return 0.0f;
  }
  // Anchor on the highest measured amp comp: the endpoint is above all of them,
  // so that point is the nearest one and its slope is the relevant one.
  int top = -1;
  for (int i = 0; i < count; ++i) {
    if (freqs[i] <= 0.0f || amps[i] <= 0.0f) continue;
    if (top < 0 || amps[i] > amps[top]) top = i;
  }
  if (top < 0) {
    return 0.0f;
  }
  const float s = freq_trace_local_slope(freqs, amps, count, freqs[top]);
  return freqs[top] * powf(ampTarget / amps[top], s);
}

// Amp-comp guess at which the ladder stops climbing and leaves the rest of the
// range to the full-amp endpoint probe.
static constexpr float kAmpSaturationFraction = 0.98f;

// A table the runtime can interpolate has to rise in both columns. Whoever
// built it reports under its own tag and the caller skips the FS write, so a
// bad pass never replaces a good table.
static bool cal_table_is_monotonic(const uint32_t *data, int numPairs,
                                   uint8_t dcoIndex, const char *tag) {
  bool ok = true;
  uint32_t prevFreq = data[0];
  uint32_t prevAmp  = data[1];
  for (int p = 1; p < numPairs; ++p) {
    const uint32_t f = data[2 * p];
    const uint32_t a = data[2 * p + 1];
    if (f < prevFreq || a < prevAmp) {
      Serial.println((String)"[" + tag + "] DCO=" + dcoIndex +
                     " non-monotonic at pair " + p +
                     " (freq " + prevFreq + "->" + f +
                     ", amp " + prevAmp + "->" + a + ")");
      ok = false;
    }
    prevFreq = f;
    prevAmp  = a;
  }
  return ok;
}

// Trace the freq(amp comp) curve outward from the 440 Hz manual anchor and
// build the full [frequency -> amp comp] table for the DCO in ctx.
//  - Anchor: the manual operating point (ampComp440 value, bisected ~440 Hz).
//  - Manual trim note: the trimpot operating point measured as a second exact
//    point, ~45 semitones down, so the model has a long baseline.
//  - Bootstrap: 4 probes at fixed amps just above/below the anchor (close
//    enough that the pulse cannot collapse), giving the local curvature.
//  - Ladder: the rung spacing and the anchor's rung are derived from that
//    model so the rungs span the reachable range; each rung extrapolates the
//    amp from the 3 nearest known points, fixes it, bisects the frequency and
//    stores the exact pair, upward then downward.
//  - Endpoints last: full amp comp (top) and amp comp 0 (bottom) are the only
//    probes whose frequency is unknown up front and where the pulse can
//    collapse, so they run once the model can seed them within a tight window.
// Returns false when the emitted table fails the monotonicity sanity check;
// the caller must then skip update_FS_voice() and keep the previous table.
bool calibrate_DCO_freq_trace(DCOCalibrationContext& ctx) {
  constexpr int numPairs  = (int)(chanLevelVoiceDataSize / 2);
  constexpr int firstRung = 1;              // pair 0 holds the amp-comp-0 endpoint
  constexpr int lastRung  = numPairs - 2;   // last pair holds the full-amp endpoint
  constexpr int nRungs    = lastRung - firstRung + 1;
  static_assert(nRungs >= 4, "table needs room for a ladder between both endpoints");

  const float r = calibration_interval_ratio();

  // Every point measured in this run (anchor, manual trim note, bootstrap
  // cluster, traced ladder pairs). Both guess directions draw from this set
  // via freq_trace_guess().
  constexpr int kMaxKnown = numPairs + 8;
  float knownFreq[kMaxKnown];
  float knownAmp[kMaxKnown];
  int   knownCount = 0;
  auto add_known = [&](float f, float a) {
    if (knownCount < kMaxKnown) {
      knownFreq[knownCount] = f;
      knownAmp[knownCount]  = a;
      ++knownCount;
    }
  };

  float    freqByPair[numPairs];
  uint16_t ampByPair[numPairs];

  // --- Anchor at the manual 440 Hz operating point --------------------------
  // ampComp440[] is set by the user during manual calibration step 2 (param
  // PARAM_AMP_COMP_440) and persisted in FS. 0 = never set: refuse to trace
  // rather than guessing an anchor.
  uint16_t anchorAmp = ampComp440[ctx.dcoIndex];
  if (anchorAmp == 0) {
    Serial.println((String)"[FREQ_TRACE_GUARD] DCO=" + ctx.dcoIndex +
                   " 440 Hz anchor not set - run manual calibration step 2" +
                   " (PARAM_AMP_COMP_440) and store it; aborting (previous table kept)");
    return false;
  }
  // Wide window here only: see kAnchorAcquireWindowRatio. What this probe is
  // for is finding where the dialled amp comp actually sits, not asserting that
  // it sits at 440 Hz - the re-anchor step below is what moves it there.
  float anchorFreq = find_freq_for_duty50(
    anchorAmp, note_to_freq(manual_cal_reference_note),
    kAnchorAcquireWindowRatio, true);
  if (calibrationCancelRequested) {
    return false;
  }
  if (anchorFreq <= 0.0f) {
    // PW is on the line because this probe cannot see a pulse without it: at the
    // rail the comparator has no crossing at any amp or frequency, so a PW far
    // from the stored centre points at the run's setup, not at the anchor.
    const uint8_t anchorPwCh = cal_pw_channel(ctx.dcoIndex);
    Serial.println((String)"[FREQ_TRACE_GUARD] DCO=" + ctx.dcoIndex +
                   " no signal at manual anchor amp=" + anchorAmp +
                   " PW_raw=" + pw_level_readback(anchorPwCh) +
                   " (centre " + PW_CENTER[anchorPwCh] + ")" +
                   "; aborting (previous table kept)");
    return false;
  }
  add_known(anchorFreq, (float)anchorAmp);
  // Kept for the report: every later probe overwrites g_lastFreqBisectGapUs.
  float anchorGapUs = g_lastFreqBisectGapUs;
  Serial.println((String)"[FREQ_TRACE] DCO=" + ctx.dcoIndex +
                 " anchor amp=" + anchorAmp + " freq=" + fmt_freq(anchorFreq) +
                 freq_trace_quality(anchorGapUs, anchorFreq,
                                    g_lastFreqBisectProbes, g_lastSettleChecks));

  // --- Manual trim operating point ------------------------------------------
  // The trimpot stage runs at manual_DCO_calibration_start_note with
  // initManualAmpCompCalibrationVal + manualCalibrationOffset: a second point
  // the user set by hand, about 45 semitones below the anchor. Measuring it
  // gives the model a long baseline (slope) to go with the anchor cluster
  // (curvature), and reports how far the trim actually sits from the nominal
  // note. It only feeds the model - it is never forced into a table slot - so
  // a failure here is not fatal.
  {
    int32_t manualAmp = (int32_t)ctx.initManualAmpByOsc[ctx.dcoIndex] +
                        (int32_t)ctx.manualOffsetByOsc[ctx.dcoIndex];
    if (manualAmp < 1) manualAmp = 1;
    if (manualAmp > (int32_t)DIV_COUNTER) manualAmp = (int32_t)DIV_COUNTER;

    const float nominalHz = note_to_freq(manual_DCO_calibration_start_note);
    float found = find_freq_for_duty50(
      (uint16_t)manualAmp, nominalHz, kManualNoteWindowRatio, true);
    if (calibrationCancelRequested) {
      return false;
    }
    if (found > 0.0f) {
      add_known(found, (float)manualAmp);
      const float cents = 1200.0f * log2f(found / nominalHz);
      Serial.println((String)"[FREQ_TRACE_MANUAL] DCO=" + ctx.dcoIndex +
                     " amp=" + manualAmp + " freq=" + fmt_freq(found) +
                     " nominal=" + nominalHz + " dev=" + cents + " cents" +
                     freq_trace_quality(g_lastFreqBisectGapUs, found,
                                        g_lastFreqBisectProbes,
                                        g_lastSettleChecks));
    } else {
      Serial.println((String)"[FREQ_TRACE_MANUAL] DCO=" + ctx.dcoIndex +
                     " no signal at manual trim amp=" + manualAmp +
                     " (nominal=" + nominalHz + "); continuing without it");
    }
  }

  // --- Re-anchor at 440 Hz --------------------------------------------------
  // The dialled ampComp440 is a hand measurement and can sit far from a real
  // 440 Hz operating point; everything downstream (bootstrap cluster, ladder
  // position) assumes the anchor really is at 440 Hz. Correct the amp from the
  // model - which now has the anchor probe plus the long manual-note baseline,
  // exactly the two points a log fit needs - and re-measure. The best candidate
  // wins and is written back, so the next run starts from a true anchor.
  {
    const float target440 = note_to_freq(manual_cal_reference_note);
    const uint16_t storedAmp = anchorAmp;
    float cents = 1200.0f * log2f(anchorFreq / target440);
    int tries = cal_precision().anchorTries;
    if (tries < 1) tries = 1;

    for (int attempt = 0;
         attempt < tries &&
         fabsf(anchorFreq - target440) > kAnchorToleranceHz;
         ++attempt) {
      if (calibrationCancelRequested) {
        return false;
      }
      float ampGuess = freq_trace_guess(knownFreq, knownAmp, knownCount, target440);
      int32_t ampNext = (int32_t)lroundf(ampGuess);
      if (ampNext < 1) ampNext = 1;
      if (ampNext > (int32_t)DIV_COUNTER) ampNext = (int32_t)DIV_COUNTER;
      if (ampNext == (int32_t)anchorAmp) {
        // The model insists on the amp we already measured: step one count
        // toward the target (frequency rises with amp comp) instead of
        // re-measuring the same point.
        ampNext += (anchorFreq < target440) ? 1 : -1;
        if (ampNext < 1 || ampNext > (int32_t)DIV_COUNTER) {
          break;
        }
      }

      float found = find_freq_for_duty50(
        (uint16_t)ampNext, target440, kAnchorWindowRatio, true);
      if (found <= 0.0f) {
        Serial.println((String)"[FREQ_TRACE_ANCHOR] DCO=" + ctx.dcoIndex +
                       " no signal at amp=" + ampNext + "; keeping amp=" + anchorAmp);
        break;
      }
      add_known(found, (float)ampNext);

      const float newCents = 1200.0f * log2f(found / target440);
      if (fabsf(newCents) < fabsf(cents)) {
        anchorAmp   = (uint16_t)ampNext;
        anchorFreq  = found;
        anchorGapUs = g_lastFreqBisectGapUs;
        cents       = newCents;
      }
    }

    cents = 1200.0f * log2f(anchorFreq / target440);

    Serial.println((String)"[FREQ_TRACE_ANCHOR] DCO=" + ctx.dcoIndex +
                   " stored=" + storedAmp + " refined=" + anchorAmp +
                   " freq=" + fmt_freq(anchorFreq) + " dev=" + cents + " cents" +
                   " (tol=" + kAnchorToleranceHz + " Hz)" +
                   " gapUs=" + anchorGapUs +
                   " dutyErr=" + duty_err_pct_from_gap(anchorGapUs, anchorFreq) + "%");

    if (anchorAmp != storedAmp) {
      ampComp440[ctx.dcoIndex] = anchorAmp;
      update_FS_AmpComp440(ctx.dcoIndex, anchorAmp);
      Serial.println((String)"[FREQ_TRACE_ANCHOR] DCO=" + ctx.dcoIndex +
                     " manual 440 Hz value corrected " + storedAmp + " -> " +
                     anchorAmp + " and persisted");
    }
  }

  // --- Bootstrap cluster around the anchor ----------------------------------
  // Probe 4 fixed amps straddling the 440 Hz value, kBootstrapSemitones either
  // side of it, before touching the ladder. Each probe's frequency seed comes
  // from the points measured so far (1: proportional, 2: log, 3+: quadratic), so
  // the curve model improves with every point and the first real ladder
  // extrapolations are backed by 4-5 measurements around the anchor. A FAST
  // run takes only the inner straddle (+/-3 semitones): the LSQ-quadratic
  // guess recovers the curve's shape from the rungs themselves soon enough.
  {
    const int nBootstrap =
      (calibrationPrecision == CAL_PRECISION_FAST) ? 2 : 4;
    for (int b = 0; b < nBootstrap; ++b) {
      if (calibrationCancelRequested) {
        return false;
      }
      int32_t amp = lroundf((float)anchorAmp *
                            exp2f((float)kBootstrapSemitones[b] / 12.0f));
      if (amp < 1) amp = 1;
      if (amp > (int32_t)DIV_COUNTER) amp = (int32_t)DIV_COUNTER;
      if (amp == (int32_t)anchorAmp) {
        continue;  // anchor amp too small for this interval to change it
      }

      float fSeed = freq_trace_guess(knownAmp, knownFreq, knownCount, (float)amp);
      if (fSeed <= 0.0f) fSeed = anchorFreq;
      float fFound = find_freq_for_duty50((uint16_t)amp, fSeed, r, true);
      if (fFound <= 0.0f) {
        Serial.println((String)"[FREQ_TRACE_BOOT] DCO=" + ctx.dcoIndex +
                       " no signal at amp=" + amp + "; probe skipped");
        continue;
      }
      add_known(fFound, (float)amp);
      Serial.println((String)"[FREQ_TRACE_BOOT] DCO=" + ctx.dcoIndex +
                     " amp=" + amp + " freq=" + fmt_freq(fFound) +
                     freq_trace_quality(g_lastFreqBisectGapUs, fFound,
                                        g_lastFreqBisectProbes,
                                        g_lastSettleChecks));
    }
  }

  // --- Derive the ladder spacing from the measured points -------------------
  // The rungs stay on integer semitones (musical spacing, comparable with the
  // classic tables), but how many semitones apart is a property of this
  // oscillator: ask the model where amp comp 1 and full amp comp land, and
  // spread the available rungs over that span. Degenerate model (no usable
  // estimates): fall back to the compile-time note interval and a centred
  // anchor rung.
  int ladderInterval = calibration_note_interval;
  int anchorPair     = firstRung + (nRungs - 1) / 2;
  {
    // Low end: the amp floor extrapolates below what the duty probe can usefully
    // read (under the manual trim note a segment starts to approach the gap
    // deadline, and every probe that crosses it costs a timeout), so that note is
    // the lowest rung target.
    const float fFloorHz = note_to_freq(manual_DCO_calibration_start_note);
    float fLowEst = freq_trace_guess(knownAmp, knownFreq, knownCount, 1.0f);
    if (!(fLowEst > fFloorHz)) {
      fLowEst = fFloorHz;
    }

    // High end: extrapolating from the cluster to full amp comp is where a
    // quadratic fit can run away. Charge current (hence frequency) is roughly
    // proportional to amp comp, so an estimate more than 2x off from scaling
    // the highest measured point is replaced by that scaling.
    const float ampHigh = (float)DIV_COUNTER * kAmpSaturationFraction;
    float fHighEst = freq_trace_guess(knownAmp, knownFreq, knownCount, ampHigh);
    {
      int iMax = 0;
      for (int i = 1; i < knownCount; ++i) {
        if (knownAmp[i] > knownAmp[iMax]) iMax = i;
      }
      const float prop = (knownAmp[iMax] > 0.0f)
                           ? knownFreq[iMax] * (ampHigh / knownAmp[iMax])
                           : 0.0f;
      if (prop > 0.0f && (fHighEst <= 0.5f * prop || fHighEst >= 2.0f * prop)) {
        fHighEst = prop;
      }
    }

    if (fLowEst > 0.0f && fHighEst > fLowEst) {
      const float spanSemi = 12.0f * log2f(fHighEst / fLowEst);
      int want = (int)ceilf(spanSemi / (float)(nRungs + 1));
      if (want < kLadderIntervalMin) want = kLadderIntervalMin;
      if (want > kLadderIntervalMax) want = kLadderIntervalMax;
      ladderInterval = want;

      // Keep the anchor a real entry on an exact rung, at its own position in
      // log-frequency inside the estimated span.
      float frac = logf(anchorFreq / fLowEst) / logf(fHighEst / fLowEst);
      if (frac < 0.0f) frac = 0.0f;
      if (frac > 1.0f) frac = 1.0f;
      anchorPair = firstRung + (int)lroundf(frac * (float)(nRungs - 1));
      if (anchorPair < firstRung) anchorPair = firstRung;
      if (anchorPair > lastRung)  anchorPair = lastRung;

      Serial.println((String)"[FREQ_TRACE] DCO=" + ctx.dcoIndex +
                     " ladder interval=" + ladderInterval + " semitones" +
                     " anchorPair=" + anchorPair +
                     " span=" + (spanSemi / 12.0f) + " octaves" +
                     " (fLowEst=" + fLowEst + " fHighEst=" + fHighEst + ")");
    } else {
      Serial.println((String)"[FREQ_TRACE] DCO=" + ctx.dcoIndex +
                     " model degenerate (fLowEst=" + fLowEst +
                     " fHighEst=" + fHighEst + "); ladder interval=" +
                     ladderInterval + " anchorPair=" + anchorPair);
    }
  }
  const float ladderRatio = powf(2.0f, (float)ladderInterval / 12.0f);
  calReportLadderInterval = ladderInterval;
  calReportAnchorPair     = anchorPair;

  freqByPair[anchorPair] = anchorFreq;
  ampByPair[anchorPair]  = anchorAmp;
  cal_report_set_pair_from_gap(anchorPair, anchorGapUs, anchorFreq, CAL_SRC_ANCHOR);

  // Even out a rung that landed far from its target: the amp came from an
  // extrapolation, so a wrong guess shows up as a frequency offset. Correct it
  // with the local slope and re-measure once, keeping the closer measurement -
  // the pair stored is always a measured one, never the target.
  auto retry_rung = [&](int p, float fTarget, int32_t& ampFixed, float& found,
                        FreqTraceProbeInfo& info, int32_t ampMin, int32_t ampMax) {
    for (int retry = 0; retry < cal_precision().rungRetries; ++retry) {
      const float cents = 1200.0f * log2f(found / fTarget);
      if (fabsf(cents) <= kRungToleranceCents || calibrationCancelRequested) {
        return;
      }
      const float slope = freq_trace_local_slope(knownFreq, knownAmp, knownCount, fTarget);
      int32_t ampNext = (int32_t)lroundf((float)ampFixed *
                                         powf(fTarget / found, 1.0f / slope));
      if (ampNext == ampFixed) {
        ampNext += (found < fTarget) ? 1 : -1;
      }
      if (ampNext < ampMin || ampNext > ampMax) {
        return;  // the correction would break monotonicity against a neighbour
      }

      const float again = find_freq_for_duty50((uint16_t)ampNext, fTarget,
                                               ladderRatio, true);
      if (again <= 0.0f) {
        return;
      }
      add_known(again, (float)ampNext);
      const float againCents = 1200.0f * log2f(again / fTarget);
      Serial.println((String)"[FREQ_TRACE] DCO=" + ctx.dcoIndex +
                     " pair=" + p + " retry amp=" + ampNext +
                     " freq=" + fmt_freq(again) + " dev=" + againCents + " cents" +
                     " probes=" + g_lastFreqBisectProbes +
                     " settle=" + g_lastSettleChecks +
                     " (was " + cents + " cents at amp=" + ampFixed + ")");
      if (fabsf(againCents) < fabsf(cents)) {
        ampFixed          = ampNext;
        found             = again;
        info.gapUs        = g_lastFreqBisectGapUs;
        info.probes       = g_lastFreqBisectProbes;
        info.settleChecks = g_lastSettleChecks;
      }
    }
  };

  // --- Trace upward --------------------------------------------------------
  int highestTraced = anchorPair;  // highest rung holding a measured point
  for (int p = anchorPair + 1; p <= lastRung; ++p) {
    if (calibrationCancelRequested) {
      return false;
    }
    float fTarget = anchorFreq * powf(ladderRatio, (float)(p - anchorPair));

    float ampGuess = freq_trace_guess(knownFreq, knownAmp, knownCount, fTarget);

    if (ampGuess >= (float)DIV_COUNTER * kAmpSaturationFraction) {
      // Top of the usable amp-comp range: leave the rest to the full-amp
      // endpoint, probed at the end of the run.
      Serial.println((String)"[FREQ_TRACE] DCO=" + ctx.dcoIndex +
                     " amp comp ceiling reached at pair " + p +
                     " (guess=" + ampGuess + "); ladder stops here");
      break;
    }

    // The curve must keep strictly increasing amp comp on the way up; integer
    // quantization can otherwise repeat a value between neighbouring rungs.
    int32_t ampFixed = (int32_t)(ampGuess + 0.5f);
    if (ampFixed <= (int32_t)ampByPair[p - 1]) {
      ampFixed = (int32_t)ampByPair[p - 1] + 1;
    }
    if (ampFixed > (int32_t)DIV_COUNTER) {
      break;
    }

    float found = find_freq_for_duty50((uint16_t)ampFixed, fTarget, ladderRatio, true);
    if (found <= 0.0f) {
      Serial.println((String)"[FREQ_TRACE_GUARD] DCO=" + ctx.dcoIndex +
                     " no signal tracing up at amp=" + ampFixed +
                     "; ladder stops at pair " + (p - 1));
      break;
    }
    add_known(found, (float)ampFixed);
    FreqTraceProbeInfo info = { g_lastFreqBisectGapUs, g_lastFreqBisectProbes,
                                g_lastSettleChecks };
    retry_rung(p, fTarget, ampFixed, found, info,
               (int32_t)ampByPair[p - 1] + 1, (int32_t)DIV_COUNTER);

    freqByPair[p] = found;
    ampByPair[p]  = (uint16_t)ampFixed;
    cal_report_set_pair_from_gap(p, info.gapUs, found, CAL_SRC_RUNG);
    highestTraced = p;
    Serial.println((String)"[FREQ_TRACE] DCO=" + ctx.dcoIndex +
                   " pair=" + p + " target=" + fmt_freq(fTarget) +
                   " amp=" + ampFixed + " freq=" + fmt_freq(found) +
                   freq_trace_quality(info.gapUs, found, info.probes,
                                      info.settleChecks));
  }

  // --- Trace downward -------------------------------------------------------
  int lowestTraced = anchorPair;  // lowest rung holding a measured point
  for (int p = anchorPair - 1; p >= firstRung; --p) {
    if (calibrationCancelRequested) {
      return false;
    }
    float fTarget = anchorFreq * powf(ladderRatio, (float)(p - anchorPair));

    float ampGuess = freq_trace_guess(knownFreq, knownAmp, knownCount, fTarget);

    // The curve points must keep strictly decreasing amp comp on the way down;
    // integer quantization can otherwise repeat a value at low amps.
    int32_t ampFixed = (int32_t)(ampGuess + 0.5f);
    if (ampFixed >= (int32_t)ampByPair[p + 1]) {
      ampFixed = (int32_t)ampByPair[p + 1] - 1;
    }
    if (ampFixed < 1) {
      // Integer amp floor reached before the bottom rung: the remaining pairs
      // are filled between here and the measured amp-comp-0 endpoint.
      Serial.println((String)"[FREQ_TRACE] DCO=" + ctx.dcoIndex +
                     " amp comp floor reached at pair " + p + "; ladder stops here");
      break;
    }

    // Frequency seed from the known points (freq is ~proportional to amp);
    // fall back to scaling from the pair above if the guess degenerates.
    float fGuess = freq_trace_guess(knownAmp, knownFreq, knownCount, (float)ampFixed);
    if (fGuess <= 0.0f) {
      fGuess = freqByPair[p + 1] * ((float)ampFixed / (float)ampByPair[p + 1]);
    }
    float found = find_freq_for_duty50((uint16_t)ampFixed, fGuess, ladderRatio, true);
    if (found <= 0.0f) {
      Serial.println((String)"[FREQ_TRACE_GUARD] DCO=" + ctx.dcoIndex +
                     " no signal tracing down at amp=" + ampFixed +
                     "; interpolating remaining pairs");
      break;
    }
    add_known(found, (float)ampFixed);
    FreqTraceProbeInfo info = { g_lastFreqBisectGapUs, g_lastFreqBisectProbes,
                                g_lastSettleChecks };
    retry_rung(p, fTarget, ampFixed, found, info,
               1, (int32_t)ampByPair[p + 1] - 1);

    freqByPair[p] = found;
    ampByPair[p]  = (uint16_t)ampFixed;
    cal_report_set_pair_from_gap(p, info.gapUs, found, CAL_SRC_RUNG);
    lowestTraced  = p;
    Serial.println((String)"[FREQ_TRACE] DCO=" + ctx.dcoIndex +
                   " pair=" + p + " target=" + fmt_freq(fTarget) +
                   " amp=" + ampFixed + " freq=" + fmt_freq(found) +
                   freq_trace_quality(info.gapUs, found, info.probes,
                                      info.settleChecks));
  }

  if (calibrationCancelRequested) {
    return false;
  }

  // --- Top endpoint: full amp comp ------------------------------------------
  // Measured last, and measured carefully. It is the top of the stored table, so
  // every note above the last rung is played from it, and it is the one probe
  // whose frequency is unknown up front and whose pulse can collapse - the probe
  // most likely to burn timeouts. By now the model holds ~20 measured points, so
  // it can be seeded within a few cents and searched in a tight window, and it
  // is measured at FINE quality whatever the run asked for (except FAST, which
  // keeps its own quality): up here a reading is a couple of milliseconds, so
  // the accuracy is nearly free.
  // When the ladder stopped early the endpoint takes the next slot and the
  // remaining pairs are sentinel-filled, keeping the classic table shape (and
  // with it the runtime plateau / AMP_COMP_MAX_HZ behaviour).
  const int topPair = (highestTraced < lastRung) ? (highestTraced + 1) : (numPairs - 1);
  {
    // A FAST run keeps its own quality here: the whole point is the quickest
    // usable table, and the top pair's error is bounded by the plateau anyway.
    CalPrecisionOverride fineForEndpoint(
      (calibrationPrecision == CAL_PRECISION_FAST) ? calibrationPrecision
                                                   : CAL_PRECISION_FINE);

    // Power law anchored on the highest measured point, cross-checked against
    // the quadratic; the lower wins a disagreement, since overshooting lands in
    // the collapse and a timeout says nothing about where the answer is.
    const float fPower = freq_trace_power_seed(knownFreq, knownAmp, knownCount,
                                               (float)DIV_COUNTER);
    const float fQuad  = freq_trace_guess(knownAmp, knownFreq, knownCount,
                                          (float)DIV_COUNTER);
    float fSeed = (fPower > 0.0f) ? fPower : fQuad;
    if (fPower > 0.0f && fQuad > 0.0f) {
      const float disagreeCents = fabsf(1200.0f * log2f(fQuad / fPower));
      if (disagreeCents > kEndpointSeedAgreeCents) {
        fSeed = fminf(fPower, fQuad);
        Serial.println((String)"[FREQ_TRACE] DCO=" + ctx.dcoIndex +
                       " top endpoint seeds disagree by " + disagreeCents +
                       " cents (power=" + fPower + " quad=" + fQuad +
                       "); taking " + fSeed);
      }
    }
    if (fSeed <= freqByPair[highestTraced]) {
      fSeed = freqByPair[highestTraced] * ladderRatio;
    }

    float endFreq = find_freq_for_duty50(DIV_COUNTER, fSeed, kTopEndpointWindowRatio, true);
    // One retry from a fresh seed: a tight window is only safe if a seed that
    // was wrong gets a second chance. A search that came back empty (0) or below
    // the last rung either started outside its window or walked into the
    // collapse, so restart it from the last rung, one ladder step up - a seed
    // that is a measurement rather than an extrapolation.
    if (!(endFreq > freqByPair[highestTraced]) && !calibrationCancelRequested) {
      const float fRetry = freqByPair[highestTraced] * ladderRatio;
      Serial.println((String)"[FREQ_TRACE] DCO=" + ctx.dcoIndex +
                     " top endpoint retry from " + fRetry +
                     " Hz (seed " + fSeed + " gave " + endFreq + ")");
      endFreq = find_freq_for_duty50(DIV_COUNTER, fRetry, kBottomEndpointWindowRatio, true);
    }
    if (endFreq > freqByPair[highestTraced]) {
      add_known(endFreq, (float)DIV_COUNTER);
      cal_report_set_pair_from_gap(topPair, g_lastFreqBisectGapUs, endFreq,
                                   CAL_SRC_ENDPOINT_FULL);
      Serial.println((String)"[FREQ_TRACE] DCO=" + ctx.dcoIndex +
                     " top endpoint pair=" + topPair +
                     " amp=" + DIV_COUNTER + " freq=" + fmt_freq(endFreq) +
                     freq_trace_quality(g_lastFreqBisectGapUs, endFreq,
                                        g_lastFreqBisectProbes,
                                        g_lastSettleChecks));
    } else {
      Serial.println((String)"[FREQ_TRACE_GUARD] DCO=" + ctx.dcoIndex +
                     " top endpoint unusable (measured=" + endFreq +
                     ", seed=" + fSeed + "); keeping the estimate");
      endFreq = fSeed;
      cal_report_set_pair(topPair, kCalDutyErrUnknown, CAL_SRC_FILLED);
    }
    freqByPair[topPair] = endFreq;
    ampByPair[topPair]  = DIV_COUNTER;
  }
  for (int q = topPair + 1; q < numPairs; ++q) {
    freqByPair[q] = 200000.0f;  // sentinel: stored as 20000000 (freq*100), like the classic path
    ampByPair[q]  = DIV_COUNTER;
    cal_report_set_pair(q, kCalDutyErrUnknown, CAL_SRC_SENTINEL);
  }

  if (calibrationCancelRequested) {
    return false;
  }

  // --- Bottom endpoint: amp comp 0 ------------------------------------------
  // Same reasoning as the top endpoint, but with a band around it instead of
  // just a ceiling. Amp comp 0 is the one point of the curve the oscillator may
  // simply not have: below some frequency it stops pulsing, and then no duty can
  // be measured at any frequency the search might try. So bound the whole thing
  // - seed, acceptance and the value finally stored - to amp0_search_band(),
  // which is wide because the point sits well below the traced data (pair 1 / 2.2
  // on a measured table) and a model extrapolating to amp comp 0 has no anchor
  // under it. What the band excludes is the region where a probe cannot even
  // tell a lopsided pulse from silence: following the model in there costs the
  // full timeout per probe and leaves pair 0 at a frequency the oscillator cannot
  // produce, which is worse for the runtime lookup than an honest extrapolation.
  const FreqSearchBounds f0Bounds = amp0_search_band(freqByPair[lowestTraced]);
  const float            f0FloorHz = f0Bounds.loHz;
  const float            f0CeilHz  = f0Bounds.hiHz;

  // The model estimate: a least-squares line through the lowest measured
  // rungs (the bottom of the curve is linear to a few tenths of a percent),
  // falling back to the quadratic guess only when the fit has nothing to work
  // with. Kept unclamped: if the measurement below is rejected, this - not the
  // band floor - is what deserves to be stored, because it reproduces the
  // measured slope of the bottom segment for every note under pair 1.
  float f0Model = amp0_fit_freq(knownAmp, knownFreq, knownCount);
  if (!(f0Model > 0.0f)) {
    f0Model = freq_trace_guess(knownAmp, knownFreq, knownCount, 0.0f);
  }

  float f0Est = f0Model;
  const bool amp0Calc = (autotuneAmp0Mode == AMP0_MODE_CALC) ||
                        (calibrationPrecision == CAL_PRECISION_FAST);
  if (amp0Calc) {
    // No live hunt: store the model estimate directly, with the same sanity
    // clamps the rejection branch below applies. This is the whole point of
    // CALC mode - on hardware whose pulse dies before 50% duty the hunt ends
    // in that rejection branch anyway, after paying for every timed-out probe.
    // A FAST run always takes this path: the hunt's timeouts are the single
    // most expensive block of the whole build, and the fit is what a rejected
    // hunt would have stored anyway.
    if (!(f0Est > 0.0f)) f0Est = sqrtf(f0FloorHz * f0CeilHz);
    if (f0Est < kAmp0StoreFloorHz) f0Est = kAmp0StoreFloorHz;
    if (f0Est > f0CeilHz) f0Est = f0CeilHz;
    cal_report_set_pair(0, kCalDutyErrUnknown, CAL_SRC_FILLED);
    Serial.println((String)"[FREQ_TRACE] DCO=" + ctx.dcoIndex +
                   " bottom endpoint calculated: " + fmt_freq(f0Est) +
                   " Hz (amp-0 hunt skipped, " +
                   ((calibrationPrecision == CAL_PRECISION_FAST)
                      ? "FAST run"
                      : "CALC mode; cmd 40 to measure") + ")");
  } else {
  // The search seed, unlike the stored fallback, stays inside the band: probes
  // below it pay the full timeout without being able to tell a lopsided pulse
  // from silence.
  {
    const float raw = f0Est;
    if (!(f0Est > 0.0f)) {
      f0Est = sqrtf(f0FloorHz * f0CeilHz);  // no usable model: middle of the band
    } else if (f0Est < f0FloorHz) {
      f0Est = f0FloorHz;
    } else if (f0Est > f0CeilHz) {
      f0Est = f0CeilHz;
    }
    if (f0Est != raw) {
      Serial.println((String)"[FREQ_TRACE] DCO=" + ctx.dcoIndex +
                     " bottom endpoint seed " + fmt_freq(raw) + " -> " + fmt_freq(f0Est) +
                     " Hz (band " + f0FloorHz + ".." + f0CeilHz + ")");
    }
  }
  {
    float found = measure_lowest_freq_at_amp0(f0Est, &f0Bounds);
    const float foundErr = duty_err_pct_from_gap(g_lastFreqBisectGapUs, found);
    if (found >= f0FloorHz && found <= f0CeilHz &&
        fabsf(foundErr) <= kEndpointAcceptDutyPct) {
      cal_report_set_pair_from_gap(0, g_lastFreqBisectGapUs, found,
                                   CAL_SRC_ENDPOINT_AMP0);
      Serial.println((String)"[FREQ_TRACE] DCO=" + ctx.dcoIndex +
                     " bottom endpoint amp=0 freq=" + fmt_freq(found) +
                     freq_trace_quality(g_lastFreqBisectGapUs, found,
                                        g_lastFreqBisectProbes,
                                        g_lastSettleChecks) +
                     " (seed=" + f0Est + ")");
      f0Est = found;
    } else {
      // Prefer the amp-0 scan secant (the last frequency that actually
      // bracketed a sign change) over the model intercept, which can sit
      // below the pulse floor (~5.62 Hz fill vs a 7.93 Hz scan).
      float stored = g_lastAmp0ScanSeedHz;
      const char *srcName = "scan secant";
      if (!(stored > 0.0f)) {
        stored = f0Model;
        srcName = "model estimate";
      }
      if (!(stored > 0.0f)) stored = f0Est;
      if (stored < kAmp0StoreFloorHz) stored = kAmp0StoreFloorHz;
      if (stored > f0CeilHz) stored = f0CeilHz;
      f0Est = stored;
      cal_report_set_pair(0, kCalDutyErrUnknown, CAL_SRC_FILLED);
      Serial.println((String)"[FREQ_TRACE_GUARD] DCO=" + ctx.dcoIndex +
                     " bottom endpoint rejected: best " +
                     ((found > 0.0f) ? fmt_freq(found) : String("n/a")) +
                     " Hz dutyErr=" +
                     ((found > 0.0f) ? String(foundErr, 2) : String("n/a")) +
                     "% (band " + fmt_freq(f0FloorHz) + ".." + fmt_freq(f0CeilHz) +
                     ", accept " + kEndpointAcceptDutyPct +
                     "%); storing the " + srcName + " " + fmt_freq(f0Est));
    }
  }
  }  // AMP0_MODE_MEASURE

  // Synthetic fill for rungs below the traced floor (if any): spread them
  // linearly between the amp-comp-0 endpoint and the lowest traced point.
  for (int q = lowestTraced - 1; q >= firstRung; --q) {
    float frac = (float)q / (float)lowestTraced;
    freqByPair[q] = f0Est + (freqByPair[lowestTraced] - f0Est) * frac;
    ampByPair[q]  = (uint16_t)((float)ampByPair[lowestTraced] * frac + 0.5f);
    cal_report_set_pair(q, kCalDutyErrUnknown, CAL_SRC_FILLED);
  }

  // --- Emit ascending into calibrationData ---------------------------------
  ctx.calibrationData[0] = (uint32_t)(f0Est * 100.0f);
  ctx.calibrationData[1] = 0;
  for (int p = 1; p < numPairs; ++p) {
    ctx.calibrationData[2 * p]     = (uint32_t)(freqByPair[p] * 100.0f);
    ctx.calibrationData[2 * p + 1] = ampByPair[p];
  }

  // --- Monotonicity sanity check --------------------------------------------
  return cal_table_is_monotonic(ctx.calibrationData, numPairs, ctx.dcoIndex,
                                "FREQ_TRACE_ERROR");
}


// --- Fine pass: refine the stored table ------------------------------------

// Bracket for a stored pair's re-measurement. The stored frequency is the
// previous answer for that exact amp comp, so the window only has to cover
// drift plus the error of the run that produced it - and it should not cover
// much more: the opening step is a quarter of the window, so a wide window is
// what let one noisy first reading send a pair hunting 20 cents away from a
// value that was already right. +/-34 cents (about 8.5-cent opening steps)
// still covers real drift; a pair that moved further shows up as the search
// giving up at the window edge and a large moved= in the [CAL_REFINE] report,
// which is worth seeing rather than silently chasing.
static constexpr float kRefineWindowRatio = 1.02f;

// Re-measure the table this oscillator already has instead of building a new
// one: every amp-comp value is kept exactly as stored and only the frequency
// it sits at is measured again, at the FINE precision profile. There is no
// anchor, no bootstrap cluster and no ladder derivation, so nothing is guessed
// and nothing moves except the numbers the hardware disagrees with.
// Method-agnostic: fixing an amp and finding its 50%-duty frequency is valid
// for a classic table too.
// Returns false when there is no usable stored table (previous data kept) or
// the refined table fails the monotonicity check.
bool refine_DCO_amp_table(DCOCalibrationContext& ctx) {
  constexpr int numPairs = (int)(chanLevelVoiceDataSize / 2);
  const int base = (int)ctx.dcoIndex * (int)chanLevelVoiceDataSize;

  float    storedFreq[numPairs];
  uint16_t storedAmp[numPairs];
  for (int p = 0; p < numPairs; ++p) {
    const int32_t fx100 = freq_to_amp_comp_array[base + 2 * p];
    int32_t a           = freq_to_amp_comp_array[base + 2 * p + 1];
    if (a < 0) a = 0;
    if (a > (int32_t)DIV_COUNTER) a = (int32_t)DIV_COUNTER;
    storedFreq[p] = (fx100 > 0) ? ((float)fx100 / 100.0f) : 0.0f;
    storedAmp[p]  = (uint16_t)a;
  }

  // The last pair worth measuring is the first one at full amp comp; above it
  // the table is sentinel padding with no operating point behind it.
  int topPair = -1;
  for (int p = 1; p < numPairs; ++p) {
    if (storedAmp[p] >= DIV_COUNTER) {
      topPair = p;
      break;
    }
  }

  const char *reject = nullptr;
  if (topPair < 4) {
    reject = "no full-amp endpoint (or too few pairs below it)";
  } else {
    int distinctAmps = 1;
    for (int p = 1; p <= topPair && reject == nullptr; ++p) {
      if (storedFreq[p] <= storedFreq[p - 1]) {
        reject = "frequencies are not increasing";
      } else if (storedAmp[p] < storedAmp[p - 1]) {
        reject = "amp comp values are not increasing";
      } else if (storedAmp[p] != storedAmp[p - 1]) {
        ++distinctAmps;
      }
    }
    if (reject == nullptr && distinctAmps < 4) {
      reject = "table is flat (looks seeded, not calibrated)";
    }
  }
  if (reject != nullptr) {
    Serial.println((String)"[CAL_REFINE_GUARD] DCO=" + ctx.dcoIndex +
                   " no usable stored table (" + reject +
                   "); run a normal calibration first");
    return false;
  }

  Serial.println((String)"[CAL_REFINE] DCO=" + ctx.dcoIndex +
                 " refining " + (topPair + 1) + " stored pairs" +
                 " (amp comp values kept, frequencies re-measured)");

  float refinedFreq[numPairs];
  for (int p = 0; p < numPairs; ++p) {
    refinedFreq[p] = storedFreq[p];
  }

  int   measured  = 0;
  float errSum    = 0.0f;
  float worstMove = 0.0f;
  int   worstPair = -1;

  for (int p = 0; p <= topPair; ++p) {
    if (calibrationCancelRequested) {
      return false;
    }
    // Pair 0 at amp comp 0 is not a rung, it is the bottom endpoint: the point
    // where the oscillator runs out of range, which may not have a 50% duty at
    // all. It gets the lowest-note treatment - a scan of the whole band for a
    // bracket, a band it may not leave, and a result that has to be near 50% to
    // be believed - instead of being re-measured in a +/-5% window around a
    // stored frequency the last run may itself have only extrapolated.
    const bool isBottomEndpoint = (p == 0 && storedAmp[0] == 0);
    if (isBottomEndpoint && autotuneAmp0Mode == AMP0_MODE_CALC) {
      // CALC mode: no live hunt. The endpoint is recomputed after this loop,
      // from the rungs the pass is about to re-measure - at this point none of
      // them have been refined yet, so a fit here would describe the old run.
      continue;
    }
    float found;
    if (isBottomEndpoint) {
      const FreqSearchBounds bounds = amp0_search_band(storedFreq[1]);
      found = measure_lowest_freq_at_amp0(storedFreq[p], &bounds);
      const float err = duty_err_pct_from_gap(g_lastFreqBisectGapUs, found);
      if (!(found >= bounds.loHz && found <= bounds.hiHz) ||
          fabsf(err) > kEndpointAcceptDutyPct) {
        cal_report_set_pair(p, kCalDutyErrUnknown, CAL_SRC_FILLED);
        Serial.println((String)"[CAL_REFINE_GUARD] DCO=" + ctx.dcoIndex +
                       " pair=0 amp=0 rejected: best " + found + " Hz dutyErr=" +
                       ((found > 0.0f) ? String(err, 2) : String("n/a")) +
                       "% (band " + bounds.loHz + ".." + bounds.hiHz +
                       ", accept " + kEndpointAcceptDutyPct +
                       "%); keeping the stored " + storedFreq[p]);
        continue;
      }
    } else {
      found = find_freq_for_duty50(storedAmp[p], storedFreq[p],
                                   kRefineWindowRatio, true);
    }
    if (found <= 0.0f) {
      cal_report_set_pair(p, kCalDutyErrUnknown, CAL_SRC_FILLED);
      Serial.println((String)"[CAL_REFINE_GUARD] DCO=" + ctx.dcoIndex +
                     " pair=" + p + " amp=" + storedAmp[p] +
                     " no signal near " + storedFreq[p] +
                     " Hz; keeping the stored frequency");
      continue;
    }

    refinedFreq[p] = found;
    cal_report_set_pair_from_gap(p, g_lastFreqBisectGapUs, found,
                                 CAL_SRC_REFINED);
    ++measured;

    const float moveCents = 1200.0f * log2f(found / storedFreq[p]);
    const float errPct    = duty_err_pct_from_gap(g_lastFreqBisectGapUs, found);
    if (errPct != kCalDutyErrUnknown) {
      errSum += fabsf(errPct);
    }
    if (fabsf(moveCents) > fabsf(worstMove)) {
      worstMove = moveCents;
      worstPair = p;
    }

    Serial.println((String)"[CAL_REFINE] DCO=" + ctx.dcoIndex +
                   " pair=" + p + " amp=" + storedAmp[p] +
                   " stored=" + storedFreq[p] + " -> found=" + found +
                   " moved=" + moveCents + " cents" +
                   freq_trace_quality(g_lastFreqBisectGapUs, found,
                                      g_lastFreqBisectProbes,
                                      g_lastSettleChecks));
  }

  for (int p = topPair + 1; p < numPairs; ++p) {
    cal_report_set_pair(p, kCalDutyErrUnknown, CAL_SRC_SENTINEL);
  }
  calReportLadderInterval = 0;   // the stored ladder is whatever built it
  calReportAnchorPair     = -1;

  // CALC mode: the amp-0 endpoint skipped in the loop above is recomputed here,
  // now that the rungs it extrapolates from are freshly measured. Clamped like
  // the FREQ_TRACE fallback: a sanity floor, and under pair 1 for monotonicity.
  if (storedAmp[0] == 0 && autotuneAmp0Mode == AMP0_MODE_CALC) {
    float fitAmps[kAmp0FitPoints + 3];
    float fitFreqs[kAmp0FitPoints + 3];
    int   fitCount = 0;
    for (int p = 1;
         p <= topPair && fitCount < (int)(sizeof(fitAmps) / sizeof(fitAmps[0]));
         ++p) {
      if (!(refinedFreq[p] > 0.0f) || storedAmp[p] == 0) continue;
      fitAmps[fitCount]  = (float)storedAmp[p];
      fitFreqs[fitCount] = refinedFreq[p];
      ++fitCount;
    }
    float f0 = amp0_fit_freq(fitAmps, fitFreqs, fitCount);
    if (!(f0 > 0.0f)) f0 = refinedFreq[0];  // no usable fit: keep the stored value
    const float f0CeilHz = refinedFreq[1] * 0.99f;
    if (f0 < kAmp0StoreFloorHz) f0 = kAmp0StoreFloorHz;
    if (f0 > f0CeilHz) f0 = f0CeilHz;
    refinedFreq[0] = f0;
    cal_report_set_pair(0, kCalDutyErrUnknown, CAL_SRC_FILLED);
    Serial.println((String)"[CAL_REFINE] DCO=" + ctx.dcoIndex +
                   " pair=0 amp=0 calculated: " + f0 +
                   " Hz (amp-0 hunt skipped, CALC mode; cmd 40 to measure)");
  }

  // Emit: refined frequencies, stored amp comp values, sentinels untouched.
  for (int p = 0; p < numPairs; ++p) {
    if (p > topPair) {
      ctx.calibrationData[2 * p]     = (uint32_t)freq_to_amp_comp_array[base + 2 * p];
      ctx.calibrationData[2 * p + 1] = (uint32_t)freq_to_amp_comp_array[base + 2 * p + 1];
      continue;
    }
    ctx.calibrationData[2 * p]     = (uint32_t)(refinedFreq[p] * 100.0f);
    ctx.calibrationData[2 * p + 1] = storedAmp[p];
  }

  String summary = (String)"[CAL_REFINE] DCO=" + ctx.dcoIndex +
                   " measured=" + measured + "/" + (topPair + 1);
  if (measured > 0) {
    summary += (String)" dutyErr avg=" + (errSum / (float)measured) + "%";
    summary += (String)" largest move=" + worstMove + " cents at pair " + worstPair;
  }
  Serial.println(summary);

  return cal_table_is_monotonic(ctx.calibrationData, numPairs, ctx.dcoIndex,
                                "CAL_REFINE_ERROR");
}


// 3-point quadratic interpolate y at x. Used by calibrate_DCO helpers / find_lowest_freq.
float quadraticInterpolation(float x0, float y0, float x1, float y1, float x2, float y2, float x) {
  // Calculate the coefficients of the quadratic polynomial
  float a = ((y2 - (x2 * (y1 - y0) + x1 * y0 - x0 * y1) / (x1 - x0)) / (x2 * (x2 - x0 - x1) + x0 * x1));
  float b = ((y1 - y0) / (x1 - x0) - a * (x0 + x1));
  float c = y0 - x0 * (b + a * x0);

  // Use the polynomial to estimate the next value
  return a * x * x + b * x + c;
}

// Log interpolate between two points → uint16. Used by compute_initial_amp_for_note().
uint16_t logarithmicInterpolation(float x0, float y0, float x1, float y1, float x) {
  // Ensure x0 and x1 are not zero or negative to avoid log(0) or log of negative number
  if (x0 <= 0 || x1 <= 0) {
    return 0;  // or handle the error as needed
  }

  // Calculate the constants a and b
  float a = (y1 - y0) / (log(x1) - log(x0));
  float b = y0 - a * log(x0);

  // Calculate the y value at the given x
  float y = a * log(x) + b;

  return (uint16_t)round(y);
}

// Linear interpolate between two points. Used by find_lowest_freq().
float linearInterpolation(float x0, float y0, float x1, float y1, float x) {
  // Ensure x0 and x1 are not the same to avoid division by zero
  if (x0 == x1) {
    return 0;  // or handle the error as needed
  }

  // Calculate the slope (m) of the line
  float m = (y1 - y0) / (x1 - x0);

  // Calculate the y-intercept (b) of the line
  float b = y0 - m * x0;

  // Calculate the y value at the given x
  float y = m * x + b;

  return y;
}

// Solve exponential interpolation for y at x (log-space lerp). Used by initMultiplierTables().
double expInterpolationSolveY(double x, double x0, double x1, double y0, double y1) {
  if (x0 <= 0 || x1 <= 0) {
    // Handle error: x0 and x1 must be greater than 0 for exponential interpolation
    return NAN;
  }

  double log_y0 = log(y0);
  double log_y1 = log(y1);

  double log_y = log_y0 + (log_y1 - log_y0) * (x - x0) / (x1 - x0);

  return exp(log_y);
}

#endif  // __AUTOTUNE_SEARCH_IMPL_H__
