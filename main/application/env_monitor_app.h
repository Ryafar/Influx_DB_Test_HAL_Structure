#ifndef ENV_MONITOR_APP_H
#define ENV_MONITOR_APP_H

#include "esp_err.h"
#include "driver/i2c.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // I2C configuration
    i2c_port_t i2c_port;
    gpio_num_t sda_io;
    gpio_num_t scl_io;
    uint32_t i2c_clk_hz;

    // App behavior
    uint32_t measurement_interval_ms; // delay between readings
    uint32_t measurements_per_cycle;  // 0=infinite; else stop after N
    bool enable_logging;
    bool enable_wifi;
    bool enable_http_sending;

    // IDs
    char device_id[32];
} env_monitor_config_t;

typedef struct {
    env_monitor_config_t config;
    bool is_running;
} env_monitor_app_t;

void env_monitor_get_default_config(env_monitor_config_t* cfg);

esp_err_t env_monitor_init(env_monitor_app_t* app, const env_monitor_config_t* cfg);

esp_err_t env_monitor_start(env_monitor_app_t* app);

esp_err_t env_monitor_wait_for_completion(env_monitor_app_t* app, uint32_t timeout_ms);

esp_err_t env_monitor_stop(env_monitor_app_t* app);

esp_err_t env_monitor_deinit(env_monitor_app_t* app);

#ifdef __cplusplus
}
#endif

#endif // ENV_MONITOR_APP_H
