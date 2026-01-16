/**
 * @file ntp_time.c
 * @brief NTP Time Synchronization Implementation for Switzerland
 */

#include "ntp_time.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <sys/time.h>
#include <string.h>
#include <stdio.h>

static const char* TAG = "NTP_TIME";

// NTP configuration for Switzerland
#define NTP_SERVER_PRIMARY      "ch.pool.ntp.org"      // Swiss NTP pool
#define NTP_SERVER_SECONDARY    "pool.ntp.org"         // International NTP pool
#define NTP_SERVER_TERTIARY     "time.nist.gov"        // NIST time server
#define NTP_TIMEZONE_SWISS      "CET-1CEST,M3.5.0,M10.5.0/3"  // Swiss timezone (CET/CEST)
#define NTP_SYNC_TIMEOUT_MS     30000                   // 30 seconds timeout
#define MIN_VALID_YEAR          2020                    // Minimum valid year to consider time as synced

// Static variables
static ntp_status_t s_ntp_status = NTP_STATUS_NOT_INITIALIZED;
static ntp_sync_callback_t s_sync_callback = NULL;
static EventGroupHandle_t s_time_event_group = NULL;

// Event bits
#define TIME_SYNC_BIT    BIT0

// Forward declarations
static void ntp_sync_notification_cb(struct timeval *tv);
static void ntp_sync_task(void *pvParameters);

esp_err_t ntp_time_init(ntp_sync_callback_t callback)
{
    ESP_LOGI(TAG, "Initializing NTP time synchronization for Switzerland");

    // Store callback
    s_sync_callback = callback;

    // Create event group for time sync
    s_time_event_group = xEventGroupCreate();
    if (s_time_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create time event group");
        return ESP_ERR_NO_MEM;
    }

    // Set timezone for Switzerland (CET/CEST)
    // CET = Central European Time (UTC+1)
    // CEST = Central European Summer Time (UTC+2)
    // Daylight saving: Last Sunday in March to last Sunday in October
    setenv("TZ", NTP_TIMEZONE_SWISS, 1);
    tzset();

    ESP_LOGI(TAG, "Timezone set to Swiss (CET/CEST)");

    // Set up NTP sync notification callback
    sntp_set_time_sync_notification_cb(ntp_sync_notification_cb);

    // Configure NTP operating mode
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);

    // Set NTP servers (Swiss servers first for better latency)
    esp_sntp_setservername(0, NTP_SERVER_PRIMARY);
    esp_sntp_setservername(1, NTP_SERVER_SECONDARY);
    esp_sntp_setservername(2, NTP_SERVER_TERTIARY);

    ESP_LOGI(TAG, "NTP servers configured:");
    ESP_LOGI(TAG, "  Primary: %s", NTP_SERVER_PRIMARY);
    ESP_LOGI(TAG, "  Secondary: %s", NTP_SERVER_SECONDARY);
    ESP_LOGI(TAG, "  Tertiary: %s", NTP_SERVER_TERTIARY);

    // Initialize and start SNTP
    esp_sntp_init();

    s_ntp_status = NTP_STATUS_SYNCING;

    // Create task to handle initial sync
    BaseType_t task_created = xTaskCreate(
        ntp_sync_task,
        "ntp_sync_task",
        4096,
        NULL,
        5,
        NULL
    );

    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create NTP sync task");
        esp_sntp_stop();
        vEventGroupDelete(s_time_event_group);
        s_time_event_group = NULL;
        s_ntp_status = NTP_STATUS_FAILED;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "NTP time synchronization initialized");
    return ESP_OK;
}

esp_err_t ntp_time_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing NTP time synchronization");

    // Stop SNTP
    esp_sntp_stop();

    // Clean up event group
    if (s_time_event_group != NULL) {
        vEventGroupDelete(s_time_event_group);
        s_time_event_group = NULL;
    }

    s_ntp_status = NTP_STATUS_NOT_INITIALIZED;
    s_sync_callback = NULL;

    ESP_LOGI(TAG, "NTP time synchronization deinitialized");
    return ESP_OK;
}

bool ntp_time_is_synced(void)
{
    if (s_ntp_status != NTP_STATUS_SYNCED) {
        return false;
    }

    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    // Check if year is reasonable (after MIN_VALID_YEAR)
    return (timeinfo.tm_year + 1900) >= MIN_VALID_YEAR;
}

