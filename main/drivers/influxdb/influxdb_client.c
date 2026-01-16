
#include "influxdb_client.h"
#include "../../config/esp32-config.h"
#if INFLUXDB_USE_HTTPS
#include <esp_crt_bundle.h>
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "InfluxDBClient";

// Static variables
static influxdb_client_config_t s_config;
static int s_last_status_code = 0;
static bool s_initialized = false;
static esp_http_client_handle_t s_client = NULL;
static bool s_last_write_success = false; // true when last write received 2xx

// Forward declarations
static esp_err_t influxdb_event_handler(esp_http_client_event_t *evt);
static esp_err_t influxdb_send_line_protocol(const char* line_protocol);

esp_err_t influxdb_client_init(const influxdb_client_config_t* config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&s_config, config, sizeof(influxdb_client_config_t));
    
    // Create HTTP client for InfluxDB
    char url[256];
#if INFLUXDB_USE_HTTPS
    snprintf(url, sizeof(url), "https://%s:%d%s", 
             s_config.server, s_config.port, s_config.endpoint);
#else
    snprintf(url, sizeof(url), "http://%s:%d%s", 
             s_config.server, s_config.port, s_config.endpoint);
#endif

    esp_http_client_config_t client_config = {
        .url = url,
        .event_handler = influxdb_event_handler,
        .timeout_ms = s_config.timeout_ms,
        .method = HTTP_METHOD_POST,
        .keep_alive_enable = true,
        .keep_alive_idle = 5,
        .keep_alive_interval = 5,
        .keep_alive_count = 3,
        .max_redirection_count = 5,  // Handle redirects from HTTP to HTTPS
#if INFLUXDB_USE_HTTPS
    .transport_type = HTTP_TRANSPORT_OVER_SSL,
    .crt_bundle_attach = esp_crt_bundle_attach, // Use built-in certificate bundle
#endif
    };

    s_client = esp_http_client_init(&client_config);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize InfluxDB HTTP client");
        return ESP_FAIL;
    }

    s_initialized = true;
    
    ESP_LOGI(TAG, "InfluxDB client initialized for server %s:%d", 
             s_config.server, s_config.port);
    ESP_LOGI(TAG, "Protocol: %s", INFLUXDB_USE_HTTPS ? "HTTPS" : "HTTP");
    ESP_LOGI(TAG, "Bucket: %s, Organization: %s", s_config.bucket, s_config.org);
    ESP_LOGI(TAG, "Full URL: %s", url);
    if (strlen(s_config.token) == 0) {
        ESP_LOGW(TAG, "InfluxDB token is EMPTY - writes will fail (401). Check credentials.h");
    } else {
        ESP_LOGI(TAG, "InfluxDB token length=%d (not printed)", strlen(s_config.token));
    }
    
    return ESP_OK;
}

esp_err_t influxdb_client_deinit(void)
{
    if (s_client) {
        esp_http_client_cleanup(s_client);
        s_client = NULL;
    }
    
    s_initialized = false;
    ESP_LOGI(TAG, "InfluxDB client deinitialized");
    return ESP_OK;
}

influxdb_response_status_t influxdb_write_soil_data(const influxdb_soil_data_t* data)
{
    if (!s_initialized || data == NULL) {
        return INFLUXDB_RESPONSE_ERROR;
    }

    // Create InfluxDB line protocol format
    // soil_moisture,device=ESP32_XXXXXX voltage=2.5,moisture_percent=45.2,raw_adc=2048 [timestamp]
    char line_protocol[512];
    
#if NTP_ENABLED
    // With NTP: Include timestamp in nanoseconds
    snprintf(line_protocol, sizeof(line_protocol),
        "soil_moisture,device=%s voltage=%.3f,moisture_percent=%.2f,raw_adc=%d %llu",
        data->device_id,
        data->voltage,
        data->moisture_percent,
        data->raw_adc,
        data->timestamp_ns
    );
#else
    // Without NTP: Let InfluxDB use server time (omit timestamp)
    snprintf(line_protocol, sizeof(line_protocol),
        "soil_moisture,device=%s voltage=%.3f,moisture_percent=%.2f,raw_adc=%d",
        data->device_id,
        data->voltage,
        data->moisture_percent,
        data->raw_adc
    );
#endif

    // ESP_LOGI(TAG, "Soil data line protocol: %s", line_protocol);

    esp_err_t result = influxdb_send_line_protocol(line_protocol);
    if (result == ESP_OK) {
        return INFLUXDB_RESPONSE_OK;
    } else {
        return INFLUXDB_RESPONSE_ERROR;
    }
}

