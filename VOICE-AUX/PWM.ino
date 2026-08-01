#include "globals.h"

void init_dist_pwm() {
  gpio_set_function(DIST_DRIVE_PIN, GPIO_FUNC_PWM);
  DIST_DRIVE_PWM_SLICE = pwm_gpio_to_slice_num(DIST_DRIVE_PIN);
  DIST_DRIVE_PWM_CHAN = pwm_gpio_to_channel(DIST_DRIVE_PIN);
  pwm_set_wrap(DIST_DRIVE_PWM_SLICE, DIV_COUNTER_CV);
  pwm_set_enabled(DIST_DRIVE_PWM_SLICE, true);

  gpio_set_function(DIST_MIX_PIN, GPIO_FUNC_PWM);
  DIST_MIX_PWM_SLICE = pwm_gpio_to_slice_num(DIST_MIX_PIN);
  DIST_MIX_PWM_CHAN = pwm_gpio_to_channel(DIST_MIX_PIN);
  if (DIST_MIX_PWM_SLICE != DIST_DRIVE_PWM_SLICE) {
    pwm_set_wrap(DIST_MIX_PWM_SLICE, DIV_COUNTER_CV);
  }
  pwm_set_enabled(DIST_MIX_PWM_SLICE, true);
}

void write_dist_pwm() {
  pwm_set_chan_level(DIST_DRIVE_PWM_SLICE, DIST_DRIVE_PWM_CHAN, DIST_DRIVE);
  pwm_set_chan_level(DIST_MIX_PWM_SLICE, DIST_MIX_PWM_CHAN, DIST_MIX);
}
