/**
 * @file http_buffer.c
 * @brief HTTP Packet Buffering System Implementation
 */

#include "http_buffer.h"
#include "../../utils/esp_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "HTTPBuffer";

// NVS buffering constants
#define HTTP_BUFFER_NAMESPACE "http_buffer"
#define HTTP_BUFFER_COUNT_KEY "pkt_count"
#define HTTP_BUFFER_PACKET_KEY "pkt_%03d"
#define MAX_PACKET_SIZE 1024
#define DEFAULT_MAX_BUFFERED_PACKETS 50

// Static variables
static nvs_handle_t s_nvs_handle = 0;
static bool s_buffering_enabled = false;
static int32_t s_max_buffered_packets = DEFAULT_MAX_BUFFERED_PACKETS;

esp_err_t http_buffer_init(const http_buffer_config_t* config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_buffering_enabled = config->enable_buffering;
    s_max_buffered_packets = (config->max_buffered_packets > 0) ? 
                            config->max_buffered_packets : DEFAULT_MAX_BUFFERED_PACKETS;
    
    // Initialize NVS for buffering if enabled
    if (s_buffering_enabled) {
        esp_err_t nvs_ret = nvs_open(HTTP_BUFFER_NAMESPACE, NVS_READWRITE, &s_nvs_handle);
        if (nvs_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to open NVS for buffering: %s", esp_err_to_name(nvs_ret));
            s_buffering_enabled = false;
            return nvs_ret;
        } else {
            ESP_LOGI(TAG, "HTTP buffering initialized (max %ld packets)", (long)s_max_buffered_packets);
        }
    }
    
    return ESP_OK;
}

esp_err_t http_buffer_deinit(void)
{
    if (s_buffering_enabled && s_nvs_handle != 0) {
        nvs_close(s_nvs_handle);
        s_nvs_handle = 0;
    }
    
    s_buffering_enabled = false;
    ESP_LOGI(TAG, "HTTP buffer deinitialized");
    return ESP_OK;
}

esp_err_t http_buffer_add_packet(const char* json_payload)
{
    if (!s_buffering_enabled || s_nvs_handle == 0 || json_payload == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Get current packet count
    int32_t packet_count = 0;
    esp_err_t ret = nvs_get_i32(s_nvs_handle, HTTP_BUFFER_COUNT_KEY, &packet_count);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        packet_count = 0;
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get packet count: %s", esp_err_to_name(ret));
        return ret;
    }

    // Check buffer limit
    if (packet_count >= s_max_buffered_packets) {
        ESP_LOGW(TAG, "Buffer full (%ld packets), dropping oldest", (long)packet_count);
        // Remove oldest packet (packet 0) and shift others down
        for (int32_t i = 0; i < packet_count - 1; i++) {
            char old_key[32], new_key[32];
            snprintf(old_key, sizeof(old_key), HTTP_BUFFER_PACKET_KEY, (int)(i + 1));
            snprintf(new_key, sizeof(new_key), HTTP_BUFFER_PACKET_KEY, (int)i);
            
            size_t blob_size = MAX_PACKET_SIZE;
            char temp_buffer[MAX_PACKET_SIZE];
            ret = nvs_get_blob(s_nvs_handle, old_key, temp_buffer, &blob_size);
            if (ret == ESP_OK) {
                nvs_set_blob(s_nvs_handle, new_key, temp_buffer, blob_size);
            }
        }
        packet_count--;
    }

    // Store new packet
    char packet_key[32];
    snprintf(packet_key, sizeof(packet_key), HTTP_BUFFER_PACKET_KEY, (int)packet_count);
    
    size_t payload_len = strlen(json_payload);
    if (payload_len >= MAX_PACKET_SIZE - sizeof(uint32_t) - sizeof(uint16_t)) {
        ESP_LOGE(TAG, "Packet too large to buffer (%d bytes)", payload_len);
        return ESP_ERR_INVALID_SIZE;
    }

    // Create buffered packet structure
    http_buffered_packet_t* packet = malloc(sizeof(http_buffered_packet_t) + payload_len + 1);
    if (packet == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for buffered packet");
        return ESP_ERR_NO_MEM;
    }

    packet->timestamp = (uint32_t)(esp_utils_get_timestamp_ms());
    packet->payload_size = payload_len;
    strcpy(packet->payload, json_payload);

    size_t packet_size = sizeof(http_buffered_packet_t) + payload_len + 1;
    ret = nvs_set_blob(s_nvs_handle, packet_key, packet, packet_size);
    free(packet);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store buffered packet: %s", esp_err_to_name(ret));
        return ret;
    }

    // Update packet count
    packet_count++;
    ret = nvs_set_i32(s_nvs_handle, HTTP_BUFFER_COUNT_KEY, packet_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update packet count: %s", esp_err_to_name(ret));
        return ret;
    }

    // Commit changes
    nvs_commit(s_nvs_handle);
    
    ESP_LOGI(TAG, "Packet buffered (%ld/%ld packets stored)", (long)packet_count, (long)s_max_buffered_packets);
    return ESP_OK;
}

