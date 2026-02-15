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
#include "esp_timer.h"
#include "nvs_flash.h"

#include "config/esp32-config.h"
#include "config/credentials.h"
#include "wifi_manager.h"
#include "application/influx_sender.h"
#include "influxdb_client.h"
#include "esp_utils.h"

#if ENABLE_BATTERY_MONITOR
#include "application/battery_monitor_task.h"
#endif

#if ENABLE_ENV_MONITOR
#include "application/env_monitor_app.h"

static env_monitor_app_t env_app;
#endif

#if ENABLE_SOIL_MONITOR
#include "application/soil_monitor_app.h"

static soil_monitor_app_t soil_app;
#endif

#if ENABLE_EPAPER_DISPLAY
#include "application/epaper_display_app.h"

static epaper_display_app_t epaper_app;
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
    
#if ENABLE_WIFI
    // Initialize network stack (only needed for WiFi/InfluxDB)
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
#else
    ESP_LOGI(TAG, "WiFi disabled - running in offline mode");
#endif
    
    return ESP_OK;
}

static esp_err_t init_sensors(void) {
    esp_err_t ret = ESP_OK;
    
#if ENABLE_BATTERY_MONITOR
    ESP_LOGI(TAG, "Battery Monitor enabled (init handled by task)");
#endif

#if ENABLE_ENV_MONITOR
    ESP_LOGI(TAG, "Initializing Environment Monitor (AHT20)...");
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
    soil_monitor_config_t soil_config;
    soil_monitor_get_default_config(&soil_config);
    soil_config.measurements_per_cycle = SOIL_MEASUREMENTS_PER_CYCLE;
    
    ret = soil_monitor_init(&soil_app, &soil_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize soil monitor: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Soil Monitor initialized");
#endif

#if ENABLE_EPAPER_DISPLAY
    ESP_LOGI(TAG, "Initializing ePaper Display (1.54\" 200x200)...");
    epaper_display_config_t epaper_config;
    epaper_display_get_default_config(&epaper_config);
    
    ret = epaper_display_init(&epaper_app, &epaper_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ePaper display: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "ePaper Display initialized");
#endif

#if !ENABLE_ENV_MONITOR && !ENABLE_BATTERY_MONITOR && !ENABLE_SOIL_MONITOR && !ENABLE_EPAPER_DISPLAY
    #error "At least one monitor or display must be enabled!"
#endif

    return ESP_OK;
}

// ============================================================================
// ePaper Display Test Routine
// ============================================================================

#if ENABLE_EPAPER_DISPLAY
static void run_epaper_test_routine(void) {
    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "Starting Simple ePaper Test");
    ESP_LOGI(TAG, "======================================");
    
    // Test 1: Clear display (should be all white)
    ESP_LOGI(TAG, "Test 1: Clearing to white...");
    epaper_clear(&epaper_app.driver);
    epaper_update(&epaper_app.driver, true);
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // Test 2: Simple text at different positions
    ESP_LOGI(TAG, "Test 2: Drawing simple text...");
    epaper_clear(&epaper_app.driver);
    epaper_draw_text(&epaper_app.driver, 10, 10, "Hello ESP32!", 1, EPAPER_ALIGN_LEFT);
    epaper_draw_text(&epaper_app.driver, 10, 30, "2.13\" Display", 1, EPAPER_ALIGN_LEFT);
    epaper_draw_text(&epaper_app.driver, 10, 50, "250x122 pixels", 1, EPAPER_ALIGN_LEFT);
    epaper_update(&epaper_app.driver, true);
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // Test 3: Larger text
    ESP_LOGI(TAG, "Test 3: Larger text...");
    epaper_clear(&epaper_app.driver);
    epaper_draw_text(&epaper_app.driver, 10, 20, "BIG TEXT", 2, EPAPER_ALIGN_LEFT);
    epaper_draw_text(&epaper_app.driver, 10, 50, "Size 2", 2, EPAPER_ALIGN_LEFT);
    epaper_update(&epaper_app.driver, true);
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // Test 4: Draw border rectangle
    ESP_LOGI(TAG, "Test 4: Border rectangle...");
    epaper_clear(&epaper_app.driver);
    epaper_draw_rect(&epaper_app.driver, 5, 5, 240, 112, EPAPER_COLOR_BLACK, false);
    epaper_draw_text(&epaper_app.driver, 125, 56, "BORDER", 2, EPAPER_ALIGN_CENTER);
    epaper_update(&epaper_app.driver, true);
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // Test 5: Draw diagonal lines
    ESP_LOGI(TAG, "Test 5: Drawing diagonal lines...");
    epaper_clear(&epaper_app.driver);
    epaper_draw_text(&epaper_app.driver, 10, 10, "DIAGONAL LINES", 1, EPAPER_ALIGN_LEFT);
    // Draw some diagonal lines
    for (int i = 0; i < 50; i += 10) {
        epaper_draw_line(&epaper_app.driver, 10 + i, 40, 60 + i, 40, EPAPER_COLOR_BLACK);
    }
    epaper_draw_text(&epaper_app.driver, 10, 60, "BLACK LINES ABOVE", 1, EPAPER_ALIGN_LEFT);
    epaper_update(&epaper_app.driver, true);
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "Simple Test Complete");
    ESP_LOGI(TAG, "======================================\n");
}

// ============================================================================
// ePaper Partial Refresh Demo
// ============================================================================

