/**
 * @file http_buffer.h
 * @brief HTTP Packet Buffering System
 * 
 * This module provides NVS-based packet buffering for HTTP requests when
 * the server is temporarily unavailable. It implements a FIFO buffer with
 * automatic overflow handling.
 */

#ifndef HTTP_BUFFER_H
#define HTTP_BUFFER_H

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Buffered packet structure
 */
typedef struct {
    uint32_t timestamp;     ///< Packet timestamp
    uint16_t payload_size;  ///< Size of JSON payload
    char payload[];         ///< JSON payload data
} http_buffered_packet_t;

/**
 * @brief HTTP buffer configuration
 */
typedef struct {
    int32_t max_buffered_packets;  ///< Maximum packets to buffer
    bool enable_buffering;         ///< Enable/disable buffering
} http_buffer_config_t;

/**
 * @brief Initialize HTTP packet buffer
 * 
 * @param config Buffer configuration
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t http_buffer_init(const http_buffer_config_t* config);

/**
 * @brief Deinitialize HTTP packet buffer
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t http_buffer_deinit(void);

/**
 * @brief Add a packet to the buffer
 * 
 * @param json_payload JSON string to buffer
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t http_buffer_add_packet(const char* json_payload);

/**
 * @brief Get count of buffered packets
 * 
 * @return int32_t Number of packets currently buffered
 */
int32_t http_buffer_get_count(void);

/**
 * @brief Clear all buffered packets
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t http_buffer_clear_all(void);

/**
 * @brief Flush buffered packets using provided send function
 * 
 * @param send_func Function pointer to send individual packets
 * @return esp_err_t ESP_OK if all packets sent successfully, ESP_FAIL if some failed
 */
typedef esp_err_t (*http_buffer_send_func_t)(const char* json_payload);
esp_err_t http_buffer_flush_packets(http_buffer_send_func_t send_func);

/**
 * @brief Check if buffering is enabled and available
 * 
 * @return true if buffering is available, false otherwise
 */
bool http_buffer_is_enabled(void);

#endif // HTTP_BUFFER_H