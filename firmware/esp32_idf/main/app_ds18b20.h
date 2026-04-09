#ifndef APP_DS18B20_H
#define APP_DS18B20_H

#include <stdbool.h>

#include "freertos/FreeRTOS.h"

#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
    gpio_num_t pin;
    bool conversion_pending;
    TickType_t conversion_start_tick;
} app_ds18b20_t;

esp_err_t app_ds18b20_init(app_ds18b20_t *sensor, gpio_num_t pin);
esp_err_t app_ds18b20_read_temperature(app_ds18b20_t *sensor, float *temperature_c);

#endif
