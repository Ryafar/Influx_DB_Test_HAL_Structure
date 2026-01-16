// Measurement task for reading sensor data
#pragma once


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include "esp_log.h"


// Prototype for temperature measurement task
void measurement_task(void *pvParameters);



