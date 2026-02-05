/**
 * @file battery_monitor_task.h
 * @brief Battery Voltage Monitoring Task
 * 
 * Simple battery voltage monitoring via ADC (direct 1:1 connection on GPIO0).
 */

#ifndef BATTERY_MONITOR_TASK_H
#define BATTERY_MONITOR_TASK_H

#include "esp_err.h"

#include <stdint.h>

#include "adc_manager.h"
#include "led.h"

esp_err_t battery_monitor_init();
esp_err_t battery_monitor_deinit();
esp_err_t battery_monitor_read_voltage(float* voltage);
esp_err_t battery_monitor_start(uint32_t measurements_per_cycle);
esp_err_t battery_monitor_stop();
esp_err_t battery_monitor_wait_for_completion(uint32_t timeout_ms);
void battery_monitor_task(void* pvParameters);

#endif // BATTERY_MONITOR_TASK_H