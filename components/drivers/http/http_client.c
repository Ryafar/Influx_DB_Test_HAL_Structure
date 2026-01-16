/**
 * @file http_client.c
 * @brief HTTP Client Implementation
 */

#include "http_client.h"
#include "http_buffer.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

static const char *TAG = "HTTPClient";

// Static variables
static http_client_config_t s_config;
static int s_last_status_code = 0;
static bool s_initialized = false;
static esp_http_client_handle_t s_persistent_client = NULL;

// Forward declarations
static esp_err_t http_event_handler(esp_http_client_event_t *evt);

esp_err_t http_client_init(const http_client_config_t* config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&s_config, config, sizeof(http_client_config_t));
    
    // Create persistent HTTP client
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d", 
             s_config.server_ip, s_config.server_port);

    esp_http_client_config_t client_config = {
        .url = url,
        .event_handler = http_event_handler,
        .timeout_ms = s_config.timeout_ms,
        .method = HTTP_METHOD_POST,
        .keep_alive_enable = true,
        .keep_alive_idle = 5,
        .keep_alive_interval = 5,
        .keep_alive_count = 3,
    };

    s_persistent_client = esp_http_client_init(&client_config);
    if (s_persistent_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize persistent HTTP client");
        return ESP_FAIL;
    }

    s_initialized = true;
    
    // Initialize HTTP buffer
    http_buffer_config_t buffer_config = {
        .enable_buffering = s_config.enable_buffering,
        .max_buffered_packets = s_config.max_buffered_packets
    };
    
    esp_err_t buffer_ret = http_buffer_init(&buffer_config);
    if (buffer_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize HTTP buffer: %s", esp_err_to_name(buffer_ret));
    }
    
    ESP_LOGI(TAG, "HTTP client initialized for server %s:%d%s with persistent connection", 
             s_config.server_ip, s_config.server_port, s_config.endpoint);
    
    return ESP_OK;
}

esp_err_t http_client_deinit(void)
{
    if (s_persistent_client) {
        esp_http_client_cleanup(s_persistent_client);
        s_persistent_client = NULL;
    }
    
    // Deinitialize HTTP buffer
    http_buffer_deinit();
    
    s_initialized = false;
    ESP_LOGI(TAG, "HTTP client deinitialized");
    return ESP_OK;
}

http_response_status_t http_client_send_json(const char* json_payload)
{
    if (!s_initialized || json_payload == NULL || s_persistent_client == NULL) {
        return HTTP_RESPONSE_ERROR;
    }

    // Build full URL with endpoint
    char full_url[256];
    snprintf(full_url, sizeof(full_url), "http://%s:%d%s", 
             s_config.server_ip, s_config.server_port, s_config.endpoint);

    // Set the specific endpoint for this request
    esp_http_client_set_url(s_persistent_client, full_url);

    // Set headers and payload for this request
    esp_http_client_set_header(s_persistent_client, "Content-Type", "application/json");
    esp_http_client_set_post_field(s_persistent_client, json_payload, strlen(json_payload));

    ESP_LOGD(TAG, "Sending HTTP POST to %s", full_url);
    ESP_LOGD(TAG, "Payload: %s", json_payload);

    http_response_status_t result = HTTP_RESPONSE_ERROR;
    int retry_count = 0;

    while (retry_count <= s_config.max_retries) {
        esp_err_t err = esp_http_client_perform(s_persistent_client);
        
        if (err == ESP_OK) {
            s_last_status_code = esp_http_client_get_status_code(s_persistent_client);
            ESP_LOGD(TAG, "HTTP POST Status = %d", s_last_status_code);
            
            if (s_last_status_code >= 200 && s_last_status_code < 300) {
                result = HTTP_RESPONSE_OK;
                break;
            } else {
                result = HTTP_RESPONSE_ERROR;
            }
        } else if (err == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "HTTP request timeout (retry %d/%d)", retry_count, s_config.max_retries);
            result = HTTP_RESPONSE_TIMEOUT;
        } else if (err == ESP_ERR_HTTP_EAGAIN) {
            ESP_LOGW(TAG, "HTTP EAGAIN - server busy or connection issue (retry %d/%d)", retry_count, s_config.max_retries);
            result = HTTP_RESPONSE_TIMEOUT;
            // Recreate client connection on EAGAIN error
            esp_http_client_close(s_persistent_client);
        } else {
            ESP_LOGE(TAG, "HTTP POST request failed: %s (retry %d/%d)", esp_err_to_name(err), retry_count, s_config.max_retries);
            result = HTTP_RESPONSE_NO_CONNECTION;
            // Close and recreate connection for other errors too
            esp_http_client_close(s_persistent_client);
        }

        retry_count++;
        if (retry_count <= s_config.max_retries) {
            ESP_LOGW(TAG, "Retrying HTTP request (%d/%d) in 2 seconds...", retry_count, s_config.max_retries);
            vTaskDelay(pdMS_TO_TICKS(2000)); // Wait 2 seconds before retry (increased)
        }
    }
    
    return result;
}

