/**
 * @file soil_monitor_app.c
 * @brief Soil Moisture Monitoring Application - Implementation
 */

#include "soil_monitor_app.h"
#include "../config/esp32-config.h"
#include "../utils/esp_utils.h"
#include "../utils/ntp_time.h"
#include "../drivers/influxdb/influxdb_client.h"
#include "influx_sender.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "SOIL_MONITOR_APP";

// Task handle for the monitoring task
static TaskHandle_t monitoring_task_handle = NULL;

/**
 * @brief Send soil sensor reading to InfluxDB
 */
static influxdb_response_status_t soil_send_reading_to_influxdb(const csm_v2_reading_t* reading, const char* device_id) {
    if (reading == NULL || device_id == NULL) {
        return INFLUXDB_RESPONSE_ERROR;
    }

    influxdb_soil_data_t influx_data;
    
    // Use NTP timestamp if available, otherwise fallback to system timestamp
    uint64_t timestamp_ms;
    if (ntp_time_is_synced()) {
        timestamp_ms = ntp_time_get_timestamp_ms();
    } else {
        timestamp_ms = esp_utils_get_timestamp_ms();
    }
    
    // Convert milliseconds to nanoseconds for InfluxDB
    influx_data.timestamp_ns = timestamp_ms * 1000000ULL;
    influx_data.voltage = reading->voltage;
    influx_data.moisture_percent = reading->moisture_percent;
    influx_data.raw_adc = reading->raw_adc;
    strncpy(influx_data.device_id, device_id, sizeof(influx_data.device_id) - 1);
    influx_data.device_id[sizeof(influx_data.device_id) - 1] = '\0';

    // Route via sender task to avoid stack pressure in this task
    influx_sender_init();
    return influx_sender_enqueue_soil(&influx_data);
}

/**
 * @brief Soil monitoring task
 */
static void soil_monitoring_task(void* pvParameters) {
    soil_monitor_app_t* app = (soil_monitor_app_t*)pvParameters;
    csm_v2_reading_t reading;
    uint32_t measurement_count = 0;
    
    ESP_LOGI(TAG, "Soil monitoring task started");
    
    // Measurement loop - runs until configured count reached or infinite if measurements_per_cycle == 0
    while (app->is_running) {

        // Power the sensor
        esp_err_t ret = csm_v2_enable_power(&app->sensor_driver);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to power on sensor: %s", esp_err_to_name(ret));
            continue;
        }
        
        // Wait for sensor to stabilize
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Read sensor data
        ret = csm_v2_read(&app->sensor_driver, &reading);

        // Power off the sensor to save energy
        esp_err_t ret2 = csm_v2_disable_power(&app->sensor_driver);
        if (ret2 != ESP_OK) {
            ESP_LOGE(TAG, "Failed to power off sensor: %s", esp_err_to_name(ret2));
        }

        if (ret == ESP_OK) {
            if (app->config.enable_logging) {
                ESP_LOGI(TAG, "Soil Moisture: %.1f%% | Voltage: %.3fV | Raw ADC: %d",
                         reading.moisture_percent, reading.voltage, reading.raw_adc);
            }
            
#if USE_INFLUXDB
            // Send data to InfluxDB if enabled and WiFi is connected
            if (app->config.enable_http_sending && wifi_manager_is_connected()) {
                influxdb_response_status_t influx_status = soil_send_reading_to_influxdb(&reading, app->config.device_id);
                if (influx_status == INFLUXDB_RESPONSE_OK) {
                    if (app->config.enable_logging) {
                        ESP_LOGI(TAG, "Soil data sent successfully to InfluxDB");
                    }
                } else {
                    ESP_LOGW(TAG, "Failed to send soil data to InfluxDB (status: %d)", influx_status);
                }
            }
        } else {
            ESP_LOGE(TAG, "Failed to read sensor: %s", esp_err_to_name(ret));
        }
#endif
        
        measurement_count++;
        
        // Check if we've reached the target count (if configured)
        if (app->config.measurements_per_cycle > 0 && measurement_count >= app->config.measurements_per_cycle) {
            ESP_LOGI(TAG, "Completed %lu measurements, stopping task", measurement_count);
            break;
        }
        
        vTaskDelay(pdMS_TO_TICKS(app->config.measurement_interval_ms));
    }

    // Ensure sensor is powered off before exiting
    esp_err_t ret = csm_v2_disable_power(&app->sensor_driver);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to power off sensor: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "CSM V2 Sensor powered off successfully");
    }

    ESP_LOGI(TAG, "Soil monitoring task stopped");
    app->is_running = false;
    monitoring_task_handle = NULL;
    vTaskDelete(NULL);
}

