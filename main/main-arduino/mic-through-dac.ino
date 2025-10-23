#include <Arduino.h>
#include "driver/i2s.h"
#include "driver/adc.h"

// I2S pins
const int DAC_BCK  = 26;
const int DAC_WS   = 25;
const int DAC_DATA = 22;
#define I2S_PORT I2S_NUM_0

const int SAMPLE_RATE = 16000;
const int BUF_SAMPLES = 256; // frames per buffer

// ADC input: GPIO34 -> ADC1_CHANNEL_6
const adc1_channel_t MIC_ADC_CHANNEL = ADC1_CHANNEL_6;

void setupI2S() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags = 0,
    .dma_buf_count = 4,
    .dma_buf_len = BUF_SAMPLES,
    .use_apll = false,
    .tx_desc_auto_clear = true
  };
  i2s_driver_install(I2S_PORT, &cfg, 0, NULL);

  i2s_pin_config_t pins = {
    .bck_io_num = DAC_BCK,
    .ws_io_num = DAC_WS,
    .data_out_num = DAC_DATA,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_set_pin(I2S_PORT, &pins);
  i2s_zero_dma_buffer(I2S_PORT);
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("Mic passthrough: ADC (GPIO34) -> I2S DAC");
  setupI2S();

  // Configure ADC1: 12-bit width and attenuation for full-scale
  adc1_config_width(ADC_WIDTH_BIT_12);                 // 0..4095
  adc1_config_channel_atten(MIC_ADC_CHANNEL, ADC_ATTEN_DB_11); // full-scale ~3.3V
}

void loop() {
  // interleaved stereo buffer: BUF_SAMPLES frames * 2 channels
  static int16_t buffer[BUF_SAMPLES * 2];

  // Fill the buffer with samples from the ADC
  for (int i = 0; i < BUF_SAMPLES; ++i) {
    // Read raw ADC (0..4095 for 12-bit)
    int raw = adc1_get_raw(MIC_ADC_CHANNEL);

    // Convert 12-bit unsigned (0..4095) to signed 16-bit PCM:
    // Step 1: center around zero (-2048..+2047)
    // Step 2: scale up  (<< 4) to map roughly to -32768..+32752
    int16_t pcm = (int16_t)((raw - 2048) << 4);

    // Duplicate mono input to left and right channels (stereo)
    buffer[2*i] = pcm;       // left
    buffer[2*i + 1] = pcm;   // right
  }

  // Write the buffer to I2S (blocks until space is available)
  size_t bytes_written = 0;
  i2s_write(I2S_PORT, (const char*)buffer, sizeof(buffer), &bytes_written, portMAX_DELAY);
}
