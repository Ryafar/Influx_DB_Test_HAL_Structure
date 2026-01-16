/**
 * @file led.h
 * @brief Simple LED Driver for GPIO Control
 * 
 * This module provides basic LED control functionality for status indication.
 */

#ifndef LED_H
#define LED_H

#include "driver/gpio.h"
#include "esp_err.h"

#define LED_STATE_ON  0   ///< LED on state (active low)
#define LED_STATE_OFF 1   ///< LED off state

/**
 * @brief Initialize LED on specified GPIO pin
 * 
 * Configures the GPIO pin as output for LED control.
 * 
 * @param gpio_num GPIO pin number for LED
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t led_init(gpio_num_t gpio_num);

/**
 * @brief Set LED state (on/off)
 * 
 * @param gpio_num GPIO pin number of LED
 * @param state true for ON, false for OFF
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t led_set_state(gpio_num_t gpio_num, bool state);

/**
 * @brief Toggle LED state
 * 
 * Switches LED from ON to OFF or vice versa.
 * 
 * @param gpio_num GPIO pin number of LED
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t led_toggle(gpio_num_t gpio_num);

#endif // LED_H