/**
 * @file mqtt_sender.c
 * @brief MQTT Data Sender Service Implementation
 * 
 * Implements a queue-based service task for asynchronously sending sensor data
 * to MQTT broker using JSON format.
 */

#include "mqtt_sender.h"
#include "mqtt_driver.h"
#include "config/esp32-config.h"
#include "config/credentials.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "MQTT_SENDER";

// Message types for the queue
typedef enum {
    MQTT_MSG_TYPE_SOIL,
    MQTT_MSG_TYPE_BATTERY,
    MQTT_MSG_TYPE_ENV
} mqtt_message_type_t;

// Union message structure
typedef struct {
    mqtt_message_type_t type;
    union {
        mqtt_soil_data_t soil;
        mqtt_battery_data_t battery;
        mqtt_env_data_t env;
    } data;
} mqtt_queue_message_t;

#define MQTT_SENDER_QUEUE_SIZE      20
#define MQTT_SENDER_TASK_STACK_SIZE (8 * 1024)
#define MQTT_SENDER_TASK_PRIORITY   4
#define MQTT_SENDER_TASK_NAME       "mqtt_sender"

static QueueHandle_t mqtt_queue = NULL;
static TaskHandle_t mqtt_task_handle = NULL;
static bool mqtt_initialized = false;

/**
 * @brief Build JSON payload for soil data
 */
static char* build_soil_json(const mqtt_soil_data_t* data) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    
    cJSON_AddStringToObject(root, "device_id", data->device_id);
    cJSON_AddNumberToObject(root, "timestamp", data->timestamp_ms);
    cJSON_AddNumberToObject(root, "voltage", data->voltage);
    cJSON_AddNumberToObject(root, "moisture_percent", data->moisture_percent);
    cJSON_AddNumberToObject(root, "raw_adc", data->raw_adc);
    
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    return json_string;
}

/**
 * @brief Build JSON payload for battery data
 */
static char* build_battery_json(const mqtt_battery_data_t* data) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    
    cJSON_AddStringToObject(root, "device_id", data->device_id);
    cJSON_AddNumberToObject(root, "timestamp", data->timestamp_ms);
    cJSON_AddNumberToObject(root, "voltage", data->voltage);
    cJSON_AddNumberToObject(root, "percentage", data->percentage);
    
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    return json_string;
}

/**
 * @brief Build JSON payload for environment data
 */
static char* build_env_json(const mqtt_env_data_t* data) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    
    cJSON_AddStringToObject(root, "device_id", data->device_id);
    cJSON_AddNumberToObject(root, "timestamp", data->timestamp_ms);
    cJSON_AddNumberToObject(root, "temperature", data->temperature);
    cJSON_AddNumberToObject(root, "humidity", data->humidity);
    
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    return json_string;
}

/**
 * @brief Process and send a queued MQTT message
 */
static esp_err_t process_mqtt_message(const mqtt_queue_message_t* msg) {
    if (!wifi_manager_is_connected()) {
        ESP_LOGW(TAG, "WiFi not connected, skipping MQTT transmission");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!mqtt_client_is_connected()) {
        ESP_LOGW(TAG, "MQTT not connected, skipping transmission");
        return ESP_ERR_INVALID_STATE;
    }
    
    char *json_payload = NULL;
    const char *topic = NULL;
    char topic_buffer[128];
    
    // Build JSON and topic based on message type
    switch (msg->type) {
        case MQTT_MSG_TYPE_SOIL:
            json_payload = build_soil_json(&msg->data.soil);
            snprintf(topic_buffer, sizeof(topic_buffer), "%s/soil", MQTT_BASE_TOPIC);
            topic = topic_buffer;
            break;
            
        case MQTT_MSG_TYPE_BATTERY:
            json_payload = build_battery_json(&msg->data.battery);
            snprintf(topic_buffer, sizeof(topic_buffer), "%s/battery", MQTT_BASE_TOPIC);
            topic = topic_buffer;
            break;
            
        case MQTT_MSG_TYPE_ENV:
            json_payload = build_env_json(&msg->data.env);
            snprintf(topic_buffer, sizeof(topic_buffer), "%s/environment", MQTT_BASE_TOPIC);
            topic = topic_buffer;
            break;
            
        default:
            ESP_LOGE(TAG, "Unknown message type: %d", msg->type);
            return ESP_ERR_INVALID_ARG;
    }
    
    if (json_payload == NULL) {
        ESP_LOGE(TAG, "Failed to build JSON payload");
        return ESP_ERR_NO_MEM;
    }
    
    // Publish to MQTT
    esp_err_t ret = mqtt_client_publish(topic, json_payload, strlen(json_payload), MQTT_QOS);
    
    // Free the JSON string
    cJSON_free(json_payload);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to publish to MQTT topic %s", topic);
        return ret;
    }
    
    ESP_LOGI(TAG, "Published to %s", topic);
    return ESP_OK;
}

/**
 * @brief MQTT sender task - processes queued messages
 */
