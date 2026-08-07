// PCM5102 I2S listen — local white/pink at true 48 kHz (not DCO Character noiseLevel[]).
// Gated by ENABLE_I2S_NOISE in VOICE-AUX.ino.

#include "i2s_noise.h"

#ifdef ENABLE_I2S_NOISE

#include <I2S.h>

#ifndef NOISE_ENGINE
#define NOISE_ENGINE 1  // FastNoiseGen — no PIO LFSR required on aux
#endif
#include "_build_libs/DCO_Noise/DCO_Noise.h"

static constexpr uint32_t I2S_SAMPLE_RATE = 48000u;
static constexpr uint32_t I2S_TONE_HZ = 440u;
static constexpr uint32_t I2S_TONE_PHASE_INC =
    (uint32_t)((I2S_TONE_HZ * (1ull << 32)) / I2S_SAMPLE_RATE);

static I2S i2sNoise(OUTPUT);
static DcoNoiseGen noiseL(NOISE_WHITE, 0xC0FFEE01u);
static DcoNoiseGen noiseR(NOISE_PINK, 0xC0FFEE02u);

// 0 idle, 1 ok pending print, 2 fail pending print, 3 done.
static volatile uint8_t i2s_noise_status = 0;
static bool i2s_noise_running = false;
#ifdef I2S_NOISE_TEST_TONE
static uint32_t i2s_tone_phase = 0;
#endif

static inline int32_t i2s_q15_to_i32(int16_t q15) {
  return (int32_t)q15 << 16;
}

void i2s_noise_init() {
  pinMode(I2S_XMT_PIN, OUTPUT);
  digitalWrite(I2S_XMT_PIN, HIGH);

  i2sNoise.setBCLK(I2S_BCLK_PIN);  // LRCK/RCK = BCLK + 1
  i2sNoise.setDATA(I2S_DOUT_PIN);
  i2sNoise.setBitsPerSample(32);
  if (i2sNoise.begin((long)I2S_SAMPLE_RATE)) {
    i2s_noise_running = true;
    i2s_noise_status = 1;
  } else {
    i2s_noise_running = false;
    i2s_noise_status = 2;
  }
}

void i2s_noise_poll_status() {
  uint8_t st = i2s_noise_status;
  if (st != 1 && st != 2) {
    return;
  }
  if (!Serial) {
    return;
  }
  if (!__atomic_compare_exchange_n(&i2s_noise_status, &st, (uint8_t)3, false,
                                  __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
    return;
  }
  if (st == 1) {
    Serial.printf("[I2S] begin ok BCK=%u RCK=%u DIN=%u XMT=%u 32-bit\n",
                  (unsigned)I2S_BCLK_PIN,
                  (unsigned)(I2S_BCLK_PIN + 1u),
                  (unsigned)I2S_DOUT_PIN,
                  (unsigned)I2S_XMT_PIN);
#ifdef I2S_NOISE_TEST_TONE
    Serial.println("[I2S] TEST_TONE on (440 Hz square DDS)");
#else
    Serial.println("[I2S] local noise L=white R=pink @ 48 kHz");
#endif
    Serial.println("[I2S] HW: VIN=5V/VBUS  SCK=GND(+bridge)  pads FLT/DEMP/FMT=L XSMT=H");
  } else {
    Serial.println("[I2S] begin FAILED (PIO claim/load)");
  }
}

void i2s_noise_service() {
  if (!i2s_noise_running) {
    return;
  }

#ifdef I2S_NOISE_TEST_TONE
  while (i2sNoise.availableForWrite() >= 8) {
    i2s_tone_phase += I2S_TONE_PHASE_INC;
    const int32_t s = (i2s_tone_phase & 0x80000000u) ? (int32_t)0x80000000
                                                     : (int32_t)0x7FFFFFFF;
    i2sNoise.write(s, false);
    i2sNoise.write(s, false);
  }
#else
  // True 48 kHz: one new sample per channel per stereo frame (not hold/repeat).
  while (i2sNoise.availableForWrite() >= 8) {
    const int32_t left = i2s_q15_to_i32(noiseL.next());
    const int32_t right = i2s_q15_to_i32(noiseR.next());
    i2sNoise.write(left, false);
    i2sNoise.write(right, false);
  }
#endif
}

#else  // !ENABLE_I2S_NOISE

void i2s_noise_init() {}
void i2s_noise_service() {}
void i2s_noise_poll_status() {}

#endif  // ENABLE_I2S_NOISE
