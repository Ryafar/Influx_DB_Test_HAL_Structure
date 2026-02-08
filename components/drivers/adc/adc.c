/**
 * @file adc_hal.c
 * @brief Hardware Abstraction Layer for ADC operations - Implementation
 */

#include "adc.h"

#include "esp_log.h"
#include "soc/soc_caps.h"
#include <string.h>

static const char* TAG = "ADC_HAL";

void adc_hal_get_default_config(adc_hal_config_t* config, adc_unit_t unit, adc_channel_t channel) {
    if (config == NULL) {
        return;
    }
    
    config->unit = unit;
    config->channel = channel;
    config->bitwidth = ADC_BITWIDTH_DEFAULT;
    config->attenuation = ADC_ATTEN_DB_12;  // 0-3.3V range
    config->reference_voltage = 3.3f;
}

esp_err_t adc_hal_init(adc_hal_t* adc_hal, const adc_hal_config_t* config) {
    if (adc_hal == NULL || config == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Copy configuration
    memcpy(&adc_hal->config, config, sizeof(adc_hal_config_t));
    
    // Initialize ADC unit
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = config->unit,
    };
    
    esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc_hal->handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC unit: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure ADC channel
    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = config->bitwidth,
        .atten = config->attenuation,
    };
    
    ret = adc_oneshot_config_channel(adc_hal->handle, config->channel, &chan_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC channel: %s", esp_err_to_name(ret));
        adc_oneshot_del_unit(adc_hal->handle);
        return ret;
    }
    
    ESP_LOGI(TAG, "ADC initialized successfully (Unit: %d, Channel: %d)", config->unit, config->channel);
    return ESP_OK;
}

esp_err_t adc_hal_deinit(adc_hal_t* adc_hal) {
    if (adc_hal == NULL) {
        ESP_LOGE(TAG, "Invalid parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = adc_oneshot_del_unit(adc_hal->handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize ADC: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "ADC deinitialized successfully");
    return ESP_OK;
}

esp_err_t adc_hal_read_raw(adc_hal_t* adc_hal, int* raw_value) {
    if (adc_hal == NULL || raw_value == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = adc_oneshot_read(adc_hal->handle, adc_hal->config.channel, raw_value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ADC: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

esp_err_t adc_hal_read_voltage(adc_hal_t* adc_hal, float* voltage) {
    if (adc_hal == NULL || voltage == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    int raw_value;
    esp_err_t ret = adc_hal_read_raw(adc_hal, &raw_value);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Calculate maximum ADC value based on bitwidth
    int max_adc_value;
    switch (adc_hal->config.bitwidth) {
        case ADC_BITWIDTH_9:
            max_adc_value = 511;
            break;
        case ADC_BITWIDTH_10:
            max_adc_value = 1023;
            break;
        case ADC_BITWIDTH_11:
            max_adc_value = 2047;
            break;
        case ADC_BITWIDTH_12:
            max_adc_value = 4095;
            break;
        default:
            max_adc_value = 4095;  // Default to 12-bit
            break;
    }
    
    // Convert to voltage
    *voltage = ((float)raw_value / (float)max_adc_value) * adc_hal->config.reference_voltage;
    
    ESP_LOGD(TAG, "Raw: %d, Voltage: %.3f V", raw_value, *voltage);
    return ESP_OK;
}