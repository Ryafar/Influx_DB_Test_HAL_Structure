/**
 * @file mqtt_driver.c
 * @brief MQTT Client Implementation
 */

#include "mqtt_driver.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_event.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "MQTT_CLIENT";

// MQTT client handle
static esp_mqtt_client_handle_t mqtt_client = NULL;
static mqtt_client_config_t client_config = {0};
static bool is_connected = false;
static SemaphoreHandle_t connection_semaphore = NULL;
static SemaphoreHandle_t publish_semaphore = NULL;
static int pending_publishes = 0;

/**
 * @brief MQTT event handler
 */
static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT client connected to broker");
            is_connected = true;
            if (connection_semaphore) {
                xSemaphoreGive(connection_semaphore);
            }
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT client disconnected from broker");
            is_connected = false;
            break;
            
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "Message published successfully, msg_id=%d", event->msg_id);
            if (pending_publishes > 0) {
                pending_publishes--;
            }
            if (publish_semaphore && pending_publishes == 0) {
                xSemaphoreGive(publish_semaphore);
            }
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error occurred");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "TCP transport error: 0x%x", event->error_handle->esp_transport_sock_errno);
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGE(TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
            }
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT subscribed, msg_id=%d", event->msg_id);
            break;
            
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT unsubscribed, msg_id=%d", event->msg_id);
            break;
            
        default:
            ESP_LOGD(TAG, "MQTT event: %ld", event_id);
            break;
    }
}

esp_err_t mqtt_client_init(const mqtt_client_config_t* config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (mqtt_client != NULL) {
        ESP_LOGW(TAG, "MQTT client already initialized");
        return ESP_OK;
    }
    
    // Copy configuration
    memcpy(&client_config, config, sizeof(mqtt_client_config_t));
    
    // Create semaphores
    connection_semaphore = xSemaphoreCreateBinary();
    publish_semaphore = xSemaphoreCreateBinary();
    if (connection_semaphore == NULL || publish_semaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create semaphores");
        return ESP_ERR_NO_MEM;
    }
    
    // Configure MQTT client
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = client_config.broker_uri,
        .credentials.client_id = client_config.client_id,
        .session.keepalive = client_config.keepalive,
        .network.timeout_ms = client_config.timeout_ms,
    };
    
    // Set credentials if provided
    if (strlen(client_config.username) > 0) {
        mqtt_cfg.credentials.username = client_config.username;
    }
    if (strlen(client_config.password) > 0) {
        mqtt_cfg.credentials.authentication.password = client_config.password;
    }
    
    // Create MQTT client
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        vSemaphoreDelete(connection_semaphore);
        vSemaphoreDelete(publish_semaphore);
        connection_semaphore = NULL;
        publish_semaphore = NULL;
        return ESP_FAIL;
    }
    
    // Register event handler
    esp_err_t ret = esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(ret));
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
        vSemaphoreDelete(connection_semaphore);
        vSemaphoreDelete(publish_semaphore);
        connection_semaphore = NULL;
        publish_semaphore = NULL;
        return ret;
    }
    
    ESP_LOGI(TAG, "MQTT client initialized successfully");
    return ESP_OK;
}

esp_err_t mqtt_client_deinit(void) {
    if (mqtt_client == NULL) {
        return ESP_OK;
    }
    
    if (is_connected) {
        mqtt_client_disconnect();
    }
    
    esp_err_t ret = esp_mqtt_client_destroy(mqtt_client);
    mqtt_client = NULL;
    is_connected = false;
    
    if (connection_semaphore) {
        vSemaphoreDelete(connection_semaphore);
        connection_semaphore = NULL;
    }
    if (publish_semaphore) {
        vSemaphoreDelete(publish_semaphore);
        publish_semaphore = NULL;
    }
    
    ESP_LOGI(TAG, "MQTT client deinitialized");
    return ret;
}

esp_err_t mqtt_client_connect(void) {
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (is_connected) {
        ESP_LOGI(TAG, "MQTT client already connected");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Connecting to MQTT broker: %s", client_config.broker_uri);
    esp_err_t ret = esp_mqtt_client_start(mqtt_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Wait for connection with timeout
    if (xSemaphoreTake(connection_semaphore, pdMS_TO_TICKS(client_config.timeout_ms)) == pdTRUE) {
        ESP_LOGI(TAG, "Successfully connected to MQTT broker");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "MQTT connection timeout");
        return ESP_ERR_TIMEOUT;
    }
}

    esp_err_t mqtt_client_publish(const char* topic, const char* payload, size_t payload_len, int qos) {
        if (mqtt_client == NULL || !is_connected) {
            ESP_LOGE(TAG, "MQTT client not initialized or not connected");
            return ESP_ERR_INVALID_STATE;
        }
        if (topic == NULL || payload == NULL || payload_len == 0) {
            ESP_LOGE(TAG, "Invalid publish parameters");
            return ESP_ERR_INVALID_ARG;
        }
        int msg_id = esp_mqtt_client_publish(mqtt_client, topic, payload, payload_len, qos, 0);
        if (msg_id < 0) {
            ESP_LOGE(TAG, "Failed to publish message to topic %s", topic);
            return ESP_FAIL;
        }
        pending_publishes++;
        ESP_LOGI(TAG, "Published to topic: %s (msg_id=%d)", topic, msg_id);
        return ESP_OK;
    }

esp_err_t mqtt_client_disconnect(void) {
    if (mqtt_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Disconnecting from MQTT broker");
    esp_err_t ret = esp_mqtt_client_stop(mqtt_client);
    is_connected = false;
    return ret;
}

bool mqtt_client_is_connected(void) {
    return is_connected;
}

esp_err_t mqtt_client_wait_published(uint32_t timeout_ms) {
    if (pending_publishes == 0) {
        return ESP_OK;
    }
    
    if (xSemaphoreTake(publish_semaphore, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Timeout waiting for publishes to complete (%d pending)", pending_publishes);
        return ESP_ERR_TIMEOUT;
    }
}