static void run_partial_refresh_demo(void) {
    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "Partial Refresh Demo - Watch the Speed!");
    ESP_LOGI(TAG, "======================================");
    
    // Start with base values and do a full refresh
    float temp = 20.0;
    float hum = 50.0;
    float soil = 30.0;
    float batt = 4.2;
    
    ESP_LOGI(TAG, "Demo 1: Initial full refresh...");
    epaper_display_update_data(&epaper_app, temp, hum, soil, batt);
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // Now do 12 partial refreshes with changing values
    ESP_LOGI(TAG, "Demo 2: Watch 12 updates (first 10 partial, then full refresh)...");
    ESP_LOGI(TAG, "Notice: Partial updates are MUCH faster (~0.3s vs ~2s)!");
    
    for (int i = 1; i <= 12; i++) {
        // Simulate sensor values changing slightly
        temp += 2.0;    // Temperature rising
        hum -= 3.0;     // Humidity dropping
        soil += 5.0;    // Soil moisture increasing
        batt -= 0.05;   // Battery draining slowly
        
        ESP_LOGI(TAG, "Update %d: T=%.1f°C H=%.1f%% S=%.1f%% B=%.2fV", 
                 i, temp, hum, soil, batt);
        
        uint32_t start_time = esp_timer_get_time() / 1000;
        epaper_display_update_data(&epaper_app, temp, hum, soil, batt);
        uint32_t duration = (esp_timer_get_time() / 1000) - start_time;
        
        ESP_LOGI(TAG, "Update took %lu ms", duration);
        
        if (i == 10) {
            ESP_LOGI(TAG, ">>> Next update will be FULL REFRESH (watch the difference!)");
        }
        
        vTaskDelay(pdMS_TO_TICKS(2000));  // 2 seconds between updates
    }
    
    ESP_LOGI(TAG, "\nDemo 3: Force full refresh to clear any ghosting...");
    epaper_update(&epaper_app.driver, true);  // Force full refresh
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    ESP_LOGI(TAG, "Demo 4: Rapid partial updates (10 in a row)...");
    for (int i = 0; i < 10; i++) {
        temp += 1.0;
        epaper_display_update_data(&epaper_app, temp, hum, soil, batt);
        ESP_LOGI(TAG, "Rapid update %d complete - Temperature now %.1f°C", i+1, temp);
        vTaskDelay(pdMS_TO_TICKS(800));  // 800ms between updates
    }
    
    ESP_LOGI(TAG, "\n======================================");
    ESP_LOGI(TAG, "Partial Refresh Demo Complete!");
    ESP_LOGI(TAG, "Summary:");
    ESP_LOGI(TAG, "- Partial refresh: ~300ms (fast, slight ghosting)");
    ESP_LOGI(TAG, "- Full refresh: ~2000ms (slow, no ghosting)");
    ESP_LOGI(TAG, "- Auto full refresh every 10 updates prevents ghosting");
    ESP_LOGI(TAG, "======================================\n");
}
#endif  // ENABLE_EPAPER_DISPLAY

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
    ESP_LOGI(TAG, "Starting environment monitor task...");
    ret = env_monitor_start(&env_app);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start environment monitor: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Wait for environment monitoring to complete
    ret = env_monitor_wait_for_completion(&env_app, 30000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Environment monitor timeout: %s", esp_err_to_name(ret));
    }
#endif

#if ENABLE_SOIL_MONITOR
    ESP_LOGI(TAG, "Starting soil monitor task...");
    ret = soil_monitor_start(&soil_app);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start soil monitor: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Wait for soil monitoring to complete
    ret = soil_monitor_wait_for_completion(&soil_app, 30000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Soil monitor timeout: %s", esp_err_to_name(ret));
    }
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
    
#if ENABLE_EPAPER_DISPLAY
    // Update ePaper display with latest sensor data
    ESP_LOGI(TAG, "Updating ePaper display...");
    float temp = 0, hum = 0, soil = 0, batt = 0;
    
    #if ENABLE_ENV_MONITOR
        // Get temperature and humidity from env monitor
        // (This is simplified - you may need to add getters to env_monitor_app)
        temp = 25.0;  // Placeholder
        hum = 60.0;   // Placeholder
    #endif
    
    #if ENABLE_SOIL_MONITOR
        soil = 50.0;  // Placeholder
    #endif
    
    #if ENABLE_BATTERY_MONITOR
        batt = 3.7;   // Placeholder
    #endif
    
    // epaper_display_update_data already calls epaper_update internally
    ret = epaper_display_update_data(&epaper_app, temp, hum, soil, batt);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Display update failed: %s", esp_err_to_name(ret));
    }
#endif
    
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
#if ENABLE_EPAPER_DISPLAY
    ESP_LOGI(TAG, "  - ePaper Display: ENABLED (1.54\", 200x200)");
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
    
    // Initialize sensors (only once), inits battery, env, soil monitors as needed
    ESP_LOGI(TAG, "Initializing sensors...");
    if (init_sensors() != ESP_OK) { 
        ESP_LOGE(TAG, "Sensor initialization failed! Retrying in 60s...");
        vTaskDelay(pdMS_TO_TICKS(60000));
        esp_restart();
        return;
    }
    
    ESP_LOGI(TAG, "System ready!\n");

#if ENABLE_EPAPER_DISPLAY
    // Run simple ePaper display test
    run_epaper_test_routine();
    
    // Uncomment to run partial refresh demo:
    // vTaskDelay(pdMS_TO_TICKS(2000));
    // run_partial_refresh_demo();
#endif

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
