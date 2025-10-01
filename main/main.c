#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

static const char *TAG = "ECE459";

void app_main(void)
{
    ESP_LOGI(TAG, "Hello from ECE459 Walkie-Talkies!");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "tick");
        ESP_LOGI(TAG, "Hi");
    }
}
