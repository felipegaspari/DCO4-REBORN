#ifndef VOICE_AUX_I2S_NOISE_H
#define VOICE_AUX_I2S_NOISE_H

// PCM5102 I2S listen of a local DCO_Noise fleet (ENABLE_I2S_NOISE).
// See docs/I2S_NOISE.md.

void i2s_noise_init();
void i2s_noise_service();
void i2s_noise_poll_status();

#endif  // VOICE_AUX_I2S_NOISE_H
