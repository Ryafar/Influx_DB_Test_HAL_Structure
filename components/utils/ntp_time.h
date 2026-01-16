/**
 * @file ntp_time.h
 * @brief NTP Time Synchronization for Switzerland
 * 
 * This module provides NTP time synchronization functionality specifically
 * configured for Swiss timezone (CET/CEST) with Swiss NTP servers.
 */

#ifndef NTP_TIME_H
#define NTP_TIME_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/**
 * @brief NTP synchronization status
 */
typedef enum {
    NTP_STATUS_NOT_INITIALIZED,    ///< NTP not initialized
    NTP_STATUS_SYNCING,           ///< NTP synchronization in progress
    NTP_STATUS_SYNCED,            ///< NTP synchronized successfully
    NTP_STATUS_FAILED             ///< NTP synchronization failed
} ntp_status_t;

/**
 * @brief NTP time sync callback function type
 * 
 * @param status Current NTP status
 * @param current_time Current time string (NULL if not synced)
 */
typedef void (*ntp_sync_callback_t)(ntp_status_t status, const char* current_time);

/**
 * @brief Initialize NTP time synchronization for Switzerland
 * 
 * This function sets up NTP with Swiss servers and CET/CEST timezone.
 * Should be called after WiFi connection is established.
 * 
 * @param callback Optional callback function for sync status updates (can be NULL)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ntp_time_init(ntp_sync_callback_t callback);

/**
 * @brief Deinitialize NTP time synchronization
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ntp_time_deinit(void);

/**
 * @brief Check if time is synchronized with NTP
 * 
 * @return true if time is synced and valid, false otherwise
 */
bool ntp_time_is_synced(void);

/**
 * @brief Get current timestamp in milliseconds since Unix epoch
 * 
 * @return uint64_t Timestamp in milliseconds (0 if not synced)
 */
uint64_t ntp_time_get_timestamp_ms(void);

/**
 * @brief Get current timestamp in seconds since Unix epoch
 * 
 * @return time_t Timestamp in seconds (0 if not synced)
 */
time_t ntp_time_get_timestamp_s(void);

/**
 * @brief Get current NTP synchronization status
 * 
 * @return ntp_status_t Current status
 */
ntp_status_t ntp_time_get_status(void);

/**
 * @brief Get formatted time string in Swiss locale
 * 
 * @param buffer Buffer to store formatted time string
 * @param buffer_size Size of the buffer
 * @param format Time format string (strftime format)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ntp_time_get_formatted(char* buffer, size_t buffer_size, const char* format);

/**
 * @brief Get ISO 8601 formatted timestamp string
 * 
 * Example: "2025-09-28T15:30:45+02:00"
 * 
 * @param buffer Buffer to store ISO timestamp (min 32 bytes recommended)
 * @param buffer_size Size of the buffer
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ntp_time_get_iso_string(char* buffer, size_t buffer_size);

/**
 * @brief Force NTP synchronization
 * 
 * Manually trigger NTP sync. Useful if sync failed or needs to be refreshed.
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ntp_time_force_sync(void);

/**
 * @brief Wait for NTP synchronization with timeout
 * 
 * Blocks until NTP is synchronized or timeout occurs.
 * 
 * @param timeout_ms Maximum time to wait in milliseconds
 * @return esp_err_t ESP_OK if synced, ESP_ERR_TIMEOUT if timeout, other error codes on failure
 */
esp_err_t ntp_time_wait_for_sync(uint32_t timeout_ms);

#endif // NTP_TIME_H