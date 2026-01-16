#include "led.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "LED";

esp_err_t led_init(gpio_num_t gpio_num) {
    // Configure the GPIO for LED
    gpio_reset_pin(gpio_num);
    gpio_set_direction(gpio_num, GPIO_MODE_OUTPUT);
    gpio_set_level(gpio_num, LED_STATE_OFF); // Turn off LED initially

    ESP_LOGI(TAG, "LED initialized on GPIO %d", gpio_num);
    return ESP_OK;
}

esp_err_t led_set_state(gpio_num_t gpio_num, bool state) {
    gpio_set_level(gpio_num, state ? LED_STATE_ON : LED_STATE_OFF);
    ESP_LOGD(TAG, "LED on GPIO %d set to %s", gpio_num, state ? "ON" : "OFF");
    return ESP_OK;
}

esp_err_t led_toggle(gpio_num_t gpio_num) {
    int current_level = gpio_get_level(gpio_num);
    gpio_set_level(gpio_num, !current_level);
    ESP_LOGD(TAG, "LED on GPIO %d toggled to %s", gpio_num, current_level ? "OFF" : "ON");
    return ESP_OK;
}
