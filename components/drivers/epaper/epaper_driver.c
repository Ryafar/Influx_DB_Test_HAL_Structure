/**
 * @file epaper_driver.c
 * @brief WeAct Studio ePaper Display Driver - Implementation
 * 
 * Implements SPI communication with ePaper display controllers.
 * Based on GxEPD2 Arduino library patterns adapted for ESP-IDF.
 */

#include "epaper_driver.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "EPAPER";

// Simple 5x8 bitmap font for ASCII characters 32-126
static const uint8_t font_5x8[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // ' '
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // '!'
    {0x00, 0x07, 0x00, 0x07, 0x00}, // '"'
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // '#'
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // '$'
    {0x23, 0x13, 0x08, 0x64, 0x62}, // '%'
    {0x36, 0x49, 0x56, 0x20, 0x50}, // '&'
    {0x00, 0x08, 0x07, 0x03, 0x00}, // '''
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // '('
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // ')'
    {0x2A, 0x1C, 0x7F, 0x1C, 0x2A}, // '*'
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // '+'
    {0x00, 0x80, 0x70, 0x30, 0x00}, // ','
    {0x08, 0x08, 0x08, 0x08, 0x08}, // '-'
    {0x00, 0x00, 0x60, 0x60, 0x00}, // '.'
    {0x20, 0x10, 0x08, 0x04, 0x02}, // '/'
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // '0'
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // '1'
    {0x72, 0x49, 0x49, 0x49, 0x46}, // '2'
    {0x21, 0x41, 0x49, 0x4D, 0x33}, // '3'
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // '4'
    {0x27, 0x45, 0x45, 0x45, 0x39}, // '5'
    {0x3C, 0x4A, 0x49, 0x49, 0x31}, // '6'
    {0x41, 0x21, 0x11, 0x09, 0x07}, // '7'
    {0x36, 0x49, 0x49, 0x49, 0x36}, // '8'
    {0x46, 0x49, 0x49, 0x29, 0x1E}, // '9'
    {0x00, 0x00, 0x14, 0x00, 0x00}, // ':'
    {0x00, 0x40, 0x34, 0x00, 0x00}, // ';'
    {0x00, 0x08, 0x14, 0x22, 0x41}, // '<'
    {0x14, 0x14, 0x14, 0x14, 0x14}, // '='
    {0x00, 0x41, 0x22, 0x14, 0x08}, // '>'
    {0x02, 0x01, 0x59, 0x09, 0x06}, // '?'
    {0x3E, 0x41, 0x5D, 0x59, 0x4E}, // '@'
    {0x7C, 0x12, 0x11, 0x12, 0x7C}, // 'A'
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // 'B'
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // 'C'
    {0x7F, 0x41, 0x41, 0x41, 0x3E}, // 'D'
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // 'E'
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // 'F'
    {0x3E, 0x41, 0x41, 0x51, 0x73}, // 'G'
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 'H'
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // 'I'
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // 'J'
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // 'K'
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // 'L'
    {0x7F, 0x02, 0x1C, 0x02, 0x7F}, // 'M'
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 'N'
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 'O'
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // 'P'
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 'Q'
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // 'R'
    {0x26, 0x49, 0x49, 0x49, 0x32}, // 'S'
    {0x03, 0x01, 0x7F, 0x01, 0x03}, // 'T'
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 'U'
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 'V'
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // 'W'
    {0x63, 0x14, 0x08, 0x14, 0x63}, // 'X'
    {0x03, 0x04, 0x78, 0x04, 0x03}, // 'Y'
    {0x61, 0x59, 0x49, 0x4D, 0x43}, // 'Z'
    {0x00, 0x7F, 0x41, 0x41, 0x41}, // '['
    {0x02, 0x04, 0x08, 0x10, 0x20}, // '\'
    {0x00, 0x41, 0x41, 0x41, 0x7F}, // ']'
    {0x04, 0x02, 0x01, 0x02, 0x04}, // '^'
    {0x40, 0x40, 0x40, 0x40, 0x40}, // '_'
    {0x00, 0x03, 0x07, 0x08, 0x00}, // '`'
    {0x20, 0x54, 0x54, 0x78, 0x40}, // 'a'
    {0x7F, 0x28, 0x44, 0x44, 0x38}, // 'b'
    {0x38, 0x44, 0x44, 0x44, 0x28}, // 'c'
    {0x38, 0x44, 0x44, 0x28, 0x7F}, // 'd'
    {0x38, 0x54, 0x54, 0x54, 0x18}, // 'e'
    {0x00, 0x08, 0x7E, 0x09, 0x02}, // 'f'
    {0x18, 0xA4, 0xA4, 0x9C, 0x78}, // 'g'
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // 'h'
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // 'i'
    {0x20, 0x40, 0x40, 0x3D, 0x00}, // 'j'
    {0x7F, 0x10, 0x28, 0x44, 0x00}, // 'k'
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // 'l'
    {0x7C, 0x04, 0x78, 0x04, 0x78}, // 'm'
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // 'n'
    {0x38, 0x44, 0x44, 0x44, 0x38}, // 'o'
    {0xFC, 0x18, 0x24, 0x24, 0x18}, // 'p'
    {0x18, 0x24, 0x24, 0x18, 0xFC}, // 'q'
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // 'r'
    {0x48, 0x54, 0x54, 0x54, 0x24}, // 's'
    {0x04, 0x04, 0x3F, 0x44, 0x24}, // 't'
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // 'u'
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // 'v'
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // 'w'
    {0x44, 0x28, 0x10, 0x28, 0x44}, // 'x'
    {0x4C, 0x90, 0x90, 0x90, 0x7C}, // 'y'
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // 'z'
    {0x00, 0x08, 0x36, 0x41, 0x00}, // '{'
    {0x00, 0x00, 0x77, 0x00, 0x00}, // '|'
    {0x00, 0x41, 0x36, 0x08, 0x00}, // '}'
    {0x02, 0x01, 0x02, 0x04, 0x02}, // '~'
};

