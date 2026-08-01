#include "globals.h"

typedef void (*ParamHandler)(int16_t value);

struct ParamEntry {
  uint16_t id;
  ParamHandler handler;
};

static void apply_param_dist_drive(int16_t v) {
  DIST_DRIVE = (uint16_t)constrain((int)v, 0, 4095);
  write_dist_pwm();
}

static void apply_param_dist_mix(int16_t v) {
  DIST_MIX = (uint16_t)constrain((int)v, 0, 4095);
  write_dist_pwm();
}

static void apply_param_filter_mode(int16_t v) {
  FILTER_MODE = (uint8_t)constrain((int)v, 0, 3);  // LP24 / BP12 / HP6_LP18 / optional
  apply_filter_mode_gpio();
}

static const ParamEntry paramTable[] = {
  { PARAM_DIST_DRIVE,   apply_param_dist_drive },
  { PARAM_DIST_MIX,     apply_param_dist_mix },
  { PARAM_FILTER_MODE,  apply_param_filter_mode },
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
