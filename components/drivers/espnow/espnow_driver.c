/**
 * @file espnow_driver.c
 * @brief ESP-NOW Reliable Data Transfer Driver Implementation
 */

#include "espnow_driver.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_crc.h"
#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

static const char *TAG = "ESPNOW_DRIVER";

// ============================================================================
// Private Variables
// ============================================================================

static bool s_driver_initialized = false;
static espnow_config_t s_config;
static espnow_send_context_t s_send_ctx = {0};
static espnow_recv_cb_t s_recv_callback = NULL;
static espnow_send_done_cb_t s_send_done_callback = NULL;
static SemaphoreHandle_t s_send_mutex = NULL;
static EventGroupHandle_t s_send_event_group = NULL;
static int8_t s_last_rssi = 0;

// Event bits
#define ESPNOW_SEND_DONE_BIT    BIT0
#define ESPNOW_SEND_SUCCESS_BIT BIT1

// Fragmentation buffer
static espnow_packet_t s_tx_packets[32];  // Support up to 32 fragments
static const uint8_t *s_current_dest_mac = NULL;

// ============================================================================
// CRC16 Implementation
// ============================================================================

uint16_t espnow_crc16(const uint8_t *data, size_t len) {
    return esp_crc16_le(0xFFFF, data, len);
}

// ============================================================================
// MAC Address Utilities
// ============================================================================

void espnow_mac_to_str(const uint8_t *mac, char *str) {
    sprintf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

esp_err_t espnow_str_to_mac(const char *str, uint8_t *mac) {
    if (sscanf(str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) != 6) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

// ============================================================================
// ESP-NOW Callbacks
// ============================================================================

/**
 * @brief Internal send callback from ESP-NOW
 */
static void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
    char mac_str[18];
    espnow_mac_to_str(mac_addr, mac_str);
    
    if (status == ESP_NOW_SEND_SUCCESS) {
        ESP_LOGD(TAG, "Send success to %s, chunk %d/%d", 
                 mac_str, s_send_ctx.current_chunk + 1, s_send_ctx.total_chunks);
        s_send_ctx.state = ESPNOW_STATE_SUCCESS;
        xEventGroupSetBits(s_send_event_group, ESPNOW_SEND_DONE_BIT | ESPNOW_SEND_SUCCESS_BIT);
    } else {
        ESP_LOGW(TAG, "Send failed to %s, chunk %d/%d (retry %d/%d)", 
                 mac_str, s_send_ctx.current_chunk + 1, s_send_ctx.total_chunks,
                 s_send_ctx.retry_count, s_config.max_retries);
        s_send_ctx.state = ESPNOW_STATE_FAILED;
        xEventGroupSetBits(s_send_event_group, ESPNOW_SEND_DONE_BIT);
    }
}

/**
 * @brief Internal receive callback from ESP-NOW
 */
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (!recv_info || !data || len < sizeof(espnow_packet_header_t)) {
        ESP_LOGW(TAG, "Invalid received packet");
        return;
    }
    
    // Extract RSSI
    s_last_rssi = recv_info->rx_ctrl->rssi;
    
    espnow_packet_t *packet = (espnow_packet_t *)data;
    
    // Verify CRC
    uint16_t calculated_crc = espnow_crc16(packet->payload, packet->header.payload_length);
    if (calculated_crc != packet->header.crc16) {
        ESP_LOGW(TAG, "CRC mismatch! Expected 0x%04X, got 0x%04X", 
                 calculated_crc, packet->header.crc16);
        return;
    }
    
    char mac_str[18];
    espnow_mac_to_str(recv_info->src_addr, mac_str);
    
    ESP_LOGI(TAG, "Received from %s: Node %d, Seq %d, Chunk %d/%d, Len %d, RSSI %d dBm",
             mac_str, 
             packet->header.node_id,
             packet->header.packet_sequence,
             packet->header.chunk_index + 1,
             packet->header.total_chunks,
             packet->header.payload_length,
             s_last_rssi);
    
    // If user registered a callback, invoke it
    if (s_recv_callback) {
        s_recv_callback(recv_info->src_addr, packet->payload, 
                       packet->header.payload_length, s_last_rssi);
    }
}

// ============================================================================
// Fragmentation Logic
// ============================================================================

/**
 * @brief Fragment data into packets
 * @return Number of packets created
 */
static uint8_t fragment_data(const uint8_t *data, size_t len, uint16_t sequence_num) {
    uint8_t total_chunks = (len + ESPNOW_MAX_PAYLOAD_SIZE - 1) / ESPNOW_MAX_PAYLOAD_SIZE;
    
    if (total_chunks > 32) {
        ESP_LOGE(TAG, "Data too large: %d bytes requires %d chunks (max 32)", len, total_chunks);
        return 0;
    }
    
    for (uint8_t i = 0; i < total_chunks; i++) {
        size_t offset = i * ESPNOW_MAX_PAYLOAD_SIZE;
        size_t chunk_size = (offset + ESPNOW_MAX_PAYLOAD_SIZE > len) 
                           ? (len - offset) : ESPNOW_MAX_PAYLOAD_SIZE;
        
        // Fill header
        s_tx_packets[i].header.node_id = s_config.node_id;
        s_tx_packets[i].header.packet_sequence = sequence_num;
        s_tx_packets[i].header.total_chunks = total_chunks;
        s_tx_packets[i].header.chunk_index = i;
        s_tx_packets[i].header.payload_length = chunk_size;
        
        // Copy payload
        memcpy(s_tx_packets[i].payload, data + offset, chunk_size);
        
        // Calculate CRC
        s_tx_packets[i].header.crc16 = espnow_crc16(s_tx_packets[i].payload, chunk_size);
    }
    
    return total_chunks;
}

