#ifndef I2S_MANAGER_H
#define I2S_MANAGER_H

#include <driver/i2s.h>
#include <stdint.h>

class I2SManager {
public:
    I2SManager(i2s_port_t port, int sampleRate, int wsPin, int sckPin, int sdPin, int sdOutPin);
    void init();
    void write(const int32_t* data, size_t len);
    void read(int32_t* data, size_t len);

private:
    i2s_port_t i2sPort;
    int sampleRate;
    int wsPin, sckPin, sdPin, sdOutPin;
};

#endif // I2S_MANAGER_H