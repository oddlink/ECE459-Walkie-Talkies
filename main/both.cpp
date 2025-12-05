#include <WiFi.h>
#include <esp_now.h>
#include <driver/i2s.h>
#include <SPI.h>
#include "esp_system.h"
#include <esp_wifi.h>
#include <nRF24L01.h>
#include <RF24.h>

// ====== μ-law ======
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

// ====== Pins ======
#define I2S_WS          25    // LRCLK
#define I2S_SCK         26    // BCLK
#define I2S_SD          32    // Mic data in
#define I2S_SD_OUT_PIN  22    // DAC / speaker data out

#define BUTTON_PIN      21
#define RED_LED         15    // receiving indicator
#define GREEN_LED       2     // Wi-Fi TX indicator
#define YELLOW_LED      13    // RF TX indicator

// ====== Audio/I2S ======
#define SAMPLE_RATE       8000
#define PACKET_SAMPLES    32   // 32 μ-law bytes per packet
#define GAIN              1
static const i2s_port_t I2S_PORT = I2S_NUM_0;

static void setupI2S() {
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
    .data_out_num = I2S_SD_OUT_PIN,
    .data_in_num = I2S_SD
  };

  i2s_driver_uninstall(I2S_PORT);
  ESP_ERROR_CHECK(i2s_driver_install(I2S_PORT, &cfg, 0, NULL));
  ESP_ERROR_CHECK(i2s_set_pin(I2S_PORT, &pins));
  ESP_ERROR_CHECK(i2s_set_clk(I2S_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO));
  i2s_zero_dma_buffer(I2S_PORT);
}

// ====== Wi-Fi / ESP-NOW ======
#define WIFI_CHANNEL   6
static const uint8_t BroadcastMac[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
static const char PING_STR[12] = "ANYONE HOME";  // 12 bytes

static void setChannel(uint8_t ch) {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
}
static void addBroadcastPeer() {
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BroadcastMac, 6);
  peer.channel = 0;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
}

// ====== RF (nRF24L01) ======
#define CE_PIN   4
#define CSN_PIN  5
RF24 radio(CE_PIN, CSN_PIN);
static const byte RF_ADDR[5] = {'W', 'A', 'L', 'K', 'I'};

static bool radioSetup() {
  if (!radio.begin()) return false;
  // radio.setRetries(5, 15);
  radio.setAutoAck(false);
  radio.openWritingPipe(RF_ADDR);
  radio.openReadingPipe(1, RF_ADDR);
  radio.setChannel(90);
  radio.setPALevel(RF24_PA_MAX);
  radio.setDataRate(RF24_250KBPS);
  radio.setPayloadSize(32);
  radio.flush_tx();
  radio.flush_rx();
  radio.startListening();
  return true;
}

// ====== State ======
volatile bool buttonEdge = false;
volatile unsigned long lastISRTime = 0;
const unsigned long debounceMs = 300;

bool sending = false;            // PTT state (true = talk)
bool wifiSelected = false;       // true if Wi-Fi path selected
volatile bool wifiAcked = false; // becomes true when we get a 6-byte MAC ACK

// receive-side UX lockout so PTT can’t interrupt in the middle of RX audio
volatile bool ignoreButton = false;
unsigned long lastRecvMillis = 0;
const unsigned long RECEIVE_TIMEOUT_MS = 50;
volatile bool last_pressed_during_recv = false;

// ====== Button ISR ======
void IRAM_ATTR onButton() {
  if (ignoreButton) return;
  unsigned long now = (unsigned long)(esp_timer_get_time() / 1000);
  if (now - lastISRTime > debounceMs) {
    buttonEdge = true;      // edge detected, handle in loop()
    lastISRTime = now;
  }

}

// ====== ESP-NOW callbacks ======
static void OnDataSent(const uint8_t *mac, esp_now_send_status_t status) {
  // Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Send OK" : "Send FAIL");
}

// Handle three cases by packet length:
// 12 bytes --> PING_STR --> reply with MAC (6 bytes) to acknowledge
// 6 bytes --> MAC ACK --> wifiAcked=true (sender side)
// 32 bytes --> μ-law audio --> decode to I2S (receiver side, if not currently sending)
volatile int ack_count;
static void OnDataRecv(const uint8_t *mac_info, const uint8_t *data, int len) {
  if (len == 12 && memcmp(data, PING_STR, 12) == 0) {
    // Got ping: reply with our STA MAC as 6-byte ACK
    uint8_t mymac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mymac);
    esp_now_send(BroadcastMac, mymac, 6);
    return;
  }

  if (len == 6 && sending) {
    // Any well-formed 6-byte ACK marks Wi-Fi available
    ack_count++;
    if(ack_count == 2){
      wifiAcked = true;
    }
    return;
  }

  if (len != PACKET_SAMPLES) return;  // ignore unexpected sizes

  // playback path (only when not sending)
  ignoreButton = true;
  digitalWrite(RED_LED, HIGH);
  lastRecvMillis = millis();

  if (sending){
    return;
  }
  int16_t decoded[PACKET_SAMPLES];
  for (int i = 0; i < PACKET_SAMPLES; ++i) decoded[i] = mulawToLinear(data[i]);

  int32_t outBuf[PACKET_SAMPLES];
  for (int i = 0; i < PACKET_SAMPLES; ++i) outBuf[i] = ((int32_t)decoded[i]) << 16;

  size_t written = 0;
  i2s_write(I2S_PORT, outBuf, PACKET_SAMPLES * sizeof(int32_t), &written, portMAX_DELAY);
}

