/**
 * @file main.c
 * @brief Simple test application - Step by step testing
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"

static const char *TAG = "TEST";

void app_main(void) {
    ESP_LOGI(TAG, "=== ESP32 Simple Test ===");
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());
    
    int counter = 0;
    
    // Test 1: Basic loop (WORKING) ✅
    ESP_LOGI(TAG, "Test 1: Basic loop running");
    
    // Test 2: NVS init
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "Test 2: NVS initialized OK ✅");
    
    // Test 3: Network stack init
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(TAG, "Test 3: Network stack initialized OK ✅");
    
    // // Test 4: Check deep sleep config (NEXT TEST!)
    // // ESP_LOGI(TAG, "Test 4: Deep sleep enabled = %d", DEEP_SLEEP_ENABLED);
    
    while(1) {
        ESP_LOGI(TAG, "Hello from ESP32! Counter: %d", counter++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
