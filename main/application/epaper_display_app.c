/**
 * @file epaper_display_app.c
 * @brief ePaper Display Application - Implementation
 */

#include "epaper_display_app.h"
#include "../config/esp32-config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char* TAG = "EPAPER_APP";

void epaper_display_get_default_config(epaper_display_config_t* config) {
    if (config == NULL) {
        return;
    }
    
    memset(config, 0, sizeof(epaper_display_config_t));
    
    config->update_interval_ms = 60000;  // Update every 60 seconds
    config->enable_auto_update = false;  // Manual update mode
    config->enable_logging = true;
    
    // Show all sensor data by default
    config->show_temperature = true;
    config->show_humidity = true;
    config->show_soil = true;
    config->show_battery = true;
    config->show_timestamp = true;
}

esp_err_t epaper_display_init(epaper_display_app_t* app, const epaper_display_config_t* config) {
    if (app == NULL || config == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Initializing ePaper display application...");
    
    // Copy configuration
    memcpy(&app->config, config, sizeof(epaper_display_config_t));
    
    // Get driver configuration based on compile-time model selection
    epaper_config_t driver_config;
    
#if defined(EPAPER_MODEL_213_BN)
    epaper_get_default_config(&driver_config, EPAPER_MODEL_213_122x250);
#elif defined(EPAPER_MODEL_154_D67)
    epaper_get_default_config(&driver_config, EPAPER_MODEL_154_200x200);
#elif defined(EPAPER_MODEL_290_BS)
    epaper_get_default_config(&driver_config, EPAPER_MODEL_290_128x296);
#elif defined(EPAPER_MODEL_420_GDEY042T81)
    epaper_get_default_config(&driver_config, EPAPER_MODEL_420_400x300);
#else
    // Default to 2.13" if no model specified
    ESP_LOGW(TAG, "No display model defined, defaulting to 2.13\"");
    epaper_get_default_config(&driver_config, EPAPER_MODEL_213_122x250);
#endif
    
    // Configure pins from esp32-config.h
    driver_config.spi_host = EPAPER_SPI_HOST;
    driver_config.mosi_pin = EPAPER_SPI_MOSI_PIN;
    driver_config.sck_pin = EPAPER_SPI_SCK_PIN;
    driver_config.cs_pin = EPAPER_SPI_CS_PIN;
    driver_config.dc_pin = EPAPER_SPI_DC_PIN;
    driver_config.rst_pin = EPAPER_SPI_RST_PIN;
    driver_config.busy_pin = EPAPER_SPI_BUSY_PIN;
    driver_config.power_pin = EPAPER_POWER_PIN;
    driver_config.rotation = EPAPER_ROTATION;
    driver_config.full_update_interval = EPAPER_FULL_UPDATE_INTERVAL;
    
    // Initialize driver
    esp_err_t ret = epaper_init(&app->driver, &driver_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ePaper driver: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Power on display
    ret = epaper_power_on(&app->driver);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to power on display: %s", esp_err_to_name(ret));
        epaper_deinit(&app->driver);
        return ret;
    }
    
    // Just clear framebuffer in memory, don't update display yet
    // First sensor data update will initialize display properly
    epaper_clear(&app->driver);
    
    app->is_running = true;
    app->last_update_time = 0;
    
    ESP_LOGI(TAG, "ePaper display application initialized");
    return ESP_OK;
}

esp_err_t epaper_display_deinit(epaper_display_app_t* app) {
    if (app == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    app->is_running = false;
    
    // Clear display before shutdown
    epaper_clear(&app->driver);
    epaper_update(&app->driver, true);
    
    // Power off and deinitialize
    epaper_power_off(&app->driver);
    epaper_deinit(&app->driver);
    
    ESP_LOGI(TAG, "ePaper display application deinitialized");
    return ESP_OK;
}

esp_err_t epaper_display_update_data(epaper_display_app_t* app,
                                     float temperature, float humidity,
                                     float soil_moisture, float battery_voltage) {
    if (app == NULL || !app->is_running) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Update stored values
    app->config.temperature = temperature;
    app->config.humidity = humidity;
    app->config.soil_moisture = soil_moisture;
    app->config.battery_voltage = battery_voltage;
    
    if (app->config.enable_logging) {
        ESP_LOGI(TAG, "Updating display: T=%.1f°C H=%.1f%% S=%.1f%% B=%.2fV",
                 temperature, humidity, soil_moisture, battery_voltage);
    }
    
    // Clear framebuffer (set all to white)
    ESP_LOGI(TAG, "Clearing framebuffer...");
    epaper_clear(&app->driver);
    
    char buffer[64];
    uint16_t y_pos = 5;
    const uint16_t line_height = 14;  // Compact spacing for narrow display
    
    // Draw compact header
    ESP_LOGI(TAG, "Drawing header...");
    epaper_draw_text(&app->driver, app->driver.config.width / 2, y_pos, 
                     "Sensor Data", 1, EPAPER_ALIGN_CENTER);
    y_pos += 12;
    
    // Draw separator line
    ESP_LOGI(TAG, "Drawing separator at y=%d", y_pos);
    epaper_draw_line(&app->driver, 10, y_pos, app->driver.config.width - 10, y_pos, EPAPER_COLOR_BLACK);
    y_pos += 8;
    
    // Temperature - very compact format for narrow display
    if (app->config.show_temperature) {
        snprintf(buffer, sizeof(buffer), "T:%.1fC", temperature);
        ESP_LOGI(TAG, "Drawing: %s at y=%d", buffer, y_pos);
        epaper_draw_text(&app->driver, 10, y_pos, buffer, 1, EPAPER_ALIGN_LEFT);
        y_pos += line_height;
    }
    
    // Humidity - very compact format
    if (app->config.show_humidity) {
        snprintf(buffer, sizeof(buffer), "H:%.0f%%", humidity);
        ESP_LOGI(TAG, "Drawing: %s at y=%d", buffer, y_pos);
        epaper_draw_text(&app->driver, 10, y_pos, buffer, 1, EPAPER_ALIGN_LEFT);
        y_pos += line_height;
    }
    
    // Soil Moisture - very compact format
    if (app->config.show_soil) {
        snprintf(buffer, sizeof(buffer), "S:%.0f%%", soil_moisture);
        ESP_LOGI(TAG, "Drawing: %s at y=%d", buffer, y_pos);
        epaper_draw_text(&app->driver, 10, y_pos, buffer, 1, EPAPER_ALIGN_LEFT);
        y_pos += line_height;
    }
    
    // Battery Voltage - very compact format with bar
    if (app->config.show_battery) {
        snprintf(buffer, sizeof(buffer), "B:%.2fV", battery_voltage);
        ESP_LOGI(TAG, "Drawing: %s at y=%d", buffer, y_pos);
        epaper_draw_text(&app->driver, 10, y_pos, buffer, 1, EPAPER_ALIGN_LEFT);
        y_pos += line_height;
        
        // Draw battery indicator bar (full width for 122px display)
        ESP_LOGI(TAG, "Drawing battery indicator at y=%d", y_pos);
        uint16_t battery_bar_width = (uint16_t)((battery_voltage - 3.0) / (4.2 - 3.0) * 102);
        if (battery_bar_width > 102) battery_bar_width = 102;
        if (battery_bar_width > 0) {
            epaper_draw_rect(&app->driver, 10, y_pos, battery_bar_width, 8, EPAPER_COLOR_BLACK, true);
        }
        epaper_draw_rect(&app->driver, 10, y_pos, 102, 8, EPAPER_COLOR_BLACK, false);
    }
    
    // Update display (will auto-select full/partial based on counter)
    ESP_LOGI(TAG, "Sending framebuffer to display...");
    esp_err_t ret = epaper_update(&app->driver, false);  // Let driver decide full vs partial
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Display updated successfully");
        app->last_update_time = esp_timer_get_time() / 1000; // Convert to ms
    } else {
        ESP_LOGE(TAG, "Display update failed: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

esp_err_t epaper_display_refresh(epaper_display_app_t* app, bool full_update) {
    if (app == NULL || !app->is_running) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Redraw current data
    return epaper_display_update_data(app, 
                                      app->config.temperature,
                                      app->config.humidity,
                                      app->config.soil_moisture,
                                      app->config.battery_voltage);
}

esp_err_t epaper_display_show_message(epaper_display_app_t* app, const char* message) {
    if (app == NULL || message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Showing message: %s", message);
    
    // Clear and draw message
    epaper_clear(&app->driver);
    epaper_draw_text(&app->driver, 10, 30, message, 2, EPAPER_ALIGN_LEFT);
    
    return ESP_OK;
}

esp_err_t epaper_display_sleep(epaper_display_app_t* app) {
    if (app == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Putting display to sleep");
    return epaper_power_off(&app->driver);
}