// Display specifications
typedef struct {
    uint16_t width;
    uint16_t height;
    const char* name;
} epaper_spec_t;

static const epaper_spec_t display_specs[] = {
    [EPAPER_MODEL_154_200x200] = {200, 200, "1.54\" GDEH0154D67"},
    [EPAPER_MODEL_213_122x250] = {122, 250, "2.13\" DEPG0213BN"},  // 122 wide x 250 tall (rotated orientation)
    [EPAPER_MODEL_290_128x296] = {128, 296, "2.9\" DEPG0290BS"},
    [EPAPER_MODEL_420_400x300] = {400, 300, "4.2\" GDEY042T81"},
};

// ============================================================================
// Low-Level SPI Communication
// ============================================================================

/**
 * @brief Send command byte to display (DC=LOW)
 */
static esp_err_t epaper_send_command(epaper_driver_t* driver, uint8_t cmd) {
    gpio_set_level(driver->config.dc_pin, 0);  // DC=0 for command
    
    spi_transaction_t trans = {
        .length = 8,
        .tx_buffer = &cmd,
        .flags = SPI_TRANS_USE_TXDATA,
    };
    trans.tx_data[0] = cmd;
    
    return spi_device_transmit(driver->spi, &trans);
}

/**
 * @brief Send data byte to display (DC=HIGH)
 */
static esp_err_t epaper_send_data(epaper_driver_t* driver, uint8_t data) {
    gpio_set_level(driver->config.dc_pin, 1);  // DC=1 for data
    
    spi_transaction_t trans = {
        .length = 8,
        .tx_buffer = &data,
        .flags = SPI_TRANS_USE_TXDATA,
    };
    trans.tx_data[0] = data;
    
    return spi_device_transmit(driver->spi, &trans);
}

/**
 * @brief Send data buffer to display (DC=HIGH)
 */
static esp_err_t epaper_send_data_buffer(epaper_driver_t* driver, const uint8_t* data, size_t len) {
    if (len == 0) return ESP_OK;
    
    gpio_set_level(driver->config.dc_pin, 1);  // DC=1 for data
    
    spi_transaction_t trans = {
        .length = len * 8,
        .tx_buffer = data,
    };
    
    return spi_device_transmit(driver->spi, &trans);
}

/**
 * @brief Hardware reset via RST pin
 */
