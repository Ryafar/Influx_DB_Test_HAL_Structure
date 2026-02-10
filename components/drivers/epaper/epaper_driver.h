/**
 * @file epaper_driver.h
 * @brief WeAct Studio ePaper Display Driver - Header
 * 
 * Generic driver for WeAct Studio ePaper modules using SPI interface.
 * Supports various display sizes (1.54", 2.13", 2.9", 4.2").
 */

#ifndef EPAPER_DRIVER_H
#define EPAPER_DRIVER_H

#include "esp_err.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Display Model Identifiers
typedef enum {
    EPAPER_MODEL_154_200x200,   // 1.54" GDEH0154D67
    EPAPER_MODEL_213_122x250,   // 2.13" DEPG0213BN
    EPAPER_MODEL_290_128x296,   // 2.9" DEPG0290BS
    EPAPER_MODEL_420_400x300,   // 4.2" GDEY042T81
} epaper_model_t;

// Color Definitions
typedef enum {
    EPAPER_COLOR_WHITE = 0,
    EPAPER_COLOR_BLACK = 1
} epaper_color_t;

// Display Configuration
typedef struct {
    // SPI Configuration
    spi_host_device_t spi_host;
    gpio_num_t mosi_pin;
    gpio_num_t sck_pin;
    gpio_num_t cs_pin;
    
    // Control Pins
    gpio_num_t dc_pin;      // Data/Command
    gpio_num_t rst_pin;     // Reset
    gpio_num_t busy_pin;    // Busy signal input
    gpio_num_t power_pin;   // Power control (optional, -1 to disable)
    
    // Display Parameters
    epaper_model_t model;
    uint16_t width;
    uint16_t height;
    uint8_t rotation;       // 0, 1, 2, 3
    
    // Update Strategy
    bool use_partial_update;
    uint8_t full_update_interval; // Full refresh every N partial updates
} epaper_config_t;

// Display Driver Handle
typedef struct {
    epaper_config_t config;
    spi_device_handle_t spi;
    uint8_t* framebuffer;
    uint32_t fb_size;
    bool is_initialized;
    bool is_powered;
    uint8_t partial_update_count;
} epaper_driver_t;

// Text Alignment
typedef enum {
    EPAPER_ALIGN_LEFT,
    EPAPER_ALIGN_CENTER,
    EPAPER_ALIGN_RIGHT
} epaper_text_align_t;

/**
 * @brief Get default configuration for specific display model
 */
esp_err_t epaper_get_default_config(epaper_config_t* config, epaper_model_t model);

/**
 * @brief Initialize ePaper display
 */
esp_err_t epaper_init(epaper_driver_t* driver, const epaper_config_t* config);

/**
 * @brief Deinitialize and free resources
 */
esp_err_t epaper_deinit(epaper_driver_t* driver);

/**
 * @brief Power on the display
 */
esp_err_t epaper_power_on(epaper_driver_t* driver);

/**
 * @brief Power off and enter deep sleep
 */
esp_err_t epaper_power_off(epaper_driver_t* driver);

/**
 * @brief Clear display (fill with white)
 */
esp_err_t epaper_clear(epaper_driver_t* driver);

/**
 * @brief Fill display with color (0=white, 1=black)
 */
esp_err_t epaper_fill(epaper_driver_t* driver, uint8_t color);

/**
 * @brief Draw pixel at (x, y)
 */
esp_err_t epaper_draw_pixel(epaper_driver_t* driver, uint16_t x, uint16_t y, epaper_color_t color);

/**
 * @brief Draw line from (x0, y0) to (x1, y1)
 */
esp_err_t epaper_draw_line(epaper_driver_t* driver, uint16_t x0, uint16_t y0, 
                            uint16_t x1, uint16_t y1, epaper_color_t color);

/**
 * @brief Draw rectangle
 */
esp_err_t epaper_draw_rect(epaper_driver_t* driver, uint16_t x, uint16_t y, 
                            uint16_t width, uint16_t height, epaper_color_t color, bool filled);

/**
 * @brief Draw text string at (x, y)
 */
esp_err_t epaper_draw_text(epaper_driver_t* driver, uint16_t x, uint16_t y, 
                            const char* text, uint8_t size, epaper_text_align_t align);

/**
 * @brief Update display (full or partial depending on config)
 */
esp_err_t epaper_update(epaper_driver_t* driver, bool force_full);

/**
 * @brief Wait until display is not busy
 */
esp_err_t epaper_wait_idle(epaper_driver_t* driver, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif // EPAPER_DRIVER_H
