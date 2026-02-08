/**
 * @file influx_sender.c
 * @brief InfluxDB Data Sender Service - Implementation
 * 
 * Implements a dedicated FreeRTOS task with message queue for handling
 * InfluxDB write operations. Supports soil, battery, and environmental
 * sensor data transmission with proper error handling and logging.
 */

#include "influx_sender.h"
#include "esp_log.h"
#include "string.h"
#include "../config/esp32-config.h"
#include "../config/credentials.h"

#define INFLUX_SENDER_STACK   (14 * 1024)
#define INFLUX_SENDER_PRIO    5
#define INFLUX_QUEUE_LEN      10

static const char* TAG = "INFLUX_SENDER";

typedef enum {
    INFLUX_MSG_SOIL,
    INFLUX_MSG_BATTERY,
    INFLUX_MSG_ENV
} influx_msg_type_t;

typedef struct {
    influx_msg_type_t type;
    union {
        influxdb_soil_data_t soil;
        influxdb_battery_data_t battery;
        influxdb_env_data_t env;
    } payload;
} influx_msg_t;

static TaskHandle_t s_task = NULL;
static QueueHandle_t s_queue = NULL;

static void influx_sender_task(void* pv) {
    ESP_LOGI(TAG, "Influx sender task started");
    // Log stack high watermark to detect potential stack pressure
    UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "Sender task stack high watermark: %lu bytes", (unsigned long)(hwm * sizeof(StackType_t)));

    influx_msg_t msg;
    while (1) {
        if (xQueueReceive(s_queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (msg.type == INFLUX_MSG_SOIL) {
                influxdb_response_status_t r = influxdb_write_soil_data(&msg.payload.soil);
                ESP_LOGI(TAG, "Soil write result: %d (http=%d, success=%s)", r, influxdb_get_last_status_code(), influxdb_last_write_succeeded()?"yes":"no");
            } else if (msg.type == INFLUX_MSG_BATTERY) {
                influxdb_response_status_t r = influxdb_write_battery_data(&msg.payload.battery);
                ESP_LOGI(TAG, "Battery write result: %d (http=%d, success=%s)", r, influxdb_get_last_status_code(), influxdb_last_write_succeeded()?"yes":"no");
            } else if (msg.type == INFLUX_MSG_ENV) {
                influxdb_response_status_t r = influxdb_write_env_data(&msg.payload.env);
                ESP_LOGI(TAG, "Env write result: %d (http=%d, success=%s)", r, influxdb_get_last_status_code(), influxdb_last_write_succeeded()?"yes":"no");
            }
            // Periodically log stack watermark
            hwm = uxTaskGetStackHighWaterMark(NULL);
            ESP_LOGI(TAG, "Sender task stack high watermark: %lu bytes", (unsigned long)(hwm * sizeof(StackType_t)));
        }
    }
}

esp_err_t influx_sender_init(void) {
    static bool client_initialized = false;
    
    // Initialize InfluxDB client once
    if (!client_initialized) {
        influxdb_client_config_t influx_config = {
            .server = INFLUXDB_SERVER,
            .port = INFLUXDB_PORT,
            .bucket = INFLUXDB_BUCKET,
            .org = INFLUXDB_ORG,
            .endpoint = INFLUXDB_ENDPOINT,
            .timeout_ms = 10000,
        };
        strncpy(influx_config.token, INFLUXDB_TOKEN, sizeof(influx_config.token) - 1);
        influx_config.token[sizeof(influx_config.token) - 1] = '\0';
        
        esp_err_t ret = influxdb_client_init(&influx_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize InfluxDB client");
            return ret;
        }
        client_initialized = true;
        ESP_LOGI(TAG, "InfluxDB client initialized");
    }
    
    if (s_queue == NULL) {
        s_queue = xQueueCreate(INFLUX_QUEUE_LEN, sizeof(influx_msg_t));
        if (!s_queue) {
            ESP_LOGE(TAG, "Failed to create queue");
            return ESP_FAIL;
        }
    }
    if (s_task == NULL) {
        BaseType_t ok = xTaskCreatePinnedToCore(
            influx_sender_task,
            "influx_sender",
            INFLUX_SENDER_STACK,
            NULL,
            INFLUX_SENDER_PRIO,
            &s_task,
            0
        );
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "Failed to create sender task");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

esp_err_t influx_sender_enqueue_soil(const influxdb_soil_data_t* data) {
    if (!s_queue || !data) return ESP_ERR_INVALID_STATE;
    influx_msg_t msg = { .type = INFLUX_MSG_SOIL };
    memcpy(&msg.payload.soil, data, sizeof(*data));
    return xQueueSend(s_queue, &msg, 0) == pdTRUE ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t influx_sender_enqueue_battery(const influxdb_battery_data_t* data) {
    if (!s_queue || !data) return ESP_ERR_INVALID_STATE;
    influx_msg_t msg = { .type = INFLUX_MSG_BATTERY };
    memcpy(&msg.payload.battery, data, sizeof(*data));
    return xQueueSend(s_queue, &msg, 0) == pdTRUE ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t influx_sender_enqueue_env(const influxdb_env_data_t* data) {
    if (!s_queue || !data) return ESP_ERR_INVALID_STATE;
    influx_msg_t msg = { .type = INFLUX_MSG_ENV };
    memcpy(&msg.payload.env, data, sizeof(*data));
    return xQueueSend(s_queue, &msg, 0) == pdTRUE ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t influx_sender_wait_until_empty(uint32_t timeout_ms) {
    if (!s_queue) {
        ESP_LOGW(TAG, "Sender queue not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    uint32_t elapsed_ms = 0;
    const uint32_t check_interval_ms = 100;
    
    ESP_LOGI(TAG, "Waiting for InfluxDB sender queue to empty...");
    
    while (uxQueueMessagesWaiting(s_queue) > 0) {
        vTaskDelay(pdMS_TO_TICKS(check_interval_ms));
        elapsed_ms += check_interval_ms;
        
        if (timeout_ms > 0 && elapsed_ms >= timeout_ms) {
            ESP_LOGW(TAG, "Timeout waiting for sender queue to empty (%lu messages remaining)", 
                     uxQueueMessagesWaiting(s_queue));
            return ESP_ERR_TIMEOUT;
        }
    }
    
    // Give sender task a bit more time to complete the last transmission
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    if (influxdb_last_write_succeeded()) {
        ESP_LOGI(TAG, "InfluxDB sender queue is empty, last write confirmed (http %d)", influxdb_get_last_status_code());
    } else {
        ESP_LOGW(TAG, "InfluxDB sender queue empty BUT last write failed (http %d) -- data NOT accepted", influxdb_get_last_status_code());
    }
    return ESP_OK;
}

esp_err_t influx_sender_deinit(void) {
    // Stop task:
    if (s_task) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
    // Delete queue:
    if (s_queue) {
        vQueueDelete(s_queue);
        s_queue = NULL;
    }
    ESP_LOGI(TAG, "Influx sender deinitialized");
    return ESP_OK;
}