static esp_err_t epaper_hw_reset(epaper_driver_t* driver) {
    gpio_set_level(driver->config.rst_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(driver->config.rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

/**
 * @brief Wait for display BUSY signal to go LOW
 */
esp_err_t epaper_wait_idle(epaper_driver_t* driver, uint32_t timeout_ms) {
    if (driver == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint32_t start_time = xTaskGetTickCount();
    uint32_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    
    while (gpio_get_level(driver->config.busy_pin) == 1) {
        if ((xTaskGetTickCount() - start_time) > timeout_ticks) {
            ESP_LOGW(TAG, "Wait idle timeout after %lu ms", timeout_ms);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    ESP_LOGD(TAG, "Display idle");
    return ESP_OK;
}

// ============================================================================
// Display Controller Initialization
// ============================================================================

/**
 * @brief Initialize 2.13" DEPG0213BN display (SSD1680 controller)
 */
static esp_err_t epaper_init_213bn(epaper_driver_t* driver) {
    ESP_LOGI(TAG, "Initializing 2.13\" DEPG0213BN (SSD1680)");
    
    epaper_hw_reset(driver);
    epaper_wait_idle(driver, 2000);
    
    // Software reset
    epaper_send_command(driver, 0x12);
    vTaskDelay(pdMS_TO_TICKS(10));
    epaper_wait_idle(driver, 2000);
    
    // Driver output control
    epaper_send_command(driver, 0x01);
    epaper_send_data(driver, 0x27);  // (296-1) low byte = 0x127 -> Height-1 for 250 tall display
    epaper_send_data(driver, 0x01);  // (296-1) high byte
    epaper_send_data(driver, 0x00);
    
    // Data entry mode setting
    epaper_send_command(driver, 0x11);
    epaper_send_data(driver, 0x03);  // Y increment, X increment - fixes upside-down text
    
    // Set RAM X address start/end  
    epaper_send_command(driver, 0x44);
    epaper_send_data(driver, 0x00);  // Start at 0
    epaper_send_data(driver, 0x0F);  // End at 15 (16 bytes = 128 pixels, covers 122 width)
    
    // Set RAM Y address start/end (normal order for Y increment mode)
    epaper_send_command(driver, 0x45);
    epaper_send_data(driver, 0x00);  // Start Y = 0 low byte
    epaper_send_data(driver, 0x00);  // Start Y high byte
    epaper_send_data(driver, 0x27);  // End Y = 0x127 (295) low byte
    epaper_send_data(driver, 0x01);  // End Y high byte
    
    // Border waveform control
    epaper_send_command(driver, 0x3C);
    epaper_send_data(driver, 0x05);
    
    // Display update control
    epaper_send_command(driver, 0x21);
    epaper_send_data(driver, 0x00);
    epaper_send_data(driver, 0x80);  // WeActStudio uses 0x80 here
    
    // Temperature sensor control
    epaper_send_command(driver, 0x18);
    epaper_send_data(driver, 0x80);  // Internal temperature sensor
    
    epaper_wait_idle(driver, 2000);
    
    ESP_LOGI(TAG, "2.13\" display initialized");
    return ESP_OK;
}

/**
 * @brief Initialize 1.54" GDEH0154D67 display (SSD1681 controller)
 */
static esp_err_t epaper_init_154d67(epaper_driver_t* driver) {
    ESP_LOGI(TAG, "Initializing 1.54\" GDEH0154D67 (SSD1681)");
    
    epaper_hw_reset(driver);
    epaper_wait_idle(driver, 2000);
    
    // Software reset
    epaper_send_command(driver, 0x12);
    vTaskDelay(pdMS_TO_TICKS(10));
    epaper_wait_idle(driver, 2000);
    
    // Driver output control (200 lines)
    epaper_send_command(driver, 0x01);
    epaper_send_data(driver, 0xC7);  // Height - 1 (200-1 = 199 = 0xC7)
    epaper_send_data(driver, 0x00);
    epaper_send_data(driver, 0x00);
    
    // Data entry mode setting
    epaper_send_command(driver, 0x11);
    epaper_send_data(driver, 0x03);  // X increment, Y increment (normal orientation)
    
    // Set RAM X address start/end
    epaper_send_command(driver, 0x44);
    epaper_send_data(driver, 0x00);  // Start at 0
    epaper_send_data(driver, 0x18);  // End at 24 (200/8 = 25, but 0-indexed)
    
    // Set RAM Y address start/end
    epaper_send_command(driver, 0x45);
    epaper_send_data(driver, 0x00);  // Start Y = 0
    epaper_send_data(driver, 0x00);
    epaper_send_data(driver, 0xC7);  // End Y = 199
    epaper_send_data(driver, 0x00);
    
    // Border waveform control
    epaper_send_command(driver, 0x3C);
    epaper_send_data(driver, 0x01);  // VBD - LUT1 (white border)
    
    // Temperature sensor control
    epaper_send_command(driver, 0x18);
    epaper_send_data(driver, 0x80);  // Internal temperature sensor
    
    // Display update control
    epaper_send_command(driver, 0x22);
    epaper_send_data(driver, 0xB1);  // Load LUT with display mode 1
    epaper_send_command(driver, 0x20);  // Master activation
    
    epaper_wait_idle(driver, 2000);
    
    ESP_LOGI(TAG, "1.54\" display initialized");
    return ESP_OK;
}

/**
 * @brief Initialize display based on model
 */
static esp_err_t epaper_init_display_controller(epaper_driver_t* driver) {
    switch (driver->config.model) {
        case EPAPER_MODEL_213_122x250:
            return epaper_init_213bn(driver);
            
        case EPAPER_MODEL_154_200x200:
            return epaper_init_154d67(driver);
            
        case EPAPER_MODEL_290_128x296:
        case EPAPER_MODEL_420_400x300:
            ESP_LOGW(TAG, "Display model not yet implemented, using stub");
            epaper_hw_reset(driver);
            return ESP_OK;
            
        default:
            ESP_LOGE(TAG, "Unknown display model");
            return ESP_ERR_NOT_SUPPORTED;
    }
}

// ============================================================================
// Public API Implementation
// ============================================================================

esp_err_t epaper_get_default_config(epaper_config_t* config, epaper_model_t model) {
    if (config == NULL || model >= sizeof(display_specs) / sizeof(display_specs[0])) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(config, 0, sizeof(epaper_config_t));
    
    config->model = model;
    config->width = display_specs[model].width;
    config->height = display_specs[model].height;
    config->rotation = 0;
    config->use_partial_update = true;
    config->full_update_interval = 10;
    
    ESP_LOGI(TAG, "Default config for %s (%dx%d)", 
             display_specs[model].name, config->width, config->height);
    
    return ESP_OK;
}

esp_err_t epaper_init(epaper_driver_t* driver, const epaper_config_t* config) {
    if (driver == NULL || config == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Initializing ePaper display...");
    
    // Copy configuration
    memcpy(&driver->config, config, sizeof(epaper_config_t));
    
    // Calculate framebuffer size - must be padded to full bytes per row
    // Each row needs ceil(width/8) bytes, then multiply by height
    uint32_t bytes_per_row = (config->width + 7) / 8;
    driver->fb_size = bytes_per_row * config->height;
    
    ESP_LOGI(TAG, "Framebuffer: %ux%u pixels, %lu bytes per row, %lu bytes total",
             config->width, config->height, bytes_per_row, driver->fb_size);
    
    driver->framebuffer = (uint8_t*)malloc(driver->fb_size);
    if (driver->framebuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate framebuffer (%lu bytes)", driver->fb_size);
        return ESP_ERR_NO_MEM;
    }
    memset(driver->framebuffer, 0xFF, driver->fb_size); // Initialize to white
    
    // Initialize GPIO pins
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    // DC pin (Data/Command)
    io_conf.pin_bit_mask = (1ULL << config->dc_pin);
    gpio_config(&io_conf);
    
    // RST pin (Reset)
    io_conf.pin_bit_mask = (1ULL << config->rst_pin);
    gpio_config(&io_conf);
    
    // Power pin (if used)
    if (config->power_pin >= 0) {
        io_conf.pin_bit_mask = (1ULL << config->power_pin);
        gpio_config(&io_conf);
        gpio_set_level(config->power_pin, 0); // Initially off
    }
    
    // BUSY pin (input)
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << config->busy_pin);
    gpio_config(&io_conf);
    
    // Initialize SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = config->mosi_pin,
        .miso_io_num = -1,  // No MISO for ePaper
        .sclk_io_num = config->sck_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = driver->fb_size,
    };
    
    esp_err_t ret = spi_bus_initialize(config->spi_host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE means bus already initialized (shared)
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        free(driver->framebuffer);
        return ret;
    }
    
    // Add ePaper device to SPI bus
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 4 * 1000 * 1000,  // 4 MHz (ePaper displays are slow)
        .mode = 0,                           // SPI mode 0
        .spics_io_num = config->cs_pin,
        .queue_size = 1,
        .flags = 0,
        .pre_cb = NULL,
    };
    
    ret = spi_bus_add_device(config->spi_host, &dev_cfg, &driver->spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        spi_bus_free(config->spi_host);
        free(driver->framebuffer);
        return ret;
    }
    
    // Initialize display controller
    ret = epaper_init_display_controller(driver);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display controller init failed: %s", esp_err_to_name(ret));
        spi_bus_remove_device(driver->spi);
        spi_bus_free(config->spi_host);
        free(driver->framebuffer);
        return ret;
    }
    
    driver->is_initialized = true;
    driver->is_powered = false;
    driver->partial_update_count = 0;
    
    ESP_LOGI(TAG, "ePaper display %s initialized successfully", 
             display_specs[config->model].name);
    
    return ESP_OK;
}

esp_err_t epaper_deinit(epaper_driver_t* driver) {
    if (driver == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!driver->is_initialized) {
        return ESP_OK;
    }
    
    // Power off display
    epaper_power_off(driver);
    
    // Free framebuffer
    if (driver->framebuffer) {
        free(driver->framebuffer);
        driver->framebuffer = NULL;
    }
    
    // Remove SPI device and free bus
    spi_bus_remove_device(driver->spi);
    spi_bus_free(driver->config.spi_host);
    
    driver->is_initialized = false;
    ESP_LOGI(TAG, "ePaper display deinitialized");
    
    return ESP_OK;
}

esp_err_t epaper_power_on(epaper_driver_t* driver) {
    if (driver == NULL || !driver->is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (driver->is_powered) {
        ESP_LOGD(TAG, "Display already powered on");
        return ESP_OK;
    }
    
    // Set power pin HIGH
    if (driver->config.power_pin >= 0) {
        gpio_set_level(driver->config.power_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(100)); // Wait for power stabilization
    }
    
    // Re-initialize display controller
    epaper_init_display_controller(driver);
    
    driver->is_powered = true;
    ESP_LOGI(TAG, "Display powered on");
    
    return ESP_OK;
}

esp_err_t epaper_power_off(epaper_driver_t* driver) {
    if (driver == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!driver->is_powered) {
        return ESP_OK;
    }
    
    // Send deep sleep command (for SSD1680)
    if (driver->config.model == EPAPER_MODEL_213_122x250) {
        epaper_send_command(driver, 0x10);  // Deep sleep mode
        epaper_send_data(driver, 0x01);     // Enter deep sleep
    }
    
    // Set power pin LOW
    if (driver->config.power_pin >= 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(driver->config.power_pin, 0);
    }
    
    driver->is_powered = false;
    ESP_LOGI(TAG, "Display powered off");
    
    return ESP_OK;
}

esp_err_t epaper_clear(epaper_driver_t* driver) {
    if (driver == NULL || driver->framebuffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    memset(driver->framebuffer, 0xFF, driver->fb_size); // 0xFF = white
    ESP_LOGD(TAG, "Framebuffer cleared");
    
    return ESP_OK;
}

esp_err_t epaper_fill(epaper_driver_t* driver, uint8_t color) {
    if (driver == NULL || driver->framebuffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (color == EPAPER_COLOR_BLACK) {
        memset(driver->framebuffer, 0x00, driver->fb_size);  // 0x00 = black
    } else {
        memset(driver->framebuffer, 0xFF, driver->fb_size);  // 0xFF = white
    }
    
    return ESP_OK;
}

esp_err_t epaper_draw_pixel(epaper_driver_t* driver, uint16_t x, uint16_t y, epaper_color_t color) {
    if (driver == NULL || driver->framebuffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Apply rotation transformation
    uint16_t rotated_x = x;
    uint16_t rotated_y = y;
    
    switch (driver->config.rotation) {
        case 0:  // No rotation
            rotated_x = x;
            rotated_y = y;
            break;
        case 1:  // 90° clockwise
            rotated_x = driver->config.height - 1 - y;
            rotated_y = x;
            break;
        case 2:  // 180° rotation (WeActStudio EPD_ROTATE_180)
            rotated_x = driver->config.width - 1 - x;
            rotated_y = driver->config.height - 1 - y;
            break;
        case 3:  // 270° clockwise (90° counter-clockwise)
            rotated_x = y;
            rotated_y = driver->config.width - 1 - x;
            break;
    }
    
    if (rotated_x >= driver->config.width || rotated_y >= driver->config.height) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Calculate byte position and bit position
    // Each row is padded to full bytes, so we need bytes_per_row
    uint32_t bytes_per_row = (driver->config.width + 7) / 8;
    uint32_t byte_index = rotated_y * bytes_per_row + (rotated_x / 8);
    uint8_t bit_position = 7 - (rotated_x % 8);
    
    if (color == EPAPER_COLOR_BLACK) {
        driver->framebuffer[byte_index] &= ~(1 << bit_position);  // 0 = black
    } else {
        driver->framebuffer[byte_index] |= (1 << bit_position);   // 1 = white
    }
    
    return ESP_OK;
}

esp_err_t epaper_draw_text(epaper_driver_t* driver, uint16_t x, uint16_t y, 
                            const char* text, uint8_t size, epaper_text_align_t align) {
    if (driver == NULL || text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (size < 1) {
        size = 1;
    }
    
    // Calculate text width for alignment
    uint16_t text_width = 0;
    const char* temp = text;
    while (*temp && *temp != '\n') {
        text_width += 5 * size + size;  // char width + spacing
        temp++;
    }
    if (text_width > 0) {
        text_width -= size;  // Remove last spacing
    }
    
    // Apply alignment
    uint16_t start_x = x;
    if (align == EPAPER_ALIGN_CENTER) {
        start_x = x > text_width / 2 ? x - text_width / 2 : 0;
    } else if (align == EPAPER_ALIGN_RIGHT) {
        start_x = x > text_width ? x - text_width : 0;
    }
    
    uint16_t cursor_x = start_x;
    uint16_t cursor_y = y;
    const uint8_t char_width = 5 * size;
    const uint8_t char_spacing = 1 * size;
    
    while (*text) {
        if (*text == '\n') {
            cursor_x = start_x;
            cursor_y += 8 * size + char_spacing;
            text++;
            continue;
        }
        
        // Only support ASCII 32-126
        char c = *text;
        if (c < 32 || c > 126) {
            c = '?';
        }
        
        const uint8_t *glyph = font_5x8[c - 32];
        
        // Draw character with scaling
        for (uint8_t col = 0; col < 5; col++) {
            uint8_t line = glyph[col];
            for (uint8_t row = 0; row < 8; row++) {
                if (line & (1 << row)) {
                    // Draw scaled pixel
                    for (uint8_t sy = 0; sy < size; sy++) {
                        for (uint8_t sx = 0; sx < size; sx++) {
                            epaper_draw_pixel(driver, cursor_x + col * size + sx, 
                                            cursor_y + row * size + sy, EPAPER_COLOR_BLACK);
                        }
                    }
                }
            }
        }
        
        cursor_x += char_width + char_spacing;
        text++;
    }
    
    return ESP_OK;
}

esp_err_t epaper_draw_line(epaper_driver_t* driver, uint16_t x0, uint16_t y0, 
                            uint16_t x1, uint16_t y1, epaper_color_t color) {
    if (driver == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Bresenham's line algorithm
    int dx = abs((int)x1 - (int)x0);
    int dy = abs((int)y1 - (int)y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    
    while (true) {
        epaper_draw_pixel(driver, x0, y0, color);
        
        if (x0 == x1 && y0 == y1) {
            break;
        }
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
    
    return ESP_OK;
}

esp_err_t epaper_draw_rect(epaper_driver_t* driver, uint16_t x, uint16_t y, 
                            uint16_t width, uint16_t height, epaper_color_t color, bool filled) {
    if (driver == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (filled) {
        // Draw filled rectangle
        for (uint16_t row = y; row < y + height && row < driver->config.height; row++) {
            for (uint16_t col = x; col < x + width && col < driver->config.width; col++) {
                epaper_draw_pixel(driver, col, row, color);
            }
        }
    } else {
        // Draw outline
        epaper_draw_line(driver, x, y, x + width - 1, y, color);                    // Top
        epaper_draw_line(driver, x, y + height - 1, x + width - 1, y + height - 1, color); // Bottom
        epaper_draw_line(driver, x, y, x, y + height - 1, color);                    // Left
        epaper_draw_line(driver, x + width - 1, y, x + width - 1, y + height - 1, color); // Right
    }
    
    return ESP_OK;
}

esp_err_t epaper_update(epaper_driver_t* driver, bool force_full) {
    if (driver == NULL || !driver->is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    bool do_full_update = force_full || 
                          (driver->partial_update_count >= driver->config.full_update_interval);
    
    if (do_full_update) {
        ESP_LOGI(TAG, "Performing full display update");
        driver->partial_update_count = 0;
    } else {
        ESP_LOGI(TAG, "Performing partial display update (%d/%d)", 
                 driver->partial_update_count + 1, driver->config.full_update_interval);
        driver->partial_update_count++;
    }
    
    // Implementation for 2.13" SSD1680
    if (driver->config.model == EPAPER_MODEL_213_122x250) {
        // Set RAM X address counter to start
        epaper_send_command(driver, 0x4E);
        epaper_send_data(driver, 0x00);
        
        // Set RAM Y address counter to start (0x0000 in Y-increment mode)
        epaper_send_command(driver, 0x4F);
        epaper_send_data(driver, 0x00);  // Y start = 0 low byte
        epaper_send_data(driver, 0x00);  // Y start high byte
        
        // Write to 0x26 buffer first (used as "previous" for differential refresh)
        epaper_send_command(driver, 0x26);
        epaper_send_data_buffer(driver, driver->framebuffer, driver->fb_size);
        
        // Reset RAM address pointers to start again
        epaper_send_command(driver, 0x4E);
        epaper_send_data(driver, 0x00);
        epaper_send_command(driver, 0x4F);
        epaper_send_data(driver, 0x00);  // Y start = 0 low byte
        epaper_send_data(driver, 0x00);  // Y start high byte
        
        // Write to 0x24 buffer (current display data)
        epaper_send_command(driver, 0x24);
        epaper_send_data_buffer(driver, driver->framebuffer, driver->fb_size);
        
        // Update display
        epaper_send_command(driver, 0x22);  // Display Update Control 2
        epaper_send_data(driver, do_full_update ? 0xF7 : 0xFF);  // Full or partial
        epaper_send_command(driver, 0x20);  // Master Activation (Update display)
        
        // Wait for update to complete
        vTaskDelay(pdMS_TO_TICKS(10));
        epaper_wait_idle(driver, 5000);
        
        ESP_LOGI(TAG, "Display update complete");
    } else if (driver->config.model == EPAPER_MODEL_154_200x200) {
        // Implementation for 1.54" SSD1681 (supports partial refresh with dual buffers)
        // Re-initialize RAM address pointers before each update
        epaper_send_command(driver, 0x4E);
        epaper_send_data(driver, 0x00);
        
        // Set RAM Y address counter  
        epaper_send_command(driver, 0x4F);
        epaper_send_data(driver, 0x00);  // Start from 0
        epaper_send_data(driver, 0x00);
        
        if (do_full_update) {
            // Full refresh: write to both RAM buffers (0x24 and 0x26)
            // Buffer 0x24 - new data
            epaper_send_command(driver, 0x24);
            epaper_send_data_buffer(driver, driver->framebuffer, driver->fb_size);
            
            // Buffer 0x26 - old data (same as new for full refresh)
            epaper_send_command(driver, 0x26);
            epaper_send_data_buffer(driver, driver->framebuffer, driver->fb_size);
            
            // Full update mode
            epaper_send_command(driver, 0x22);
            epaper_send_data(driver, 0xF7);  // Full update sequence
        } else {
            // Partial refresh: only update 0x24 buffer
            epaper_send_command(driver, 0x24);
            epaper_send_data_buffer(driver, driver->framebuffer, driver->fb_size);
            
            // Partial update mode
            epaper_send_command(driver, 0x22);
            epaper_send_data(driver, 0xFF);  // Partial update sequence
        }
        
        epaper_send_command(driver, 0x20);  // Master Activation
        
        // Wait for update to complete (BUSY pin goes low when done)
        epaper_wait_idle(driver, 5000);
        
        ESP_LOGI(TAG, "Display update complete");
    } else {
        ESP_LOGW(TAG, "Update not implemented for this display model");
    }
    
    return ESP_OK;
}
