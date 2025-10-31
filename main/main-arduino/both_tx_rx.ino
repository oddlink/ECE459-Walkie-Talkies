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



#define BUTTON_PIN       21   // Push button

// keep mic pins (your existing)
// I2S0 (mic)
#define I2S_MIC_PORT     I2S_NUM_0
#define I2S_MIC_BCK_PIN  26   // BCLK (mic)
#define I2S_MIC_WS_PIN   25   // LRCLK/WS (mic)
#define I2S_SD_IN_PIN    32   // SD (mic -> ESP32)

// new DAC pins for I2S1 (TX)
#define I2S_DAC_PORT     I2S_NUM_1
#define I2S_DAC_BCK_PIN  26   // BCLK (DAC)
#define I2S_DAC_WS_PIN   25   // LRCLK/WS (DAC)
#define I2S_SD_OUT_PIN   22   // DOUT (ESP32 -> DAC)



// -------- AUDIO SETTINGS --------
#define SAMPLE_RATE      16000
#define BLOCK_SAMPLES    160     // 10 ms @ 16 kHz
// μ-law gives 1 byte per sample => 160 bytes payload per block

// -------- ESP-NOW / WIFI --------
#define WIFI_CHANNEL     6
static uint8_t BCAST_ADDR[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

volatile bool buttonFlag = false;
unsigned long lastISRTime = 0;
const unsigned long debounceDelay = 200;

void IRAM_ATTR onButtonPress() {
  unsigned long now = (unsigned long) (esp_timer_get_time() / 1000);
  if (now - lastISRTime > debounceDelay) {
    buttonFlag = !buttonFlag; //will turn on on rising edge and off on falling edge
    lastISRTime = now;
  }
}


// ===== μ-law encode =====
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

// ===== μ-law decode =====
static inline int16_t ulaw2linear(uint8_t u_val) {
  u_val = ~u_val;
  int t = ((u_val & 0x0F) << 3) + 0x84;
  t <<= ((unsigned)u_val & 0x70) >> 4;
  return (u_val & 0x80) ? (0x84 - t) : (t - 0x84);
}

// ===== Packet format =====
typedef struct __attribute__((packed)) {
  uint32_t seq;
  uint16_t sr;       // sample rate / 100 (160 = 16 kHz)
  uint16_t n;        // samples in block
  uint8_t  data[BLOCK_SAMPLES]; // μ-law data
} audio_pkt_t;


// // Optional: simple jitter info

volatile uint32_t seq_no = 0;
volatile uint32_t last_seq = 0;
volatile uint32_t recv_count = 0;

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

#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
void onRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  Serial.println("got to recv1");
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
  esp_err_t write_err = i2s_write(I2S_DAC_PORT, (const char*)out32, n*2*sizeof(int32_t), &written, portMAX_DELAY);
  if (write_err == ESP_OK) {
    Serial.println("wrote ok");
  }
  else {
    Serial.println(write_err);
    Serial.println("did not write ok");
  }
}
#else
void onRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  Serial.println("got to recv2");
  if (len < (int)(sizeof(uint32_t)+sizeof(uint16_t)+sizeof(uint16_t))) return;
  const audio_pkt_t *pkt = (const audio_pkt_t*)incomingData;
  Serial.println("got in front of for loop");
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
  esp_err_t write_err = i2s_write(I2S_DAC_PORT, (const char*)out32, n*2*sizeof(int32_t), &written, portMAX_DELAY);
  if (write_err == ESP_OK) {
    Serial.println("wrote ok");
  }
  else {
    Serial.println(write_err);
    Serial.println("did not write ok");
  }
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
    .bck_io_num = I2S_MIC_BCK_PIN,
    .ws_io_num = I2S_MIC_WS_PIN,
    .data_out_num = -1,
    .data_in_num = I2S_SD_IN_PIN
  };
  i2s_driver_install(I2S_MIC_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_MIC_PORT, &pins);
  i2s_set_clk(I2S_MIC_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
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
    .bck_io_num = I2S_DAC_BCK_PIN,
    .ws_io_num = I2S_DAC_WS_PIN,
    .data_out_num = I2S_SD_OUT_PIN,
    .data_in_num = -1
  };
  i2s_driver_install(I2S_DAC_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_DAC_PORT, &pins);
  i2s_set_clk(I2S_DAC_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_STEREO);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("ESP-NOW Audio TX @ 16kHz, mu-law");

  pinMode(BUTTON_PIN, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonPress, CHANGE); //want to catch both rising and falling edge of the button

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

#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
  esp_now_register_recv_cb(onRecv);
#else
  esp_now_register_recv_cb(onRecv);
#endif

  if (!addPeerBroadcast(WIFI_CHANNEL)) {
    Serial.println("Peer add failed"); while(1) delay(1000);
  }

  i2sMicBegin();
  i2sDacBegin();
}

void loop() {
    if (buttonFlag) {
    // Read 32-bit samples from I2S mic, convert to 16-bit, μ-law encode
    const int SAMPLES = BLOCK_SAMPLES;
    int32_t in32[SAMPLES];
    size_t bytesRead = 0;
    int frames = (i2s_read(I2S_MIC_PORT, (void*)in32, SAMPLES * sizeof(int32_t), &bytesRead, portMAX_DELAY) == ESP_OK)
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
    // if(buttonFlag){
      esp_err_t my_err = esp_now_send(BCAST_ADDR, (uint8_t*)&pkt, sizeof(uint32_t)+sizeof(uint16_t)+sizeof(uint16_t)+pkt.n);
      if (my_err == ESP_OK) {
        Serial.println("no error so happy");
      }
      else {
        Serial.println("yes error so sad");
      }
    }
}
