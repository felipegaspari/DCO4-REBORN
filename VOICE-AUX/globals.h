#ifndef VOICE_AUX_GLOBALS_H
#define VOICE_AUX_GLOBALS_H

#include <Arduino.h>
#include <stdint.h>
#include "hardware/pwm.h"
#include "params_def.h"

// Provisional breadboard pins — freeze on PCB later (do not assume DCO solo-B map).
// See docs/README.md and DCO/docs/DUAL_MCU.md.
static constexpr uint8_t DIST_DRIVE_PIN = 2;   // PWM
static constexpr uint8_t DIST_MIX_PIN   = 3;   // PWM
static constexpr uint8_t FILTER_MODE_A_PIN = 4;  // stage-1 SPDT sense (to DG411 + inverter)
static constexpr uint8_t FILTER_MODE_B_PIN = 5;  // stage-2 SPDT sense

static constexpr uint16_t DIV_COUNTER_CV = 4095;

// Input TX fanout → this board RX only. TX must not join the Input bus.
static constexpr uint8_t INPUT_RX_PIN = 1;  // UART0 RX (GP1); TX GP0 left unwired to bus
static constexpr uint32_t INPUT_BAUD  = 2500000;

uint16_t DIST_DRIVE = 0;
uint16_t DIST_MIX = 0;
uint8_t FILTER_MODE = 0;

uint8_t DIST_DRIVE_PWM_SLICE;
uint8_t DIST_DRIVE_PWM_CHAN;
uint8_t DIST_MIX_PWM_SLICE;
uint8_t DIST_MIX_PWM_CHAN;

void init_dist_pwm();
void write_dist_pwm();
void init_filter_mode_gpio();
void apply_filter_mode_gpio();
void update_parameters(uint16_t paramNumber, int16_t paramValue);
void init_serial();
void serial_panel_task();

#endif  // VOICE_AUX_GLOBALS_H