// ============================================================================
// Send State Machine
// ============================================================================

/**
 * @brief Send a single packet with retry logic
 */
static esp_err_t send_packet_with_retry(const uint8_t *dest_mac, const espnow_packet_t *packet) {
    esp_err_t err;
    uint8_t retry = 0;
    
    // Calculate actual packet size (header + payload)
    size_t packet_size = sizeof(espnow_packet_header_t) + packet->header.payload_length;
    
    while (retry <= s_config.max_retries) {
        // Clear event bits
        xEventGroupClearBits(s_send_event_group, ESPNOW_SEND_DONE_BIT | ESPNOW_SEND_SUCCESS_BIT);
        
        // Send packet
        s_send_ctx.state = ESPNOW_STATE_SENDING;
        s_send_ctx.retry_count = retry;
        s_send_ctx.last_send_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        err = esp_now_send(dest_mac, (const uint8_t *)packet, packet_size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_now_send failed: %s", esp_err_to_name(err));
            retry++;
            vTaskDelay(pdMS_TO_TICKS(10 * (1 << retry))); // Exponential backoff
            continue;
        }
        
        // Wait for callback
        EventBits_t bits = xEventGroupWaitBits(
            s_send_event_group,
            ESPNOW_SEND_DONE_BIT,
            pdTRUE,  // Clear on exit
            pdFALSE, // Wait for any bit
            pdMS_TO_TICKS(s_config.send_timeout_ms)
        );
        
        if (bits & ESPNOW_SEND_DONE_BIT) {
            if (bits & ESPNOW_SEND_SUCCESS_BIT) {
                // Success!
                return ESP_OK;
            } else {
                // Failed, retry
                retry++;
                vTaskDelay(pdMS_TO_TICKS(10 * (1 << retry))); // Exponential backoff
            }
        } else {
            // Timeout
            ESP_LOGW(TAG, "Send timeout");
            retry++;
            vTaskDelay(pdMS_TO_TICKS(10 * (1 << retry)));
        }
    }
    
    ESP_LOGE(TAG, "Failed to send packet after %d retries", s_config.max_retries);
    return ESP_ERR_TIMEOUT;
}

// ============================================================================
// Public API Implementation
// ============================================================================

esp_err_t espnow_driver_init(const espnow_config_t *config) {
    if (s_driver_initialized) {
        ESP_LOGW(TAG, "Driver already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Initializing ESP-NOW driver (Node ID: %d, Channel: %d)", 
             config->node_id, config->wifi_channel);
    
    // Copy configuration
    memcpy(&s_config, config, sizeof(espnow_config_t));
    
    // Create synchronization primitives
    s_send_mutex = xSemaphoreCreateMutex();
    s_send_event_group = xEventGroupCreate();
    if (!s_send_mutex || !s_send_event_group) {
        ESP_LOGE(TAG, "Failed to create synchronization primitives");
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize ESP-NOW
    esp_err_t err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init failed: %s", esp_err_to_name(err));
        return err;
    }
    
    // Set WiFi channel
    err = esp_wifi_set_channel(config->wifi_channel, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set WiFi channel: %s", esp_err_to_name(err));
    }
    
    // Set Primary Master Key if encryption enabled
    if (config->enable_encryption) {
        err = esp_now_set_pmk(config->pmk);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_now_set_pmk failed: %s", esp_err_to_name(err));
            esp_now_deinit();
            return err;
        }
        ESP_LOGI(TAG, "ESP-NOW encryption enabled");
    }
    
    // Register callbacks
    err = esp_now_register_send_cb(espnow_send_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_register_send_cb failed: %s", esp_err_to_name(err));
        esp_now_deinit();
        return err;
    }
    
    err = esp_now_register_recv_cb(espnow_recv_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_register_recv_cb failed: %s", esp_err_to_name(err));
        esp_now_deinit();
        return err;
    }
    
    s_driver_initialized = true;
    s_send_ctx.state = ESPNOW_STATE_IDLE;
    
    ESP_LOGI(TAG, "ESP-NOW driver initialized successfully");
    return ESP_OK;
}