influxdb_response_status_t influxdb_write_battery_data(const influxdb_battery_data_t* data)
{
    if (!s_initialized || data == NULL) {
        return INFLUXDB_RESPONSE_ERROR;
    }

    // Create InfluxDB line protocol format
    // battery,device=ESP32_XXXXXX voltage=3.7,percentage=85.0 [timestamp]
    char line_protocol[512];
    
#if NTP_ENABLED
    // With NTP: Include timestamp
    if (data->percentage >= 0) {
        snprintf(line_protocol, sizeof(line_protocol),
            "battery,device=%s voltage=%.3f,percentage=%.1f %llu",
            data->device_id,
            data->voltage,
            data->percentage,
            data->timestamp_ns
        );
    } else {
        // No percentage available
        snprintf(line_protocol, sizeof(line_protocol),
            "battery,device=%s voltage=%.3f %llu",
            data->device_id,
            data->voltage,
            data->timestamp_ns
        );
    }
#else
    // Without NTP: Let InfluxDB use server time (omit timestamp)
    if (data->percentage >= 0) {
        snprintf(line_protocol, sizeof(line_protocol),
            "battery,device=%s voltage=%.3f,percentage=%.1f",
            data->device_id,
            data->voltage,
            data->percentage
        );
    } else {
        // No percentage available
        snprintf(line_protocol, sizeof(line_protocol),
            "battery,device=%s voltage=%.3f",
            data->device_id,
            data->voltage
        );
    }
#endif

    // ESP_LOGD(TAG, "Battery data line protocol: %s", line_protocol);

    esp_err_t result = influxdb_send_line_protocol(line_protocol);
    if (result == ESP_OK) {
        return INFLUXDB_RESPONSE_OK;
    } else {
        return INFLUXDB_RESPONSE_ERROR;
    }
}

