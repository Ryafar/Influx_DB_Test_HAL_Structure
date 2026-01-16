#include "wifi_connection_main.h"
#include "esp_log.h"
#include "utils/ntp_time.h"  // Add NTP module



static const char *TAG = "WIFI_CONN_TESTING";

// NTP callback function
void ntp_status_callback(ntp_status_t status, const char* current_time) {
    switch (status) {
        case NTP_STATUS_SYNCED:
            ESP_LOGI(TAG, "✅ NTP Time synchronized: %s", current_time ? current_time : "Unknown");
            break;
        case NTP_STATUS_FAILED:
            ESP_LOGW(TAG, "❌ NTP Time synchronization failed");
            break;
        case NTP_STATUS_SYNCING:
            ESP_LOGI(TAG, "🔄 NTP synchronization in progress...");
            break;
        default:
            break;
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting WiFi Connection Testing");
    
    wifi_manager_config_t wifi_config = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASSWORD,
        .max_retry = WIFI_MAX_RETRY,
    };

    wifi_manager_init(&wifi_config, NULL);
    wifi_manager_connect();

    // Get IP address
    char ip_str[WIFI_IP_STRING_MAX_LEN];
    if (wifi_manager_get_ip(ip_str) == ESP_OK) {
        ESP_LOGI(TAG, "Got IP: %s", ip_str);
        
        // Initialize NTP after successful WiFi connection
        ESP_LOGI(TAG, "Initializing NTP time synchronization...");
        esp_err_t ntp_ret = ntp_time_init(ntp_status_callback);
        if (ntp_ret == ESP_OK) {
            ESP_LOGI(TAG, "NTP initialized, waiting for synchronization...");
            
            // Wait for time sync with timeout
            ntp_ret = ntp_time_wait_for_sync(30000);  // 30 seconds timeout
            if (ntp_ret == ESP_OK) {
                ESP_LOGI(TAG, "✅ NTP synchronized successfully!");
                
                // Demonstrate time usage
                uint64_t timestamp_ms = ntp_time_get_timestamp_ms();
                char iso_time[32];
                if (ntp_time_get_iso_string(iso_time, sizeof(iso_time)) == ESP_OK) {
                    ESP_LOGI(TAG, "📅 Current timestamp: %llu ms", timestamp_ms);
                    ESP_LOGI(TAG, "🕐 Current Swiss time: %s", iso_time);
                }
            } else {
                ESP_LOGW(TAG, "⏰ NTP sync timeout - will continue without synchronized time");
            }
        } else {
            ESP_LOGE(TAG, "❌ Failed to initialize NTP: %s", esp_err_to_name(ntp_ret));
        }
    }

    // Test HTTP connection
    http_client_config_t http_config = {
        .server_ip = HTTP_SERVER_IP,
        .server_port = HTTP_SERVER_PORT,
        .endpoint = HTTP_ENDPOINT,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .max_retries = HTTP_MAX_RETRIES,
    };

    http_client_init(&http_config);
    http_client_test_connection();

    // Example: Send some test data with correct timestamp
    if (ntp_time_is_synced()) {
        ESP_LOGI(TAG, "📡 Sending test data with synchronized timestamp...");
        // Your sensor data would go here
        uint64_t current_timestamp = ntp_time_get_timestamp_ms();
        ESP_LOGI(TAG, "Using timestamp: %llu", current_timestamp);
        
        // This timestamp is now correct and will show proper time in your CSV!
    } else {
        ESP_LOGW(TAG, "⚠️ Time not synchronized - timestamps may be incorrect");
    }

    // Cleanup
    http_client_deinit();
    ntp_time_deinit();  // Clean up NTP resources
    wifi_manager_deinit();
    ESP_LOGI(TAG, "WiFi Connection Testing Cleanup Completed");
}
