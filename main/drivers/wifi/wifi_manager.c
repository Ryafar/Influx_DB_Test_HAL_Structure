/**
 * @file wifi_manager.c
 * @brief WiFi Connection Manager Implementation
 */

#include "../../config/esp32-config.h"
#include "wifi_manager.h"

#include <string.h>
#include "nvs_flash.h"
#include "freertos/task.h"

static const char *TAG = WIFI_MANAGER_TAG;

// Static variables
static wifi_manager_config_t s_wifi_config;
static wifi_status_callback_t s_status_callback = NULL;
static wifi_status_t s_current_status = WIFI_STATUS_DISCONNECTED;
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static esp_netif_t *s_sta_netif = NULL;

// Internal function declarations
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void update_status(wifi_status_t new_status, const char* ip_addr);

esp_err_t wifi_manager_init(const wifi_manager_config_t* config, wifi_status_callback_t callback)
{
    esp_err_t ret = ESP_OK;

    // Copy configuration
    strcpy(s_wifi_config.ssid, config->ssid);
    strcpy(s_wifi_config.password, config->password);
    s_wifi_config.max_retry = config->max_retry;
    s_status_callback = callback;

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Create event group
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Create default WiFi STA interface
    s_sta_netif = esp_netif_create_default_wifi_sta();
    
    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Reduce WiFi driver verbosity
    esp_log_level_set("wifi", ESP_LOG_WARN);

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                       ESP_EVENT_ANY_ID,
                                                       &wifi_event_handler,
                                                       NULL,
                                                       NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                       IP_EVENT_STA_GOT_IP,
                                                       &wifi_event_handler,
                                                       NULL,
                                                       NULL));

    ESP_LOGI(TAG, "WiFi manager initialized");
    return ESP_OK;
}

esp_err_t wifi_manager_connect(void)
{
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    
    // Copy SSID and password
    strcpy((char*)wifi_config.sta.ssid, s_wifi_config.ssid);
    strcpy((char*)wifi_config.sta.password, s_wifi_config.password);

    // Reset retry counter
    s_retry_num = 0;
    
    // Set WiFi mode and configuration
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    update_status(WIFI_STATUS_CONNECTING, NULL);

    // Wait for connection or failure
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        return ESP_FAIL;
    } else {
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t wifi_manager_disconnect(void)
{
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    update_status(WIFI_STATUS_DISCONNECTED, NULL);
    return ESP_OK;
}

esp_err_t wifi_manager_deinit(void)
{
    esp_wifi_stop();
    esp_wifi_deinit();
    
    if (s_wifi_event_group != NULL) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    
    update_status(WIFI_STATUS_DISCONNECTED, NULL);
    ESP_LOGI(TAG, "WiFi manager deinitialized");
    return ESP_OK;
}

wifi_status_t wifi_manager_get_status(void)
{
    return s_current_status;
}

bool wifi_manager_is_connected(void)
{
    return s_current_status == WIFI_STATUS_CONNECTED;
}

esp_err_t wifi_manager_get_ip(char* ip_str)
{
    if (!wifi_manager_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(s_sta_netif, &ip_info);
    if (ret == ESP_OK) {
        sprintf(ip_str, IPSTR, IP2STR(&ip_info.ip));
    }
    
    return ret;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi station started, attempting connection...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*) event_data;
        
        ESP_LOGW(TAG, "WiFi disconnected - Reason: %d", disconnected->reason);
        
        // Handle different disconnection reasons
        switch(disconnected->reason) {
            case WIFI_REASON_NO_AP_FOUND:
                ESP_LOGE(TAG, "WiFi Error: Network '%s' not found! Check SSID.", s_wifi_config.ssid);
                break;
            case WIFI_REASON_AUTH_FAIL:
                ESP_LOGE(TAG, "WiFi Error: Authentication failed! Check password.");
                break;
            case WIFI_REASON_ASSOC_FAIL:
                ESP_LOGE(TAG, "WiFi Error: Association failed! Router may be rejecting connection.");
                break;
            case WIFI_REASON_HANDSHAKE_TIMEOUT:
                ESP_LOGE(TAG, "WiFi Error: Handshake timeout! Weak signal or router issues.");
                break;
            default:
                ESP_LOGW(TAG, "WiFi disconnected with reason code: %d", disconnected->reason);
                break;
        }
        
        if (s_retry_num < s_wifi_config.max_retry) {
            ESP_LOGI(TAG, "Retrying WiFi connection (%d/%d) in 2 seconds...", 
                     s_retry_num + 1, s_wifi_config.max_retry);
            vTaskDelay(pdMS_TO_TICKS(2000)); // Wait 2 seconds between retries
            esp_wifi_connect();
            s_retry_num++;
            update_status(WIFI_STATUS_CONNECTING, NULL);
        } else {
            ESP_LOGE(TAG, "WiFi connection failed after %d attempts", s_wifi_config.max_retry);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            update_status(WIFI_STATUS_ERROR, NULL);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        char ip_str[WIFI_IP_STRING_MAX_LEN];
        sprintf(ip_str, IPSTR, IP2STR(&event->ip_info.ip));
        
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        update_status(WIFI_STATUS_CONNECTED, ip_str);
        ESP_LOGI(TAG, "WiFi connected successfully! IP: %s", ip_str);
    }
}

static void update_status(wifi_status_t new_status, const char* ip_addr)
{
    if (s_current_status != new_status) {
        s_current_status = new_status;
        if (s_status_callback != NULL) {
            s_status_callback(new_status, ip_addr);
        }
    }
}