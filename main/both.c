#include <WiFi.h>
#include <driver/i2s.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <esp_now.h>
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
#define SAMPLE_RATE     16000
#define GAIN            5

// ---------- RF CONFIG ----------
#define CE_PIN  4
#define CSN_PIN 5
RF24 radio(CE_PIN, CSN_PIN);
const byte address[6] = "00001";

// ---------- WIFI CONFIG ----------
#define WIFI_CHANNEL     6
bool broadcast_mode = true;
uint8_t BroadcastMac[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// ---------- STATE ----------
bool usingWifi = false;
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

// ---------- BUTTON ISR ----------
void IRAM_ATTR onButtonPress() {
  if (ignoreButton) return;

  unsigned long now = esp_timer_get_time() / 1000;
  if (now - lastISRTime > debounceDelay) {
    ButtonState = !ButtonState;
    lastISRTime = now;
  }
}

// ---------- WIFI SETUP ----------
void setPeers () {
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, BroadcastMac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.print("Failed to add peer");
  }
}

void setChannel(uint8_t ch) {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void OnDataRecv(const uint8_t *info, const uint8_t *data, int len) {
  // Expecting incoming buffer of int16_t samples
  if (len <= 0) return;

  // Mark that we are actively receiving (prevent button toggles)
  receivingActive = true;
  ignoreButton = true;
  digitalWrite(LED_TX, HIGH);
  lastRecvMillis = millis();

  // Don't play back while we're in sending state (still keep this guard)
  if (sending) return;

  // μ-law decode from 8-bit to 16-bit
  int sampleCount = len;
  int16_t inSamples[sampleCount];
  for (int i = 0; i < sampleCount; ++i) {
    inSamples[i] = mulawToLinear(((uint8_t*)data)[i]);
  }


  // Prepare output buffer: each I2S "frame" is a 32-bit sample per channel, we will create stereo pairs
  // so outSamples length = sampleCount * 2 (left+right) of int32_t
  // For memory safety, chunk the write if sampleCount is large.
  const int CHUNK = 128;
  int idx = 0;
  while (idx < sampleCount) {
    int chunkSamples = min(CHUNK, sampleCount - idx);
    int32_t outBuf[CHUNK * 2]; // stereo
    for (int i = 0; i < chunkSamples; ++i) {
      int16_t s = inSamples[idx + i];
      // Convert 16-bit to 32-bit left-aligned (MSB) for the DAC:
      // place 16-bit sample into high 16 bits of 32-bit word (common approach)
      int32_t s32 = ((int32_t)s) << 16;
      // duplicate to stereo
      outBuf[2 * i + 0] = s32;
      outBuf[2 * i + 1] = s32;
    }
    size_t bytes_written = 0;
    i2s_write(I2S_NUM_0, outBuf, chunkSamples * 2 * sizeof(int32_t), &bytes_written, portMAX_DELAY);
    idx += chunkSamples;

    lastRecvMillis = millis();
  }
}

// ---------- I2S SETUP ----------
bool i2sInstalled = false;
void setupI2SFullDuplex() {
  if (i2sInstalled) return;
  i2s_driver_uninstall(I2S_NUM_0);

  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
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

  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_set_clk(I2S_NUM_0, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
  i2s_zero_dma_buffer(I2S_NUM_0);

  i2sInstalled = true;
}

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);

  pinMode(LED_TX, OUTPUT);
  pinMode(LED_RX, OUTPUT);
  digitalWrite(LED_TX, LOW);
  digitalWrite(LED_RX, LOW);

  pinMode(BUTTON_PIN, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonPress, CHANGE);

  WiFi.mode(WIFI_STA);
  setupI2SFullDuplex();

  // --- RADIO INIT ---
  if (!radio.begin()) {
    Serial.println("Radio not responding!");
    while (1);
  }
  radio.openWritingPipe(address);
  radio.openReadingPipe(1, address);
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_1MBPS);
  radio.startListening();

  Serial.println("RF Walkie-Talkie Ready");

  setChannel(WIFI_CHANNEL);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Failed");
    return;
  }
  setPeers();
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  Serial.println("WIFI Walkie-Talkie Ready");


}

