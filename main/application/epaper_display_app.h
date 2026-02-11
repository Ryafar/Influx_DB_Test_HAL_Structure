/**
 * @file epaper_display_app.h
 * @brief ePaper Display Application - Header
 * 
 * Application layer for managing ePaper display updates with sensor data.
 * Follows the same pattern as env_monitor_app and soil_monitor_app.
 */

#ifndef EPAPER_DISPLAY_APP_H
#define EPAPER_DISPLAY_APP_H

#include "esp_err.h"
#include "epaper/epaper_driver.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Display Update Configuration
typedef struct {
    uint32_t update_interval_ms;     // Time between display updates
    bool enable_auto_update;         // Auto-update on sensor data change
    bool enable_logging;
    
    // Sensor data to display
    float temperature;
    float humidity;
    float soil_moisture;
    float battery_voltage;
    
    // Display preferences
    bool show_temperature;
    bool show_humidity;
    bool show_soil;
    bool show_battery;
    bool show_timestamp;
} epaper_display_config_t;

// Application Handle
typedef struct {
    epaper_driver_t driver;
    epaper_display_config_t config;
    bool is_running;
    uint64_t last_update_time;
} epaper_display_app_t;

/**
 * @brief Get default configuration
 */
void epaper_display_get_default_config(epaper_display_config_t* config);

/**
 * @brief Initialize display application
 */
esp_err_t epaper_display_init(epaper_display_app_t* app, const epaper_display_config_t* config);

/**
 * @brief Deinitialize display application
 */
esp_err_t epaper_display_deinit(epaper_display_app_t* app);

/**
 * @brief Update sensor data to display
 */
esp_err_t epaper_display_update_data(epaper_display_app_t* app,
                                     float temperature, float humidity,
                                     float soil_moisture, float battery_voltage);

/**
 * @brief Force display refresh
 */
esp_err_t epaper_display_refresh(epaper_display_app_t* app, bool full_update);

/**
 * @brief Show custom message on display
 */
esp_err_t epaper_display_show_message(epaper_display_app_t* app, const char* message);

/**
 * @brief Put display to sleep (power off)
 */
esp_err_t epaper_display_sleep(epaper_display_app_t* app);

#ifdef __cplusplus
}
#endif

#endif // EPAPER_DISPLAY_APP_H
