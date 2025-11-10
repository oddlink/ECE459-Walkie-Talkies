#ifndef AUDIO_HANDLER_H
#define AUDIO_HANDLER_H

#include "I2SManager.h"
#include <stdint.h>
#include <cmath>

class AudioHandler {
public:
    AudioHandler(I2SManager& i2sManager);
    void encodeMuLaw(const int16_t* samples, int len, uint8_t* outData);
    void decodeMuLaw(const uint8_t* data, int len, int16_t* outSamples);
    void playAudio(const int16_t* samples, int len);
    uint8_t linearToMulaw(int16_t sample);
    int16_t mulawToLinear(uint8_t muSample);

private:
    I2SManager& i2sManager;
};

#endif // AUDIO_HANDLER_H