uint64_t ntp_time_get_timestamp_ms(void)
{
    if (!ntp_time_is_synced()) {
        return 0;
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

time_t ntp_time_get_timestamp_s(void)
{
    if (!ntp_time_is_synced()) {
        return 0;
    }

    time_t now;
    time(&now);
    return now;
}

ntp_status_t ntp_time_get_status(void)
{
    return s_ntp_status;
}

esp_err_t ntp_time_get_formatted(char* buffer, size_t buffer_size, const char* format)
{
    if (buffer == NULL || format == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!ntp_time_is_synced()) {
        strncpy(buffer, "Time not synced", buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return ESP_ERR_INVALID_STATE;
    }

    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    size_t result = strftime(buffer, buffer_size, format, &timeinfo);
    if (result == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t ntp_time_get_iso_string(char* buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size < 32) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!ntp_time_is_synced()) {
        strncpy(buffer, "1970-01-01T00:00:00+00:00", buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return ESP_ERR_INVALID_STATE;
    }

    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    // Get timezone offset manually
    // For Switzerland: CET = UTC+1, CEST = UTC+2
    // Check if we're in daylight saving time
    int tz_offset_hours = timeinfo.tm_isdst ? 2 : 1;  // CEST = +2, CET = +1
    
    // Format as ISO 8601: YYYY-MM-DDTHH:MM:SS+TZ
    int written = snprintf(buffer, buffer_size, 
                          "%04d-%02d-%02dT%02d:%02d:%02d%+03d:00",
                          timeinfo.tm_year + 1900,
                          timeinfo.tm_mon + 1,
                          timeinfo.tm_mday,
                          timeinfo.tm_hour,
                          timeinfo.tm_min,
                          timeinfo.tm_sec,
                          tz_offset_hours);

    if (written >= buffer_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t ntp_time_force_sync(void)
{
    if (s_ntp_status == NTP_STATUS_NOT_INITIALIZED) {
        ESP_LOGE(TAG, "NTP not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Forcing NTP synchronization");
    
    // Clear sync bit
    if (s_time_event_group != NULL) {
        xEventGroupClearBits(s_time_event_group, TIME_SYNC_BIT);
    }

    s_ntp_status = NTP_STATUS_SYNCING;

    // Restart SNTP
    esp_sntp_stop();
    vTaskDelay(100 / portTICK_PERIOD_MS);
    esp_sntp_init();

    return ESP_OK;
}

esp_err_t ntp_time_wait_for_sync(uint32_t timeout_ms)
{
    if (s_ntp_status == NTP_STATUS_NOT_INITIALIZED) {
        return ESP_ERR_INVALID_STATE;
    }

    if (ntp_time_is_synced()) {
        return ESP_OK;  // Already synced
    }

    if (s_time_event_group == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Waiting for NTP sync (timeout: %lu ms)", timeout_ms);

    EventBits_t bits = xEventGroupWaitBits(
        s_time_event_group,
        TIME_SYNC_BIT,
        pdFALSE,  // Don't clear bits
        pdFALSE,  // Wait for any bit
        pdMS_TO_TICKS(timeout_ms)
    );

    if (bits & TIME_SYNC_BIT) {
        ESP_LOGI(TAG, "NTP sync completed successfully");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "NTP sync timeout after %lu ms", timeout_ms);
        return ESP_ERR_TIMEOUT;
    }
}

// Private functions

static void ntp_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "NTP time synchronized successfully");

    s_ntp_status = NTP_STATUS_SYNCED;

    // Set the sync bit
    if (s_time_event_group != NULL) {
        xEventGroupSetBits(s_time_event_group, TIME_SYNC_BIT);
    }

    // Get formatted time
    char time_str[64];
    esp_err_t ret = ntp_time_get_formatted(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S %Z");
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Current Swiss time: %s", time_str);
    }

    // Call user callback if registered
    if (s_sync_callback != NULL) {
        s_sync_callback(NTP_STATUS_SYNCED, ret == ESP_OK ? time_str : NULL);
    }
}

static void ntp_sync_task(void *pvParameters)
{
    ESP_LOGI(TAG, "NTP sync task started");

    const int max_wait_iterations = NTP_SYNC_TIMEOUT_MS / 1000;  // Wait up to 30 seconds
    int wait_count = 0;

    while (wait_count < max_wait_iterations) {
        if (ntp_time_is_synced()) {
            ESP_LOGI(TAG, "NTP sync completed in %d seconds", wait_count);
            break;
        }

        wait_count++;
        ESP_LOGI(TAG, "Waiting for NTP sync... (%d/%d)", wait_count, max_wait_iterations);
        vTaskDelay(1000 / portTICK_PERIOD_MS);  // Wait 1 second
    }

    if (!ntp_time_is_synced()) {
        ESP_LOGW(TAG, "NTP sync failed after %d seconds", max_wait_iterations);
        s_ntp_status = NTP_STATUS_FAILED;

        // Call user callback if registered
        if (s_sync_callback != NULL) {
            s_sync_callback(NTP_STATUS_FAILED, NULL);
        }
    }

    // Task complete - delete self
    vTaskDelete(NULL);
}