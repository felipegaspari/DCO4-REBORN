#include "globals.h"

// Two GPIO bits for AS3320 stages 1–2 LP/HP (each needs complementary drive off-board
// or a second pin). Encoding matches DCO/docs/FILTER_ROUTING.md:
//   00 LP24, 01 HP6_LP18, 10 optional, 11 BP12

void init_filter_mode_gpio() {
  pinMode(FILTER_MODE_A_PIN, OUTPUT);
  pinMode(FILTER_MODE_B_PIN, OUTPUT);
  digitalWrite(FILTER_MODE_A_PIN, LOW);
  digitalWrite(FILTER_MODE_B_PIN, LOW);
}

void apply_filter_mode_gpio() {
  const uint8_t m = FILTER_MODE & 0x03;
  digitalWrite(FILTER_MODE_A_PIN, (m & 0x01) ? HIGH : LOW);
  digitalWrite(FILTER_MODE_B_PIN, (m & 0x02) ? HIGH : LOW);
}
