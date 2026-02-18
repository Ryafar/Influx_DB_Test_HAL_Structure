/**
 * @file espnow_driver.h
 * @brief ESP-NOW Reliable Data Transfer Driver
 * 
 * Features:
 * - Fragmentation for large payloads (>250 bytes)
 * - Retry mechanism with exponential backoff
 * - Packet sequencing and verification
 * - RTC memory persistence for deep sleep scenarios
 * - Send callback state machine
 * - CRC16 checksum validation
 * - Both unicast and broadcast support
 */

#ifndef ESPNOW_DRIVER_H
#define ESPNOW_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_now.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration Constants
// ============================================================================

#define ESPNOW_MAX_PAYLOAD_SIZE     200     // Safe payload size (250 - header)
#define ESPNOW_MAX_RETRY_COUNT      3       // Max retransmission attempts
#define ESPNOW_SEND_TIMEOUT_MS      100     // Timeout per packet
#define ESPNOW_MAX_PEERS            10      // Maximum number of peers
#define ESPNOW_WIFI_CHANNEL         1       // Default WiFi channel (1-13)
#define ESPNOW_PMK_LEN              16      // Primary Master Key length

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief ESP-NOW packet header
 * This header is prepended to every packet for sequencing and validation
 */
typedef struct __attribute__((packed)) {
    uint8_t node_id;            // Sender identification
    uint16_t packet_sequence;   // Sequential packet number
    uint8_t total_chunks;       // Total number of fragments
    uint8_t chunk_index;        // Current fragment index (0-based)
    uint16_t payload_length;    // Actual payload bytes in this chunk
    uint16_t crc16;             // CRC16 checksum of payload
} espnow_packet_header_t;

/**
 * @brief Complete ESP-NOW packet structure
 */
typedef struct __attribute__((packed)) {
    espnow_packet_header_t header;
    uint8_t payload[ESPNOW_MAX_PAYLOAD_SIZE];
} espnow_packet_t;

/**
 * @brief Peer information structure
 */
typedef struct {
    uint8_t mac_addr[6];        // MAC address of peer
    uint8_t channel;            // WiFi channel
    bool encrypt;               // Enable encryption
    uint8_t lmk[16];           // Local Master Key (if encrypted)
    int8_t rssi;               // Last known RSSI
} espnow_peer_t;

/**
 * @brief Send state tracking
 */
typedef enum {
    ESPNOW_STATE_IDLE,
    ESPNOW_STATE_SENDING,
    ESPNOW_STATE_WAIT_ACK,
    ESPNOW_STATE_SUCCESS,
    ESPNOW_STATE_FAILED
} espnow_send_state_t;

/**
 * @brief Send context for state machine
 */
typedef struct {
    espnow_send_state_t state;
    uint8_t current_chunk;
    uint8_t total_chunks;
    uint8_t retry_count;
    uint32_t last_send_time;
    bool is_sending;
} espnow_send_context_t;

/**
 * @brief Driver configuration
 */
typedef struct {
    uint8_t node_id;                    // This node's ID
    uint8_t wifi_channel;               // WiFi channel to use
    bool enable_encryption;             // Enable ESP-NOW encryption
    uint8_t pmk[ESPNOW_PMK_LEN];       // Primary Master Key
    uint32_t send_timeout_ms;           // Send timeout in milliseconds
    uint8_t max_retries;                // Maximum retry attempts
} espnow_config_t;

/**
 * @brief Receive callback function type
 * @param src_mac Source MAC address
 * @param data Received data buffer
 * @param len Length of received data
 * @param rssi Signal strength
 */
typedef void (*espnow_recv_cb_t)(const uint8_t *src_mac, const uint8_t *data, size_t len, int8_t rssi);

/**
 * @brief Send completion callback function type
 * @param dest_mac Destination MAC address
 * @param success True if send was successful
 */
typedef void (*espnow_send_done_cb_t)(const uint8_t *dest_mac, bool success);

// ============================================================================
// API Functions
// ============================================================================

/**
 * @brief Initialize ESP-NOW driver
 * @param config Driver configuration
 * @return ESP_OK on success
 */
esp_err_t espnow_driver_init(const espnow_config_t *config);

/**
 * @brief Deinitialize ESP-NOW driver
 * @return ESP_OK on success
 */
esp_err_t espnow_driver_deinit(void);

/**
 * @brief Add a peer to communicate with
 * @param peer Peer information
 * @return ESP_OK on success
 */
esp_err_t espnow_driver_add_peer(const espnow_peer_t *peer);

/**
 * @brief Remove a peer
 * @param mac_addr MAC address of peer to remove
 * @return ESP_OK on success
 */
esp_err_t espnow_driver_remove_peer(const uint8_t *mac_addr);

/**
 * @brief Send data to a specific peer (with fragmentation if needed)
 * @param dest_mac Destination MAC address
 * @param data Data buffer to send
 * @param len Length of data
 * @return ESP_OK on success
 */
esp_err_t espnow_driver_send(const uint8_t *dest_mac, const uint8_t *data, size_t len);

/**
 * @brief Send data to all peers (broadcast)
 * @param data Data buffer to send
 * @param len Length of data
 * @return ESP_OK on success
 */
esp_err_t espnow_driver_broadcast(const uint8_t *data, size_t len);

/**
 * @brief Register callback for received data
 * @param cb Callback function
 * @return ESP_OK on success
 */
esp_err_t espnow_driver_register_recv_cb(espnow_recv_cb_t cb);

/**
 * @brief Register callback for send completion
 * @param cb Callback function
 * @return ESP_OK on success
 */
esp_err_t espnow_driver_register_send_done_cb(espnow_send_done_cb_t cb);

/**
 * @brief Get current send state (for monitoring)
 * @return Current send state
 */
espnow_send_state_t espnow_driver_get_send_state(void);

/**
 * @brief Wait for current send operation to complete
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK if completed successfully
 */
esp_err_t espnow_driver_wait_send_done(uint32_t timeout_ms);

/**
 * @brief Get RSSI of last received packet
 * @return RSSI value in dBm
 */
int8_t espnow_driver_get_last_rssi(void);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Calculate CRC16 checksum
 * @param data Data buffer
 * @param len Length of data
 * @return CRC16 value
 */
uint16_t espnow_crc16(const uint8_t *data, size_t len);

/**
 * @brief Convert MAC address to string
 * @param mac MAC address
 * @param str Output string buffer (min 18 bytes)
 */
void espnow_mac_to_str(const uint8_t *mac, char *str);

/**
 * @brief Convert string to MAC address
 * @param str MAC string (format: "AA:BB:CC:DD:EE:FF")
 * @param mac Output MAC address buffer (6 bytes)
 * @return ESP_OK on success
 */
esp_err_t espnow_str_to_mac(const char *str, uint8_t *mac);

#ifdef __cplusplus
}
#endif

#endif // ESPNOW_DRIVER_H
