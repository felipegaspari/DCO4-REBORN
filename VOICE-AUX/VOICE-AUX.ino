// Voice-aux: RP2040 helper for post-filter IO (Dist Drive/Mix, AS3320 mode, later FX).
// RX-only on Input TX fanout — never TX upstream. See DCO/docs/DUAL_MCU.md.
// Optional PCM5102 I2S noise listen: docs/I2S_NOISE.md.

// ENABLE_I2S_NOISE — local white/pink → PCM5102 @ 48 kHz / 32-bit (GP6/7/8/10).
#define ENABLE_I2S_NOISE
// I2S_NOISE_TEST_TONE — full-scale 440 Hz square DDS; comment out for noise.
// #define I2S_NOISE_TEST_TONE

#include "globals.h"
#include "mod_matrix.h"
#include "i2s_noise.h"
#include "serial_parser.h"
#include "serial_input_protocol.h"
#include "serial_param_protocol.h"

void setup() {
  Serial.begin(115200);
  init_serial();
  init_dist_pwm();
  init_filter_mode_gpio();
  mod_matrix_init();
  write_dist_pwm();
  apply_filter_mode_gpio();
  i2s_noise_init();
}

void loop() {
  i2s_noise_poll_status();
  serial_panel_task();
  mod_matrix_apply_dist();
  i2s_noise_service();
}
