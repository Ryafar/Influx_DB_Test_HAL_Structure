#include "battery_monitor_task.h"
#include "../config/esp32-config.h"
#include "../utils/esp_utils.h"
#include "../utils/ntp_time.h"
#include "../drivers/adc/adc_manager.h"
#include "../drivers/influxdb/influxdb_client.h"
#include "influx_sender.h"
#include "../drivers/wifi/wifi_manager.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <sys/time.h>

static const char* TAG = "BATTERY_MONITOR_TASK";

// NVS keys for calibration
#define NVS_NAMESPACE "battery_cal"
#define NVS_KEY_RESET_COUNT "reset_count"
#define NVS_KEY_LAST_RESET_TIME "last_reset"
#define NVS_KEY_SCALE_FACTOR "scale_factor"
#define CALIBRATION_RESET_THRESHOLD 3
#define CALIBRATION_TIME_WINDOW_SEC 60

// Task handle for the monitoring task
static TaskHandle_t monitoring_task_handle = NULL;

// Configuration for measurement cycles
static uint32_t measurements_per_cycle = 0;  // 0 = infinite loop
static volatile bool is_running = false;

// Static scale factor that can be updated through calibration
static float current_scale_factor = BATTERY_MONITOR_VOLTAGE_SCALE_FACTOR;

/**
 * @brief Check if calibration mode should be triggered (3 resets within 1 minute)
 */
bool battery_monitor_check_calibration_trigger() {
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    
    // Initialize NVS if not already done
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated, erasing...");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Open NVS namespace
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Get current time
    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    int64_t current_time = tv_now.tv_sec;
    
    // Read previous reset count and time
    uint32_t reset_count = 0;
    int64_t last_reset_time = 0;
    
    nvs_get_u32(nvs_handle, NVS_KEY_RESET_COUNT, &reset_count);
    nvs_get_i64(nvs_handle, NVS_KEY_LAST_RESET_TIME, &last_reset_time);
    
    ESP_LOGI(TAG, "Reset tracking - Count: %lu, Last reset: %lld sec ago", 
             reset_count, current_time - last_reset_time);
    
    // Check if resets are within time window
    bool should_calibrate = false;
    if ((current_time - last_reset_time) <= CALIBRATION_TIME_WINDOW_SEC) {
        // Within time window, increment count
        reset_count++;
        ESP_LOGI(TAG, "Reset within time window, count now: %lu", reset_count);
        
        if (reset_count >= CALIBRATION_RESET_THRESHOLD) {
            ESP_LOGI(TAG, "!!! CALIBRATION MODE TRIGGERED !!!");
            should_calibrate = true;
            reset_count = 0;  // Reset counter after triggering calibration
        }
    } else {
        // Outside time window, reset count
        ESP_LOGI(TAG, "Reset outside time window, resetting count");
        reset_count = 1;
    }
    
    // Save updated values
    nvs_set_u32(nvs_handle, NVS_KEY_RESET_COUNT, reset_count);
    nvs_set_i64(nvs_handle, NVS_KEY_LAST_RESET_TIME, current_time);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    return should_calibrate;
}

/**
 * @brief Perform automatic calibration to match target voltage
 */
