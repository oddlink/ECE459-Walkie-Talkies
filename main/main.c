#include <WiFi.h>
#include <esp_now.h>
#include <driver/i2s.h>
#include <SPI.h>
#include "esp_system.h"
#include <esp_wifi.h>

#define I2S_WS      25  // LR clock (WS)
#define I2S_SCK     26  // BCLK
#define I2S_SD      32  // Data in from INMP441
#define I2S_SD_OUT_PIN   22   // Data out to DAC

#define BUTTON      21

#define WIFI_CHANNEL     6
bool broadcast_mode = true;
uint8_t BroadcastMac[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
const int GAIN = 5;

const i2s_port_t I2S_PORT = I2S_NUM_0; // single I2S port used for both RX and TX

// state
bool ButtonState = true;
volatile unsigned long lastISRTime = 0;
const unsigned long debounceDelay = 200;
bool sending = false;
bool prevButtonState = true;

void IRAM_ATTR onButtonPress() {
  unsigned long now = (unsigned long) (esp_timer_get_time() / 1000);
  if (now - lastISRTime > debounceDelay) {
    ButtonState = !ButtonState;
    lastISRTime = now;
  }
}

void setPeer () {
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, BroadcastMac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
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

  // Don't play back while we're in sending state
  if (sending) return;

  // len bytes == N * 2 (int16_t). We'll convert each int16 sample to a 32-bit I2S word,
  // duplicate to both channels (stereo) and write to I2S TX.
  int16_t *inSamples = (int16_t *)data;
  int sampleCount = len / 2;

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
    i2s_write(I2S_PORT, outBuf, chunkSamples * 2 * sizeof(int32_t), &bytes_written, portMAX_DELAY);
    idx += chunkSamples;
  }
}

// Setup single I2S driver in full-duplex mode
bool i2sInstalled = false;
void setupI2SFullDuplex() {
  if (i2sInstalled) return;

  // uninstall any previously installed driver on this port (safe)
  i2s_driver_uninstall(I2S_PORT);

  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT, // use 32-bit frames for flexibility
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, // stereo frames
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

  esp_err_t err = i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("Failed installing I2S driver: %d\n", err);
    return;
  }
  err = i2s_set_pin(I2S_PORT, &pins);
  if (err != ESP_OK) {
    Serial.printf("Failed setting I2S pins: %d\n", err);
    return;
  }
  // set clock explicitly (sample rate, bits, channels)
  i2s_set_clk(I2S_PORT, 16000, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_STEREO);
  i2s_zero_dma_buffer(I2S_PORT);

  i2sInstalled = true;
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(BUTTON), onButtonPress, CHANGE);

  WiFi.mode(WIFI_STA);
  setupI2SFullDuplex();

  setChannel(WIFI_CHANNEL);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Failed");
    return;
  }
  setPeer();
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  Serial.println("Walkie Talkie (single I2S full-duplex) Set Up");
}

void loop() {
  // detect edge change and switch sending/receiving state
  if (ButtonState != prevButtonState) {
    prevButtonState = ButtonState;
    sending = ButtonState;
    if (sending) {
      // stop rx callback while sending (optional)
      esp_now_register_recv_cb(NULL);
      Serial.println("Switching to SENDING (mic -> network)");
    } else {
      // restore rx callback for playback
      esp_now_register_recv_cb(OnDataRecv);
      Serial.println("Switching to RECEIVING (network -> speaker)");
    }
  }

  if (sending) {
    // Capture from I2S RX, convert to int16_t network samples, send via ESP-NOW
    const int SAMPLES = 100; // number of frames to read (frames are stereo 32-bit words)
    // we'll read SAMPLES * 1 (frame per sample) but we only care about one channel (mic is left)
    int32_t inBuf[SAMPLES]; // read 32-bit frames (we configured stereo but mic likely uses left only)
    size_t bytes_read = 0;
    // read SAMPLES frames of 32-bit each (mono word per frame because channel_format is stereo but mic provides data in left)
    esp_err_t r = i2s_read(I2S_PORT, inBuf, sizeof(inBuf), &bytes_read, portMAX_DELAY);
    if (r == ESP_OK && bytes_read > 0) {
      int framesRead = bytes_read / sizeof(int32_t); // number of 32-bit frames read
      // convert frames to int16_t samples for sending
      int16_t outSamples[framesRead];
      for (int i = 0; i < framesRead; ++i) {
        int32_t w = inBuf[i];
        // Extract a 16-bit sample from the 32-bit word.
        // Common INMP441 alignment: 32-bit word left-aligned with 24-bit data in high bits.
        // Here we assume high 16 bits contain the meaningful audio; shift right 16 to get 16-bit sample.
        // If your mic alignment differs, change this shift.
        int16_t s16 = (int16_t)(w >> 16);

        // apply gain safely
        int32_t amplified = (int32_t)s16 * GAIN;
        if (amplified > 32767) amplified = 32767;
        if (amplified < -32768) amplified = -32768;
        outSamples[i] = (int16_t)amplified;
      }
      // send the data over ESP-NOW
      esp_now_send(BroadcastMac, (uint8_t*)outSamples, framesRead * sizeof(int16_t));
    }
    delay(2);
  } // end sending
}
