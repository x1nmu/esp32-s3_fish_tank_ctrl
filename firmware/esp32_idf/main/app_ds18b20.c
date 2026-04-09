#include "app_ds18b20.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_rom_sys.h"

#include "app_config.h"

static void ds18b20_drive_low(gpio_num_t pin)
{
    gpio_set_direction(pin, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(pin, 0);
}

static void ds18b20_release(gpio_num_t pin)
{
    gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_level(pin, 1);
}

static bool ds18b20_reset(app_ds18b20_t *sensor)
{
    ds18b20_drive_low(sensor->pin);
    esp_rom_delay_us(480);
    ds18b20_release(sensor->pin);
    esp_rom_delay_us(70);
    bool presence = (gpio_get_level(sensor->pin) == 0);
    esp_rom_delay_us(410);
    return presence;
}

static void ds18b20_write_bit(app_ds18b20_t *sensor, uint8_t bit)
{
    ds18b20_drive_low(sensor->pin);
    if (bit) {
        esp_rom_delay_us(6);
        ds18b20_release(sensor->pin);
        esp_rom_delay_us(64);
    } else {
        esp_rom_delay_us(60);
        ds18b20_release(sensor->pin);
        esp_rom_delay_us(10);
    }
}

static uint8_t ds18b20_read_bit(app_ds18b20_t *sensor)
{
    uint8_t bit;
    ds18b20_drive_low(sensor->pin);
    esp_rom_delay_us(6);
    ds18b20_release(sensor->pin);
    esp_rom_delay_us(9);
    bit = (uint8_t)gpio_get_level(sensor->pin);
    esp_rom_delay_us(55);
    return bit;
}

static void ds18b20_write_byte(app_ds18b20_t *sensor, uint8_t data)
{
    for (int i = 0; i < 8; ++i) {
        ds18b20_write_bit(sensor, data & 0x01U);
        data >>= 1;
    }
}

static uint8_t ds18b20_read_byte(app_ds18b20_t *sensor)
{
    uint8_t data = 0;
    for (int i = 0; i < 8; ++i) {
        data >>= 1;
        if (ds18b20_read_bit(sensor)) {
            data |= 0x80U;
        }
    }
    return data;
}

static uint8_t ds18b20_crc8(const uint8_t *data, int len)
{
    uint8_t crc = 0;
    for (int i = 0; i < len; ++i) {
        uint8_t inbyte = data[i];
        for (int j = 0; j < 8; ++j) {
            uint8_t mix = (crc ^ inbyte) & 0x01U;
            crc >>= 1;
            if (mix) {
                crc ^= 0x8CU;
            }
            inbyte >>= 1;
        }
    }
    return crc;
}

static uint8_t ds18b20_resolution_cfg(void)
{
#if APP_DS18B20_RESOLUTION_BITS >= 12
    return 0x7F;
#elif APP_DS18B20_RESOLUTION_BITS == 11
    return 0x5F;
#elif APP_DS18B20_RESOLUTION_BITS == 10
    return 0x3F;
#else
    return 0x1F;
#endif
}

static void ds18b20_configure_resolution(app_ds18b20_t *sensor)
{
    if (!ds18b20_reset(sensor)) {
        return;
    }

    ds18b20_write_byte(sensor, 0xCC); /* Skip ROM */
    ds18b20_write_byte(sensor, 0x4E); /* Write Scratchpad */
    ds18b20_write_byte(sensor, 0x4B); /* TH register */
    ds18b20_write_byte(sensor, 0x46); /* TL register */
    ds18b20_write_byte(sensor, ds18b20_resolution_cfg());
}

static esp_err_t ds18b20_start_conversion(app_ds18b20_t *sensor)
{
    if (!ds18b20_reset(sensor)) {
        sensor->conversion_pending = false;
        return ESP_ERR_NOT_FOUND;
    }

    ds18b20_write_byte(sensor, 0xCC);
    ds18b20_write_byte(sensor, 0x44);
    sensor->conversion_pending = true;
    sensor->conversion_start_tick = xTaskGetTickCount();
    return ESP_OK;
}

esp_err_t app_ds18b20_init(app_ds18b20_t *sensor, gpio_num_t pin)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_FALSE(sensor != NULL, ESP_ERR_INVALID_ARG, "ds18b20", "sensor is null");
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), "ds18b20", "gpio_config failed");
    sensor->pin = pin;
    sensor->conversion_pending = false;
    sensor->conversion_start_tick = 0;
    ds18b20_release(pin);
    ds18b20_configure_resolution(sensor);
    (void)ds18b20_start_conversion(sensor);
    return ESP_OK;
}

esp_err_t app_ds18b20_read_temperature(app_ds18b20_t *sensor, float *temperature_c)
{
    uint8_t scratchpad[9];
    int16_t raw;
    TickType_t elapsed_ticks;
    TickType_t conversion_ticks = pdMS_TO_TICKS(APP_DS18B20_CONVERSION_MS);

    ESP_RETURN_ON_FALSE(sensor != NULL && temperature_c != NULL, ESP_ERR_INVALID_ARG, "ds18b20", "invalid args");

    if (!sensor->conversion_pending) {
        ESP_RETURN_ON_ERROR(ds18b20_start_conversion(sensor), "ds18b20", "start conversion failed");
        return ESP_ERR_NOT_FINISHED;
    }

    elapsed_ticks = xTaskGetTickCount() - sensor->conversion_start_tick;
    if (elapsed_ticks < conversion_ticks) {
        return ESP_ERR_NOT_FINISHED;
    }

    if (!ds18b20_reset(sensor)) {
        sensor->conversion_pending = false;
        return ESP_ERR_NOT_FOUND;
    }
    ds18b20_write_byte(sensor, 0xCC);
    ds18b20_write_byte(sensor, 0xBE);

    for (int i = 0; i < 9; ++i) {
        scratchpad[i] = ds18b20_read_byte(sensor);
    }

    if (ds18b20_crc8(scratchpad, 8) != scratchpad[8]) {
        sensor->conversion_pending = false;
        return ESP_ERR_INVALID_RESPONSE;
    }

    raw = (int16_t)((scratchpad[1] << 8) | scratchpad[0]);
    *temperature_c = (float)raw / 16.0f;
    ESP_RETURN_ON_ERROR(ds18b20_start_conversion(sensor), "ds18b20", "restart conversion failed");
    return ESP_OK;
}
