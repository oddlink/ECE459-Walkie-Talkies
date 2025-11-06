#include <WiFi.h>
#include <driver/i2s.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

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

// ---------- RF CONFIG ----------
#define CE_PIN  4
#define CSN_PIN 5
RF24 radio(CE_PIN, CSN_PIN);
const byte address[6] = "00001";

// ---------- STATE ----------
volatile bool buttonPressed = false;
bool sending = false;
unsigned long lastPress = 0;
const unsigned long debounceDelay = 50;

// ---- RX ring buffer ----
const int PACKET_SAMPLES = 32;
const int BUFFER_PACKETS = 10;
uint8_t rxBuffer[BUFFER_PACKETS][PACKET_SAMPLES];
volatile int head = 0;
volatile int tail = 0;
volatile int count = 0;
unsigned long lastPlay = 0;
const unsigned long PLAY_INTERVAL_US = (PACKET_SAMPLES * 1000000UL) / SAMPLE_RATE; // 4000 µs


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
  unsigned long now = millis();
  if (now - lastPress > debounceDelay) {
    buttonPressed = true;
    lastPress = now;
  }
}

// ---------- I2S SETUP ----------
void setupI2S() {
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
  i2s_set_clk(I2S_NUM_0, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
  i2s_zero_dma_buffer(I2S_NUM_0);

  
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

  setupI2S();

  // --- RADIO INIT ---
  if (!radio.begin()) {
    Serial.println("Radio not responding!");
    while (1);
  }
  radio.openWritingPipe(address);
  radio.openReadingPipe(1, address);
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_1MBPS);
  radio.setPayloadSize(32);
  radio.setAutoAck(false);
  radio.startListening();

  Serial.println("RF Walkie-Talkie Ready");
}

// ---------- MAIN LOOP ----------
void loop() {
  // handle button toggle
  if (buttonPressed) {
    buttonPressed = false;
    sending = !sending;
    if (sending) {
      Serial.println("PTT pressed → TRANSMIT mode");
      digitalWrite(LED_TX, HIGH);
      digitalWrite(LED_RX, LOW);
      radio.stopListening();
    } else {
      Serial.println("PTT released → RECEIVE mode");
      digitalWrite(LED_TX, LOW);
      digitalWrite(LED_RX, HIGH);
      radio.startListening();
    }
  }

  // ---------- TRANSMIT ----------
  if (sending) {
    const int FRAMES = 32; // small chunk for RF packet
    int32_t inBuf[FRAMES];
    size_t bytesRead = 0;
    esp_err_t r = i2s_read(I2S_NUM_0, inBuf, sizeof(inBuf), &bytesRead, portMAX_DELAY);
    Serial.printf("Raw: 0x%08X  shifted: %d\n", inBuf[0], (int16_t)(inBuf[0] >> 16));
    if (r == ESP_OK && bytesRead > 0) {
        int frames = bytesRead / sizeof(int32_t);
        uint8_t encoded[FRAMES];
        for (int i = 0; i < frames; i++) {
            int16_t s16 = (int16_t)(inBuf[i] >> 16);
            int32_t amplified = (int32_t)s16 * GAIN;
            if (amplified >= 32767 || amplified <= -32768) Serial.println("CLIP");
            if (amplified > 32767) amplified = 32767;
            if (amplified < -32768) amplified = -32768;
            encoded[i] = linearToMulaw((int16_t)amplified);
        }
        radio.write(encoded, 32);
        //delayMicroseconds((32UL * 1000000UL) / SAMPLE_RATE);  // ≈4000 µs for 8 kHz
        // static unsigned long lastTxMicros = 0;
        // unsigned long now = micros();
        // if (lastTxMicros) {
        //     unsigned long dt = now - lastTxMicros;
        //     Serial.printf("TX interval: %lu us\n", dt);
        // }
        // lastTxMicros = now;
    }
  }

    // else if (!sending) {

    // // --- 1. Store one packet if available ---
    // static unsigned long lastRx = 0;
    // if (radio.available()) {
    //     // unsigned long now = micros();
    //     // if (lastRx) Serial.printf("RX interval: %lu us\n", now - lastRx);
    //     // lastRx = now;
    //     uint8_t encoded[PACKET_SAMPLES];
    //     radio.read(encoded, sizeof(encoded));

    //     // push into circular buffer
    //     if (count < BUFFER_PACKETS) {
    //     memcpy(rxBuffer[head], encoded, PACKET_SAMPLES);
    //     head = (head + 1) % BUFFER_PACKETS;
    //     count++;
    //     } else {
    //     // buffer full → overwrite oldest
    //     tail = (tail + 1) % BUFFER_PACKETS;
    //     memcpy(rxBuffer[head], encoded, PACKET_SAMPLES);
    //     head = (head + 1) % BUFFER_PACKETS;
    //     }
    // }

    // // --- 2. Fixed-rate playout ---
    // unsigned long now = micros();
    // if (now - lastPlay >= PLAY_INTERVAL_US && count > 0) {
    //     lastPlay = now;

    //     // pop one packet
    //     uint8_t encoded[PACKET_SAMPLES];
    //     memcpy(encoded, rxBuffer[tail], PACKET_SAMPLES);
    //     tail = (tail + 1) % BUFFER_PACKETS;
    //     count--;

    //     // decode μ-law to 16-bit PCM
    //     // decode μ-law to 16-bit PCM
    //     int16_t decoded[PACKET_SAMPLES];
    //     for (int i = 0; i < PACKET_SAMPLES; i++) {
    //         decoded[i] = mulawToLinear(encoded[i]);
    //     }


    //     // convert to 32-bit stereo for I²S
    //     int32_t outBuf[PACKET_SAMPLES * 2];
    //     for (int i = 0; i < PACKET_SAMPLES; i++) {
    //     int32_t s32 = ((int32_t)decoded[i]) << 16;
    //     outBuf[2 * i] = s32;
    //     outBuf[2 * i + 1] = s32;
    //     }

    //     size_t written;
    //     i2s_write(I2S_NUM_0, outBuf, sizeof(outBuf), &written, portMAX_DELAY);
    // }
    // }
  // ---------- RECEIVE ----------
  else if (!sending && radio.available()) {
    static unsigned long lastRx = 0;
    unsigned long now = micros();
    if (lastRx) {
    unsigned long dt = now - lastRx;
    Serial.printf("RX interval = %lu us\n", dt);
    }
    lastRx = now;
    uint8_t encoded[32];
    radio.read(encoded, sizeof(encoded));

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
