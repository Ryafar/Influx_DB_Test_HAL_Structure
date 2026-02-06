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
#include "wifi_manager.h"
#include "config/esp32-config.h"
#include "application/battery_monitor_task.h"

static const char *TAG = "TEST";

// WiFi callback
static void wifi_status_cb(wifi_status_t status, const char* ip_addr) {
    switch(status) {
        case WIFI_STATUS_CONNECTED:
            ESP_LOGI(TAG, "WiFi Connected! IP: %s", ip_addr);
            break;
        case WIFI_STATUS_DISCONNECTED:
            ESP_LOGW(TAG, "WiFi Disconnected");
            break;
        case WIFI_STATUS_CONNECTING:
            ESP_LOGI(TAG, "WiFi Connecting...");
            break;
        case WIFI_STATUS_ERROR:
            ESP_LOGE(TAG, "WiFi Error");
            break;
    }
}

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
    
    // Test 4: WiFi Manager init (without connecting)
    wifi_manager_config_t wifi_config = {
        .ssid = "TestSSID",
        .password = "TestPassword",
        .max_retry = 5
    };
    ESP_ERROR_CHECK(wifi_manager_init(&wifi_config, wifi_status_cb));
    ESP_LOGI(TAG, "Test 4: WiFi Manager initialized OK ✅ (not connecting yet)");
    
    // Test 5: Battery Monitor - Read voltage from GPIO0
    #if ENABLE_BATTERY_MONITOR
    ESP_LOGI(TAG, "Test 5: Initializing Battery Monitor on GPIO0...");
    ESP_ERROR_CHECK(battery_monitor_init());
    
    float voltage = 0.0f;
    esp_err_t err = battery_monitor_read_voltage(&voltage);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Test 5: Battery voltage = %.2f V ✅", voltage);
    } else {
        ESP_LOGE(TAG, "Test 5: Failed to read battery voltage (error: %d)", err);
    }
    
    battery_monitor_deinit();
    ESP_LOGI(TAG, "Test 5: Battery Monitor test completed");
    #else
    ESP_LOGW(TAG, "Test 5: Battery Monitor disabled in config");
    #endif
    
    while(1) {
        ESP_LOGI(TAG, "Hello from ESP32! Counter: %d", counter++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
