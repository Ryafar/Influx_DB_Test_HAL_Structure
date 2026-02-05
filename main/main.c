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
#if ENABLE_ENV_MONITOR
#include "application/env_monitor_app.h"
#endif
#if ENABLE_BATTERY_MONITOR
#include "application/battery_monitor_task.h"
#endif
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
 */
static void cleanup_and_sleep(void) {
    ESP_LOGI(TAG, "Preparing for deep sleep...");
    
    // Stop sender first to avoid races with HTTP/TLS deinit
    influx_sender_deinit();
    
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
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t init_application(void) {
    esp_err_t ret = ESP_OK;
    
#if ENABLE_ENV_MONITOR
    static env_monitor_app_t env_app;
    env_monitor_config_t env_config;
    env_monitor_get_default_config(&env_config);
    
    ESP_LOGI(TAG, "Initializing environment monitor (AHT20)...");
    ret = env_monitor_init(&env_app, &env_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ENV monitor init failed: %s", esp_err_to_name(ret));
        return ret;
    }
#endif

#if ENABLE_BATTERY_MONITOR
    ESP_LOGI(TAG, "Initializing battery monitor (GPIO0/ADC)...");
    ret = battery_monitor_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Battery monitor init failed: %s", esp_err_to_name(ret));
#if ENABLE_ENV_MONITOR
        env_monitor_deinit(&env_app);
#endif
        return ret;
    }
#endif

#if !ENABLE_ENV_MONITOR && !ENABLE_BATTERY_MONITOR && !ENABLE_SOIL_MONITOR
    #error "At least one monitor must be enabled!"
#endif
    
#if NTP_ENABLED
    init_ntp_sync();
#else
    ESP_LOGI(TAG, "NTP disabled - using server timestamps for InfluxDB");
#endif
    
    return ESP_OK;
}

/**
 * @brief Run the monitoring cycle for all enabled monitors
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t run_monitoring_cycle(void) {
    esp_err_t ret = ESP_OK;
    
#if ENABLE_ENV_MONITOR
    static env_monitor_app_t env_app;
    ESP_LOGI(TAG, "Starting environment monitoring...");
    ret = env_monitor_start(&env_app);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ENV monitor start failed: %s", esp_err_to_name(ret));
        return ret;
    }
#endif

#if ENABLE_BATTERY_MONITOR
    ESP_LOGI(TAG, "Starting battery monitoring...");
    ret = battery_monitor_start(BATTERY_MEASUREMENTS_PER_CYCLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Battery monitor start failed: %s", esp_err_to_name(ret));
        return ret;
    }
#endif

    // Wait for all monitors to complete
    ESP_LOGI(TAG, "Waiting for measurements to complete...");
    
#if ENABLE_ENV_MONITOR
    ret = env_monitor_wait_for_completion(&env_app, 30000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ENV monitor timeout: %s", esp_err_to_name(ret));
    }
    env_monitor_deinit(&env_app);
#endif

#if ENABLE_BATTERY_MONITOR
    ret = battery_monitor_wait_for_completion(30000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Battery monitor timeout: %s", esp_err_to_name(ret));
    }
    battery_monitor_deinit();
#endif
    
    // Wait for all InfluxDB data to be sent
    ESP_LOGI(TAG, "Waiting for all data to be sent to InfluxDB...");
    ret = influx_sender_wait_until_empty(30000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "InfluxDB sender queue not empty: %s", esp_err_to_name(ret));
    }
    
    ESP_LOGI(TAG, "All measurements complete!");
    return ESP_OK;
}

// ============================================================================
// Main Entry Point
// ============================================================================

void app_main(void) {
    ESP_LOGI(TAG, "=== ESP32 Sensor Node with Deep Sleep ===");
#if ENABLE_ENV_MONITOR
    ESP_LOGI(TAG, "  - ENV Monitor: ENABLED");
#endif
#if ENABLE_BATTERY_MONITOR
    ESP_LOGI(TAG, "  - Battery Monitor: ENABLED");
#endif
#if ENABLE_SOIL_MONITOR
    ESP_LOGI(TAG, "  - Soil Monitor: ENABLED");
#endif
    ESP_LOGI(TAG, "ESP-IDF Version: %s", esp_get_idf_version());
    
    log_wakeup_reason();
    
    // Initialize enabled monitors (only once)
    if (init_application() != ESP_OK) {
        handle_init_failure();
        return;
    }
    
    // Main measurement loop
    while (1) {
        // Run monitoring cycle
        if (run_monitoring_cycle() != ESP_OK) {
            ESP_LOGW(TAG, "Monitoring cycle had warnings");
        }
        
        // Sleep or delay before next cycle
        cleanup_and_sleep();
        
        // If deep sleep is enabled, we never reach here (device resets)
        // If disabled, loop continues after delay
        if (DEEP_SLEEP_ENABLED) {
            break;  // Should never reach, but safety exit
        }
    }
}