esp_err_t espnow_driver_deinit(void) {
    if (!s_driver_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Deinitializing ESP-NOW driver");
    
    esp_now_deinit();
    
    if (s_send_mutex) {
        vSemaphoreDelete(s_send_mutex);
        s_send_mutex = NULL;
    }
    
    if (s_send_event_group) {
        vEventGroupDelete(s_send_event_group);
        s_send_event_group = NULL;
    }
    
    s_driver_initialized = false;
    s_recv_callback = NULL;
    s_send_done_callback = NULL;
    
    return ESP_OK;
}

esp_err_t espnow_driver_add_peer(const espnow_peer_t *peer) {
    if (!s_driver_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!peer) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_now_peer_info_t peer_info = {0};
    memcpy(peer_info.peer_addr, peer->mac_addr, 6);
    peer_info.channel = peer->channel;
    peer_info.ifidx = WIFI_IF_STA;
    peer_info.encrypt = peer->encrypt;
    
    if (peer->encrypt) {
        memcpy(peer_info.lmk, peer->lmk, 16);
    }
    
    esp_err_t err = esp_now_add_peer(&peer_info);
    if (err != ESP_OK) {
        if (err == ESP_ERR_ESPNOW_EXIST) {
            ESP_LOGW(TAG, "Peer already exists");
            return ESP_OK; // Not really an error
        }
        char mac_str[18];
        espnow_mac_to_str(peer->mac_addr, mac_str);
        ESP_LOGE(TAG, "Failed to add peer %s: %s", mac_str, esp_err_to_name(err));
        return err;
    }
    
    char mac_str[18];
    espnow_mac_to_str(peer->mac_addr, mac_str);
    ESP_LOGI(TAG, "Added peer: %s (Channel %d, Encrypt: %s)", 
             mac_str, peer->channel, peer->encrypt ? "Yes" : "No");
    
    return ESP_OK;
}

esp_err_t espnow_driver_remove_peer(const uint8_t *mac_addr) {
    if (!s_driver_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!mac_addr) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t err = esp_now_del_peer(mac_addr);
    if (err != ESP_OK) {
        char mac_str[18];
        espnow_mac_to_str(mac_addr, mac_str);
        ESP_LOGE(TAG, "Failed to remove peer %s: %s", mac_str, esp_err_to_name(err));
        return err;
    }
    
    return ESP_OK;
}

esp_err_t espnow_driver_send(const uint8_t *dest_mac, const uint8_t *data, size_t len) {
    if (!s_driver_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!dest_mac || !data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Take mutex to prevent concurrent sends
    if (xSemaphoreTake(s_send_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire send mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    char mac_str[18];
    espnow_mac_to_str(dest_mac, mac_str);
    ESP_LOGI(TAG, "Sending %d bytes to %s", len, mac_str);
    
    // Generate sequence number
    static uint16_t sequence_num = 0;
    sequence_num++;
    
    // Fragment data
    uint8_t total_chunks = fragment_data(data, len, sequence_num);
    if (total_chunks == 0) {
        xSemaphoreGive(s_send_mutex);
        return ESP_ERR_INVALID_SIZE;
    }
    
    s_send_ctx.total_chunks = total_chunks;
    s_send_ctx.is_sending = true;
    s_current_dest_mac = dest_mac;
    
    // Send all chunks
    esp_err_t result = ESP_OK;
    for (uint8_t i = 0; i < total_chunks; i++) {
        s_send_ctx.current_chunk = i;
        
        ESP_LOGD(TAG, "Sending chunk %d/%d (%d bytes)", 
                 i + 1, total_chunks, s_tx_packets[i].header.payload_length);
        
        esp_err_t err = send_packet_with_retry(dest_mac, &s_tx_packets[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send chunk %d/%d", i + 1, total_chunks);
            result = err;
            break;
        }
        
        // Small delay between chunks to avoid buffer overflow
        if (i < total_chunks - 1) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    
    s_send_ctx.is_sending = false;
    s_send_ctx.state = (result == ESP_OK) ? ESPNOW_STATE_IDLE : ESPNOW_STATE_FAILED;
    
    // Notify user callback if registered
    if (s_send_done_callback) {
        s_send_done_callback(dest_mac, result == ESP_OK);
    }
    
    xSemaphoreGive(s_send_mutex);
    
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Successfully sent %d chunks to %s", total_chunks, mac_str);
    }
    
    return result;
}

esp_err_t espnow_driver_broadcast(const uint8_t *data, size_t len) {
    static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return espnow_driver_send(broadcast_mac, data, len);
}

esp_err_t espnow_driver_register_recv_cb(espnow_recv_cb_t cb) {
    s_recv_callback = cb;
    return ESP_OK;
}

esp_err_t espnow_driver_register_send_done_cb(espnow_send_done_cb_t cb) {
    s_send_done_callback = cb;
    return ESP_OK;
}

espnow_send_state_t espnow_driver_get_send_state(void) {
    return s_send_ctx.state;
}

esp_err_t espnow_driver_wait_send_done(uint32_t timeout_ms) {
    uint32_t start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    while (s_send_ctx.is_sending) {
        uint32_t elapsed = (xTaskGetTickCount() * portTICK_PERIOD_MS) - start_time;
        if (elapsed >= timeout_ms) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    return (s_send_ctx.state == ESPNOW_STATE_IDLE) ? ESP_OK : ESP_FAIL;
}

int8_t espnow_driver_get_last_rssi(void) {
    return s_last_rssi;
}