http_response_status_t http_client_test_connection(void)
{
    if (!s_initialized) {
        return HTTP_RESPONSE_ERROR;
    }

    // Create a simple test JSON payload
    // TODO: Adjust server to give better response for invalid data packet format
    const char* test_json = "{"
        "\"timestamp\": 1234567890000,"
        "\"voltage\": 3.1415,"
        "\"moisture_percent\": 3.1415,"
        "\"raw_adc\": 31415,"
        "\"device_id\": \"TEST_CONNECTION\""
        "}";

    ESP_LOGI(TAG, "Testing HTTP connection...");
    return http_client_send_json(test_json);
}



int http_client_get_last_status_code(void)
{
    return s_last_status_code;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}

// Helper function to convert HTTP response to esp_err_t for buffer callback
static esp_err_t http_send_callback(const char* json_payload)
{
    http_response_status_t result = http_client_send_json(json_payload);
    return (result == HTTP_RESPONSE_OK) ? ESP_OK : ESP_FAIL;
}

http_response_status_t http_client_send_json_buffered(const char* json_payload)
{
    if (!s_initialized || json_payload == NULL) {
        return HTTP_RESPONSE_ERROR;
    }

    // Try to send packet normally first
    http_response_status_t result = http_client_send_json(json_payload);
    
    if (result == HTTP_RESPONSE_OK) {
        // Success - also try to flush any buffered packets
        if (http_buffer_is_enabled()) {
            esp_err_t flush_ret = http_client_flush_buffered_packets();
            if (flush_ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to flush buffered packets: %s", esp_err_to_name(flush_ret));
            }
        }
        return result;
    }

    // Failed to send - buffer the packet if buffering is enabled
    if (http_buffer_is_enabled() && (result == HTTP_RESPONSE_NO_CONNECTION || 
                                     result == HTTP_RESPONSE_TIMEOUT || 
                                     result == HTTP_RESPONSE_ERROR)) {
        esp_err_t buffer_ret = http_buffer_add_packet(json_payload);
        if (buffer_ret == ESP_OK) {
            ESP_LOGW(TAG, "Server unavailable, packet buffered for later transmission");
            return HTTP_RESPONSE_OK; // Return OK since we buffered successfully
        } else {
            ESP_LOGE(TAG, "Failed to buffer packet: %s", esp_err_to_name(buffer_ret));
        }
    }

    return result;
}

esp_err_t http_client_flush_buffered_packets(void)
{
    return http_buffer_flush_packets(http_send_callback);
}

int32_t http_client_get_buffered_packet_count(void)
{
    return http_buffer_get_count();
}

esp_err_t http_client_clear_buffered_packets(void)
{
    return http_buffer_clear_all();
}

bool http_client_ping_server(void)
{
    if (!s_initialized) {
        return false;
    }

    ESP_LOGI(TAG, "Testing connectivity to %s:%d", s_config.server_ip, s_config.server_port);
    
    // First try a simple socket connection test
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "Socket creation failed");
        return false;
    }
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(s_config.server_port);
    
    // Convert IP string to binary format
    if (inet_pton(AF_INET, s_config.server_ip, &server_addr.sin_addr) <= 0) {
        ESP_LOGE(TAG, "Invalid IP address: %s", s_config.server_ip);
        close(sock);
        return false;
    }
    
    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = 5;  // 5 second timeout
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    // Try to connect
    int connect_result = connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    close(sock);
    
    if (connect_result < 0) {
        ESP_LOGW(TAG, "Socket connection to %s:%d failed: errno %d (%s)", 
                 s_config.server_ip, s_config.server_port, errno, strerror(errno));
        return false;
    } else {
        ESP_LOGI(TAG, "Socket connection to %s:%d successful!", s_config.server_ip, s_config.server_port);
        return true;
    }
}