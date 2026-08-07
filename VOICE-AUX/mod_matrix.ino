#include "globals.h"
#include "mod_matrix.h"

static ModSlot g_mod_slots[MOD_SLOT_COUNT];
static int16_t mod_random_snh_q15 = 0;
static uint32_t mod_random_last_ms = 0;

void mod_matrix_init() {
  for (uint8_t i = 0; i < MOD_SLOT_COUNT; i++) {
    g_mod_slots[i].source = MOD_SRC_EMPTY;
    g_mod_slots[i].dest = MOD_DEST_EMPTY;
    g_mod_slots[i].depth = 0;
  }
  mod_random_snh_q15 = 0;
  mod_random_last_ms = 0;
}

void mod_matrix_set_source(uint8_t slot, int16_t v) {
  if (slot >= MOD_SLOT_COUNT) return;
  if (v < 0 || v >= (int16_t)MOD_SRC_COUNT) {
    g_mod_slots[slot].source = MOD_SRC_EMPTY;
  } else {
    g_mod_slots[slot].source = (uint8_t)v;
  }
}

void mod_matrix_set_dest(uint8_t slot, int16_t v) {
  if (slot >= MOD_SLOT_COUNT) return;
  if (v < 0 || v >= (int16_t)MOD_DEST_COUNT) {
    g_mod_slots[slot].dest = MOD_DEST_EMPTY;
  } else {
    g_mod_slots[slot].dest = (uint8_t)v;
  }
}

void mod_matrix_set_depth(uint8_t slot, int16_t v) {
  if (slot >= MOD_SLOT_COUNT) return;
  g_mod_slots[slot].depth = v;
}

// Aux has no MIDI / EnvDCO / LFO: ADSR3/4, LFO1–4, vel, keytrack, AT, pitch bend, mod wheel → 0.
// Random free-runs ~5 Hz until performance broadcast exists.
static int32_t mod_matrix_read_source_q15(uint8_t src) {
  switch (src) {
    case MOD_SRC_RANDOM:
      return (int32_t)mod_random_snh_q15;
    default:
      return 0;
  }
}

static uint16_t mod_clamp_u16(int32_t v) {
  if (v < 0) return 0;
  if (v > 4095) return 4095;
  return (uint16_t)v;
}

static int32_t mod_matrix_accumulate_dest(ModDest dest) {
  int32_t accum = 0;
  for (uint8_t i = 0; i < MOD_SLOT_COUNT; i++) {
    const ModSlot& s = g_mod_slots[i];
    if (s.source == MOD_SRC_EMPTY || s.dest != (uint8_t)dest) continue;
    if (s.depth == 0) continue;
    const int32_t src_q15 = mod_matrix_read_source_q15(s.source);
    accum += (int32_t)(((int64_t)src_q15 * (int64_t)s.depth) >> 15);
  }
  return accum;
}

void mod_matrix_apply_dist() {
  uint32_t now = millis();
  if ((now - mod_random_last_ms) >= 200) {
    mod_random_last_ms = now;
    mod_random_snh_q15 = (int16_t)(((int32_t)random(0, 2001) - 1000) * 32);
  }

  uint16_t dist_out = mod_clamp_u16((int32_t)DIST_DRIVE + mod_matrix_accumulate_dest(MOD_DEST_DIST_DRIVE));
  uint16_t mix_out = mod_clamp_u16((int32_t)DIST_MIX + mod_matrix_accumulate_dest(MOD_DEST_DIST_MIX));
  pwm_set_chan_level(DIST_DRIVE_PWM_SLICE, DIST_DRIVE_PWM_CHAN, dist_out);
  pwm_set_chan_level(DIST_MIX_PWM_SLICE, DIST_MIX_PWM_CHAN, mix_out);
}