// ---------- MAIN LOOP ----------
void loop() {

  if (ignoreButton && (millis() - lastRecvMillis > RECEIVE_TIMEOUT_MS)) {
    ignoreButton = false;
    receivingActive = false;
    digitalWrite(LED_TX, LOW);
    // optional: give user feedback that button is enabled again
    Serial.println("Receive idle — button re-enabled");
  }

  // handle button toggle
  if (ButtonState != prevButtonState) {
    prevButtonState = ButtonState;
    sending = ButtonState;
    digitalWrite(LED_RX, ButtonState);
    if (sending) {
      if (usingWifi) {
        esp_now_register_recv_cb(NULL);
      Serial.println("WIFI Switching to SENDING (mic -> network)");
      }
      else {
        Serial.println("PTT pressed → TRANSMIT mode");
        digitalWrite(LED_TX, HIGH);
        digitalWrite(LED_RX, LOW);
        radio.stopListening();
      }
    } else {
      if (usingWifi) {
        esp_now_register_recv_cb(OnDataRecv);
        Serial.println("WIFI Switching to RECEIVING (network -> speaker)");
      }
      else {
        Serial.println("PTT released → RECEIVE mode");
        digitalWrite(LED_TX, LOW);
        digitalWrite(LED_RX, HIGH);
        radio.startListening();
      }
    }
  }

  // ---------- TRANSMIT ----------
  if (sending) {
    if (usingWifi) {
      const int SAMPLES = 100; // number of frames to read (frames are stereo 32-bit words)
      int32_t inBuf[SAMPLES]; // read 32-bit frames (we configured stereo but mic likely uses left only)
      size_t bytes_read = 0;
      esp_err_t r = i2s_read(I2S_NUM_0, inBuf, sizeof(inBuf), &bytes_read, portMAX_DELAY);
      if (r == ESP_OK && bytes_read > 0) {
        int framesRead = bytes_read / sizeof(int32_t); // number of 32-bit frames read
        int16_t outSamples[framesRead];
        for (int i = 0; i < framesRead; ++i) {
          int32_t w = inBuf[i];
          int16_t s16 = (int16_t)(w >> 16);
          int32_t amplified = (int32_t)s16 * GAIN;
          if (amplified > 32767) amplified = 32767;
          if (amplified < -32768) amplified = -32768;
          outSamples[i] = (int16_t)amplified;
        }
        if (r == ESP_OK && bytes_read > 0) {
          int framesRead = bytes_read / sizeof(int32_t);
          int16_t outSamples[framesRead];
          for (int i = 0; i < framesRead; ++i) {
            int32_t w = inBuf[i];
            int16_t s16 = (int16_t)(w >> 16);
            int32_t amplified = (int32_t)s16 * GAIN;
            if (amplified > 32767) amplified = 32767;
            if (amplified < -32768) amplified = -32768;
            outSamples[i] = (int16_t)amplified;
          }
          uint8_t encoded[framesRead];
          for (int i = 0; i < framesRead; ++i) {
            encoded[i] = linearToMulaw(outSamples[i]);
          }
          esp_now_send(BroadcastMac, encoded, framesRead);
        }
      }
      delay(2);
    }
    else { // using rf
      const int FRAMES = 32; // small chunk for RF packet
      int32_t inBuf[FRAMES];
      size_t bytesRead = 0;
      esp_err_t r = i2s_read(I2S_NUM_0, inBuf, sizeof(inBuf), &bytesRead, portMAX_DELAY);
      if (r == ESP_OK && bytesRead > 0) {
        int frames = bytesRead / sizeof(int32_t);
        uint8_t encoded[FRAMES];
        for (int i = 0; i < frames; i++) {
          int16_t s16 = (int16_t)(inBuf[i] >> 16);
          int32_t amplified = (int32_t)s16 * GAIN;
          if (amplified > 32767) amplified = 32767;
          if (amplified < -32768) amplified = -32768;
          encoded[i] = linearToMulaw((int16_t)amplified);
        }
        radio.write(&encoded, frames);
      }
    }
  }

  // ---------- RECEIVE ----------
  if (!usingWifi && !sending && radio.available()) {
    // receivingActive = true;
    //ignoreButton = true;
    // digitalWrite(LED_TX, HIGH);
    //lastRecvMillis = millis();
    uint8_t encoded[32];
    radio.read(&encoded, sizeof(encoded));

    int16_t decoded[32];
    for (int i = 0; i < 32; i++) {
      decoded[i] = mulawToLinear(encoded[i]);
    }

    int32_t outBuf[64];
    for (int i = 0; i < 32; i++) {
      int32_t s32 = ((int32_t)decoded[i]) << 16;
      outBuf[2 * i] = s32;
      outBuf[2 * i + 1] = s32;
    }
    size_t written;
    i2s_write(I2S_NUM_0, outBuf, sizeof(outBuf), &written, portMAX_DELAY);
  }
}
