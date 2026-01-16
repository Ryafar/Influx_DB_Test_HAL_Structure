/**
 * @file http_client.h
 * @brief HTTP Client for Soil Moisture Data Transmission
 * 
 * This module handles HTTP communication to send soil moisture sensor
 * data to a remote server (your PC).
 */

#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include "../../utils/esp_utils.h"
#include "../../config/esp32-config.h"

#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <sys/time.h>
#include <string.h>
#include <stdint.h>

/**
 * @brief HTTP client configuration
 */
typedef struct {
    char server_ip[16];     ///< Server IP address
    int server_port;        ///< Server port
    char endpoint[64];      ///< API endpoint path
    int timeout_ms;         ///< Request timeout in milliseconds
    int max_retries;        ///< Maximum retry attempts
    bool enable_buffering;  ///< Enable offline packet buffering
    int max_buffered_packets; ///< Maximum packets to buffer offline
} http_client_config_t;



/**
 * @brief HTTP response status
 */
typedef enum {
    HTTP_RESPONSE_OK = 0,
    HTTP_RESPONSE_ERROR,
    HTTP_RESPONSE_TIMEOUT,
    HTTP_RESPONSE_NO_CONNECTION
} http_response_status_t;

/**
 * @brief Initialize HTTP client
 * 
 * @param config HTTP client configuration
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t http_client_init(const http_client_config_t* config);

/**
 * @brief Deinitialize HTTP client
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t http_client_deinit(void);

/**
 * @brief Send JSON payload to server
 * 
 * @param json_payload JSON string to send
 * @return http_response_status_t Response status
 */
http_response_status_t http_client_send_json(const char* json_payload);

/**
 * @brief Test HTTP connection to server
 * 
 * @return http_response_status_t Connection status
 */
http_response_status_t http_client_test_connection(void);

/**
 * @brief Simple connectivity check by attempting to reach the server
 * 
 * @return bool true if server is reachable, false otherwise
 */
bool http_client_ping_server(void);

/**
 * @brief Get the last HTTP status code
 * 
 * @return int HTTP status code from last request
 */
int http_client_get_last_status_code(void);

/**
 * @brief Send JSON payload with automatic buffering when server unavailable
 * 
 * @param json_payload JSON string to send
 * @return http_response_status_t Response status
 */
http_response_status_t http_client_send_json_buffered(const char* json_payload);

/**
 * @brief Flush all buffered packets when server becomes available
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t http_client_flush_buffered_packets(void);

/**
 * @brief Get count of buffered packets
 * 
 * @return int32_t Number of packets currently buffered
 */
int32_t http_client_get_buffered_packet_count(void);

/**
 * @brief Clear all buffered packets (for maintenance/reset)
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t http_client_clear_buffered_packets(void);

#endif // HTTP_CLIENT_H