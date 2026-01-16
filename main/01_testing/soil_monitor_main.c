#include "soil_monitor_main.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SOIL_MONITOR_TESTING";

void app_main(void) {
    ESP_LOGI(TAG, "Starting Soil Moisture Monitoring Testing");

    // Initialize components
    csm_v2_driver_t sensor_driver;
    csm_v2_config_t sensor_config;
    csm_v2_get_default_config(&sensor_config, SOIL_ADC_UNIT, SOIL_ADC_CHANNEL);
    csm_v2_init(&sensor_driver, &sensor_config);

    while(1) {
        csm_v2_reading_t reading;
        csm_v2_read(&sensor_driver, &reading);

        ESP_LOGI(TAG, "Soil Moisture Reading:");
        ESP_LOGI(TAG, "Timestamp: %llu", reading.timestamp);
        ESP_LOGI(TAG, "Voltage: %.2f V", reading.voltage);
        ESP_LOGI(TAG, "Moisture: %.2f%%", reading.moisture_percent);
        ESP_LOGI(TAG, "Raw ADC: %d", reading.raw_adc);

        vTaskDelay(pdMS_TO_TICKS(1000));  // Delay 1 second between readings
    }

    // Cleanup
    csm_v2_deinit(&sensor_driver);
}
