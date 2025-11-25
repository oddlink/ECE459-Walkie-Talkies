#include <Arduino.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <driver/i2s.h>

// ===== Pins =====
#define CE_PIN        4
#define CSN_PIN       5
#define BUTTON_PIN    21
#define I2S_WS        25
#define I2S_SCK       26
#define I2S_SD_OUT    22

// ===== I2S config =====
#define SAMPLE_RATE   8000
static const i2s_port_t I2S_PORT = I2S_NUM_0;

// ===== RF =====
RF24 radio(CE_PIN, CSN_PIN);
const byte RF_ADDR[5] = {'W','A','L','K','I'};

// ===== Helper: setup I2S speaker =====
void setupI2S() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
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
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_PORT, &pins);
  i2s_set_clk(I2S_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
  i2s_zero_dma_buffer(I2S_PORT);
}

// ===== Tone generator =====
void playTone(float freq, int durationMs, float volume = 0.3f) {
  const int CHUNK = 128;
  int32_t buf[CHUNK];
  const double phaseInc = 2 * M_PI * freq / SAMPLE_RATE;
  double phase = 0;

  int totalSamples = SAMPLE_RATE * durationMs / 1000;
  while (totalSamples > 0) {
    int n = min(totalSamples, CHUNK);
    for (int i = 0; i < n; ++i) {
      int16_t s = (int16_t)(sin(phase) * volume * 32767);
      buf[i] = ((int32_t)s) << 16;
      phase += phaseInc;
      if (phase > 2 * M_PI) phase -= 2 * M_PI;
    }
    size_t written;
    i2s_write(I2S_PORT, buf, n * sizeof(int32_t), &written, portMAX_DELAY);
    totalSamples -= n;
  }
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLDOWN);
  setupI2S();

  if (!radio.begin()) {
    Serial.println("⚠️ RF24 not responding!");
    while (1) delay(1000);
  }
  radio.setChannel(90);
  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_1MBPS);
  radio.openWritingPipe(RF_ADDR);
  radio.openReadingPipe(1, RF_ADDR);
  radio.startListening();

  Serial.println("🎧 RF Tone Test ready. Press the button to send tones!");
}

// ===== Loop =====
void loop() {
  static bool lastState = LOW;
  bool pressed = digitalRead(BUTTON_PIN);

  // When this node’s button is pressed → send tone command
  if (pressed && !lastState) {
    Serial.println("📤 Button pressed → Sending tone command...");
    radio.stopListening();
    const char msg[] = "BEEPSEQ";
    radio.write(&msg, sizeof(msg));
    radio.startListening();
  }
  lastState = pressed;

  // Receive tone commands
  if (radio.available()) {
    char msg[32] = {0};
    radio.read(&msg, sizeof(msg));
    Serial.printf("📩 Received: %s\n", msg);

    if (strcmp(msg, "BEEPSEQ") == 0) {
      Serial.println("🎵 Playing received tone sequence...");
      playTone(440, 200);
      playTone(660, 200);
      playTone(880, 200);
      Serial.println("✅ Tone sequence done.");
    }
  }

  delay(10);
}
