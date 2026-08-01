// Voice-aux: RP2040 helper for post-filter IO (Dist Drive/Mix, AS3320 mode, later FX).
// RX-only on Input TX fanout — never TX upstream. See DCO/docs/DUAL_MCU.md.

#include "globals.h"
#include "serial_parser.h"
#include "serial_input_protocol.h"
#include "serial_param_protocol.h"

void setup() {
  Serial.begin(115200);
  init_serial();
  init_dist_pwm();
  init_filter_mode_gpio();
  write_dist_pwm();
  apply_filter_mode_gpio();
}

void loop() {
  serial_panel_task();
}
