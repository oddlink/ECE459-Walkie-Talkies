// WORKINGGGGGGGG - communication
// #include <Arduino.h>
// #include "driver/i2s.h"

// // PCM5102 pin connections
// const int I2S_BCK  = 26;  // Bit Clock
// const int I2S_WS   = 25;  // Left/Right Clock (Word Select)
// const int I2S_DATA = 22;  // Serial Data
// #define I2S_PORT I2S_NUM_0

// // Audio settings
// const int SAMPLE_RATE = 16000;   // 16 kHz sample rate
// const float TONE_FREQ = 440.0f;  // 440 Hz sine wave
// const int BUF_SAMPLES = 256;     // Samples per buffer

// void setupI2S()
// {
//   i2s_config_t config = {
//     .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
//     .sample_rate = SAMPLE_RATE,
//     .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
//     .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,  // stereo
//     .communication_format = I2S_COMM_FORMAT_I2S_MSB,
//     .intr_alloc_flags = 0,
//     .dma_buf_count = 4,
//     .dma_buf_len = BUF_SAMPLES,
//     .use_apll = false,
//     .tx_desc_auto_clear = true
//   };

//   i2s_driver_install(I2S_PORT, &config, 0, NULL);

//   i2s_pin_config_t pins = {
//     .bck_io_num = I2S_BCK,
//     .ws_io_num = I2S_WS,
//     .data_out_num = I2S_DATA,
//     .data_in_num = I2S_PIN_NO_CHANGE
//   };

//   i2s_set_pin(I2S_PORT, &pins);
//   i2s_zero_dma_buffer(I2S_PORT);
// }

// void setup() {
//   Serial.begin(115200);
//   delay(50);
//   Serial.println("PCM5102 I2S test: generating 440Hz sine wave");
//   setupI2S();
// }

// void loop() {
//   static int16_t buffer[256 * 2];
//   static float phase = 0.0f;
//   const float phaseInc = 2.0f * PI * 440.0f / 16000.0f;

//   for (int i = 0; i < 256; ++i) {
//     float s = sinf(phase);
//     phase += phaseInc;
//     if (phase >= 2.0f * PI) phase -= 2.0f * PI;
//     int16_t val = (int16_t)(s * 32767);
//     buffer[2*i] = val;
//     buffer[2*i + 1] = val;
//   }

//   size_t written;
//   i2s_write(I2S_NUM_0, (const char*)buffer, sizeof(buffer), &written, portMAX_DELAY);

//   // brief status print every second so serial remains human-readable
//   static unsigned long lastPrint = 0;
//   if (millis() - lastPrint > 1000) {
//     Serial.println("I2S sending 440Hz tone...");
//     lastPrint = millis();
//   }
// }



// three tones
#include <Arduino.h>
#include "driver/i2s.h"

const int DAC_BCK  = 26;
const int DAC_WS   = 25;
const int DAC_DATA = 22;
#define I2S_PORT I2S_NUM_0

const int SAMPLE_RATE = 16000;
const int BUF_SAMPLES = 256;

// Define three tone frequencies (in Hz)
const float toneList[] = { 1000.0f, 1500.0f, 700.0f, 3000.0f };
const int numTones = sizeof(toneList) / sizeof(toneList[0]);

int currentToneIndex = 0;
float toneFreq = toneList[currentToneIndex];

unsigned long lastSwitch = 0;
const int SWITCH_INTERVAL = 1000;  // ms

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
  Serial.println("Alternating 3-tone test");
  setupI2S();
}

void loop() {
  static int16_t buffer[BUF_SAMPLES * 2];
  static float phase = 0.0f;

  // Switch to the next tone every SWITCH_INTERVAL ms
  if (millis() - lastSwitch > SWITCH_INTERVAL) {
    currentToneIndex = (currentToneIndex + 1) % numTones;
    toneFreq = toneList[currentToneIndex];
    lastSwitch = millis();
    Serial.print("Switched to tone: ");
    Serial.println(toneFreq);
  }

  // Calculate phase increment for current tone
  float phaseInc = 2.0f * PI * toneFreq / SAMPLE_RATE;

  // Fill buffer with sine wave samples
  for (int i = 0; i < BUF_SAMPLES; ++i) {
    float s = sinf(phase);
    phase += phaseInc;
    if (phase >= 2.0f * PI) phase -= 2.0f * PI;
    int16_t v = (int16_t)(s * 32767);
    buffer[2*i] = buffer[2*i+1] = v;  // stereo
  }

  size_t written;
  i2s_write(I2S_PORT, (const char*)buffer, sizeof(buffer), &written, portMAX_DELAY);
}