int32_t http_buffer_get_count(void)
{
    if (!s_buffering_enabled || s_nvs_handle == 0) {
        return 0;
    }

    int32_t stored_count = 0;
    esp_err_t ret = nvs_get_i32(s_nvs_handle, HTTP_BUFFER_COUNT_KEY, &stored_count);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return 0;
    } else if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get buffered packet count: %s", esp_err_to_name(ret));
        return 0;
    }
    
    // Validate that packets actually exist and count real packets
    int32_t actual_count = 0;
    for (int32_t i = 0; i < stored_count; i++) {
        char packet_key[32];
        snprintf(packet_key, sizeof(packet_key), HTTP_BUFFER_PACKET_KEY, (int)i);
        
        size_t required_size = 0;
        ret = nvs_get_blob(s_nvs_handle, packet_key, NULL, &required_size);
        if (ret == ESP_OK && required_size > 0) {
            actual_count++;
        }
    }
    
    // If count mismatch detected, correct it
    if (actual_count != stored_count) {
        ESP_LOGW(TAG, "Packet count mismatch detected: stored=%ld, actual=%ld. Correcting...", 
                 (long)stored_count, (long)actual_count);
        nvs_set_i32(s_nvs_handle, HTTP_BUFFER_COUNT_KEY, actual_count);
        nvs_commit(s_nvs_handle);
    }
    
    return actual_count;
}

esp_err_t http_buffer_clear_all(void)
{
    if (!s_buffering_enabled || s_nvs_handle == 0) {
        return ESP_OK;
    }

    int32_t packet_count = http_buffer_get_count();
    
    // Clear all packet entries
    for (int32_t i = 0; i < packet_count; i++) {
        char packet_key[32];
        snprintf(packet_key, sizeof(packet_key), HTTP_BUFFER_PACKET_KEY, (int)i);
        nvs_erase_key(s_nvs_handle, packet_key);
    }
    
    // Reset packet count
    esp_err_t ret = nvs_set_i32(s_nvs_handle, HTTP_BUFFER_COUNT_KEY, 0);
    if (ret != ESP_OK) {
        return ret;
    }
    
    nvs_commit(s_nvs_handle);
    ESP_LOGI(TAG, "Cleared %ld buffered packets", (long)packet_count);
    
    return ESP_OK;
}

esp_err_t http_buffer_flush_packets(http_buffer_send_func_t send_func)
{
    if (!s_buffering_enabled || s_nvs_handle == 0 || send_func == NULL) {
        return ESP_OK; // Nothing to flush
    }

    int32_t packet_count = http_buffer_get_count();
    if (packet_count <= 0) {
        return ESP_OK; // No packets to flush
    }

    ESP_LOGI(TAG, "Flushing %ld buffered packets...", (long)packet_count);
    
    int32_t sent_count = 0;
    int32_t failed_count = 0;

    for (int32_t i = 0; i < packet_count; i++) {
        char packet_key[32];
        snprintf(packet_key, sizeof(packet_key), HTTP_BUFFER_PACKET_KEY, (int)i);
        
        // First check if the packet exists
        size_t required_size = 0;
        esp_err_t check_ret = nvs_get_blob(s_nvs_handle, packet_key, NULL, &required_size);
        if (check_ret != ESP_OK || required_size == 0) {
            ESP_LOGD(TAG, "Packet %ld does not exist, skipping", (long)i);
            continue; // Skip non-existent packets without counting as failure
        }
        
        size_t packet_size = MAX_PACKET_SIZE;
        http_buffered_packet_t* packet = malloc(MAX_PACKET_SIZE);
        if (packet == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for packet flush");
            failed_count++;
            continue;
        }

        esp_err_t ret = nvs_get_blob(s_nvs_handle, packet_key, packet, &packet_size);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read buffered packet %ld: %s", (long)i, esp_err_to_name(ret));
            free(packet);
            failed_count++;
            continue;
        }

        // Try to send the buffered packet using provided function
        esp_err_t send_result = send_func(packet->payload);
        if (send_result == ESP_OK) {
            sent_count++;
            // Remove successful packet from storage
            nvs_erase_key(s_nvs_handle, packet_key);
        } else {
            ESP_LOGW(TAG, "Failed to send buffered packet %ld, keeping in buffer", (long)i);
            failed_count++;
        }
        
        free(packet);
        
        // Small delay between packets to avoid overwhelming server
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Compact the buffer by removing gaps
    if (sent_count > 0) {
        int32_t new_count = 0;
        for (int32_t i = 0; i < packet_count; i++) {
            char packet_key[32];
            snprintf(packet_key, sizeof(packet_key), HTTP_BUFFER_PACKET_KEY, (int)i);
            
            size_t packet_size = MAX_PACKET_SIZE;
            char temp_buffer[MAX_PACKET_SIZE];
            esp_err_t ret = nvs_get_blob(s_nvs_handle, packet_key, temp_buffer, &packet_size);
            if (ret == ESP_OK) {
                if (new_count != i) {
                    char new_key[32];
                    snprintf(new_key, sizeof(new_key), HTTP_BUFFER_PACKET_KEY, (int)new_count);
                    nvs_set_blob(s_nvs_handle, new_key, temp_buffer, packet_size);
                    nvs_erase_key(s_nvs_handle, packet_key);
                }
                new_count++;
            }
        }
        
        // Update packet count
        nvs_set_i32(s_nvs_handle, HTTP_BUFFER_COUNT_KEY, new_count);
        nvs_commit(s_nvs_handle);
        
        ESP_LOGI(TAG, "Flush complete: %ld sent, %ld failed, %ld remaining", 
                 (long)sent_count, (long)failed_count, (long)new_count);
    }

    return (failed_count == 0) ? ESP_OK : ESP_FAIL;
}

bool http_buffer_is_enabled(void)
{
    return s_buffering_enabled && s_nvs_handle != 0;
}