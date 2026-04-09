#include "app_oled.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2c.h"
#include "esp_check.h"

#include "app_config.h"

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_BUF_SIZE (OLED_WIDTH * OLED_HEIGHT / 8)

static uint8_t s_oled_buffer[OLED_BUF_SIZE];
static uint8_t s_oled_last_buffer[OLED_BUF_SIZE];
static bool s_oled_ready = false;
static bool s_oled_last_valid = false;
static uint8_t s_oled_addr = APP_OLED_I2C_ADDRESS;
#define OLED_SH1106_COL_OFFSET 2

static const char *oled_state_text(app_state_t state)
{
    switch (state) {
        case APP_STATE_INIT: return "INIT";
        case APP_STATE_IDLE: return "IDLE";
        case APP_STATE_HEATING: return "HEAT";
        case APP_STATE_COOLING: return "COOL";
        case APP_STATE_DEGRADED: return "DEGR";
        case APP_STATE_ALARM: return "ALRM";
        case APP_STATE_FAULT_STOP: return "FAIL";
        default: return "UNKN";
    }
}

static const uint8_t *glyph_for_char(char c)
{
    static const uint8_t space[5] = {0x00,0x00,0x00,0x00,0x00};
    static const uint8_t dash[5]  = {0x08,0x08,0x08,0x08,0x08};
    static const uint8_t underscore[5] = {0x40,0x40,0x40,0x40,0x40};
    static const uint8_t dot[5]   = {0x00,0x00,0x60,0x60,0x00};
    static const uint8_t colon[5] = {0x00,0x36,0x36,0x00,0x00};
    static const uint8_t slash[5] = {0x20,0x10,0x08,0x04,0x02};
    static const uint8_t digits[10][5] = {
        {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x62,0x51,0x49,0x49,0x46},{0x22,0x41,0x49,0x49,0x36},
        {0x18,0x14,0x12,0x7F,0x10},{0x2F,0x49,0x49,0x49,0x31},{0x3E,0x49,0x49,0x49,0x32},{0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36},{0x26,0x49,0x49,0x49,0x3E}
    };
    static const uint8_t letters[26][5] = {
        {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
        {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
        {0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
        {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
        {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x26,0x49,0x49,0x49,0x32},{0x01,0x01,0x7F,0x01,0x01},
        {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
    };

    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    if (c >= 'a' && c <= 'z') return letters[c - 'a'];

    switch (c) {
        case '-': return dash;
        case '_': return underscore;
        case '.': return dot;
        case ':': return colon;
        case '/': return slash;
        case ' ': return space;
        default: return space;
    }
}

static esp_err_t oled_send_command(uint8_t cmd)
{
    uint8_t buffer[2] = {0x00, cmd};
    return i2c_master_write_to_device(APP_I2C_PORT, s_oled_addr, buffer, sizeof(buffer), pdMS_TO_TICKS(100));
}

static esp_err_t oled_send_data(const uint8_t *data, size_t len)
{
    uint8_t chunk[17];
    chunk[0] = 0x40;
    while (len > 0) {
        size_t n = len > 16 ? 16 : len;
        memcpy(&chunk[1], data, n);
        ESP_RETURN_ON_ERROR(i2c_master_write_to_device(APP_I2C_PORT, s_oled_addr, chunk, n + 1, pdMS_TO_TICKS(100)), "oled", "data send failed");
        data += n;
        len -= n;
    }
    return ESP_OK;
}

static void oled_clear_buffer(void)
{
    memset(s_oled_buffer, 0, sizeof(s_oled_buffer));
}

static void oled_draw_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) {
        return;
    }
    uint16_t index = x + (y / 8) * OLED_WIDTH;
    uint8_t mask = 1U << (y % 8);
    if (on) s_oled_buffer[index] |= mask;
    else s_oled_buffer[index] &= (uint8_t)~mask;
}

static void oled_draw_char(int x, int y, char c)
{
    const uint8_t *glyph = glyph_for_char(c);
    for (int col = 0; col < 5; ++col) {
        for (int row = 0; row < 7; ++row) {
            oled_draw_pixel(x + col, y + row, ((glyph[col] >> row) & 0x01U) != 0);
        }
    }
}

static void oled_draw_string(int x, int y, const char *text)
{
    while (*text != '\0') {
        oled_draw_char(x, y, *text++);
        x += 6;
    }
}

static esp_err_t oled_flush(void)
{
    bool has_change = !s_oled_last_valid;

    if (!has_change) {
        for (uint8_t page = 0; page < 8; ++page) {
            if (memcmp(&s_oled_buffer[page * OLED_WIDTH], &s_oled_last_buffer[page * OLED_WIDTH], OLED_WIDTH) != 0) {
                has_change = true;
                break;
            }
        }
    }

    if (!has_change) {
        return ESP_OK;
    }

    for (uint8_t page = 0; page < 8; ++page) {
        bool page_changed = !s_oled_last_valid ||
                            memcmp(&s_oled_buffer[page * OLED_WIDTH], &s_oled_last_buffer[page * OLED_WIDTH], OLED_WIDTH) != 0;

        if (!page_changed) {
            continue;
        }

        ESP_RETURN_ON_ERROR(oled_send_command((uint8_t)(0xB0 | page)), "oled", "set page failed");
#if APP_OLED_DRIVER_SH1106
        ESP_RETURN_ON_ERROR(oled_send_command((uint8_t)(0x00 | (OLED_SH1106_COL_OFFSET & 0x0F))), "oled", "set lower column failed");
        ESP_RETURN_ON_ERROR(oled_send_command((uint8_t)(0x10 | (OLED_SH1106_COL_OFFSET >> 4))), "oled", "set upper column failed");
#else
        ESP_RETURN_ON_ERROR(oled_send_command(0x00), "oled", "set lower column failed");
        ESP_RETURN_ON_ERROR(oled_send_command(0x10), "oled", "set upper column failed");
#endif
        ESP_RETURN_ON_ERROR(oled_send_data(&s_oled_buffer[page * OLED_WIDTH], OLED_WIDTH), "oled", "page data send failed");
        memcpy(&s_oled_last_buffer[page * OLED_WIDTH], &s_oled_buffer[page * OLED_WIDTH], OLED_WIDTH);
    }

    s_oled_last_valid = true;
    return ESP_OK;
}

esp_err_t app_oled_init(void)
{
    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };

    esp_err_t err;

    ESP_RETURN_ON_ERROR(i2c_param_config(APP_I2C_PORT, &cfg), "oled", "i2c param failed");
    err = i2c_driver_install(APP_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    s_oled_addr = APP_OLED_I2C_ADDRESS;
    if (oled_send_command(0xAE) != ESP_OK) {
        s_oled_addr = 0x3D;
        ESP_RETURN_ON_ERROR(oled_send_command(0xAE), "oled", "display off failed");
    }
#if APP_OLED_DRIVER_SH1106
    ESP_RETURN_ON_ERROR(oled_send_command(0xD5), "oled", "clock divide failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0x80), "oled", "clock divide value failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0xA8), "oled", "mux ratio failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0x3F), "oled", "mux value failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0xD3), "oled", "display offset failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0x00), "oled", "display offset value failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0x40), "oled", "start line failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0xAD), "oled", "dc-dc setup failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0x8B), "oled", "dc-dc enable failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0xA1), "oled", "segment remap failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0xC8), "oled", "com scan dir failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0xDA), "oled", "com pins failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0x12), "oled", "com pins cfg failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0x81), "oled", "contrast failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0x7F), "oled", "contrast value failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0xD9), "oled", "precharge failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0x22), "oled", "precharge value failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0xDB), "oled", "vcom detect failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0x35), "oled", "vcom value failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0xA4), "oled", "resume RAM failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0xA6), "oled", "normal display failed");
