// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "esp_system.h"
// #include "esp_log.h"
// #include "driver/adc.h"
// #include "driver/dac.h"
// #include "driver/gpio.h"
// #include "esp_timer.h"

// static const char *TAG = "ECE459";
// #define LED GPIO_NUM_2

// // ADC and DAC configuration
// // ADC1_CHANNEL_6 == GPIO34
// #define ADC_CHANNEL ADC1_CHANNEL_6
// // Using DAC_CHANNEL_1 = GPIO22
// #define OUT_DAC_CHANNEL DAC_CHANNEL_1

// // Desired sample rate (Hz). 8000 is a good simple starting point.
// #define SAMPLE_RATE 8000

// void app_main(void)
// {
//     // --- ADC setup (12-bit) ---
//     adc1_config_width(ADC_WIDTH_BIT_12);
//     adc1_config_channel_atten(ADC_CHANNEL, ADC_ATTEN_DB_11); // allow ~0-3.3V

//     // --- DAC setup ---
//     dac_output_enable(OUT_DAC_CHANNEL); // enable DAC on GPIO

//     // --- LED setup ---
//     gpio_config_t io_conf = {
//         .intr_type = GPIO_INTR_DISABLE,
//         .mode = GPIO_MODE_OUTPUT,
//         .pin_bit_mask = (1ULL << LED),
//         .pull_down_en = GPIO_PULLDOWN_DISABLE,
//         .pull_up_en = GPIO_PULLUP_DISABLE
//     };
//     gpio_config(&io_conf);
//     int led_state = 1;
//     gpio_set_level(LED, led_state);

//     // Timing variables
//     const int64_t period_us = 1000000LL / SAMPLE_RATE;
//     int64_t last_time = esp_timer_get_time();

//     while (1) {
//         // Read ADC (12-bit: 0..4095)
//         int raw = adc1_get_raw(ADC_CHANNEL);

//         // Map 12-bit ADC value to 8-bit DAC value (0..255)
//         // Simple linear mapping:
//         int dac_val = (raw * 255) / 4095;
//         if (dac_val < 0) dac_val = 0;
//         if (dac_val > 255) dac_val = 255;

//         // Output to DAC
//         dac_output_voltage(OUT_DAC_CHANNEL, (uint8_t)dac_val);

//         // Optional debug print (reduce frequency in real use for performance)
//         static int print_counter = 0;
//         if (++print_counter >=  (SAMPLE_RATE / 10)) { // ~10 prints/sec
//             float voltage = ((float)raw / 4095.0f) * 3.3f;
//             ESP_LOGI(TAG, "ADC raw=%d  V=%.3fV  DAC=%d", raw, voltage, dac_val);
//             print_counter = 0;
//         }

//         // LED control based on voltage threshold (kept from your original)
//         float voltage = ((float)raw/4095)* 3.3;
//         if (led_state != 1 && voltage > 1.70) {
//             led_state = 1;
//             gpio_set_level(LED, led_state);
//         } else if (led_state != 0 && voltage < 1.55) {
//             led_state = 0;
//             gpio_set_level(LED, led_state);
//         }

//         // Precise sleep to hit SAMPLE_RATE using esp_timer_get_time
//         int64_t now = esp_timer_get_time();
//         int64_t elapsed = now - last_time;
//         int64_t to_wait = period_us - elapsed;
//         if (to_wait > 0) {
//             // For short waits use vTaskDelay or busy wait depending on duration.
//             // If to_wait > 2000 us, yield with vTaskDelay; else busy wait.
//             if (to_wait > 2000) {
//                 // convert micros to ticks (coarse) to yield CPU
//                 vTaskDelay(pdMS_TO_TICKS((to_wait + 500) / 1000));
//             } else {
//                 // small busy-wait
//                 esp_rom_delay_us(to_wait);
//             }
//         }
//         last_time = esp_timer_get_time();
//     }
// }

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/adc.h"

static const char *TAG = "ECE459";
#define LED GPIO_NUM_2
#define DAC GPIO_NUM_22

void app_main(void)
{
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11); // GPIO34 - 12 bit resolution
    // input = 3.3 V, when it is quiet the output hovers at VCC/2 so 3.3/2 = ~1.65 V = quiet

    // LED
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << LED),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE
    };
    gpio_config(&io_conf);

    // DAC
    gpio_config_t io_conf2 = {
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = (1ULL << LED),
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .pull_up_en = GPIO_PULLUP_DISABLE
    };
    gpio_config(&io_conf2);

    int led_state = 1;
    gpio_set_level(LED, led_state);

    while (1) {
        int val = adc1_get_raw(ADC1_CHANNEL_6);
        printf("ADC Raw: %d\n", val);
        float voltage = ((float)val/4095)* 3.3; // raw voltage divided by 2^12 bits - 1 * 3.3v
        printf("Output Voltage: %.5f\n", voltage);

        if (led_state != 1 && voltage > 1.70)
        {
            led_state = 1;
            gpio_set_level(LED, led_state);
            gpio_set_level(DAC, led_state);
        }
        else if(led_state !=0 && voltage < 1.55){
            led_state = 0;
            gpio_set_level(LED, led_state);
            gpio_set_level(DAC, led_state);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