static esp_err_t influxdb_send_line_protocol(const char* line_protocol)
{
    if (!s_initialized || line_protocol == NULL || s_client == NULL) {
        return ESP_FAIL;
    }

    // Build full URL with query parameters
    char full_url[256];
#if INFLUXDB_USE_HTTPS
    #if NTP_ENABLED
        // With NTP: Include precision parameter for timestamps
        snprintf(full_url, sizeof(full_url), "https://%s:%d%s?org=%s&bucket=%s&precision=ns", 
                 s_config.server, s_config.port, s_config.endpoint, s_config.org, s_config.bucket);
    #else
        // Without NTP: No precision parameter needed (server time)
        snprintf(full_url, sizeof(full_url), "https://%s:%d%s?org=%s&bucket=%s", 
                 s_config.server, s_config.port, s_config.endpoint, s_config.org, s_config.bucket);
    #endif
#else
    #if NTP_ENABLED
        snprintf(full_url, sizeof(full_url), "http://%s:%d%s?org=%s&bucket=%s&precision=ns", 
                 s_config.server, s_config.port, s_config.endpoint, s_config.org, s_config.bucket);
    #else
        snprintf(full_url, sizeof(full_url), "http://%s:%d%s?org=%s&bucket=%s", 
                 s_config.server, s_config.port, s_config.endpoint, s_config.org, s_config.bucket);
    #endif
#endif

    // Set the URL for this request
    esp_http_client_set_url(s_client, full_url);

    // Set headers
    esp_http_client_set_header(s_client, "Content-Type", "text/plain; charset=utf-8");
    esp_http_client_set_header(s_client, "Accept", "application/json");
    // Avoid reusing a potentially stale connection across deep-sleep cycles
    esp_http_client_set_header(s_client, "Connection", "close");
    
    // Set authorization header if token is provided
    if (strlen(s_config.token) > 0) {
        char auth_header[512];
        snprintf(auth_header, sizeof(auth_header), "Token %s", s_config.token);
        esp_http_client_set_header(s_client, "Authorization", auth_header);
    }

    // Set payload (append newline to be proxy-friendly)
    char lp_with_newline[520];
    int lp_len = snprintf(lp_with_newline, sizeof(lp_with_newline), "%s\n", line_protocol);
    if (lp_len < 0 || lp_len >= sizeof(lp_with_newline)) {
        ESP_LOGE(TAG, "Line protocol formatting error (len=%d)", lp_len);
        return ESP_FAIL;
    }
    esp_http_client_set_post_field(s_client, lp_with_newline, lp_len);

    // Verbose logging enabled to troubleshoot missing data in bucket
    ESP_LOGI(TAG, "Sending to InfluxDB: %s", full_url);
    ESP_LOGI(TAG, "Line Protocol: %s", line_protocol);

    esp_err_t result = ESP_FAIL;
    s_last_write_success = false; // reset before attempt
    int retry_count = 0;

    while (retry_count <= s_config.max_retries) {
        esp_err_t err = esp_http_client_perform(s_client);
        
        if (err == ESP_OK) {
            s_last_status_code = esp_http_client_get_status_code(s_client);
            ESP_LOGI(TAG, "InfluxDB POST status code = %d", s_last_status_code);
            
            if (s_last_status_code >= 200 && s_last_status_code < 300) {
                result = ESP_OK;
                s_last_write_success = true;
                break;
            } else if (s_last_status_code == 401) {
                ESP_LOGE(TAG, "InfluxDB authentication failed - check token");
                result = ESP_ERR_NOT_ALLOWED;
                break; // Don't retry auth errors
            } else if (s_last_status_code == 404) {
                ESP_LOGE(TAG, "InfluxDB endpoint not found (404) - check nginx routing to InfluxDB");
                result = ESP_FAIL;
            } else if (s_last_status_code == 502 || s_last_status_code == 503) {
                ESP_LOGE(TAG, "nginx reverse proxy error (%d) - InfluxDB backend may be down", s_last_status_code);
                result = ESP_FAIL;
            } else {
                ESP_LOGW(TAG, "InfluxDB returned non-success status %d", s_last_status_code);
                result = ESP_FAIL;
            }
        } else if (err == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "InfluxDB request timeout (retry %d/%d)", retry_count, s_config.max_retries);
            result = ESP_ERR_TIMEOUT;
        } else if (err == ESP_ERR_HTTP_EAGAIN) {
            ESP_LOGW(TAG, "InfluxDB EAGAIN - server busy (retry %d/%d)", retry_count, s_config.max_retries);
            result = ESP_ERR_TIMEOUT;
            esp_http_client_close(s_client);
        } else {
            int sock_errno = esp_http_client_get_errno(s_client);
            ESP_LOGE(TAG, "InfluxDB POST failed: %s (0x%x), errno=%d (retry %d/%d)",
                     esp_err_to_name(err), err, sock_errno, retry_count, s_config.max_retries);
            result = ESP_FAIL;
            esp_http_client_close(s_client);
        }

        retry_count++;
        if (retry_count <= s_config.max_retries) {
            ESP_LOGW(TAG, "Retrying InfluxDB request (%d/%d) in 2 seconds...", retry_count, s_config.max_retries);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
    
    if (!s_last_write_success) {
        if (result == ESP_OK) {
            // Should not happen: success without 2xx
            ESP_LOGW(TAG, "Write marked OK but no success status captured");
        } else {
            ESP_LOGW(TAG, "InfluxDB write failed after retries (last_status=%d, result=%s)",
                     s_last_status_code, esp_err_to_name(result));
        }
    }
    return result;
}

static bool influxdb_test_socket_connection(void)
{
    ESP_LOGI(TAG, "Testing socket connection to %s:%d", s_config.server, s_config.port);
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "Socket creation failed");
        return false;
    }
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(s_config.port);
    
    // Resolve hostname to IP
    struct hostent *host_entry = gethostbyname(s_config.server);
    if (host_entry == NULL) {
        ESP_LOGE(TAG, "Failed to resolve hostname: %s", s_config.server);
        close(sock);
        return false;
    }
    
    memcpy(&server_addr.sin_addr, host_entry->h_addr_list[0], host_entry->h_length);
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &server_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
    ESP_LOGI(TAG, "Resolved %s to %s", s_config.server, ip_str);
    
    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    // Try to connect
    int connect_result = connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    close(sock);
    
    if (connect_result < 0) {
        ESP_LOGE(TAG, "Socket connection to %s:%d (%s) failed: errno %d (%s)", 
                 s_config.server, s_config.port, ip_str, errno, strerror(errno));
        return false;
    } else {
        ESP_LOGI(TAG, "Socket connection to %s:%d (%s) successful!", 
                 s_config.server, s_config.port, ip_str);
        return true;
    }
}

