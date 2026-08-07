#ifndef VOICE_AUX_GLOBALS_H
#define VOICE_AUX_GLOBALS_H

#include <Arduino.h>
#include <stdint.h>
#include "hardware/pwm.h"
#include "params_def.h"

// Provisional breadboard pins — freeze on PCB later (do not assume DCO solo-B map).
// See docs/PINOUT.md, docs/README.md, and DCO/docs/DUAL_MCU.md.
static constexpr uint8_t DIST_DRIVE_PIN = 2;   // PWM
static constexpr uint8_t DIST_MIX_PIN   = 3;   // PWM
static constexpr uint8_t FILTER_MODE_A_PIN = 4;  // stage-1 SPDT sense (to DG411 + inverter)
static constexpr uint8_t FILTER_MODE_B_PIN = 5;  // stage-2 SPDT sense

static constexpr uint16_t DIV_COUNTER_CV = 4095;

// Input TX fanout → this board RX only. TX must not join the Input bus.
static constexpr uint8_t INPUT_RX_PIN = 1;  // UART0 RX (GP1); TX GP0 left unwired to bus
static constexpr uint32_t INPUT_BAUD  = 2500000;

// PCM5102 I2S listen (ENABLE_I2S_NOISE). Arduino-Pico I2S requires LRCK = BCLK+1.
static constexpr uint8_t I2S_BCLK_PIN = 6;
static constexpr uint8_t I2S_DOUT_PIN = 8;
static constexpr uint8_t I2S_XMT_PIN  = 10;  // driven HIGH in i2s_noise_init

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

// mod_matrix.ino
void mod_matrix_init();
void mod_matrix_apply_dist();

// i2s_noise.ino
void i2s_noise_init();
void i2s_noise_service();
void i2s_noise_poll_status();

#endif  // VOICE_AUX_GLOBALS_H
