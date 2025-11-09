/*
  ESP32 + INMP441 -> SD card WAV recorder
  - Sample rate: 16 kHz, mono, 16-bit PCM
  - SD CS: GPIO 5
  - I2S: BCLK=26, WS=25, DOUT=22
*/

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "driver/i2s.h"

#define SD_CS_PIN        5     // change to your SD module CS pin
#define I2S_WS           25    // LRCLK / WS
#define I2S_SCK          26    // BCLK
#define I2S_SD           32    // DOUT (mic -> ESP32)

// ---------- Audio config ----------
static const int SAMPLE_RATE_HZ = 16000;   // voice-friendly, light storage
static const size_t SAMPLES_PER_CHUNK = 512;
static const size_t I2S_READ_BYTES = SAMPLES_PER_CHUNK * sizeof(int32_t);

// Record length (ms). Set 0 to record "forever" (until power off/reset).
static const uint32_t RECORD_DURATION_MS = 15 * 1000;

// ---------- Buffers ----------
int32_t i2s_read_buf[SAMPLES_PER_CHUNK];
int16_t pcm16_buf[SAMPLES_PER_CHUNK];

File wavFile;

// ---------- WAV helpers ----------
void write_le_u32(File &f, uint32_t v) {
  uint8_t b[4] = { (uint8_t)(v & 0xFF), (uint8_t)((v>>8)&0xFF), (uint8_t)((v>>16)&0xFF), (uint8_t)((v>>24)&0xFF) };
  f.write(b, 4);
}
void write_le_u16(File &f, uint16_t v) {
  uint8_t b[2] = { (uint8_t)(v & 0xFF), (uint8_t)((v>>8)&0xFF) };
  f.write(b, 2);
}

bool wav_begin(File &f, uint32_t sampleRate, uint16_t bitsPerSample, uint16_t channels) {
  if (!f) return false;

  // RIFF
  f.write((const uint8_t*)"RIFF", 4);
  write_le_u32(f, 0); // placeholder
  f.write((const uint8_t*)"WAVE", 4);

  // fmt chunk
  f.write((const uint8_t*)"fmt ", 4);
  write_le_u32(f, 16);         // Subchunk1Size (PCM)
  write_le_u16(f, 1);          // AudioFormat = PCM
  write_le_u16(f, channels);
  write_le_u32(f, sampleRate);
  uint16_t blockAlign = channels * (bitsPerSample / 8);
  uint32_t byteRate = sampleRate * blockAlign;
  write_le_u32(f, byteRate);
  write_le_u16(f, blockAlign);
  write_le_u16(f, bitsPerSample);

  // data chunk
  f.write((const uint8_t*)"data", 4);
  write_le_u32(f, 0); // placeholder
  return true;
}

void wav_end_and_patch(File &f) {
  if (!f) return;
  uint32_t fileSize = f.size();
  uint32_t dataSize = fileSize - 44;

  // Patch data size at offset 40
  f.seek(40);
  write_le_u32(f, dataSize);

  // Patch RIFF chunk size at offset 4 (fileSize - 8)
  f.seek(4);
  write_le_u32(f, fileSize - 8);

  f.close();
}

// ---------- I2S init ----------
void i2s_setup() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE_HZ,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, // <-- if INMP441 L/R pad is VDD, use ONLY_RIGHT
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S),
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num   = I2S_SCK,
    .ws_io_num    = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = I2S_SD
  };

  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== ESP32 INMP441 -> SD WAV Recorder ===");

  // Bring up SPI (explicit init helps some SD modules)
  SPI.begin(18, 19, 23, SD_CS_PIN);

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD.begin failed. Check wiring, CS pin (5), power (3.3V).");
    while (1) { delay(500); }
  }
  Serial.println("SD OK.");

  // Remove old file (simple demo). Add rotation if you want multiple files.
  if (SD.exists("/recording.wav")) {
    SD.remove("/recording.wav");
  }
  wavFile = SD.open("/recording.wav", FILE_WRITE);
  if (!wavFile) {
    Serial.println("Failed to open /recording.wav for writing.");
    while (1) { delay(500); }
  }

  if (!wav_begin(wavFile, SAMPLE_RATE_HZ, 16, 1)) {
    Serial.println("Failed to write WAV header.");
    wavFile.close();
    while (1) { delay(500); }
  }
  Serial.println("WAV header written.");

  i2s_setup();
  Serial.printf("I2S ready @ %d Hz. Recording...\n", SAMPLE_RATE_HZ);
}

void loop() {
  uint32_t start = millis();
  uint32_t totalBytes = 0;

  while (RECORD_DURATION_MS == 0 || (millis() - start) < RECORD_DURATION_MS) {
    size_t bytes_read = 0;
    if (i2s_read(I2S_NUM_0, (void*)i2s_read_buf, I2S_READ_BYTES, &bytes_read, portMAX_DELAY) == ESP_OK && bytes_read) {
      size_t n = bytes_read / sizeof(int32_t);

      // INMP441 outputs 24-bit data in 32-bit frames, MSB-aligned. Start with >>14.
      for (size_t i = 0; i < n; i++) {
        int32_t s = i2s_read_buf[i];
        pcm16_buf[i] = (int16_t)(s >> 14);  // try 13 for louder, 15 for quieter
      }

      size_t toWrite = n * sizeof(int16_t);
      size_t w = wavFile.write((const uint8_t*)pcm16_buf, toWrite);
      if (w != toWrite) {
        Serial.println("Warning: short write to SD.");
      }
      totalBytes += w;
    } else {
      delay(1);
    }
  }

  Serial.printf("Done. Wrote %u bytes PCM.\n", (unsigned)totalBytes);
  wav_end_and_patch(wavFile);
  Serial.println("WAV finalized at /recording.wav");

  // Stop here; reset to record again. (Add rotation if you want multiple files per boot.)
  while (1) { delay(1000); }
}
