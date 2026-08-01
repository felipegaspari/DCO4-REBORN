#include "globals.h"
#include "mod_matrix.h"

typedef void (*ParamHandler)(int16_t value);

struct ParamEntry {
  uint16_t id;
  ParamHandler handler;
};

static void apply_param_dist_drive(int16_t v) {
  DIST_DRIVE = (uint16_t)constrain((int)v, 0, 4095);
  // PWM written from mod_matrix_apply_dist() in loop.
}

static void apply_param_dist_mix(int16_t v) {
  DIST_MIX = (uint16_t)constrain((int)v, 0, 4095);
}

static void apply_param_filter_mode(int16_t v) {
  FILTER_MODE = (uint8_t)constrain((int)v, 0, 3);  // LP24 / BP12 / HP6_LP18 / optional
  apply_filter_mode_gpio();
}

#define DECL_MOD_SLOT_APPLIERS(N) \
  static void apply_param_mod_slot##N##_source(int16_t v) { mod_matrix_set_source(N, v); } \
  static void apply_param_mod_slot##N##_dest(int16_t v) { mod_matrix_set_dest(N, v); } \
  static void apply_param_mod_slot##N##_depth(int16_t v) { mod_matrix_set_depth(N, v); }

DECL_MOD_SLOT_APPLIERS(0)
DECL_MOD_SLOT_APPLIERS(1)
DECL_MOD_SLOT_APPLIERS(2)
DECL_MOD_SLOT_APPLIERS(3)
DECL_MOD_SLOT_APPLIERS(4)
DECL_MOD_SLOT_APPLIERS(5)
DECL_MOD_SLOT_APPLIERS(6)
DECL_MOD_SLOT_APPLIERS(7)

#undef DECL_MOD_SLOT_APPLIERS

static const ParamEntry paramTable[] = {
  { PARAM_DIST_DRIVE,          apply_param_dist_drive },
  { PARAM_DIST_MIX,            apply_param_dist_mix },
  { PARAM_FILTER_MODE,         apply_param_filter_mode },
  { PARAM_MOD_SLOT0_SOURCE,    apply_param_mod_slot0_source },
  { PARAM_MOD_SLOT0_DEST,      apply_param_mod_slot0_dest },
  { PARAM_MOD_SLOT0_DEPTH,     apply_param_mod_slot0_depth },
  { PARAM_MOD_SLOT1_SOURCE,    apply_param_mod_slot1_source },
  { PARAM_MOD_SLOT1_DEST,      apply_param_mod_slot1_dest },
  { PARAM_MOD_SLOT1_DEPTH,     apply_param_mod_slot1_depth },
  { PARAM_MOD_SLOT2_SOURCE,    apply_param_mod_slot2_source },
  { PARAM_MOD_SLOT2_DEST,      apply_param_mod_slot2_dest },
  { PARAM_MOD_SLOT2_DEPTH,     apply_param_mod_slot2_depth },
  { PARAM_MOD_SLOT3_SOURCE,    apply_param_mod_slot3_source },
  { PARAM_MOD_SLOT3_DEST,      apply_param_mod_slot3_dest },
  { PARAM_MOD_SLOT3_DEPTH,     apply_param_mod_slot3_depth },
  { PARAM_MOD_SLOT4_SOURCE,    apply_param_mod_slot4_source },
  { PARAM_MOD_SLOT4_DEST,      apply_param_mod_slot4_dest },
  { PARAM_MOD_SLOT4_DEPTH,     apply_param_mod_slot4_depth },
  { PARAM_MOD_SLOT5_SOURCE,    apply_param_mod_slot5_source },
  { PARAM_MOD_SLOT5_DEST,      apply_param_mod_slot5_dest },
  { PARAM_MOD_SLOT5_DEPTH,     apply_param_mod_slot5_depth },
  { PARAM_MOD_SLOT6_SOURCE,    apply_param_mod_slot6_source },
  { PARAM_MOD_SLOT6_DEST,      apply_param_mod_slot6_dest },
  { PARAM_MOD_SLOT6_DEPTH,     apply_param_mod_slot6_depth },
  { PARAM_MOD_SLOT7_SOURCE,    apply_param_mod_slot7_source },
  { PARAM_MOD_SLOT7_DEST,      apply_param_mod_slot7_dest },
  { PARAM_MOD_SLOT7_DEPTH,     apply_param_mod_slot7_depth },
};

void update_parameters(uint16_t paramNumber, int16_t paramValue) {
  for (size_t i = 0; i < sizeof(paramTable) / sizeof(paramTable[0]); ++i) {
    if (paramTable[i].id == paramNumber) {
      paramTable[i].handler(paramValue);
      return;
    }
  }
  // Unknown / DCO-owned IDs: discard
}
