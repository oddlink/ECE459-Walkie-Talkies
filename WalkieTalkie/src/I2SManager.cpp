#include "I2SManager.h"
#include <driver/i2s.h>

I2SManager::I2SManager(i2s_port_t port, int sampleRate, int wsPin, int sckPin, int sdPin, int sdOutPin)
    : i2sPort(port), sampleRate(sampleRate), wsPin(wsPin), sckPin(sckPin), sdPin(sdPin), sdOutPin(sdOutPin) {}

void I2SManager::init() {
    // uninstall any previously installed driver on this port (safe)
    i2s_driver_uninstall(i2sPort);

    // I2S configuration
    i2s_config_t cfg = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
        .sample_rate = sampleRate,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT, // use 32-bit frames for flexibility
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, // stereo frames
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 6,
        .dma_buf_len = 256,
        .use_apll = false,//true,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    // I2S pin configuration
    i2s_pin_config_t pins = {
        .bck_io_num = sckPin,
        .ws_io_num = wsPin,
        .data_out_num = sdOutPin,
        .data_in_num = sdPin
    };

    // Install and configure I2S driver, pins, and clks
    i2s_driver_install(i2sPort, &cfg, 0, NULL);
    i2s_set_pin(i2sPort, &pins);
    i2s_set_clk(i2sPort, sampleRate, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_STEREO); // THIS IS DIFF THAN ABOVE 
    i2s_zero_dma_buffer(i2sPort);
}

void I2SManager::write(const int32_t* data, size_t len) {
    size_t bytesWritten;
    i2s_write(i2sPort, data, len * sizeof(int32_t), &bytesWritten, portMAX_DELAY);
}

void I2SManager::read(int32_t* data, size_t len) {
    size_t bytesRead;
    i2s_read(i2sPort, data, len * sizeof(int32_t), &bytesRead, portMAX_DELAY);
}