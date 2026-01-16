/**
 * @file wifi_manager.h
 * @brief WiFi Connection Manager for Soil Moisture Sensor
 * 
 * This module handles WiFi connectivity including connection, reconnection,
 * and status monitoring.
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <stdbool.h>

// WiFi Manager constants
#define WIFI_MANAGER_TAG "WiFiManager"
#define WIFI_IP_STRING_MAX_LEN 16

/**
 * @brief WiFi connection status
 */
typedef enum {
    WIFI_STATUS_DISCONNECTED,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED,
    WIFI_STATUS_ERROR
} wifi_status_t;

/**
 * @brief WiFi manager configuration
 */
typedef struct {
    char ssid[32];
    char password[64];
    int max_retry;
} wifi_manager_config_t;

/**
 * @brief WiFi connection callback function type
 * 
 * @param status Current WiFi status
 * @param ip_addr IP address when connected (NULL if not connected)
 */
typedef void (*wifi_status_callback_t)(wifi_status_t status, const char* ip_addr);

/**
 * @brief Initialize WiFi manager
 * 
 * @param config WiFi configuration
 * @param callback Status callback function (can be NULL)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_init(const wifi_manager_config_t* config, wifi_status_callback_t callback);

/**
 * @brief Start WiFi connection
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_connect(void);

/**
 * @brief Stop WiFi connection
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_disconnect(void);

/**
 * @brief Deinitialize WiFi manager
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_deinit(void);

/**
 * @brief Get current WiFi status
 * 
 * @return wifi_status_t Current status
 */
wifi_status_t wifi_manager_get_status(void);

/**
 * @brief Check if WiFi is connected
 * 
 * @return true if connected, false otherwise
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Get current IP address
 * 
 * @param ip_str Buffer to store IP address string (min WIFI_IP_STRING_MAX_LEN bytes)
 * @return esp_err_t ESP_OK if connected and IP retrieved, error otherwise
 */
esp_err_t wifi_manager_get_ip(char* ip_str);

#endif // WIFI_MANAGER_H