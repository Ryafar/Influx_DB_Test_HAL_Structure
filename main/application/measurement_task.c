#include "measurement_task.h"



// task
void measurement_task(void *pvParameters) {
    // Initialize sensor here (if needed)
    // For example, setup ADC, configure pins, etc.

    while (1) {
        // Read sensor data
        // For example, read ADC value
        int adc_reading = 0; // Replace with actual ADC reading code

        // Process the reading (e.g., convert to voltage or moisture level)
        float voltage = (adc_reading / 4095.0f) * 3.3f; // Assuming 12-bit ADC and 3.3V reference

        // Log the reading
        printf("ADC Reading: %d, Voltage: %.2f V\n", adc_reading, voltage);

        // Wait for the next measurement interval
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1 second delay
    }

    // Cleanup if necessary (not reached in this infinite loop)
    vTaskDelete(NULL);
}