esp_err_t battery_monitor_auto_calibrate(float target_voltage) {
    if (target_voltage <= 0.0f) {
        ESP_LOGE(TAG, "Invalid target voltage: %.3f", target_voltage);
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Starting battery ADC auto-calibration");
    ESP_LOGI(TAG, "Target voltage: %.3f V", target_voltage);
    ESP_LOGI(TAG, "========================================");
    
    // Initialize ADC for reading
    esp_err_t ret = battery_monitor_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize battery monitor for calibration");
        return ret;
    }
    
    // Read raw ADC voltage (without scale factor)
    float raw_voltage = 0.0f;
    ret = adc_shared_read_voltage(BATTERY_ADC_UNIT, BATTERY_ADC_CHANNEL, &raw_voltage);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read raw voltage: %s", esp_err_to_name(ret));
        battery_monitor_deinit();
        return ret;
    }
    
    ESP_LOGI(TAG, "Raw ADC voltage reading: %.3f V", raw_voltage);
    
    if (raw_voltage < 0.1f) {
        ESP_LOGE(TAG, "Raw voltage too low (%.3f V), cannot calibrate", raw_voltage);
        battery_monitor_deinit();
        return ESP_ERR_INVALID_STATE;
    }
    
    // Calculate new scale factor
    float new_scale_factor = target_voltage / raw_voltage;
    
    ESP_LOGI(TAG, "Calculated scale factor: %.4f", new_scale_factor);
    ESP_LOGI(TAG, "Old scale factor: %.4f", BATTERY_MONITOR_VOLTAGE_SCALE_FACTOR);
    
    // Sanity check on scale factor (should be between 1.0 and 10.0 for typical voltage dividers)
    if (new_scale_factor < 1.0f || new_scale_factor > 10.0f) {
        ESP_LOGW(TAG, "Calculated scale factor (%.4f) seems unusual, but will save it anyway", new_scale_factor);
    }
    
    // Save new scale factor to NVS
    nvs_handle_t nvs_handle;
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for saving calibration: %s", esp_err_to_name(ret));
        battery_monitor_deinit();
        return ret;
    }
    
    // Save as float (stored as blob in NVS)
    ret = nvs_set_blob(nvs_handle, NVS_KEY_SCALE_FACTOR, &new_scale_factor, sizeof(float));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save scale factor: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        battery_monitor_deinit();
        return ret;
    }
    
    ret = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit scale factor: %s", esp_err_to_name(ret));
        battery_monitor_deinit();
        return ret;
    }
    
    // Update current scale factor
    current_scale_factor = new_scale_factor;
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Calibration successful!");
    ESP_LOGI(TAG, "New scale factor saved: %.4f", new_scale_factor);
    ESP_LOGI(TAG, "Expected reading: %.3f V", raw_voltage * new_scale_factor);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Please update BATTERY_MONITOR_VOLTAGE_SCALE_FACTOR");
    ESP_LOGI(TAG, "in esp32-config.h to: %.4f", new_scale_factor);
    ESP_LOGI(TAG, "========================================");
    
    battery_monitor_deinit();
    return ESP_OK;
}

/**
 * @brief Load calibrated scale factor from NVS if available
 */
static void battery_monitor_load_scale_factor() {
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "No saved scale factor found, using default");
        return;
    }
    
    float saved_scale_factor = 0.0f;
    size_t required_size = sizeof(float);
    ret = nvs_get_blob(nvs_handle, NVS_KEY_SCALE_FACTOR, &saved_scale_factor, &required_size);
    
    if (ret == ESP_OK && saved_scale_factor > 0.0f) {
        current_scale_factor = saved_scale_factor;
        ESP_LOGI(TAG, "Loaded calibrated scale factor: %.4f", current_scale_factor);
        if (current_scale_factor != BATTERY_MONITOR_VOLTAGE_SCALE_FACTOR) {
            ESP_LOGW(TAG, "Using calibrated value (%.4f) instead of config default (%.4f)", 
                     current_scale_factor, BATTERY_MONITOR_VOLTAGE_SCALE_FACTOR);
        }
    }
    
    nvs_close(nvs_handle);
}

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
    
    // Load calibrated scale factor from NVS if available
    battery_monitor_load_scale_factor();
    
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

    ESP_LOGI(TAG, "Battery monitor initialized on ADC%d CH%d", BATTERY_ADC_UNIT + 1, BATTERY_ADC_CHANNEL);
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

    esp_err_t ret = adc_shared_read_voltage(BATTERY_ADC_UNIT, BATTERY_ADC_CHANNEL, voltage);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read battery voltage: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Apply voltage scale factor (for voltage divider) - use calibrated value if available
    *voltage = *voltage * current_scale_factor;

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