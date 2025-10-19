#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/adc.h"

static const char *TAG = "ECE459";
#define LED GPIO_NUM_2

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
        }
        else if(led_state !=0 && voltage < 1.55){
            led_state = 0;
            gpio_set_level(LED, led_state);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
