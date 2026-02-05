/**
 * @file battery_monitor_task.c
 * @brief Battery Voltage Monitoring Task - Implementation
 * 
 * Simple battery voltage monitoring using ADC (direct 1:1 connection).
 * Reads voltage from GPIO0 and sends measurements to InfluxDB.
 */

#include "battery_monitor_task.h"
#include "../config/esp32-config.h"
#include "esp_utils.h"
#include "ntp_time.h"
#include "adc_manager.h"
#include "../drivers/influxdb/influxdb_client.h"
#include "influx_sender.h"
#include "../drivers/wifi/wifi_manager.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "BATTERY_MONITOR_TASK";

// Task handle for the monitoring task
static TaskHandle_t monitoring_task_handle = NULL;

// Configuration for measurement cycles
static uint32_t measurements_per_cycle = 0;  // 0 = infinite loop
static volatile bool is_running = false;

/**
 * @brief Send battery voltage reading to InfluxDB
 */
static influxdb_response_status_t battery_send_reading_to_influxdb(float voltage, const char* device_id) {
    if (device_id == NULL) {
        return INFLUXDB_RESPONSE_ERROR;
    }

    influxdb_battery_data_t influx_data;
    
    // Use NTP timestamp if available, otherwise fallback to system timestamp
    uint64_t timestamp_ms;
    if (ntp_time_is_synced()) {
        timestamp_ms = ntp_time_get_timestamp_ms();
    } else {
        timestamp_ms = esp_utils_get_timestamp_ms();
    }
    
    // Convert milliseconds to nanoseconds for InfluxDB
    influx_data.timestamp_ns = timestamp_ms * 1000000ULL;
    influx_data.voltage = voltage;
    influx_data.percentage = -1;  // No percentage calculation for now
    strncpy(influx_data.device_id, device_id, sizeof(influx_data.device_id) - 1);
    influx_data.device_id[sizeof(influx_data.device_id) - 1] = '\0';

    influx_sender_init();
    return influx_sender_enqueue_battery(&influx_data);
}

esp_err_t battery_monitor_init() {
    // Initialize shared ADC unit
    esp_err_t ret = adc_shared_init(BATTERY_ADC_UNIT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize shared ADC unit for battery monitoring: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Add battery monitoring channel to shared ADC
    ret = adc_shared_add_channel(BATTERY_ADC_UNIT, BATTERY_ADC_CHANNEL, 
                                     BATTERY_ADC_BITWIDTH, BATTERY_ADC_ATTENUATION, 
                                     BATTERY_ADC_VREF);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add battery channel to shared ADC: %s", esp_err_to_name(ret));
        adc_shared_deinit(BATTERY_ADC_UNIT);
        return ret;
    }

    ESP_LOGI(TAG, "Battery monitor initialized on GPIO0 (ADC%d CH%d, 1:1 direct)", 
             BATTERY_ADC_UNIT + 1, BATTERY_ADC_CHANNEL);
    return ESP_OK;
}

