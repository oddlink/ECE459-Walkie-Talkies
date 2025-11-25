#include <Arduino.h>
#include <driver/i2s.h>

// ===== Pins (from your project) =====
#define I2S_WS          25    // LRCLK
#define I2S_SCK         26    // BCLK
#define I2S_SD_IN       32    // Mic data in
#define I2S_SD_OUT      22    // Speaker data out

// ===== Audio parameters =====
#define SAMPLE_RATE      8000
#define FRAME_SAMPLES    256
#define GAIN             2.0f   // amplify mic slightly
static const i2s_port_t I2S_PORT = I2S_NUM_0;

// ===== I²S setup =====
void setupI2S() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 6,
    .dma_buf_len = FRAME_SAMPLES,
    .use_apll = true,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pins = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_SD_OUT,
    .data_in_num = I2S_SD_IN
  };

  i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_PORT, &pins);
  i2s_set_clk(I2S_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
  i2s_zero_dma_buffer(I2S_PORT);
}

void setup() {
  Serial.begin(115200);
  setupI2S();
  Serial.println("🎙️ Mic → Speaker loopback started (no transmission)");
}

void loop() {
  static int32_t buffer[FRAME_SAMPLES];
  size_t bytesRead = 0;

  // read a block from mic
  esp_err_t res = i2s_read(I2S_PORT, buffer, sizeof(buffer), &bytesRead, portMAX_DELAY);
  if (res == ESP_OK && bytesRead > 0) {
    int samples = bytesRead / sizeof(int32_t);

    // amplify & send to speaker
    for (int i = 0; i < samples; i++) {
      int16_t s = (int16_t)(buffer[i] >> 16);  // 32-bit to 16-bit
      int32_t amplified = (int32_t)(s * GAIN);
      if (amplified > 32767) amplified = 32767;
      if (amplified < -32768) amplified = -32768;
      buffer[i] = amplified << 16;
    }

    size_t bytesWritten;
    i2s_write(I2S_PORT, buffer, samples * sizeof(int32_t), &bytesWritten, portMAX_DELAY);
  }
}
