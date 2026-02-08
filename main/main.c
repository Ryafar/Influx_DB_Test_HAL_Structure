/**
 * @file main.c
 * @brief Main application entry point for ESP32 sensor monitoring
 * 
 * Supports multiple sensors (Battery, Environment, Soil) with configurable
 * feature toggles. Handles WiFi connectivity, InfluxDB data transmission,
 * and optional deep sleep power management.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include "nvs_flash.h"

#include "config/esp32-config.h"
#include "wifi_manager.h"
#include "application/influx_sender.h"
#include "influxdb_client.h"
#include "esp_utils.h"

#if ENABLE_BATTERY_MONITOR
#include "application/battery_monitor_task.h"
#endif

#if ENABLE_ENV_MONITOR
#include "application/env_monitor_app.h"
#endif

#if ENABLE_SOIL_MONITOR
#include "application/soil_monitor_app.h"
#endif

static const char *TAG = "MAIN";

// ============================================================================
// Initialization & Utility Functions
// ============================================================================


// WiFi callback
static void wifi_status_cb(wifi_status_t status, const char* ip_addr) {
    switch(status) {
        case WIFI_STATUS_CONNECTED:
            ESP_LOGI(TAG, "WiFi Connected! IP: %s", ip_addr ? ip_addr : "N/A");
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

static void log_wakeup_reason(void) {
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    switch (wakeup_reason) {
        case ESP_SLEEP_WAKEUP_TIMER:
            ESP_LOGI(TAG, "Wakeup caused by timer");
            break;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        default:
            ESP_LOGI(TAG, "First boot or reset (not a deep sleep wakeup)");
            break;
    }
}

static void enter_deep_sleep(uint32_t duration_seconds) {
    if (!DEEP_SLEEP_ENABLED) {
        ESP_LOGI(TAG, "Deep sleep disabled, waiting %lu seconds before next cycle...", duration_seconds);
        vTaskDelay(pdMS_TO_TICKS(duration_seconds * 1000));
        return;  // Return to loop
    }
    
    uint64_t sleep_time_us = (uint64_t)duration_seconds * 1000000ULL;
    esp_sleep_enable_timer_wakeup(sleep_time_us);
    
    ESP_LOGI(TAG, "Entering deep sleep for %lu seconds...", duration_seconds);
    ESP_LOGI(TAG, "============================================");
    
    vTaskDelay(pdMS_TO_TICKS(DEEP_SLEEP_WAKEUP_DELAY_MS));
    esp_deep_sleep_start();
}

static esp_err_t init_system(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");
    
    // Initialize network stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(TAG, "Network stack initialized");
    
    // Initialize and connect WiFi
    wifi_manager_config_t wifi_config = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASSWORD,
        .max_retry = WIFI_MAX_RETRY
    };
    ESP_ERROR_CHECK(wifi_manager_init(&wifi_config, wifi_status_cb));
    ESP_LOGI(TAG, "WiFi Manager initialized");
    
    ret = wifi_manager_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connection failed!");
        return ret;
    }
    ESP_LOGI(TAG, "WiFi connected successfully");
    
    // Initialize InfluxDB sender
    if (USE_INFLUXDB) {
        ESP_ERROR_CHECK(influx_sender_init());
        ESP_LOGI(TAG, "InfluxDB sender initialized");
    }
    
    return ESP_OK;
}

static esp_err_t init_sensors(void) {
    esp_err_t ret = ESP_OK;
    
#if ENABLE_BATTERY_MONITOR
    ESP_LOGI(TAG, "Battery Monitor enabled (init handled by task)");
#endif

#if ENABLE_ENV_MONITOR
    ESP_LOGI(TAG, "Initializing Environment Monitor (AHT20)...");
    static env_monitor_app_t env_app;
    env_monitor_config_t env_config;
    env_monitor_get_default_config(&env_config);
    ret = env_monitor_init(&env_app, &env_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ENV monitor init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Environment Monitor initialized");
#endif

#if ENABLE_SOIL_MONITOR
    ESP_LOGI(TAG, "Initializing Soil Monitor...");
    // Add soil monitor init here when ready
    ESP_LOGI(TAG, "Soil Monitor initialized");
#endif

#if !ENABLE_ENV_MONITOR && !ENABLE_BATTERY_MONITOR && !ENABLE_SOIL_MONITOR
    #error "At least one monitor must be enabled!"
#endif

    return ESP_OK;
}

// ============================================================================
// Monitoring Cycle
// ============================================================================

static esp_err_t run_measurement_cycle(void) {
    esp_err_t ret = ESP_OK;
    
    ESP_LOGI(TAG, "--- Starting Measurement Cycle ---");
    
#if ENABLE_BATTERY_MONITOR
    // Battery monitor task handles everything internally
    ESP_LOGI(TAG, "Starting battery monitor task...");
    ret = battery_monitor_start(BATTERY_MEASUREMENTS_PER_CYCLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start battery monitor: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Wait for battery monitoring to complete
    ret = battery_monitor_wait_for_completion(30000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Battery monitor timeout: %s", esp_err_to_name(ret));
    }
#endif

#if ENABLE_ENV_MONITOR
    ESP_LOGI(TAG, "Reading environment sensor...");
    // ENV monitor reading logic here when enabled
#endif

#if ENABLE_SOIL_MONITOR
    ESP_LOGI(TAG, "Reading soil moisture...");
    // Soil monitor reading logic here when enabled
#endif
    
    // Wait for InfluxDB transmission to complete
    if (USE_INFLUXDB) {
        ESP_LOGI(TAG, "Waiting for InfluxDB transmission...");
        ret = influx_sender_wait_until_empty(10000);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "InfluxDB queue not empty: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Data sent successfully");
        }
    }
    
    ESP_LOGI(TAG, "--- Measurement Cycle Complete ---\n");
    return ESP_OK;
}

// ============================================================================
// Main Entry Point
// ============================================================================

void app_main(void) {
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "=== ESP32 Sensor Monitor v2.0 ===");
    ESP_LOGI(TAG, "====================================");
#if ENABLE_ENV_MONITOR
    ESP_LOGI(TAG, "  - ENV Monitor: ENABLED");
#endif
#if ENABLE_BATTERY_MONITOR
    ESP_LOGI(TAG, "  - Battery Monitor: ENABLED");
#endif
#if ENABLE_SOIL_MONITOR
    ESP_LOGI(TAG, "  - Soil Monitor: ENABLED");
#endif
#if DEEP_SLEEP_ENABLED
    ESP_LOGI(TAG, "  - Deep Sleep: ENABLED (%ds cycles)", DEEP_SLEEP_DURATION_SECONDS);
#else
    ESP_LOGI(TAG, "  - Deep Sleep: DISABLED (continuous loop)");
#endif
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "====================================\n");
    
    log_wakeup_reason();
    
    // Initialize system (only once)
    ESP_LOGI(TAG, "Initializing system...");
    if (init_system() != ESP_OK) {
        ESP_LOGE(TAG, "System initialization failed! Retrying in 60s...");
        vTaskDelay(pdMS_TO_TICKS(60000));
        esp_restart();
        return;
    }
    
    // Initialize sensors (only once)
    ESP_LOGI(TAG, "Initializing sensors...");
    if (init_sensors() != ESP_OK) {
        ESP_LOGE(TAG, "Sensor initialization failed! Retrying in 60s...");
        vTaskDelay(pdMS_TO_TICKS(60000));
        esp_restart();
        return;
    }
    
    ESP_LOGI(TAG, "System ready!\n");

    // // debug while loop
    // while (1) {
    //     // write hello world
    //     ESP_LOGI(TAG, "Hello, world! Running main loop...");
    //     vTaskDelay(pdMS_TO_TICKS(5000));
    // }
    
    // Main measurement loop
    while (1) {
        run_measurement_cycle();
        
        // Sleep or delay before next cycle
        enter_deep_sleep(DEEP_SLEEP_DURATION_SECONDS);
        
        // If deep sleep is enabled, we never reach here (device resets)
        // If disabled, loop continues after delay
        if (DEEP_SLEEP_ENABLED) {
            break;  // Should never reach, but safety exit
        }
    }
}
