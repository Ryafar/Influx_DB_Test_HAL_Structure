#ifndef BATTERY_MONITOR_TASK_H
#define BATTERY_MONITOR_TASK_H

#include "esp_err.h"

#include "../drivers/adc/adc_manager.h"
#include "../drivers/led/led.h"
#include "../config/esp32-config.h"

esp_err_t battery_monitor_init();
esp_err_t battery_monitor_deinit();
esp_err_t battery_monitor_read_voltage(float* voltage);
esp_err_t battery_monitor_start(uint32_t measurements_per_cycle);
esp_err_t battery_monitor_stop();
esp_err_t battery_monitor_wait_for_completion(uint32_t timeout_ms);
void battery_monitor_task(void* pvParameters);

/**
 * @brief Check if calibration mode should be triggered (3 resets within 1 minute)
 * @return true if calibration should be performed, false otherwise
 */
bool battery_monitor_check_calibration_trigger();

/**
 * @brief Perform automatic calibration to match target voltage
 * @param target_voltage Target voltage to calibrate to (e.g., 4.2V)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t battery_monitor_auto_calibrate(float target_voltage);

#endif // BATTERY_MONITOR_TASK_H