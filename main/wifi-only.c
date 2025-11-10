#include <WiFi.h>
#include <esp_now.h>
#include <driver/i2s.h>
#include <SPI.h>
#include "esp_system.h"
#include <esp_wifi.h>

// ---------- I2S PIN CONFIG ----------
#define I2S_WS          25
#define I2S_SCK         26
#define I2S_SD          32
#define I2S_SD_OUT_PIN  22

// ---------- CONTROL PINS ----------
#define BUTTON_PIN      21
#define LED_TX          15
#define LED_RX          2

// ---------- AUDIO SETTINGS ----------
#define SAMPLE_RATE     8000
#define GAIN            1

// ---------- NETWORK / PACKET ----------
#define WIFI_CHANNEL     6
bool broadcast_mode = true;
uint8_t BroadcastMac[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// must match RF code: 32 samples per packet
const int PACKET_SAMPLES = 32;

// ---------- STATE ----------
bool ButtonState = false;
volatile unsigned long lastISRTime = 0;
const unsigned long debounceDelay = 40;
bool sending = false;
bool prevButtonState = true;
volatile bool ignoreButton = false;
unsigned long lastRecvMillis = 0;     // updated by OnDataRecv when audio arrives
const unsigned long RECEIVE_TIMEOUT_MS = 800; // time after last packet to re-enable button
bool receivingActive = false;

// ---------- μ-LAW ----------
uint8_t linearToMulaw(int16_t sample) {
  const float MU = 255.0f;
  float x = (float)sample / 32768.0f;
  float sign = (x < 0) ? -1.0f : 1.0f;
  x = fabs(x);
  float companded = sign * (log(1.0f + MU * x) / log(1.0f + MU));
  int8_t encoded = (int8_t)(companded * 127.0f);
  return (uint8_t)encoded;
}

int16_t mulawToLinear(uint8_t muSample) {
  const float MU = 255.0f;
  float x = (float)((int8_t)muSample) / 127.0f;
  float sign = (x < 0) ? -1.0f : 1.0f;
  x = fabs(x);
  float linear = sign * ((pow(1.0f + MU, x) - 1.0f) / MU);
  return (int16_t)(linear * 32767.0f);
}

// ---------- Button ISR ----------
void IRAM_ATTR onButtonPress() {
  if (ignoreButton) return;
  unsigned long now = (unsigned long) (esp_timer_get_time() / 1000);
  if (now - lastISRTime > debounceDelay) {
    ButtonState = !ButtonState;
    lastISRTime = now;
  }
}

// ---------- ESP-NOW helpers ----------
void setPeers () {
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, BroadcastMac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
}

void setChannel(uint8_t ch) {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // optional debug
  // Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

// ---------- I2S setup (mono) ----------
void setupI2S() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT, // mono, use only right channel
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
    .data_out_num = I2S_SD_OUT_PIN,
    .data_in_num = I2S_SD
  };

  i2s_driver_uninstall(I2S_NUM_0);
  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
  // mono clock (one 32-bit word per sample)
  i2s_set_clk(I2S_NUM_0, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

// ---------- OnDataRecv (ESP-NOW receive callback) ----------
// Note: keep this callback small and deterministic. We decode and write to I2S immediately.
// Expect exactly PACKET_SAMPLES bytes per packet; ignore others (defensive).
void OnDataRecv(const uint8_t *info, const uint8_t *data, int len) {
  if (len != PACKET_SAMPLES) {
    // ignore unexpected-size packets (you may log once for debugging)
    // Serial.printf("Got packet size %d (expected %d)\n", len, PACKET_SAMPLES);
    return;
  }

  // Mark receiving so button is disabled briefly
  receivingActive = true;
  ignoreButton = true;
  digitalWrite(LED_TX, HIGH);
  lastRecvMillis = millis();

  if (sending) return; // do not play if we are currently sending

  // Decode μ-law into int16_t
  int16_t decoded[PACKET_SAMPLES];
  for (int i = 0; i < PACKET_SAMPLES; ++i) {
    decoded[i] = mulawToLinear(data[i]);
  }

  // Convert to 32-bit mono frames and write exactly PACKET_SAMPLES frames
  int32_t outBuf[PACKET_SAMPLES];
  for (int i = 0; i < PACKET_SAMPLES; ++i) {
    outBuf[i] = ((int32_t)decoded[i]) << 16; // left-align into 32-bit word
  }
  size_t bytes_written = 0;
  i2s_write(I2S_NUM_0, outBuf, PACKET_SAMPLES * sizeof(int32_t), &bytes_written, portMAX_DELAY);

  // done; update lastRecvMillis above
}

// ---------- setup / loop ----------
void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonPress, CHANGE);

  pinMode(LED_TX, OUTPUT);
  digitalWrite(LED_TX, 0);
  pinMode(LED_RX, OUTPUT);
  digitalWrite(LED_RX, 0);

  WiFi.mode(WIFI_STA);
  setupI2S();

  setChannel(WIFI_CHANNEL);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Failed");
    while (1);
  }
  setPeers();
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  Serial.println("Walkie Talkie (ESP-NOW, mono 8kHz) Set Up");
}

void loop() {
  // Re-enable button after receive inactivity
  if (ignoreButton && (millis() - lastRecvMillis > RECEIVE_TIMEOUT_MS)) {
    ignoreButton = false;
    receivingActive = false;
    digitalWrite(LED_TX, LOW);
    Serial.println("Receive idle — button re-enabled");
  }

  // Edge detection to change sending/receiving
  if (ButtonState != prevButtonState) {
    prevButtonState = ButtonState;
    sending = ButtonState;
    digitalWrite(LED_RX, ButtonState);

    if (sending) {
      // disable RX callback while we send (optional)
      esp_now_register_recv_cb(NULL);
      Serial.println("Switching to SENDING (mic -> network)");
    } else {
      // restore receive callback
      esp_now_register_recv_cb(OnDataRecv);
      Serial.println("Switching to RECEIVING (network -> speaker)");
    }
  }

  // ---------- TRANSMIT ----------
  if (sending) {
    // Read exactly PACKET_SAMPLES frames (each frame is 32-bit word)
    const int SAMPLES = PACKET_SAMPLES;
    int32_t inBuf[SAMPLES];
    size_t bytes_read = 0;
    esp_err_t r = i2s_read(I2S_NUM_0, inBuf, sizeof(inBuf), &bytes_read, portMAX_DELAY);
    if (r == ESP_OK && bytes_read >= sizeof(int32_t)) {
      int framesRead = bytes_read / sizeof(int32_t);
      if (framesRead > SAMPLES) framesRead = SAMPLES; // defensive cap

      // Convert frames -> int16 samples and μ-law encode
      uint8_t encoded[PACKET_SAMPLES];
      for (int i = 0; i < framesRead; ++i) {
        int16_t s16 = (int16_t)(inBuf[i] >> 16);   // extract top 16 bits (as in RF code)
        int32_t amplified = (int32_t)s16 * GAIN;
        if (amplified > 32767) amplified = 32767;
        if (amplified < -32768) amplified = -32768;
        encoded[i] = linearToMulaw((int16_t)amplified);
      }
      // If framesRead < PACKET_SAMPLES, zero-pad the rest
      for (int i = framesRead; i < PACKET_SAMPLES; ++i) encoded[i] = linearToMulaw(0);

      // send one full packet (32 bytes)
      esp_err_t res = esp_now_send(BroadcastMac, encoded, PACKET_SAMPLES);
      // Serial.printf("esp_now_send res=%d\n", res);

    }
  }
}
