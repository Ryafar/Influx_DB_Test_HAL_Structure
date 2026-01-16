#include "env_monitor_app.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"

#include "../config/esp32-config.h"
#include "esp_utils.h"
#include "ntp_time.h"
#include "wifi_manager.h"
#include "influxdb_client.h"
#include "influx_sender.h"
#include "aht20.h"

static const char* TAG = "ENV_MONITOR_APP";

static TaskHandle_t s_task = NULL;
static aht20_t s_aht20;

static influxdb_response_status_t send_env_to_influx(float temperature_c, float humidity_rh, const char* device_id)
{
    if (!device_id) return INFLUXDB_RESPONSE_ERROR;

    influxdb_env_data_t data;

    uint64_t timestamp_ms;
    if (ntp_time_is_synced()) {
        timestamp_ms = ntp_time_get_timestamp_ms();
    } else {
        timestamp_ms = esp_utils_get_timestamp_ms();
    }

    data.timestamp_ns = timestamp_ms * 1000000ULL;
    data.temperature_c = temperature_c;
    data.humidity_rh = humidity_rh;
    strncpy(data.device_id, device_id, sizeof(data.device_id) - 1);
    data.device_id[sizeof(data.device_id) - 1] = '\0';

    influx_sender_init();
    return influx_sender_enqueue_env(&data);
}

static void env_monitor_task(void* pv)
{
    env_monitor_app_t* app = (env_monitor_app_t*)pv;

    ESP_LOGI(TAG, "Environment monitor task started");
    // Log stack high watermark to detect potential stack pressure
    UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "Env task stack high watermark: %lu bytes", (unsigned long)(hwm * sizeof(StackType_t)));

    uint32_t count = 0;
    while (app->is_running) {
        float t = 0.0f, h = 0.0f;
        esp_err_t ret = aht20_read(&s_aht20, &t, &h);
        if (ret == ESP_OK) {
            if (app->config.enable_logging) {
                ESP_LOGI(TAG, "AHT20: T=%.2f C, RH=%.2f%%", t, h);
                // Extra serial output for quick testing
                printf("AHT20 TEST -> Temperature: %.2f C, Humidity: %.2f %%\n", t, h);
            }
#if USE_INFLUXDB
            if (app->config.enable_http_sending && wifi_manager_is_connected()) {
                influxdb_response_status_t st = send_env_to_influx(t, h, app->config.device_id);
                if (st != INFLUXDB_RESPONSE_OK) {
                    ESP_LOGW(TAG, "Failed to enqueue env data (status %d)", st);
                }
            }
#endif
        } else {
            ESP_LOGE(TAG, "AHT20 read failed: %s", esp_err_to_name(ret));
        }

        count++;
        if (app->config.measurements_per_cycle > 0 && count >= app->config.measurements_per_cycle) {
            ESP_LOGI(TAG, "Completed %lu measurements, stopping task", count);
            // Log stack watermark before exiting
            hwm = uxTaskGetStackHighWaterMark(NULL);
            ESP_LOGI(TAG, "Env task exiting, stack high watermark: %lu bytes", (unsigned long)(hwm * sizeof(StackType_t)));
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(app->config.measurement_interval_ms));
    }

    ESP_LOGI(TAG, "Environment monitor task stopped");
    // Extra serial output to confirm task end
    printf("ENV MONITOR: task completed, preparing for sleep...\n");
    app->is_running = false;
    s_task = NULL;
    vTaskDelete(NULL);
}

