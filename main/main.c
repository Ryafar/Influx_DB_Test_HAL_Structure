/**
 * @file main.c
 * @brief Main application entry point
 * 
 * Entry point for the ESP32 application. Initializes the selected monitoring
 * application (environment/soil/battery), handles deep sleep power management,
 * and coordinates the overall system lifecycle.
 */

#include "main.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "application/influx_sender.h"
#include "application/env_monitor_app.h"
#include "ntp_time.h"

static const char *TAG = "MAIN";

#if NTP_ENABLED
// NTP sync callback
static void ntp_sync_callback(ntp_status_t status, const char* time_str) {
    switch (status) {
        case NTP_STATUS_SYNCED:
            ESP_LOGI(TAG, "NTP Time Synchronized! Swiss time: %s", time_str ? time_str : "N/A");
            break;
        case NTP_STATUS_FAILED:
            ESP_LOGW(TAG, "NTP Time Synchronization Failed");
            break;
        case NTP_STATUS_SYNCING:
            ESP_LOGI(TAG, "NTP Time Synchronizing...");
            break;
        default:
            break;
    }
}
#endif

void app_main(void) {
    ESP_LOGI(TAG, "=== Environment (AHT20) Sensor with Deep Sleep ===");
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());
    
    // Battery calibration not used in environment monitor mode
    
    // Check if this is a deep sleep wakeup
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
    
    // Initialize environment monitoring application (AHT20)
    static env_monitor_app_t app;
    env_monitor_config_t config;

    env_monitor_get_default_config(&config);

    ESP_LOGI(TAG, "Initializing environment monitoring application...");
    esp_err_t ret = env_monitor_init(&app, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize env application: %s", esp_err_to_name(ret));
        // Wi-Fi likely failed; enter backoff deep sleep if enabled
        if (DEEP_SLEEP_ENABLED) {
            ESP_LOGW(TAG, "Entering Wi-Fi failure backoff deep sleep for %d seconds", WIFI_FAILURE_BACKOFF_SECONDS);
            uint64_t sleep_time_us = (uint64_t)WIFI_FAILURE_BACKOFF_SECONDS * 1000000ULL;
            esp_sleep_enable_timer_wakeup(sleep_time_us);
            vTaskDelay(pdMS_TO_TICKS(DEEP_SLEEP_WAKEUP_DELAY_MS));
            esp_deep_sleep_start();
        }
        return;
    }
    
    // Initialize NTP time synchronization (after WiFi is connected) - if enabled
#if NTP_ENABLED
    ESP_LOGI(TAG, "Initializing NTP time synchronization...");
    ret = ntp_time_init(ntp_sync_callback);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize NTP: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "NTP initialization started, waiting for sync...");
        ret = ntp_time_wait_for_sync(NTP_SYNC_TIMEOUT_MS);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "NTP synchronized successfully!");
        } else {
            ESP_LOGW(TAG, "NTP sync timeout, will continue syncing in background");
        }
    }
#else
    ESP_LOGI(TAG, "NTP disabled - using server timestamps for InfluxDB");
#endif
    
    // Start environment monitoring task
    ESP_LOGI(TAG, "Starting environment monitoring (%lu measurement(s))...", config.measurements_per_cycle);
    ret = env_monitor_start(&app);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start env monitoring: %s", esp_err_to_name(ret));
        env_monitor_deinit(&app);
        if (DEEP_SLEEP_ENABLED) {
            ESP_LOGW(TAG, "Entering measurement start failure backoff deep sleep for %d seconds", WIFI_FAILURE_BACKOFF_SECONDS);
            uint64_t sleep_time_us = (uint64_t)WIFI_FAILURE_BACKOFF_SECONDS * 1000000ULL;
            esp_sleep_enable_timer_wakeup(sleep_time_us);
            vTaskDelay(pdMS_TO_TICKS(DEEP_SLEEP_WAKEUP_DELAY_MS));
            esp_deep_sleep_start();
        }
        return;
    }
    
    // Wait for both tasks to complete their measurements
    ESP_LOGI(TAG, "Waiting for all measurements to complete...");
    
    // Wait for environment monitoring task (30 second timeout)
    ret = env_monitor_wait_for_completion(&app, config.measurements_per_cycle * config.measurement_interval_ms + 30000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Env monitoring completion failed: %s", esp_err_to_name(ret));
    }
    
    // Wait for all InfluxDB data to be sent (30 second timeout)
    ESP_LOGI(TAG, "Waiting for all data to be sent to InfluxDB...");
    ret = influx_sender_wait_until_empty(30000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "InfluxDB sender queue not empty: %s", esp_err_to_name(ret));
    }
    
    ESP_LOGI(TAG, "All measurements complete and data sent!");
    
    // Check if deep sleep is enabled
    if (DEEP_SLEEP_ENABLED) {
        ESP_LOGI(TAG, "Preparing for deep sleep...");
        
        // Clean up resources before deep sleep
        // Stop sender first to avoid races with HTTP/TLS deinit
        influx_sender_deinit();
        env_monitor_deinit(&app);
        
        // Configure timer wakeup
        uint64_t sleep_time_us = (uint64_t)DEEP_SLEEP_DURATION_SECONDS * 1000000ULL;
        esp_sleep_enable_timer_wakeup(sleep_time_us);
        
        ESP_LOGI(TAG, "Entering deep sleep for %d seconds...", DEEP_SLEEP_DURATION_SECONDS);
        ESP_LOGI(TAG, "============================================");
        
        // Small delay before entering deep sleep
        vTaskDelay(pdMS_TO_TICKS(DEEP_SLEEP_WAKEUP_DELAY_MS));
        
        // Enter deep sleep
        esp_deep_sleep_start();
    } else {
        ESP_LOGI(TAG, "Deep sleep disabled, restarting in 5 seconds...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }
}
