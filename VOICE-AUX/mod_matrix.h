#ifndef VOICE_AUX_MOD_MATRIX_H
#define VOICE_AUX_MOD_MATRIX_H

#include <stdint.h>

// Slim mod matrix for Dist Drive / Dist Mix. Same ParamIds / enums as DCO.
// See DCO/docs/MOD_MATRIX.md.

static constexpr uint8_t MOD_SLOT_COUNT = 8;
static constexpr uint8_t MOD_SRC_EMPTY = 0xFF;
static constexpr uint8_t MOD_DEST_EMPTY = 0xFF;

enum ModSource : uint8_t {
  MOD_SRC_ADSR3 = 0,
  MOD_SRC_ADSR4 = 1,
  MOD_SRC_LFO3 = 2,
  MOD_SRC_LFO4 = 3,
  MOD_SRC_VELOCITY = 4,
  MOD_SRC_KEYTRACK = 5,
  MOD_SRC_RANDOM = 6,
  MOD_SRC_AFTERTOUCH = 7,
  MOD_SRC_LFO1 = 8,
  MOD_SRC_LFO2 = 9,
  MOD_SRC_PITCH_BEND = 10,
  MOD_SRC_MOD_WHEEL = 11,
  MOD_SRC_COUNT = 12
};

enum ModDest : uint8_t {
  MOD_DEST_OSC1_LEVEL = 0,
  MOD_DEST_OSC2_LEVEL = 1,
  MOD_DEST_OSC3_LEVEL = 2,
  MOD_DEST_SUB_LEVEL = 3,
  MOD_DEST_VCF1_RESO = 4,
  MOD_DEST_VCF2_RESO = 5,
  MOD_DEST_DIST_DRIVE = 6,
  MOD_DEST_VCF_CUTOFF = 7,
  MOD_DEST_DIST_MIX = 8,
  MOD_DEST_PITCH = 9,  // DCO-only; ignored by aux apply
  MOD_DEST_COUNT = 10
};

struct ModSlot {
  uint8_t source;
  uint8_t dest;
  int16_t depth;
};

void mod_matrix_init();
void mod_matrix_set_source(uint8_t slot, int16_t v);
void mod_matrix_set_dest(uint8_t slot, int16_t v);
void mod_matrix_set_depth(uint8_t slot, int16_t v);
// Re-sum Dist Drive / Dist Mix from slots and write PWM. Call from loop.
void mod_matrix_apply_dist();

#endif
