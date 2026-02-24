
/**
 * @file mqtt_driver.h
 * @brief MQTT Client for IoT Data Publishing
 *
 * This module handles MQTT communication to publish sensor data to an MQTT broker.
 */

#ifndef MQTT_DRIVER_H
#define MQTT_DRIVER_H

#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mqtt_client.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MQTT client configuration
 */
typedef struct {
    char broker_uri[128];       ///< MQTT broker URI (mqtt://host:port or mqtts://host:port)
    char username[64];          ///< MQTT username (optional)
    char password[128];         ///< MQTT password (optional)
    char client_id[64];         ///< MQTT client ID
    char base_topic[64];        ///< Base topic for publishing (e.g., "soil_sensor")
    int keepalive;              ///< Keep-alive interval in seconds
    int timeout_ms;             ///< Connection timeout in milliseconds
    bool use_ssl;               ///< Use SSL/TLS for connection
} mqtt_client_config_t;

/**
 * @brief MQTT client status
 */
typedef enum {
    MQTT_CLIENT_STATUS_OK = 0,
    MQTT_CLIENT_STATUS_ERROR,
    MQTT_CLIENT_STATUS_NOT_CONNECTED,
    MQTT_CLIENT_STATUS_TIMEOUT,
    MQTT_CLIENT_STATUS_INVALID_PARAM
} mqtt_client_status_t;

/**
 * @brief Initialize MQTT client
 * 
 * @param config MQTT client configuration
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_client_init(const mqtt_client_config_t* config);

/**
 * @brief Deinitialize MQTT client
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_client_deinit(void);

/**
 * @brief Connect to MQTT broker
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_client_connect(void);

/**
 * @brief Disconnect from MQTT broker
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_client_disconnect(void);

/**
 * @brief Check if MQTT client is connected
 * 
 * @return bool true if connected, false otherwise
 */
bool mqtt_client_is_connected(void);

/**
 * @brief Publish a message to MQTT broker
 * 
 * @param topic MQTT topic to publish to
 * @param payload Message payload
 * @param payload_len Length of the payload
 * @param qos Quality of Service level (0, 1, or 2)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_client_publish(const char* topic, const char* payload, size_t payload_len, int qos);

/**
 * @brief Wait for all pending publishes to complete
 * 
 * @param timeout_ms Maximum time to wait in milliseconds
 * @return esp_err_t ESP_OK on success, ESP_ERR_TIMEOUT on timeout
 */
esp_err_t mqtt_client_wait_published(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif // MQTT_DRIVER_H
