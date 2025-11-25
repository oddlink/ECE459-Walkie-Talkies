#include <Arduino.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <driver/i2s.h>
#include <math.h>

// ========== CONFIG ==========
#define IS_TRANSMITTER true   // <-- set true for the sender, false for the receiver
#define SAMPLE_RATE     8000
#define PACKET_SAMPLES  32

// I2S pins (same as your project)
#define I2S_WS          25
#define I2S_SCK         26
#define I2S_SD_IN       32   // mic data in
#define I2S_SD_OUT      22   // speaker data out

// nRF24L01 pins
#define CE_PIN  4
#define CSN_PIN 5

// Shared address for both
const byte RF_ADDR[5] = {'A','U','D','I','O'};

// ========== μ-law Encode/Decode ==========
static inline uint8_t linearToMulaw(int16_t sample) {
  const float MU = 255.0f;
  float x = (float)sample / 32768.0f;
  float sign = (x < 0) ? -1.0f : 1.0f;
  x = fabsf(x);
  float companded = sign * (logf(1.0f + MU * x) / logf(1.0f + MU));
  int8_t encoded = (int8_t)(companded * 127.0f);
  return (uint8_t)encoded;
}

static inline int16_t mulawToLinear(uint8_t muSample) {
  const float MU = 255.0f;
  float x = (float)((int8_t)muSample) / 127.0f;
  float sign = (x < 0) ? -1.0f : 1.0f;
  x = fabsf(x);
  float linear = sign * ((powf(1.0f + MU, x) - 1.0f) / MU);
  return (int16_t)(linear * 32767.0f);
}

// ========== I²S Setup ==========
static const i2s_port_t I2S_PORT = I2S_NUM_0;

void setupI2S() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 6,
    .dma_buf_len = 256,
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

// ========== Radio Setup ==========
RF24 radio(CE_PIN, CSN_PIN);

void setupRadio() {
  if (!radio.begin()) {
    Serial.println("⚠️ nRF24L01 not responding — check wiring!");
    while (1) delay(1000);
  }
  radio.setChannel(90);
  radio.setPALevel(RF24_PA_MAX);
  radio.setDataRate(RF24_250KBPS); // more reliable for audio
  radio.setAutoAck(false);
  radio.setPayloadSize(PACKET_SAMPLES);
  radio.openWritingPipe(RF_ADDR);
  radio.openReadingPipe(1, RF_ADDR);

  if (IS_TRANSMITTER) {
    radio.stopListening();
  } else {
    radio.startListening();
  }

  Serial.println(IS_TRANSMITTER ? "🎙️ Transmitter ready" : "🔊 Receiver ready");
}

// ========== Setup ==========
void setup() {
  Serial.begin(115200);
  setupI2S();
  setupRadio();
  Serial.println("Audio RF link running (no button)");
}

// ========== Main Loop ==========
void loop() {
  if (IS_TRANSMITTER) {
    // Read mic samples and send packets
    int32_t micBuf[PACKET_SAMPLES];
    size_t bytesRead = 0;

    if (i2s_read(I2S_PORT, micBuf, sizeof(micBuf), &bytesRead, portMAX_DELAY) == ESP_OK) {
      int frames = bytesRead / sizeof(int32_t);
      if (frames > PACKET_SAMPLES) frames = PACKET_SAMPLES;

      uint8_t encoded[PACKET_SAMPLES];
      for (int i = 0; i < frames; i++) {
        int16_t s16 = (int16_t)(micBuf[i] >> 16);
        encoded[i] = linearToMulaw(s16);
      }

      bool ok = radio.write(encoded, PACKET_SAMPLES);
      if (!ok) {
        Serial.println("❌ RF send failed");
      }
    }
  } 
  else {
    // Receive μ-law packets and play
    if (radio.available()) {
      uint8_t encoded[PACKET_SAMPLES];
      radio.read(encoded, sizeof(encoded));

      int16_t decoded[PACKET_SAMPLES];
      int32_t outBuf[PACKET_SAMPLES];
      for (int i = 0; i < PACKET_SAMPLES; ++i) {
        decoded[i] = mulawToLinear(encoded[i]);
        outBuf[i] = ((int32_t)decoded[i]) << 16;
      }

      size_t written;
      i2s_write(I2S_PORT, outBuf, sizeof(outBuf), &written, portMAX_DELAY);
    }
  }

  delay(2); // small pacing delay
}
