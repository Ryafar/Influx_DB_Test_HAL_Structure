#include "influx_db_main.h"
#include "esp_log.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "INFLUX_DB_MAIN";

#define INFLUX_DEMO_TASK_STACK   (12 * 1024)  // Larger stack for TLS/HTTP client
#define INFLUX_DEMO_TASK_PRIO    5

static void influx_demo_task(void *arg);

void app_main(void) {
    ESP_LOGI(TAG, "Starting InfluxDB Main - WiFi Connection Only");
    
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
        ESP_LOGI(TAG, "✅ WiFi connection successful - ready for InfluxDB operations");
    } else {
        ESP_LOGE(TAG, "❌ Failed to get IP address");
    }

    // Start Influx demo in a dedicated task with a larger stack to avoid stack overflow
    xTaskCreatePinnedToCore(influx_demo_task, "influx_demo", INFLUX_DEMO_TASK_STACK, NULL, INFLUX_DEMO_TASK_PRIO, NULL, 0);
    // app_main returns; WiFi manager keeps running; the demo task handles Influx work
}

static void influx_demo_task(void *arg)
{
    ESP_LOGI(TAG, "Influx demo task started");

    // Initialize InfluxDB client after successful WiFi connection
    ESP_LOGI(TAG, "Initializing InfluxDB client...");

    influxdb_client_config_t influxdb_config = {
        .server = INFLUXDB_SERVER,
        .port = INFLUXDB_PORT,
        .bucket = INFLUXDB_BUCKET,
        .org = INFLUXDB_ORG,
        .token = INFLUXDB_TOKEN,
        .endpoint = INFLUXDB_ENDPOINT,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .max_retries = HTTP_MAX_RETRIES,
    };

    // Copy strings to avoid const issues
    strncpy(influxdb_config.server, INFLUXDB_SERVER, sizeof(influxdb_config.server) - 1);
    strncpy(influxdb_config.bucket, INFLUXDB_BUCKET, sizeof(influxdb_config.bucket) - 1);
    strncpy(influxdb_config.org, INFLUXDB_ORG, sizeof(influxdb_config.org) - 1);
    strncpy(influxdb_config.token, INFLUXDB_TOKEN, sizeof(influxdb_config.token) - 1);
    strncpy(influxdb_config.endpoint, INFLUXDB_ENDPOINT, sizeof(influxdb_config.endpoint) - 1);

    esp_err_t influx_ret = influxdb_client_init(&influxdb_config);
    if (influx_ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize InfluxDB client: %s", esp_err_to_name(influx_ret));
        vTaskDelete(NULL);
        return;
    }

    // Test InfluxDB connection
    ESP_LOGI(TAG, "Testing InfluxDB connection...");
    influxdb_response_status_t conn_status = influxdb_test_connection();
    if (conn_status == INFLUXDB_RESPONSE_OK) {
        ESP_LOGI(TAG, "✅ InfluxDB connection test successful!");
    } else {
        ESP_LOGW(TAG, "⚠️ InfluxDB connection test failed (status: %d)", conn_status);
    }

    // Create and send a simple test data packet
    ESP_LOGI(TAG, "Creating and sending test InfluxDB packet...");

    // Create test soil moisture data
    influxdb_soil_data_t test_data = {
        .timestamp_ns = 0,          // No timestamp - let InfluxDB use server time
        .voltage = 2.50f,           // Test voltage value
        .moisture_percent = 42.5f,  // Test moisture percentage
        .raw_adc = 2048,            // Test ADC reading
    };

    // Set device ID
    strncpy(test_data.device_id, "ESP32_TEST", sizeof(test_data.device_id) - 1);

    // Send the test data
    influxdb_response_status_t send_status = influxdb_write_soil_data(&test_data);
    if (send_status == INFLUXDB_RESPONSE_OK) {
        ESP_LOGI(TAG, "✅ Test InfluxDB packet sent successfully!");
        ESP_LOGI(TAG, "📊 Sent: voltage=%.2fV, moisture=%.1f%%, raw_adc=%d",
                 test_data.voltage, test_data.moisture_percent, test_data.raw_adc);
    } else {
        ESP_LOGE(TAG, "❌ Failed to send InfluxDB packet (status: %d)", send_status);
        ESP_LOGI(TAG, "HTTP Status Code: %d", influxdb_get_last_status_code());
    }

    // Keep the connection alive and demonstrate periodic data sending
    ESP_LOGI(TAG, "InfluxDB client ready - starting periodic test data transmission...");

    int packet_counter = 1;

    // Main application loop - send test data every 5 seconds
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000)); // Wait 5 seconds

        ESP_LOGI(TAG, "📡 Sending periodic test packet #%d...", packet_counter);

        // Vary the test data slightly
        test_data.voltage = 2.45f + (packet_counter % 10) * 0.05f;          // 2.45-2.90V
        test_data.moisture_percent = 40.0f + (packet_counter % 20) * 1.5f;  // 40-70%
        test_data.raw_adc = 2000 + (packet_counter % 10) * 100;             // 2000-2900

        influxdb_response_status_t status = influxdb_write_soil_data(&test_data);
        if (status == INFLUXDB_RESPONSE_OK) {
            ESP_LOGI(TAG, "✅ Packet #%d sent: V=%.2f, M=%.1f%%", packet_counter, test_data.voltage, test_data.moisture_percent);
        } else {
            ESP_LOGW(TAG, "❌ Packet #%d failed (status: %d, HTTP: %d)", packet_counter, status, influxdb_get_last_status_code());
        }

        packet_counter++;
    }
}