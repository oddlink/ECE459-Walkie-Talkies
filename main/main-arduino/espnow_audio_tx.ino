
/*
  ESP32 Walkie-Talkie (Transmitter)
  - Captures mono audio from I2S mic (e.g., INMP441)
  - μ-law encodes to 8-bit @ 16 kHz
  - Sends blocks over ESP-NOW broadcast
*/

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include "driver/i2s.h"

// -------- PINS --------
#define I2S_PORT         I2S_NUM_0
#define I2S_BCK_PIN      26   // BCLK
#define I2S_WS_PIN       25   // LRCLK/WS
#define I2S_SD_IN_PIN    32   // INMP441 SD -> ESP32
// ---------------------------------------------

// -------- AUDIO SETTINGS --------
#define SAMPLE_RATE      16000
#define BLOCK_SAMPLES    160     // 10 ms @ 16 kHz
// μ-law gives 1 byte per sample => 160 bytes payload per block
// --------------------------------

// -------- ESP-NOW / WIFI --------
#define WIFI_CHANNEL     6
static uint8_t BCAST_ADDR[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
// --------------------------------

// ===== μ-law encode/decode =====
static inline uint8_t linear2ulaw(int16_t pcm) {
  const uint16_t BIAS = 0x84; // 132
  const uint16_t CLIP = 32635;
  uint16_t mag;
  uint8_t sign;
  uint8_t exponent;
  uint8_t mantissa;
  uint8_t ulawbyte;

  sign = (pcm < 0) ? 0x80 : 0x00;
  if (pcm < 0) pcm = -pcm;
  if (pcm > CLIP) pcm = CLIP;
  pcm = pcm + BIAS;
  // Convert linear to ulaw
  static const uint16_t exp_lut[8] = {0x000,0x020,0x040,0x080,0x100,0x200,0x400,0x800};
  exponent = 7;
  for (int i=7; i>0; --i) {
    if (pcm >= exp_lut[i]) { exponent = i; break; }
  }
  mantissa = (pcm >> (exponent + 3)) & 0x0F;
  ulawbyte = ~(sign | (exponent << 4) | mantissa);
  return ulawbyte;
}

// ===== Packet format =====
typedef struct __attribute__((packed)) {
  uint32_t seq;
  uint16_t sr;       // sample rate / 100 (160 = 16 kHz)
  uint16_t n;        // samples in block
  uint8_t  data[BLOCK_SAMPLES]; // μ-law data
} audio_pkt_t;

volatile uint32_t seq_no = 0;

// ===== Version-safe callbacks (send only needed here) =====
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
void onSend(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  // Optional: log send status
}
#else
void onSend(const uint8_t *mac, esp_now_send_status_t status) {
  // Optional: log send status
}
#endif

bool addPeerBroadcast(uint8_t ch) {
  esp_now_peer_info_t p{};
  memcpy(p.peer_addr, BCAST_ADDR, 6);
  p.channel = ch;
  p.encrypt = false;
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
  p.ifidx = WIFI_IF_STA;
#endif
  if (esp_now_is_peer_exist(BCAST_ADDR)) esp_now_del_peer(BCAST_ADDR);
  return esp_now_add_peer(&p) == ESP_OK;
}

void setChannel(uint8_t ch) {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
}

void i2sMicBegin() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pins = {
    .bck_io_num = I2S_BCK_PIN,
    .ws_io_num = I2S_WS_PIN,
    .data_out_num = -1,
    .data_in_num = I2S_SD_IN_PIN
  };
  i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_PORT, &pins);
  i2s_set_clk(I2S_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("ESP-NOW Audio TX @ 16kHz, mu-law");

  WiFi.mode(WIFI_STA);
  setChannel(WIFI_CHANNEL);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed"); while(1) delay(1000);
  }
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
  esp_now_register_send_cb(onSend);
#else
  esp_now_register_send_cb(onSend);
#endif
  if (!addPeerBroadcast(WIFI_CHANNEL)) {
    Serial.println("Peer add failed"); while(1) delay(1000);
  }

  i2sMicBegin();
}

void loop() {
  // Read 32-bit samples from I2S mic, convert to 16-bit, μ-law encode
  const int SAMPLES = BLOCK_SAMPLES;
  int32_t in32[SAMPLES];
  size_t bytesRead = 0;
  int frames = i2s_read(I2S_PORT, (void*)in32, SAMPLES * sizeof(int32_t), &bytesRead, portMAX_DELAY) == ESP_OK
               ? (bytesRead / sizeof(int32_t)) : 0;
  if (frames <= 0) return;

  audio_pkt_t pkt{};
  pkt.seq = seq_no++;
  pkt.sr = SAMPLE_RATE / 100;
  pkt.n  = frames;

  for (int i=0; i<frames && i<BLOCK_SAMPLES; ++i) {
    // INMP441 outputs 24-bit in 32-bit word, typically MSB-aligned
    int32_t s = in32[i] >> 8;      // reduce to ~24->16 bits
    if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
    pkt.data[i] = linear2ulaw((int16_t)s);
  }

  esp_now_send(BCAST_ADDR, (uint8_t*)&pkt, sizeof(uint32_t)+sizeof(uint16_t)+sizeof(uint16_t)+pkt.n);
}
