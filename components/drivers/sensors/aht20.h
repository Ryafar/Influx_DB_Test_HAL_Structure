#ifndef AHT20_H
#define AHT20_H

#include "esp_err.h"
#include "driver/i2c.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AHT20_I2C_ADDR         0x38

typedef struct {
    i2c_port_t i2c_port;
    gpio_num_t sda_io;
    gpio_num_t scl_io;
    uint32_t clk_speed_hz;
    bool initialized;
} aht20_t;

/**
 * Initialize I2C and AHT20 sensor (soft reset + init/calibration)
 */
esp_err_t aht20_init(aht20_t* dev, i2c_port_t port, gpio_num_t sda, gpio_num_t scl, uint32_t clk_speed_hz);

/**
 * Deinitialize I2C for this device (does not uninstall shared bus if shared elsewhere)
 */
esp_err_t aht20_deinit(aht20_t* dev);

/**
 * Trigger measurement and read temperature (C) and humidity (%RH)
 */
esp_err_t aht20_read(aht20_t* dev, float* temperature_c, float* humidity_rh);

#ifdef __cplusplus
}
#endif

#endif // AHT20_H
