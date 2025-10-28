
/*
  ESP32 Walkie-Talkie (Receiver)
  - Receives μ-law audio blocks over ESP-NOW
  - Decodes to 16-bit PCM
  - Plays out via I2S to external DAC (BCLK/LRCLK/DOUT)
*/

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include "driver/i2s.h"

// -------- PINS (adjust to your wiring) --------
#define I2S_PORT         I2S_NUM_0
#define I2S_BCK_PIN      26   // BCLK
#define I2S_WS_PIN       25   // LRCLK/WS
#define I2S_SD_OUT_PIN   22   // DOUT to DAC
// ---------------------------------------------

// -------- AUDIO SETTINGS --------
#define SAMPLE_RATE      16000
#define BLOCK_SAMPLES    160
// --------------------------------

// -------- ESP-NOW / WIFI --------
#define WIFI_CHANNEL     6
// --------------------------------

// μ-law decode
static inline int16_t ulaw2linear(uint8_t u_val) {
  u_val = ~u_val;
  int t = ((u_val & 0x0F) << 3) + 0x84;
  t <<= ((unsigned)u_val & 0x70) >> 4;
  return (u_val & 0x80) ? (0x84 - t) : (t - 0x84);
}

typedef struct __attribute__((packed)) {
  uint32_t seq;
  uint16_t sr;       // sr/100 expected 160
  uint16_t n;
  uint8_t  data[BLOCK_SAMPLES];
} audio_pkt_t;

// Optional: simple jitter info
volatile uint32_t last_seq = 0;
volatile uint32_t recv_count = 0;

#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
void onRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  if (len < (int)(sizeof(uint32_t)+sizeof(uint16_t)+sizeof(uint16_t))) return;
  const audio_pkt_t *pkt = (const audio_pkt_t*)incomingData;
  recv_count++;
  last_seq = pkt->seq;

  // Decode into 32-bit I2S frames (stereo duplicated)
  static int32_t out32[BLOCK_SAMPLES*2];
  int n = pkt->n;
  if (n > BLOCK_SAMPLES) n = BLOCK_SAMPLES;
  for (int i=0; i<n; ++i) {
    int16_t s16 = ulaw2linear(pkt->data[i]);
    int32_t s32 = ((int32_t)s16) << 16;  // 16->32 align MSBs
    out32[2*i+0] = s32;
    out32[2*i+1] = s32;
  }
  size_t written = 0;
  i2s_write(I2S_PORT, (const char*)out32, n*2*sizeof(int32_t), &written, portMAX_DELAY);
}
#else
void onRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  if (len < (int)(sizeof(uint32_t)+sizeof(uint16_t)+sizeof(uint16_t))) return;
  const audio_pkt_t *pkt = (const audio_pkt_t*)incomingData;
  recv_count++;
  last_seq = pkt->seq;

  static int32_t out32[BLOCK_SAMPLES*2];
  int n = pkt->n;
  if (n > BLOCK_SAMPLES) n = BLOCK_SAMPLES;
  for (int i=0; i<n; ++i) {
    int16_t s16 = ulaw2linear(pkt->data[i]);
    int32_t s32 = ((int32_t)s16) << 16;
    out32[2*i+0] = s32;
    out32[2*i+1] = s32;
  }
  size_t written = 0;
  i2s_write(I2S_PORT, (const char*)out32, n*2*sizeof(int32_t), &written, portMAX_DELAY);
}
#endif

void setChannel(uint8_t ch) {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
}

void i2sDacBegin() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 6,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pins = {
    .bck_io_num = I2S_BCK_PIN,
    .ws_io_num = I2S_WS_PIN,
    .data_out_num = I2S_SD_OUT_PIN,
    .data_in_num = -1
  };
  i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_PORT, &pins);
  i2s_set_clk(I2S_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_STEREO);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("ESP-NOW Audio RX @16kHz mu-law");

  WiFi.mode(WIFI_STA);
  setChannel(WIFI_CHANNEL);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed"); while(1) delay(1000);
  }
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
  esp_now_register_recv_cb(onRecv);
#else
  esp_now_register_recv_cb(onRecv);
#endif
  i2sDacBegin();
}

void loop() {
  // Optional: periodic stats
  static uint32_t last = 0;
  if (millis() - last > 1000) {
    last = millis();
    Serial.printf("recv=%u last_seq=%u\n", (unsigned)recv_count, (unsigned)last_seq);
  }
}
