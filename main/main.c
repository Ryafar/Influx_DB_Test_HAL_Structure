/**
 * @file main.c
 * @brief Main application entry point
 * 
 * Entry point for the ESP32 application. Initializes the selected monitoring
 * application (environment/soil/battery), handles deep sleep power management,
 * and coordinates the overall system lifecycle.
 */

#include <stdio.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config/esp32-config.h"
#include "application/influx_sender.h"
#include "application/env_monitor_app.h"
#include "ntp_time.h"

static const char *TAG = "MAIN";

// ============================================================================
// Power Management Helper Functions
// ============================================================================

/**
 * @brief Enter deep sleep for specified duration
 * @param duration_seconds Sleep duration in seconds
 */
static void enter_deep_sleep(uint32_t duration_seconds) {
    if (!DEEP_SLEEP_ENABLED) {
        ESP_LOGI(TAG, "Deep sleep disabled, restarting in 5 seconds...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
        return;
    }
    
    uint64_t sleep_time_us = (uint64_t)duration_seconds * 1000000ULL;
    esp_sleep_enable_timer_wakeup(sleep_time_us);
    
    ESP_LOGI(TAG, "Entering deep sleep for %lu seconds...", duration_seconds);
    ESP_LOGI(TAG, "============================================");
    
    vTaskDelay(pdMS_TO_TICKS(DEEP_SLEEP_WAKEUP_DELAY_MS));
    esp_deep_sleep_start();
}

/**
 * @brief Handle initialization failure (WiFi/sensor init failed)
 * Enters backoff sleep or exits depending on configuration
 */
static void handle_init_failure(void) {
    ESP_LOGW(TAG, "Initialization failed - entering backoff sleep");
    enter_deep_sleep(WIFI_FAILURE_BACKOFF_SECONDS);
}

/**
 * @brief Cleanup resources and enter normal sleep cycle
 * @param app Pointer to environment monitor app
 */
static void cleanup_and_sleep(env_monitor_app_t* app) {
    ESP_LOGI(TAG, "Preparing for deep sleep...");
    
    // Clean up resources before deep sleep
    // Stop sender first to avoid races with HTTP/TLS deinit
    influx_sender_deinit();
    env_monitor_deinit(app);
    
    enter_deep_sleep(DEEP_SLEEP_DURATION_SECONDS);
}

/**
 * @brief Log wakeup reason for debugging
 */
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

#if NTP_ENABLED
/**
 * @brief NTP synchronization callback
 */
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

/**
 * @brief Initialize NTP time synchronization
 */
static void init_ntp_sync(void) {
    ESP_LOGI(TAG, "Initializing NTP time synchronization...");
    esp_err_t ret = ntp_time_init(ntp_sync_callback);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize NTP: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "NTP initialization started, waiting for sync...");
    ret = ntp_time_wait_for_sync(NTP_SYNC_TIMEOUT_MS);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "NTP synchronized successfully!");
    } else {
        ESP_LOGW(TAG, "NTP sync timeout, will continue syncing in background");
    }
}
#endif

// ============================================================================
// Application Lifecycle Functions
// ============================================================================

/**
 * @brief Initialize the monitoring application
 * @param app Pointer to environment monitor app
 * @param config Pointer to configuration structure
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t init_application(env_monitor_app_t* app, env_monitor_config_t* config) {
    env_monitor_get_default_config(config);
    
    ESP_LOGI(TAG, "Initializing environment monitoring application...");
    esp_err_t ret = env_monitor_init(app, config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize env application: %s", esp_err_to_name(ret));
        return ret;
    }
    
#if NTP_ENABLED
    init_ntp_sync();
#else
    ESP_LOGI(TAG, "NTP disabled - using server timestamps for InfluxDB");
#endif
    
    return ESP_OK;
}

/**
 * @brief Run the monitoring cycle (measurements and data transmission)
 * @param app Pointer to environment monitor app
 * @param config Pointer to configuration
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t run_monitoring_cycle(env_monitor_app_t* app, const env_monitor_config_t* config) {
    // Start environment monitoring task
    ESP_LOGI(TAG, "Starting environment monitoring (%lu measurement(s))...", config->measurements_per_cycle);
    esp_err_t ret = env_monitor_start(app);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start env monitoring: %s", esp_err_to_name(ret));
        env_monitor_deinit(app);
        return ret;
    }
    
    // Wait for measurements to complete
    ESP_LOGI(TAG, "Waiting for all measurements to complete...");
    ret = env_monitor_wait_for_completion(app, config->measurements_per_cycle * config->measurement_interval_ms + 30000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Env monitoring completion failed: %s", esp_err_to_name(ret));
    }
    
    // Wait for all InfluxDB data to be sent
    ESP_LOGI(TAG, "Waiting for all data to be sent to InfluxDB...");
    ret = influx_sender_wait_until_empty(30000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "InfluxDB sender queue not empty: %s", esp_err_to_name(ret));
    }
    
    ESP_LOGI(TAG, "All measurements complete and data sent!");
    return ESP_OK;
}

// ============================================================================
// Main Entry Point
// ============================================================================

void app_main(void) {
    ESP_LOGI(TAG, "=== Environment (AHT20) Sensor with Deep Sleep ===");
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());
    
    log_wakeup_reason();
    
    // Initialize application
    static env_monitor_app_t app;
    env_monitor_config_t config;
    
    if (init_application(&app, &config) != ESP_OK) {
        handle_init_failure();
        return;
    }
    
    // Run monitoring cycle
    if (run_monitoring_cycle(&app, &config) != ESP_OK) {
        ESP_LOGW(TAG, "Monitoring cycle had warnings");
    }
    
    // Cleanup and enter sleep
    cleanup_and_sleep(&app);
}