void env_monitor_get_default_config(env_monitor_config_t* cfg)
{
    if (!cfg) return;
    cfg->i2c_port = I2C_PORT;
    cfg->sda_io = I2C_SDA_PIN;
    cfg->scl_io = I2C_SCL_PIN;
    cfg->i2c_clk_hz = I2C_FREQ_HZ;
    cfg->measurement_interval_ms = ENV_MEASUREMENT_INTERVAL_MS;
    cfg->measurements_per_cycle = CONFIG_ENV_MEASUREMENTS_PER_CYCLE;
    cfg->enable_logging = (CONFIG_ENV_ENABLE_LOGGING != 0);
    cfg->enable_wifi = true;
    cfg->enable_http_sending = true;

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(cfg->device_id, sizeof(cfg->device_id), "ENV_%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

esp_err_t env_monitor_init(env_monitor_app_t* app, const env_monitor_config_t* cfg)
{
    if (!app || !cfg) return ESP_ERR_INVALID_ARG;

    memcpy(&app->config, cfg, sizeof(*cfg));

    // Wi-Fi and Influx setup
    wifi_manager_config_t wifi_cfg = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASSWORD,
        .max_retry = WIFI_MAX_RETRY,
    };

    influxdb_client_config_t influx_cfg = {
        .port = INFLUXDB_PORT,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .max_retries = HTTP_MAX_RETRIES,
    };
    strncpy(influx_cfg.server, INFLUXDB_SERVER, sizeof(influx_cfg.server) - 1);
    influx_cfg.server[sizeof(influx_cfg.server) - 1] = '\0';
    strncpy(influx_cfg.bucket, INFLUXDB_BUCKET, sizeof(influx_cfg.bucket) - 1);
    influx_cfg.bucket[sizeof(influx_cfg.bucket) - 1] = '\0';
    strncpy(influx_cfg.org, INFLUXDB_ORG, sizeof(influx_cfg.org) - 1);
    influx_cfg.org[sizeof(influx_cfg.org) - 1] = '\0';
    strncpy(influx_cfg.token, INFLUXDB_TOKEN, sizeof(influx_cfg.token) - 1);
    influx_cfg.token[sizeof(influx_cfg.token) - 1] = '\0';
    strncpy(influx_cfg.endpoint, INFLUXDB_ENDPOINT, sizeof(influx_cfg.endpoint) - 1);
    influx_cfg.endpoint[sizeof(influx_cfg.endpoint) - 1] = '\0';

    // Init Wi-Fi
    wifi_manager_init(&wifi_cfg, NULL);
    wifi_manager_connect();

    // Wait for Wi-Fi (up to 30s)
    int wait = 0;
    while (!wifi_manager_is_connected() && wait < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        wait++;
        ESP_LOGI(TAG, "WiFi connection attempt %d/30", wait);
    }
    if (!wifi_manager_is_connected()) {
        ESP_LOGE(TAG, "WiFi connection failed");
        return ESP_FAIL;
    }

    // Init Influx client and sender
    ESP_ERROR_CHECK(influxdb_client_init(&influx_cfg));

    // Proactively test the HTTP/TLS path like the working example does
    influxdb_response_status_t conn = influxdb_test_connection();
    if (conn != INFLUXDB_RESPONSE_OK) {
        ESP_LOGW(TAG, "InfluxDB connection test failed (status=%d), will still attempt to send", conn);
    }

    influx_sender_init();

    // Init sensor
    esp_err_t ret = aht20_init(&s_aht20, cfg->i2c_port, cfg->sda_io, cfg->scl_io, cfg->i2c_clk_hz);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init AHT20: %s", esp_err_to_name(ret));
        return ret;
    }

    app->is_running = false;
    ESP_LOGI(TAG, "Environment monitoring initialized. Device ID: %s, sleep=%ds, measurements_per_cycle=%lu", app->config.device_id, CONFIG_ENV_SLEEP_SECONDS, (unsigned long)app->config.measurements_per_cycle);
    return ESP_OK;
}

esp_err_t env_monitor_start(env_monitor_app_t* app)
{
    if (!app) return ESP_ERR_INVALID_ARG;
    if (app->is_running) return ESP_OK;
    app->is_running = true;

    BaseType_t ok = xTaskCreatePinnedToCore(
        env_monitor_task,
        "env_monitor",
        ENV_TASK_STACK_SIZE,
        app,
        ENV_TASK_PRIORITY,
        &s_task,
        0
    );
    if (ok != pdPASS) {
        app->is_running = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t env_monitor_wait_for_completion(env_monitor_app_t* app, uint32_t timeout_ms)
{
    if (!app) return ESP_ERR_INVALID_ARG;
    if (app->config.measurements_per_cycle == 0) return ESP_ERR_INVALID_STATE;

    uint32_t elapsed = 0;
    const uint32_t step = 100;
    while (s_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(step));
        elapsed += step;
        if (timeout_ms > 0 && elapsed >= timeout_ms) {
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}

esp_err_t env_monitor_stop(env_monitor_app_t* app)
{
    if (!app) return ESP_ERR_INVALID_ARG;
    app->is_running = false;
    return ESP_OK;
}

esp_err_t env_monitor_deinit(env_monitor_app_t* app)
{
    if (!app) return ESP_ERR_INVALID_ARG;

    // Stop task
    env_monitor_stop(app);
    while (s_task != NULL) vTaskDelay(pdMS_TO_TICKS(10));

    // Deinit sensor
    aht20_deinit(&s_aht20);

    // Deinit Influx and Wi-Fi
    influxdb_client_deinit();
    wifi_manager_deinit();

    // Deinit NTP
    ntp_time_deinit();

    return ESP_OK;
}