esp_err_t battery_monitor_deinit() {
    // Remove battery channel from shared ADC
    esp_err_t ret = adc_shared_remove_channel(BATTERY_ADC_UNIT, BATTERY_ADC_CHANNEL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove battery channel from shared ADC: %s", esp_err_to_name(ret));
    }
    
    // Deinitialize shared ADC unit (will only actually deinit if ref count reaches 0)
    ret = adc_shared_deinit(BATTERY_ADC_UNIT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize shared ADC for battery monitoring: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Battery monitor deinitialized");
    return ESP_OK;
}

esp_err_t battery_monitor_read_voltage(float* voltage) {
    if (voltage == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Read ADC voltage (direct 1:1, scale factor is 1.0)
    esp_err_t ret = adc_shared_read_voltage(BATTERY_ADC_UNIT, BATTERY_ADC_CHANNEL, voltage);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read battery voltage: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Apply scale factor (1.0 for direct connection, configurable if needed)
    *voltage = *voltage * BATTERY_MONITOR_VOLTAGE_SCALE_FACTOR;

    return ESP_OK;
}

esp_err_t battery_monitor_stop() {
    if (monitoring_task_handle == NULL) {
        ESP_LOGW(TAG, "Battery monitor task not running");
        return ESP_ERR_INVALID_STATE;
    }

    is_running = false;
    
    // Wait for task to finish gracefully
    uint32_t wait_count = 0;
    while (monitoring_task_handle != NULL && wait_count < 50) {  // 5 second timeout
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_count++;
    }
    
    if (monitoring_task_handle != NULL) {
        ESP_LOGW(TAG, "Battery monitor task did not stop gracefully, forcing deletion");
        vTaskDelete(monitoring_task_handle);
        monitoring_task_handle = NULL;
    }

    ESP_LOGI(TAG, "Battery monitor task stopped");
    return ESP_OK;
}

esp_err_t battery_monitor_start(uint32_t cycles) {
    if (monitoring_task_handle != NULL) {
        ESP_LOGW(TAG, "Battery monitor task already running");
        return ESP_ERR_INVALID_STATE;
    }

    measurements_per_cycle = cycles;
    is_running = true;

    BaseType_t result = xTaskCreatePinnedToCore(
        battery_monitor_task,
        "battery_monitor_task",
        BATTERY_MONITOR_TASK_STACK_SIZE, // Stack size
        NULL, // Parameters
        BATTERY_MONITOR_TASK_PRIORITY,    // Priority
        &monitoring_task_handle,
        0 /* pin to core 0 */
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create battery monitor task");
        is_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Battery monitor task started with %lu measurements per cycle", measurements_per_cycle);
    return ESP_OK;
}

esp_err_t battery_monitor_wait_for_completion(uint32_t timeout_ms) {
    if (measurements_per_cycle == 0) {
        ESP_LOGW(TAG, "measurements_per_cycle is 0, task runs indefinitely");
        return ESP_ERR_INVALID_STATE;
    }
    
    uint32_t elapsed_ms = 0;
    const uint32_t check_interval_ms = 100;
    
    ESP_LOGI(TAG, "Waiting for battery monitoring task to complete...");
    
    while (monitoring_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(check_interval_ms));
        elapsed_ms += check_interval_ms;
        
        if (timeout_ms > 0 && elapsed_ms >= timeout_ms) {
            ESP_LOGW(TAG, "Timeout waiting for battery monitoring task");
            return ESP_ERR_TIMEOUT;
        }
    }
    
    ESP_LOGI(TAG, "Battery monitoring task completed");
    return ESP_OK;
}

void battery_monitor_task(void *pvParameters) {

    // turn on the led
    led_init(LED_GPIO_NUM);
    led_set_state(LED_GPIO_NUM, true);

    battery_monitor_init();

    // Generate device ID from MAC address for battery monitoring
    char device_id[32];
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(device_id, sizeof(device_id), "BATT_%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    ESP_LOGI(TAG, "Battery monitor device ID: %s", device_id);
    
    uint32_t measurement_count = 0;

    while (is_running) {
        float battery_voltage = 0;
        battery_monitor_read_voltage(&battery_voltage);

        // Log the reading
        ESP_LOGI(TAG, "Battery Voltage: %.2f V", battery_voltage);

#if USE_INFLUXDB
        // Send data to InfluxDB if WiFi is connected
        if (wifi_manager_is_connected()) {
            influxdb_response_status_t influx_status = battery_send_reading_to_influxdb(battery_voltage, device_id);
            if (influx_status == INFLUXDB_RESPONSE_OK) {
                ESP_LOGI(TAG, "Battery data sent successfully to InfluxDB");
            } else {
                ESP_LOGW(TAG, "Failed to send battery data to InfluxDB (status: %d)", influx_status);
            }
        } else {
            ESP_LOGW(TAG, "WiFi not connected, skipping InfluxDB transmission");
        }
#endif

        if (battery_voltage < BATTERY_MONITOR_LOW_VOLTAGE_THRESHOLD) {
            ESP_LOGW(TAG, "Battery voltage low: %.2f V", battery_voltage);
            ESP_LOGW(TAG, "Please recharge or replace the battery. Shutting down.");
            
            // Perform cleanup and shutdown
            if (BATTERY_MONITOR_USE_DEEP_SLEEP_ON_LOW_BATTERY) {
                ESP_LOGI(TAG, "Entering deep sleep mode to conserve power.");
                battery_monitor_deinit();
                esp_deep_sleep_start();
            }
        }

        measurement_count++;
        
        // Check if we've reached the target count (if configured)
        if (measurements_per_cycle > 0 && measurement_count >= measurements_per_cycle) {
            ESP_LOGI(TAG, "Completed %lu measurements, stopping task", measurement_count);
            break;
        }

        // Wait for the next measurement interval
        vTaskDelay(pdMS_TO_TICKS(BATTERY_MONITOR_MEASUREMENT_INTERVAL_MS));
    }
    
    ESP_LOGI(TAG, "Battery monitor task stopped");
    is_running = false;
    monitoring_task_handle = NULL;
    vTaskDelete(NULL);
}