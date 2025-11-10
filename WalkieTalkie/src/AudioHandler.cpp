#include "AudioHandler.h"

AudioHandler::AudioHandler(I2SManager& i2sManager)
    : i2sManager(i2sManager) {}

// Encode 16-bit PCM samples to μ-law
void AudioHandler::encodeMuLaw(const int16_t* samples, int len, uint8_t* outData) {
    for (int i = 0; i < len; ++i) {
        outData[i] = linearToMulaw(samples[i]);
    }
}

// Decode μ-law to 16-bit PCM samples
void AudioHandler::decodeMuLaw(const uint8_t* data, int len, int16_t* outSamples){
    for (int i = 0; i < len; ++i) {
        outSamples[i] = mulawToLinear(data[i]);
    }
}

// Send audio data to I2S output
void AudioHandler::playAudio(const int16_t* samples, int len) {
    // Create output buffer for I2S left channel
    int32_t outBuf[len];
    for (int i = 0; i < len; ++i) {
        outBuf[i] = samples[i] << 16; // Left channel data placed in upper 16 bits
    }
    i2sManager.write(outBuf, len); // Write to I2S using I2SManager
}

uint8_t AudioHandler::linearToMulaw(int16_t sample) {
  const float MU = 255.0f;
  float x = (float)sample / 32768.0f;
  float sign = (x < 0) ? -1.0f : 1.0f;
  x = fabs(x);
  float companded = sign * (log(1.0f + MU * x) / log(1.0f + MU));
  int8_t encoded = (int8_t)(companded * 127.0f);
  return (uint8_t)encoded;
}

int16_t AudioHandler::mulawToLinear(uint8_t muSample) {
  const float MU = 255.0f;
  float x = (float)((int8_t)muSample) / 127.0f;
  float sign = (x < 0) ? -1.0f : 1.0f;
  x = fabs(x);
  float linear = sign * ((pow(1.0f + MU, x) - 1.0f) / MU);
  return (int16_t)(linear * 32767.0f);
}