void soil_monitor_get_default_config(soil_monitor_config_t* config) {
    if (config == NULL) {
        return;
    }
    
    config->adc_unit = ADC_UNIT_1;
    config->adc_channel = ADC_CHANNEL_0;
    config->measurement_interval_ms = 1000;
    config->enable_logging = true;
    config->dry_calibration_voltage = 3.0f;
    config->wet_calibration_voltage = 1.0f;
    config->enable_wifi = true;
    config->enable_http_sending = true;
    config->measurements_per_cycle = 0;  // 0 = infinite loop
    
    // Generate device ID from MAC address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(config->device_id, sizeof(config->device_id), "SOIL_%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

esp_err_t soil_monitor_init(soil_monitor_app_t* app, const soil_monitor_config_t* config) {
    if (app == NULL || config == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Copy configuration
    memcpy(&app->config, config, sizeof(soil_monitor_config_t));
    
    // Initialize sensor driver
    csm_v2_config_t sensor_config;
    csm_v2_get_default_config(&sensor_config, config->adc_unit, config->adc_channel, SOIL_SENSOR_POWER_PIN);
    sensor_config.dry_voltage = config->dry_calibration_voltage;
    sensor_config.wet_voltage = config->wet_calibration_voltage;
    sensor_config.enable_calibration = true;
    
    esp_err_t ret = csm_v2_init(&app->sensor_driver, &sensor_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize sensor driver: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize WiFi
    wifi_manager_config_t wifi_config = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASSWORD,
        .max_retry = WIFI_MAX_RETRY,
    };
    influxdb_client_config_t influx_config = {
        .port = INFLUXDB_PORT,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .max_retries = HTTP_MAX_RETRIES,
    };
    
    // Copy configuration strings
    strncpy(influx_config.server, INFLUXDB_SERVER, sizeof(influx_config.server) - 1);
    influx_config.server[sizeof(influx_config.server) - 1] = '\0';
    
    strncpy(influx_config.bucket, INFLUXDB_BUCKET, sizeof(influx_config.bucket) - 1);
    influx_config.bucket[sizeof(influx_config.bucket) - 1] = '\0';
    
    strncpy(influx_config.org, INFLUXDB_ORG, sizeof(influx_config.org) - 1);
    influx_config.org[sizeof(influx_config.org) - 1] = '\0';
    
    strncpy(influx_config.token, INFLUXDB_TOKEN, sizeof(influx_config.token) - 1);
    influx_config.token[sizeof(influx_config.token) - 1] = '\0';
    
    strncpy(influx_config.endpoint, INFLUXDB_ENDPOINT, sizeof(influx_config.endpoint) - 1);
    influx_config.endpoint[sizeof(influx_config.endpoint) - 1] = '\0';

    wifi_manager_init(&wifi_config, NULL);
    wifi_manager_connect();
    
    // Wait for WiFi connection and log network info
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    int wifi_wait_count = 0;
    while (!wifi_manager_is_connected() && wifi_wait_count < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        wifi_wait_count++;
        ESP_LOGI(TAG, "WiFi connection attempt %d/30", wifi_wait_count);
    }
    
    if (wifi_manager_is_connected()) {
        // Get and log IP information
        esp_netif_ip_info_t ip_info;
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif != NULL && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            ESP_LOGI(TAG, "=== NETWORK DIAGNOSTICS ===");
            ESP_LOGI(TAG, "ESP32 IP: " IPSTR, IP2STR(&ip_info.ip));
            ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&ip_info.gw));
            ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&ip_info.netmask));
            ESP_LOGI(TAG, "Target Server: %s:%d", INFLUXDB_SERVER, INFLUXDB_PORT);
            ESP_LOGI(TAG, "========================");
        }
    } else {
        ESP_LOGE(TAG, "WiFi connection failed after 30 seconds!");
        return ESP_FAIL;
    }
    
    // Initialize InfluxDB client (HTTP/TLS) and sender task
    esp_err_t influx_ret = influxdb_client_init(&influx_config);
    if (influx_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize InfluxDB client: %s", esp_err_to_name(influx_ret));
        return influx_ret;
    }

    // Ensure sender is ready (idempotent)
    influx_sender_init();
    
    // Heavy operations moved to task above to avoid main stack overflow


    
    app->is_running = false;
    ESP_LOGI(TAG, "Soil monitoring application initialized");
    ESP_LOGI(TAG, "Device ID: %s", app->config.device_id);
    return ESP_OK;
}