static void mqtt_sender_task(void *pvParameters) {
    mqtt_queue_message_t msg;
    
    ESP_LOGI(TAG, "MQTT sender task started");
    
    while (1) {
        // Wait for a message in the queue
        if (xQueueReceive(mqtt_queue, &msg, portMAX_DELAY) == pdTRUE) {
            esp_err_t ret = process_mqtt_message(&msg);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to process MQTT message (error: %s)", esp_err_to_name(ret));
            }
        }
    }
}

esp_err_t mqtt_sender_init(void) {
    if (mqtt_initialized) {
        ESP_LOGD(TAG, "MQTT sender already initialized");
        return ESP_OK;
    }
    
    // Create the message queue
    mqtt_queue = xQueueCreate(MQTT_SENDER_QUEUE_SIZE, sizeof(mqtt_queue_message_t));
    if (mqtt_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create MQTT queue");
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize MQTT client
    mqtt_client_config_t mqtt_config = {
        .broker_uri = MQTT_BROKER_URI,
        .username = MQTT_USERNAME,
        .password = MQTT_PASSWORD,
        .client_id = MQTT_CLIENT_ID,
        .base_topic = MQTT_BASE_TOPIC,
        .keepalive = MQTT_KEEPALIVE,
        .timeout_ms = MQTT_TIMEOUT_MS,
        .use_ssl = (strstr(MQTT_BROKER_URI, "mqtts://") != NULL)
    };
    
    esp_err_t ret = mqtt_client_init(&mqtt_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client: %s", esp_err_to_name(ret));
        vQueueDelete(mqtt_queue);
        mqtt_queue = NULL;
        return ret;
    }
    
    // Connect to MQTT broker (only if WiFi is connected)
    if (wifi_manager_is_connected()) {
        ret = mqtt_client_connect();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to connect to MQTT broker: %s", esp_err_to_name(ret));
            // Don't fail initialization, we'll retry in the task
        }
    } else {
        ESP_LOGW(TAG, "WiFi not connected, MQTT connection deferred");
    }
    
    // Create the sender task
    BaseType_t task_ret = xTaskCreate(
        mqtt_sender_task,
        MQTT_SENDER_TASK_NAME,
        MQTT_SENDER_TASK_STACK_SIZE,
        NULL,
        MQTT_SENDER_TASK_PRIORITY,
        &mqtt_task_handle
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MQTT sender task");
        mqtt_client_deinit();
        vQueueDelete(mqtt_queue);
        mqtt_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    mqtt_initialized = true;
    ESP_LOGI(TAG, "MQTT sender initialized successfully");
    return ESP_OK;
}

esp_err_t mqtt_sender_enqueue_soil(const mqtt_soil_data_t* data) {
    if (mqtt_queue == NULL || data == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    mqtt_queue_message_t msg = {
        .type = MQTT_MSG_TYPE_SOIL,
        .data.soil = *data
    };
    
    if (xQueueSend(mqtt_queue, &msg, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to enqueue soil data (queue full)");
        return ESP_ERR_TIMEOUT;
    }
    
    return ESP_OK;
}

esp_err_t mqtt_sender_enqueue_battery(const mqtt_battery_data_t* data) {
    if (mqtt_queue == NULL || data == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    mqtt_queue_message_t msg = {
        .type = MQTT_MSG_TYPE_BATTERY,
        .data.battery = *data
    };
    
    if (xQueueSend(mqtt_queue, &msg, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to enqueue battery data (queue full)");
        return ESP_ERR_TIMEOUT;
    }
    
    return ESP_OK;
}

esp_err_t mqtt_sender_enqueue_env(const mqtt_env_data_t* data) {
    if (mqtt_queue == NULL || data == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    mqtt_queue_message_t msg = {
        .type = MQTT_MSG_TYPE_ENV,
        .data.env = *data
    };
    
    if (xQueueSend(mqtt_queue, &msg, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to enqueue environment data (queue full)");
        return ESP_ERR_TIMEOUT;
    }
    
    return ESP_OK;
}

esp_err_t mqtt_sender_wait_until_empty(uint32_t timeout_ms) {
    if (mqtt_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    uint32_t start_time = esp_timer_get_time() / 1000;
    
    while (uxQueueMessagesWaiting(mqtt_queue) > 0) {
        uint32_t elapsed = (esp_timer_get_time() / 1000) - start_time;
        if (elapsed >= timeout_ms) {
            ESP_LOGW(TAG, "Timeout waiting for queue to empty");
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Wait a bit more for the last message to be transmitted
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Wait for MQTT publishes to complete
    esp_err_t ret = mqtt_client_wait_published(timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to wait for MQTT publishes: %s", esp_err_to_name(ret));
    }
    
    return ESP_OK;
}

esp_err_t mqtt_sender_deinit(void) {
    if (!mqtt_initialized) {
        return ESP_OK;
    }
    
    // Delete the task
    if (mqtt_task_handle != NULL) {
        vTaskDelete(mqtt_task_handle);
        mqtt_task_handle = NULL;
    }
    
    // Delete the queue
    if (mqtt_queue != NULL) {
        vQueueDelete(mqtt_queue);
        mqtt_queue = NULL;
    }
    
    // Deinitialize MQTT client
    mqtt_client_deinit();
    
    mqtt_initialized = false;
    ESP_LOGI(TAG, "MQTT sender deinitialized");
    return ESP_OK;
}