influxdb_response_status_t influxdb_test_connection(void)
{
    if (!s_initialized) {
        return INFLUXDB_RESPONSE_ERROR;
    }

    ESP_LOGI(TAG, "=== InfluxDB Connection Test ===");
    
    // First test basic socket connectivity
    if (!influxdb_test_socket_connection()) {
        ESP_LOGW(TAG, "Socket connection to port %d failed, trying port 443...", s_config.port);
        
        // Try port 443 if the configured port fails (nginx reverse proxy)
        if (s_config.port != 443) {
            int original_port = s_config.port;
            s_config.port = 443;
            bool port_443_works = influxdb_test_socket_connection();
            s_config.port = original_port;  // Restore original port
            
            if (!port_443_works) {
                ESP_LOGE(TAG, "Socket connection failed on both port %d and 443", original_port);
                return INFLUXDB_RESPONSE_NO_CONNECTION;
            } else {
                ESP_LOGI(TAG, "Port 443 works! Your nginx reverse proxy is working.");
            }
        } else {
            ESP_LOGE(TAG, "Socket connection to port 443 failed - server unreachable");
            return INFLUXDB_RESPONSE_NO_CONNECTION;
        }
    }

    // Test with HTTP/HTTPS ping endpoint
    char ping_url[256];
#if INFLUXDB_USE_HTTPS
    snprintf(ping_url, sizeof(ping_url), "https://%s:%d/ping", 
             s_config.server, s_config.port);
#else
    snprintf(ping_url, sizeof(ping_url), "http://%s:%d/ping", 
             s_config.server, s_config.port);
#endif

    esp_http_client_config_t ping_config = {
        .url = ping_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,  // Increase timeout
        .max_redirection_count = 5,  // Handle redirects from HTTP to HTTPS
#if INFLUXDB_USE_HTTPS
    .transport_type = HTTP_TRANSPORT_OVER_SSL,
    .crt_bundle_attach = esp_crt_bundle_attach, // Use built-in certificate bundle
#endif
    };

    esp_http_client_handle_t ping_client = esp_http_client_init(&ping_config);
    if (ping_client == NULL) {
        ESP_LOGE(TAG, "Failed to create ping HTTP client");
        return INFLUXDB_RESPONSE_ERROR;
    }

    ESP_LOGI(TAG, "Testing HTTP connection to %s", ping_url);

    esp_err_t err = esp_http_client_perform(ping_client);
    influxdb_response_status_t result;
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(ping_client);
        ESP_LOGI(TAG, "InfluxDB ping HTTP status: %d", status_code);
        
        if (status_code == 204) { // InfluxDB ping returns 204 No Content
            ESP_LOGI(TAG, "InfluxDB HTTP connection successful!");
            result = INFLUXDB_RESPONSE_OK;
        } else if (status_code >= 200 && status_code < 400) {
            ESP_LOGI(TAG, "Server responded but might not be InfluxDB");
            result = INFLUXDB_RESPONSE_OK;
        } else {
            ESP_LOGW(TAG, "InfluxDB ping returned unexpected status: %d", status_code);
            result = INFLUXDB_RESPONSE_ERROR;
        }
    } else {
        ESP_LOGE(TAG, "InfluxDB HTTP ping failed: %s", esp_err_to_name(err));
        result = INFLUXDB_RESPONSE_NO_CONNECTION;
    }

    esp_http_client_cleanup(ping_client);
    ESP_LOGI(TAG, "=== End Connection Test ===");
    return result;
}

int influxdb_get_last_status_code(void)
{
    return s_last_status_code;
}

bool influxdb_last_write_succeeded(void) {
    return s_last_write_success;
}

static esp_err_t influxdb_event_handler(esp_http_client_event_t *evt)
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
            if (evt->data_len > 0) {
                // Log response for debugging
                char response[evt->data_len + 1];
                memcpy(response, evt->data, evt->data_len);
                response[evt->data_len] = '\0';
                ESP_LOGD(TAG, "InfluxDB Response: %s", response);
            }
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

influxdb_response_status_t influxdb_write_env_data(const influxdb_env_data_t* data)
{
    if (!s_initialized || data == NULL) {
        return INFLUXDB_RESPONSE_ERROR;
    }

    // Measurement: environment (temperature/humidity)
    // environment,device=ESP32_XXXXXX temperature_c=23.45,humidity_rh=56.78 [timestamp]
    char line_protocol[512];

#if NTP_ENABLED
    snprintf(line_protocol, sizeof(line_protocol),
        "environment,device=%s temperature_c=%.3f,humidity_rh=%.3f %llu",
        data->device_id,
        data->temperature_c,
        data->humidity_rh,
        data->timestamp_ns
    );
#else
    snprintf(line_protocol, sizeof(line_protocol),
        "environment,device=%s temperature_c=%.3f,humidity_rh=%.3f",
        data->device_id,
        data->temperature_c,
        data->humidity_rh
    );
#endif

    esp_err_t result = influxdb_send_line_protocol(line_protocol);
    if (result == ESP_OK) {
        return INFLUXDB_RESPONSE_OK;
    } else {
        return INFLUXDB_RESPONSE_ERROR;
    }
}