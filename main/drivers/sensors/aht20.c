#include "aht20.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "AHT20";

static esp_err_t i2c_master_write_bytes(i2c_port_t port, uint8_t addr, const uint8_t* data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    if (len > 0) {
        i2c_master_write(cmd, (uint8_t*)data, len, true);
    }
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t i2c_master_read_bytes(i2c_port_t port, uint8_t addr, uint8_t* data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t aht20_init(aht20_t* dev, i2c_port_t port, gpio_num_t sda, gpio_num_t scl, uint32_t clk_speed_hz)
{
    if (!dev) return ESP_ERR_INVALID_ARG;

    dev->i2c_port = port;
    dev->sda_io = sda;
    dev->scl_io = scl;
    dev->clk_speed_hz = clk_speed_hz;
    dev->initialized = false;

    // Configure I2C
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = scl,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = clk_speed_hz,
        .clk_flags = 0,
    };

    ESP_ERROR_CHECK(i2c_param_config(port, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(port, conf.mode, 0, 0, 0));

    // Soft reset
    uint8_t soft_reset = 0xBA;
    esp_err_t ret = i2c_master_write_bytes(port, AHT20_I2C_ADDR, &soft_reset, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Soft reset failed: %s", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    // Initialization/calibration command: 0xBE 0x08 0x00
    uint8_t init_cmd[3] = {0xBE, 0x08, 0x00};
    ret = i2c_master_write_bytes(port, AHT20_I2C_ADDR, init_cmd, sizeof(init_cmd));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init command failed: %s", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    dev->initialized = true;
    ESP_LOGI(TAG, "AHT20 initialized on I2C%d SDA=%d SCL=%d", port, sda, scl);
    return ESP_OK;
}

esp_err_t aht20_deinit(aht20_t* dev)
{
    if (!dev) return ESP_ERR_INVALID_ARG;
    if (!dev->initialized) return ESP_OK;
    dev->initialized = false;
    i2c_driver_delete(dev->i2c_port);
    return ESP_OK;
}

esp_err_t aht20_read(aht20_t* dev, float* temperature_c, float* humidity_rh)
{
    if (!dev || !dev->initialized) return ESP_ERR_INVALID_STATE;

    // Trigger measurement: 0xAC 0x33 0x00
    uint8_t trig_cmd[3] = {0xAC, 0x33, 0x00};
    esp_err_t ret = i2c_master_write_bytes(dev->i2c_port, AHT20_I2C_ADDR, trig_cmd, sizeof(trig_cmd));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Trigger measurement failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Wait for measurement to complete (~80ms)
    vTaskDelay(pdMS_TO_TICKS(85));

    // Read 6 bytes: status + 5 data bytes
    uint8_t buf[6] = {0};
    ret = i2c_master_read_bytes(dev->i2c_port, AHT20_I2C_ADDR, buf, sizeof(buf));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Check busy bit
    if (buf[0] & 0x80) {
        ESP_LOGW(TAG, "Sensor busy after wait");
        // Optionally wait a bit more and re-read
        vTaskDelay(pdMS_TO_TICKS(20));
        ret = i2c_master_read_bytes(dev->i2c_port, AHT20_I2C_ADDR, buf, sizeof(buf));
        if (ret != ESP_OK) return ret;
        if (buf[0] & 0x80) return ESP_ERR_TIMEOUT;
    }

    // Parse 20-bit humidity and temperature per datasheet
    uint32_t humidity_raw = ((uint32_t)buf[1] << 12) | ((uint32_t)buf[2] << 4) | ((uint32_t)buf[3] >> 4);
    uint32_t temperature_raw = (((uint32_t)buf[3] & 0x0F) << 16) | ((uint32_t)buf[4] << 8) | (uint32_t)buf[5];

    float hum = ((float)humidity_raw / 1048576.0f) * 100.0f;       // 2^20 = 1048576
    float temp = ((float)temperature_raw / 1048576.0f) * 200.0f - 50.0f;

    if (humidity_rh) *humidity_rh = hum;
    if (temperature_c) *temperature_c = temp;

    return ESP_OK;
}