esp_err_t soil_monitor_start(soil_monitor_app_t* app) {
    if (app == NULL) {
        ESP_LOGE(TAG, "Invalid parameter");
        return ESP_ERR_INVALID_ARG;
    }
    if (app->is_running) {
        ESP_LOGW(TAG, "Application already running");
        return ESP_OK;
    }
    app->is_running = true;
    
    // Create monitoring task with larger stack to handle TLS/HTTP
    BaseType_t task_created = xTaskCreatePinnedToCore(
        soil_monitoring_task,
        "soil_monitor",
        SOIL_TASK_STACK_SIZE,
        app,
        SOIL_TASK_PRIORITY,
        &monitoring_task_handle,
        0 /* pin to core 0 */
    );
    
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create monitoring task");
        app->is_running = false;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Soil monitoring application started");
    return ESP_OK;
}

esp_err_t soil_monitor_stop(soil_monitor_app_t* app) {
    if (app == NULL) {
        ESP_LOGE(TAG, "Invalid parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!app->is_running) {
        ESP_LOGW(TAG, "Application not running");
        return ESP_OK;
    }
    
    app->is_running = false;
    
    // Wait for task to finish
    while (monitoring_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    ESP_LOGI(TAG, "Soil monitoring application stopped");
    return ESP_OK;
}

esp_err_t soil_monitor_wait_for_completion(soil_monitor_app_t* app, uint32_t timeout_ms) {
    if (app == NULL) {
        ESP_LOGE(TAG, "Invalid parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (app->config.measurements_per_cycle == 0) {
        ESP_LOGW(TAG, "measurements_per_cycle is 0, task runs indefinitely");
        return ESP_ERR_INVALID_STATE;
    }
    
    uint32_t elapsed_ms = 0;
    const uint32_t check_interval_ms = 100;
    
    ESP_LOGI(TAG, "Waiting for soil monitoring task to complete...");
    
    while (monitoring_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(check_interval_ms));
        elapsed_ms += check_interval_ms;
        
        if (timeout_ms > 0 && elapsed_ms >= timeout_ms) {
            ESP_LOGW(TAG, "Timeout waiting for soil monitoring task");
            return ESP_ERR_TIMEOUT;
        }
    }
    
    ESP_LOGI(TAG, "Soil monitoring task completed");
    return ESP_OK;
}

esp_err_t soil_monitor_deinit(soil_monitor_app_t* app) {
    if (app == NULL) {
        ESP_LOGE(TAG, "Invalid parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Stop application if running
    esp_err_t ret = soil_monitor_stop(app);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop application: %s", esp_err_to_name(ret));
    }
    
    // Deinitialize InfluxDB client if enabled
    if (app->config.enable_http_sending) {
        influxdb_client_deinit();
    }
    
    // Deinitialize WiFi if enabled
    if (app->config.enable_wifi) {
        wifi_manager_deinit();
    }
    
    // Deinitialize NTP time synchronization
    ntp_time_deinit();
    
    // Deinitialize sensor driver
    ret = csm_v2_deinit(&app->sensor_driver);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize sensor driver: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Soil monitoring application deinitialized");
    return ESP_OK;
}









// MARK: TODO
esp_err_t soil_monitor_calibrate(soil_monitor_app_t* app) {
    if (app == NULL) {
        ESP_LOGE(TAG, "Invalid parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Starting calibration sequence...");
    ESP_LOGI(TAG, "Place sensor in dry soil and wait for readings to stabilize");
    
    // Take dry reading
    vTaskDelay(pdMS_TO_TICKS(3000));  // Wait 3 seconds
    float dry_voltage;
    esp_err_t ret = csm_v2_read_voltage(&app->sensor_driver, &dry_voltage);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read dry voltage: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Dry voltage recorded: %.3fV", dry_voltage);
    ESP_LOGI(TAG, "Now place sensor in wet soil and wait for readings to stabilize");
    
    // Take wet reading
    vTaskDelay(pdMS_TO_TICKS(5000));  // Wait 5 seconds
    float wet_voltage;
    ret = csm_v2_read_voltage(&app->sensor_driver, &wet_voltage);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read wet voltage: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Wet voltage recorded: %.3fV", wet_voltage);
    
    // Apply calibration
    ret = csm_v2_calibrate(&app->sensor_driver, dry_voltage, wet_voltage);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply calibration: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Update app config
    app->config.dry_calibration_voltage = dry_voltage;
    app->config.wet_calibration_voltage = wet_voltage;
    
    ESP_LOGI(TAG, "Calibration completed successfully!");
    return ESP_OK;
}