#else
    ESP_RETURN_ON_ERROR(oled_send_command(0x20), "oled", "memory mode failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0x00), "oled", "horizontal mode failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0xA8), "oled", "mux ratio failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0x3F), "oled", "mux value failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0xD3), "oled", "display offset failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0x00), "oled", "display offset value failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0x40), "oled", "start line failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0xA1), "oled", "segment remap failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0xC8), "oled", "com scan dir failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0xDA), "oled", "com pins failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0x12), "oled", "com pins cfg failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0x81), "oled", "contrast failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0x7F), "oled", "contrast value failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0xA4), "oled", "resume RAM failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0xA6), "oled", "normal display failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0xD5), "oled", "clock divide failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0x80), "oled", "clock divide value failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0x8D), "oled", "charge pump failed");
    ESP_RETURN_ON_ERROR(oled_send_command(0x14), "oled", "charge pump enable failed");
#endif
    ESP_RETURN_ON_ERROR(oled_send_command(0xAF), "oled", "display on failed");
    oled_clear_buffer();
    memset(s_oled_last_buffer, 0xFF, sizeof(s_oled_last_buffer));
    s_oled_last_valid = false;
    ESP_RETURN_ON_ERROR(oled_flush(), "oled", "flush failed");
    s_oled_ready = true;
    return ESP_OK;
}

esp_err_t app_oled_show_boot(void)
{
    if (!s_oled_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    oled_clear_buffer();
    oled_draw_string(0, 0, "FISH TANK CTRL");
    oled_draw_string(0, 16, "ESP-IDF C");
    oled_draw_string(0, 32, "BOOTING...");
    return oled_flush();
}

static void oled_format_float(char *buffer, size_t size, float value, int digits)
{
    if (isnan(value)) {
        strlcpy(buffer, "--", size);
        return;
    }

    snprintf(buffer, size, digits >= 2 ? "%.2f" : "%.1f", value);
}

esp_err_t app_oled_render_status(const runtime_data_t *runtime, const char *ip_text)
{
    char line[32];
    char value[16];

    if (!s_oled_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    oled_clear_buffer();

    oled_format_float(value, sizeof(value), runtime->control_temp, 1);
    snprintf(line, sizeof(line), "TC:%sC SET:", value);
    oled_draw_string(0, 0, line);
    oled_format_float(value, sizeof(value), runtime->setpoint, 1);
    oled_draw_string(78, 0, value);

    snprintf(line, sizeof(line), "MODE:%s", oled_state_text(runtime->state));
    oled_draw_string(0, 12, line);
    snprintf(line, sizeof(line), "H%s C%s", runtime->heat_on ? "ON" : "OF", runtime->cool_on ? "ON" : "OF");
    oled_draw_string(78, 12, line);

    oled_format_float(value, sizeof(value), runtime->sensor_a.temperature, 1);
    snprintf(line, sizeof(line), "T1:%s", value);
    oled_draw_string(0, 24, line);
    oled_format_float(value, sizeof(value), runtime->sensor_b.temperature, 1);
    snprintf(line, sizeof(line), "T2:%s", value);
    oled_draw_string(64, 24, line);

    snprintf(line, sizeof(line), "FLT:%s", runtime->last_fault_text);
    oled_draw_string(0, 36, line);

    snprintf(line, sizeof(line), "IP:%s", ip_text ? ip_text : "--");
    oled_draw_string(0, 48, line);

    oled_draw_string(0, 56, runtime->fault_latched ? "HOLD SET UP CLRFLT" : "UPDN SET  HOLD FAST");
    return oled_flush();
}

bool app_oled_is_ready(void)
{
    return s_oled_ready;
}