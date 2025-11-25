#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <driver/i2s.h>
#include <math.h>

// ======== CONFIGURATION ========
#define IS_TRANSMITTER true   // <-- set true for the sender, false for the receiver

#define SAMPLE_RATE       8000
#define PACKET_SAMPLES    64
#define WIFI_CHANNEL      6

// I²S pins (same as your project)
#define I2S_WS            25
#define I2S_SCK           26
#define I2S_SD_IN         32   // mic data in
#define I2S_SD_OUT        22   // speaker data out

static const i2s_port_t I2S_PORT = I2S_NUM_0;

// μ-law encode/decode
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

// ======== I2S SETUP ========
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

// ======== ESP-NOW SETUP ========
static const uint8_t BROADCAST_ADDR[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

void setWiFiChannel(uint8_t ch) {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
}

void addBroadcastPeer() {
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BROADCAST_ADDR, 6);
  peer.channel = WIFI_CHANNEL;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
}

void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != PACKET_SAMPLES) return;

  int16_t decoded[PACKET_SAMPLES];
  int32_t outBuf[PACKET_SAMPLES];

  for (int i = 0; i < PACKET_SAMPLES; ++i) {
    decoded[i] = mulawToLinear(data[i]);
    outBuf[i] = ((int32_t)decoded[i]) << 16;
  }

  size_t written;
  i2s_write(I2S_PORT, outBuf, sizeof(outBuf), &written, portMAX_DELAY);
}

// ======== MAIN ========
void setup() {
  Serial.begin(115200);
  Serial.println(IS_TRANSMITTER ? "🎙️ Transmitter starting..." : "🔊 Receiver starting...");

  WiFi.mode(WIFI_STA);
  setWiFiChannel(WIFI_CHANNEL);

  setupI2S();

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed!");
    while (1) delay(1000);
  }
  addBroadcastPeer();

  if (!IS_TRANSMITTER) {
    esp_now_register_recv_cb(onDataRecv);
  }

  Serial.println("✅ Ready");
}

void loop() {
  if (IS_TRANSMITTER) {
    // ===== MIC CAPTURE =====
    const int N = PACKET_SAMPLES;
    int32_t inBuf[N];
    size_t bytesRead = 0;
    if (i2s_read(I2S_PORT, inBuf, sizeof(inBuf), &bytesRead, portMAX_DELAY) == ESP_OK) {
      int frames = bytesRead / sizeof(int32_t);
      if (frames > N) frames = N;

      uint8_t encoded[N];
      for (int i = 0; i < frames; ++i) {
        int16_t s16 = (int16_t)(inBuf[i] >> 16);
        encoded[i] = linearToMulaw(s16);
      }

      esp_now_send(BROADCAST_ADDR, encoded, PACKET_SAMPLES);
      delay(2);  // brief pacing to avoid flooding
    }
  } else {
    delay(10);
  }
}