void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButton, CHANGE);

  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);

  WiFi.mode(WIFI_STA);
  setupI2S();

  // ESP-NOW init
  setChannel(WIFI_CHANNEL);
  if (esp_now_init() == ESP_OK) {
    addBroadcastPeer();
    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);
  } else {
    Serial.println("ESP-NOW init failed (continuing with RF available).");
  }

  // RF init
  if (!radioSetup()) {
    Serial.println("RF24 not responding!");
  }

  Serial.println("Unified Walkie-Talkie ready: Wi-Fi first, RF fallback.");
}

void loop() {
  if(buttonEdge){
    Serial.println(digitalRead(BUTTON_PIN));
  }
  
  // Re-enable button after RX idle
  if (ignoreButton && (millis() - lastRecvMillis > RECEIVE_TIMEOUT_MS)) {
    // last_pressed_during_recv = true;
    ignoreButton = false;
    digitalWrite(RED_LED, LOW);
    // Serial.println("RX idle — PTT re-enabled");
  }
  else if(ignoreButton && buttonEdge){
    last_pressed_during_recv = true;
  }

  // Handle PTT edge (press/release)
  if (buttonEdge && !last_pressed_during_recv) {
    buttonEdge = false;
    sending = !sending;

    if (sending) {
      // Enter TX: decide path (Wi-Fi first, then RF fallback if no ACK)
      wifiAcked = false;
      wifiSelected = false;

      // stop RF RX
      radio.stopListening();
      delayMicroseconds(130);
      esp_now_register_recv_cb(NULL);

      // ping on ESP-NOW
      if (esp_now_is_peer_exist(BroadcastMac)) {
        esp_now_send(BroadcastMac, (const uint8_t*)PING_STR, 12);

        const unsigned long startMs = millis();
        const unsigned long timeoutMs = 1500;
        while (!wifiAcked && (millis() - startMs < timeoutMs) && !buttonEdge) {
          delay(10);
        }
      }
  
      if (wifiAcked && !buttonEdge) {
        ack_count = 0;
        wifiSelected = true;
        digitalWrite(GREEN_LED, HIGH);
        digitalWrite(YELLOW_LED, LOW);
        Serial.println("Wi-Fi path ACKed → using ESP-NOW");
      } 
      else if (buttonEdge){
        sending = false;  // Cancel TX attempt
        buttonEdge = false;
        radio.startListening();
        return;
      }
      else {
        wifiSelected = false;
        digitalWrite(GREEN_LED, LOW);
        digitalWrite(YELLOW_LED, HIGH);
        Serial.println("No Wi-Fi ACK → falling back to RF");
      }
    } else {
      // Enter RX
      digitalWrite(GREEN_LED, LOW);
      digitalWrite(YELLOW_LED, LOW);
      esp_now_register_recv_cb(OnDataRecv);
      radio.startListening();
      delayMicroseconds(130);
      // radio.flush_rx(); 
      // radio.flush_tx();
      Serial.println("RX mode");
    }
  }
  else if (buttonEdge && last_pressed_during_recv) {
    Serial.println("here");
    last_pressed_during_recv = false;
    buttonEdge = false;
  }

  // ====== TX ======
  if (sending) {
    // mic read
    const int N = PACKET_SAMPLES;
    int32_t inBuf[N];
    size_t bytesRead = 0;
    if (i2s_read(I2S_PORT, inBuf, sizeof(inBuf), &bytesRead, portMAX_DELAY) == ESP_OK && bytesRead >= (int)sizeof(int32_t)) {
      int frames = (int)(bytesRead / sizeof(int32_t));
      if (frames > N) frames = N;

      uint8_t encoded[N];
      for (int i = 0; i < frames; ++i) {
        int16_t s16 = (int16_t)(inBuf[i] >> 16);
        int32_t amplified = (int32_t)s16 * GAIN;
        if (amplified > 32767) amplified = 32767;
        if (amplified < -32768) amplified = -32768;
        encoded[i] = linearToMulaw((int16_t)amplified);
      }
      for (int i = frames; i < N; ++i) encoded[i] = linearToMulaw(0);

      if (wifiSelected) {
        // Wi-Fi/ESP-NOW TX
        esp_now_send(BroadcastMac, encoded, PACKET_SAMPLES);
        delay(2); // let things stabilize
      } else {
        // RF TX
        auto err = radio.write(encoded, 32);
        delay(2);
        Serial.println(err);
      }
    }
  }

  // ====== RX (RF side) ======
  if (!sending && radio.available()) {
    ignoreButton = true;
    uint8_t encoded[32];
    radio.read(encoded, sizeof(encoded));

    int16_t decoded[32];
    for (int i = 0; i < 32; ++i) decoded[i] = mulawToLinear(encoded[i]);

    int32_t outBuf[32];
    for (int i = 0; i < 32; ++i) outBuf[i] = ((int32_t)decoded[i]) << 16;

    size_t written = 0;
    i2s_write(I2S_PORT, outBuf, sizeof(outBuf), &written, portMAX_DELAY);

    // UX: show RX LED briefly
    digitalWrite(RED_LED, HIGH);
    lastRecvMillis = millis();
  }
}
