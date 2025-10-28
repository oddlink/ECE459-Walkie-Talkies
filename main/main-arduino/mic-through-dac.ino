// #include <Arduino.h>
// #include "driver/i2s.h"

// // pins (keep these three as you already use them for DAC)
// const int I2S_BCK  = 26; // shared BCLK
// const int I2S_WS   = 25; // shared LRCLK/WS
// const int I2S_DAC_DATA = 22; // DAC data out (keep this unchanged)

// // new pin for INMP441 SD (mic data out -> MCU data_in)
// const int INMP_SD = 32; // use a free input-capable GPIO (32 or 33)

// // single I2S port used both RX and TX
// #define I2S_PORT I2S_NUM_0

// const int SAMPLE_RATE = 48000;
// const int BUF_SAMPLES = 256; // frames per buffer

// void setupI2SFullDuplex() {
//   i2s_config_t cfg = {
//     .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
//     .sample_rate = SAMPLE_RATE,
//     // Use 32-bit frames so RX gets the full 32-bit word from INMP441.
//     // We'll downconvert RX->16 for audio, then re-pack to 32-bit for TX.
//     .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
//     .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, // stereo for TX; RX is mono in left or right slot
//     .communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_I2S_MSB,
//     .intr_alloc_flags = 0,
//     .dma_buf_count = 4,
//     .dma_buf_len = BUF_SAMPLES,
//     .use_apll = false,
//     .tx_desc_auto_clear = true
//   };

//   i2s_driver_install(I2S_PORT, &cfg, 0, NULL);

//   i2s_pin_config_t pins = {
//     .bck_io_num = I2S_BCK,
//     .ws_io_num = I2S_WS,
//     .data_out_num = I2S_DAC_DATA, // MCU -> DAC
//     .data_in_num = INMP_SD         // INMP441 SD -> MCU
//   };
//   i2s_set_pin(I2S_PORT, &pins);
//   i2s_zero_dma_buffer(I2S_PORT);
// }


// void setup() {
//   Serial.begin(115200);
//   delay(50);
//   Serial.println("INMP441 -> passthrough using single I2S full-duplex (keep pins 26/25/22)");
//   setupI2SFullDuplex();
// }

// void loop() {
//   // RX buffer: read BUF_SAMPLES frames, each frame 4 bytes (32-bit)
//   const int RX_BYTES = BUF_SAMPLES * 4;
//   static uint8_t rx_buffer[RX_BYTES];
//   size_t rx_read = 0;
//   i2s_read(I2S_PORT, rx_buffer, RX_BYTES, &rx_read, portMAX_DELAY);
//   int frames = rx_read / 4;
//   if (frames <= 0) return;

//   // Prepare TX buffer as 32-bit frames stereo interleaved.
//   // We'll place 16-bit audio in the MSBs of the 32-bit word so it stays left-justified,
//   // which many I2S sinks accept when the port is configured for 32-bit frames.
//   static int32_t tx_buffer[BUF_SAMPLES * 2]; // stereo frames (we'll use frames*1 since each frame = 1 LR pair)
//   int32_t *rx32 = (int32_t *)rx_buffer;

//   for (int i = 0; i < frames; ++i) {
//     // 1) shift right 8: INMP441 delivers 24 bits in the high-order bits of a 32-bit word
//     int32_t s32 = rx32[i] >> 8;          // now signed 24-bit value in 32-bit container

//     // 2) convert to 16-bit (drop low 8 bits of the 24-bit sample)
//     int16_t s16 = (int16_t)(s32 >> 8);   // keep the MS 16 bits of the 24-bit sample

//     // 3) left-justify into 32-bit word for the DAC (if your DAC expects MSB-left in 32-bit slot)
//     int32_t out32 = ((int32_t)s16) << 16;

//     tx_buffer[2 * i]     = out32; // left
//     tx_buffer[2 * i + 1] = out32; // right (duplicate)
//   }


//   // Write TX bytes: frames * 2 channels * 4 bytes per 32-bit word
//   size_t tx_bytes = 0;
//   i2s_write(I2S_PORT, (const char*)tx_buffer, frames * 2 * 4, &tx_bytes, portMAX_DELAY);

//   // occasional debug
//   static unsigned long cnt = 0;
//   if ((cnt++ & 127) == 0) {
//     Serial.printf("frames=%d rx=%d tx=%d\n", frames, (int)rx_read, (int)tx_bytes);
//   }
// }
