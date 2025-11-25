#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>

// ===== Pins (from your main code) =====
#define I2S_WS          25    // LRCLK
#define I2S_SCK         26    // BCLK
#define I2S_SD_OUT_PIN  22    // DAC / speaker data out
#define BUTTON_PIN      21

// ===== I²S Config =====
#define SAMPLE_RATE     8000
static const i2s_port_t I2S_PORT = I2S_NUM_0;

// Small chunk size to avoid large stack use
#define CHUNK_SAMPLES 128

// A reusable chunk buffer allocated in global scope (not on the stack)
static int32_t i2sChunk[CHUNK_SAMPLES];

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
    .data_out_num = I2S_SD_OUT_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  // don't call uninstall() unconditionally (avoids the "not installed" error)
  // i2s_driver_uninstall(I2S_PORT); // removed

  ESP_ERROR_CHECK(i2s_driver_install(I2S_PORT, &cfg, 0, NULL));
  ESP_ERROR_CHECK(i2s_set_pin(I2S_PORT, &pins));
  ESP_ERROR_CHECK(i2s_set_clk(I2S_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO));
  i2s_zero_dma_buffer(I2S_PORT);
}

// Play tone frequency `freq` for durationMs milliseconds at given volume (0..1)
// Generates audio in CHUNK_SAMPLES pieces so we don't allocate big buffers on the stack.
void playToneChunked(float freq, int durationMs, float volume = 0.3f) {
  if (durationMs <= 0) return;
  const int totalSamples = (SAMPLE_RATE * durationMs) / 1000;
  int samplesRemaining = totalSamples;
  // phase accumulator for continuity between chunks
  double phase = 0.0;
  const double twoPiF = 2.0 * M_PI * freq;
  const double phaseIncrementPerSample = twoPiF / (double)SAMPLE_RATE;

  while (samplesRemaining > 0) {
    int thisChunk = (samplesRemaining > CHUNK_SAMPLES) ? CHUNK_SAMPLES : samplesRemaining;
    for (int i = 0; i < thisChunk; ++i) {
      double t = phase + i * phaseIncrementPerSample;
      // compute 16-bit sample then shift into top 16 bits of 32-bit container
      int16_t s = (int16_t)(sin(t) * volume * 32767.0);
      i2sChunk[i] = ((int32_t)s) << 16;
    }
    size_t written = 0;
    // write the chunk (note: blocking write)
    i2s_write(I2S_PORT, i2sChunk, thisChunk * sizeof(int32_t), &written, portMAX_DELAY);

    // advance phase and remaining samples
    phase += thisChunk * phaseIncrementPerSample;
    // keep phase reasonable to avoid overflow
    if (phase > 1e6) phase = fmod(phase, 2.0 * M_PI);
    samplesRemaining -= thisChunk;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLDOWN);
  setupI2S();
  Serial.println("Tone test ready. Press the button to play tones.");
}

void loop() {
  static bool lastState = LOW;
  bool current = digitalRead(BUTTON_PIN);
  if (current && !lastState) {
    Serial.println("🎵 Button pressed — playing tones...");
    // Play a short 3-tone sequence (uses chunked generation)
    playToneChunked(440.0f, 300, 0.3f);  // A4
    playToneChunked(660.0f, 300, 0.3f);  // E5
    playToneChunked(880.0f, 300, 0.3f);  // A5
    Serial.println("Done.");
  }
  lastState = current;
  delay(10);
}
