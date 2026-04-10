#include "app_core.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

#include "app_config.h"
#include "app_ds18b20.h"
#include "app_network.h"
#include "app_oled.h"

static const char *TAG = "app_core";
static SemaphoreHandle_t s_runtime_lock;
static QueueHandle_t s_command_queue;

typedef struct {
    bool heat_on;
    bool cool_on;
    bool fault_latched;
    app_state_t state;
    int16_t sensor_a_tenths;
    int16_t sensor_b_tenths;
    int16_t control_tenths;
    int16_t setpoint_tenths;
    uint16_t alarm_count;
    uint16_t degraded_count;
    uint32_t heat_relay_switch_count;
    uint32_t cool_relay_switch_count;
    char ip_text[APP_IP_TEXT_MAX_LEN];
    char last_fault_text[APP_FAULT_TEXT_MAX_LEN];
} display_snapshot_t;

static display_snapshot_t s_last_display_snapshot;
static bool s_last_display_valid;

runtime_data_t g_runtime = {
    .setpoint = APP_DEFAULT_SETPOINT,
    .hysteresis = APP_DEFAULT_HYSTERESIS,
    .sensor_diff_alarm = APP_DEFAULT_SENSOR_DIFF_ALARM,
    .control_temp = NAN,
    .sensor_diff = NAN,
    .sensor_a = { .temperature = NAN, .last_raw_temperature = NAN },
    .sensor_b = { .temperature = NAN, .last_raw_temperature = NAN },
    .state = APP_STATE_INIT,
    .buzzer_enabled = APP_DEFAULT_BUZZER_ENABLED,
    .last_fault_code = APP_FAULT_NONE,
    .wifi_ssid = APP_WIFI_SSID,
    .wifi_password = APP_WIFI_PASSWORD,
    .ip_text = "--",
    .last_fault_text = "NONE",
};

static app_ds18b20_t s_sensor_a_dev;
static app_ds18b20_t s_sensor_b_dev;

static bool read_key_pressed(gpio_num_t pin)
{
    return gpio_get_level(pin) == 0;
}

static int16_t display_temp_to_tenths(float value)
{
    if (isnan(value)) {
        return (int16_t)-32768;
    }
    return (int16_t)lroundf(value * 10.0f);
}

static void build_display_snapshot(display_snapshot_t *snapshot)
{
    snapshot->heat_on = g_runtime.heat_on;
    snapshot->cool_on = g_runtime.cool_on;
    snapshot->fault_latched = g_runtime.fault_latched;
    snapshot->state = g_runtime.state;
    snapshot->sensor_a_tenths = display_temp_to_tenths(g_runtime.sensor_a.temperature);
    snapshot->sensor_b_tenths = display_temp_to_tenths(g_runtime.sensor_b.temperature);
    snapshot->control_tenths = display_temp_to_tenths(g_runtime.control_temp);
    snapshot->setpoint_tenths = display_temp_to_tenths(g_runtime.setpoint);
    snapshot->alarm_count = g_runtime.alarm_count;
    snapshot->degraded_count = g_runtime.degraded_count;
    snapshot->heat_relay_switch_count = g_runtime.heat_relay_switch_count;
    snapshot->cool_relay_switch_count = g_runtime.cool_relay_switch_count;
    strlcpy(snapshot->ip_text, g_runtime.ip_text, sizeof(snapshot->ip_text));
    strlcpy(snapshot->last_fault_text, g_runtime.last_fault_text, sizeof(snapshot->last_fault_text));
}

static bool display_snapshot_equal(const display_snapshot_t *a, const display_snapshot_t *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

uint32_t app_millis(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

float app_clamp_float(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

const char *app_state_to_text(app_state_t state)
{
    switch (state) {
        case APP_STATE_INIT:
            return "INIT";
        case APP_STATE_IDLE:
            return "IDLE";
        case APP_STATE_HEATING:
            return "HEATING";
        case APP_STATE_COOLING:
            return "COOLING";
        case APP_STATE_DEGRADED:
            return "DEGRADED";
        case APP_STATE_ALARM:
            return "ALARM";
        case APP_STATE_FAULT_STOP:
            return "FAULT_STOP";
        default:
            return "UNKNOWN";
    }
}

const char *app_fault_code_to_text(app_fault_code_t code)
{
    switch (code) {
        case APP_FAULT_NONE:
            return "NONE";
        case APP_FAULT_SENSOR_A_FAILED:
            return "SNS_A";
        case APP_FAULT_SENSOR_B_FAILED:
            return "SNS_B";
        case APP_FAULT_BOTH_SENSORS_FAILED:
            return "SNS_AB";
        case APP_FAULT_SENSOR_MISMATCH:
            return "MISMATCH";
        case APP_FAULT_WIFI_DISCONNECTED:
            return "WIFI";
        case APP_FAULT_CONTROL_TEMP_INVALID:
            return "TEMP_INV";
        case APP_FAULT_LOW_TEMP_CUTOFF:
            return "LOW_CUT";
        case APP_FAULT_HIGH_TEMP_CUTOFF:
            return "HIGH_CUT";
        default:
            return "UNKNOWN";
    }
}

const char *app_fault_code_to_description(app_fault_code_t code)
{
    switch (code) {
        case APP_FAULT_NONE:
            return "无故障";
        case APP_FAULT_SENSOR_A_FAILED:
            return "探头A失效";
        case APP_FAULT_SENSOR_B_FAILED:
            return "探头B失效";
        case APP_FAULT_BOTH_SENSORS_FAILED:
            return "双探头均失效";
        case APP_FAULT_SENSOR_MISMATCH:
            return "双探头温差超限";
        case APP_FAULT_WIFI_DISCONNECTED:
            return "WiFi连接中断";
        case APP_FAULT_CONTROL_TEMP_INVALID:
            return "控制温度无效";
        case APP_FAULT_LOW_TEMP_CUTOFF:
            return "控制温度低于安全下限";
        case APP_FAULT_HIGH_TEMP_CUTOFF:
            return "控制温度高于安全上限";
        default:
            return "未知故障";
    }
}

void app_format_float(char *buffer, size_t size, float value, uint8_t digits)
{
    if (isnan(value)) {
        strlcpy(buffer, "--", size);
        return;
    }

    snprintf(buffer, size, digits >= 2 ? "%.2f" : "%.1f", value);
}

void app_log_event(const char *fmt, ...)
{
    char message[APP_EVENT_LOG_TEXT_LEN];
    va_list args;

    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    snprintf(g_runtime.event_log[g_runtime.event_log_head],
             APP_EVENT_LOG_TEXT_LEN,
             "[%lu ms] %.72s",
             (unsigned long)app_millis(),
             message);
    g_runtime.event_log_head = (uint8_t)((g_runtime.event_log_head + 1U) % APP_EVENT_LOG_SIZE);
    if (g_runtime.event_log_count < APP_EVENT_LOG_SIZE) {
        g_runtime.event_log_count++;
    }

    ESP_LOGI(TAG, "%s", message);
}

bool app_core_lock(TickType_t wait_ticks)
{
    return s_runtime_lock != NULL && xSemaphoreTake(s_runtime_lock, wait_ticks) == pdTRUE;
}

void app_core_unlock(void)
{
    if (s_runtime_lock != NULL) {
        xSemaphoreGive(s_runtime_lock);
    }
}

void app_core_get_runtime_snapshot(runtime_data_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    if (app_core_lock(pdMS_TO_TICKS(50))) {
        *snapshot = g_runtime;
        app_core_unlock();
    } else {
        memset(snapshot, 0, sizeof(*snapshot));
    }
}

esp_err_t app_core_post_command(const app_command_t *cmd, TickType_t wait_ticks)
{
    if (cmd == NULL || s_command_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return xQueueSend(s_command_queue, cmd, wait_ticks) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

void app_core_process_pending_commands(void)
{
    app_command_t cmd;

    if (s_command_queue == NULL) {
        return;
    }

    while (xQueueReceive(s_command_queue, &cmd, 0) == pdTRUE) {
        switch (cmd.type) {
            case APP_CMD_UPDATE_SETTINGS:
                g_runtime.setpoint = app_clamp_float(cmd.setpoint, APP_MIN_SETPOINT, APP_MAX_SETPOINT);
                g_runtime.hysteresis = app_clamp_float(cmd.hysteresis, APP_MIN_HYSTERESIS, APP_MAX_HYSTERESIS);
                g_runtime.sensor_diff_alarm = app_clamp_float(cmd.sensor_diff_alarm, APP_MIN_SENSOR_DIFF_ALARM, APP_MAX_SENSOR_DIFF_ALARM);
                g_runtime.buzzer_enabled = cmd.buzzer_enabled;
                if (cmd.update_wifi) {
                    strlcpy(g_runtime.wifi_ssid, cmd.wifi_ssid, sizeof(g_runtime.wifi_ssid));
                    strlcpy(g_runtime.wifi_password, cmd.wifi_password, sizeof(g_runtime.wifi_password));
                    g_runtime.wifi_reconnect_requested = true;
                }
                app_mark_settings_dirty();
                app_core_pulse_buzzer(APP_BUZZ_PULSE_MS);
                app_log_event("Settings updated from queue: setpoint=%.1f hysteresis=%.1f diffAlarm=%.1f wifi=%s",
                              g_runtime.setpoint,
                              g_runtime.hysteresis,
                              g_runtime.sensor_diff_alarm,
                              g_runtime.wifi_ssid);
                break;

            case APP_CMD_SET_SETPOINT:
                g_runtime.setpoint = app_clamp_float(cmd.setpoint, APP_MIN_SETPOINT, APP_MAX_SETPOINT);
                app_mark_settings_dirty();
                app_core_pulse_buzzer(APP_BUZZ_PULSE_MS);
                app_log_event("Setpoint updated from queue: %.1f", g_runtime.setpoint);
                break;

            case APP_CMD_CLEAR_FAULT:
                app_core_clear_fault_latch();
                app_core_pulse_buzzer(APP_BUZZ_PULSE_MS);
                break;

            case APP_CMD_CLEAR_STATS:
                app_core_clear_runtime_statistics();
                app_core_pulse_buzzer(APP_BUZZ_PULSE_MS);
                break;

            case APP_CMD_FACTORY_RESET:
                app_core_factory_reset();
                break;

            default:
                break;
        }
    }
}

static esp_err_t load_nvs_float(nvs_handle_t handle, const char *key, float *value)
{
    size_t size = sizeof(float);
    return nvs_get_blob(handle, key, value, &size);
}

static esp_err_t save_nvs_float(nvs_handle_t handle, const char *key, float value)
{
    return nvs_set_blob(handle, key, &value, sizeof(value));
}

static void update_relay_wear_warnings(void)
{
    g_runtime.alarms.heat_relay_wear_warning = g_runtime.heat_relay_switch_count >= APP_RELAY_WARN_SWITCH_COUNT;
    g_runtime.alarms.cool_relay_wear_warning = g_runtime.cool_relay_switch_count >= APP_RELAY_WARN_SWITCH_COUNT;
}

void app_mark_settings_dirty(void)
{
    g_runtime.settings_dirty = true;
    g_runtime.last_settings_changed_ms = app_millis();
}

void app_mark_relay_stats_dirty(void)
{
    g_runtime.relay_stats_dirty = true;
    g_runtime.last_relay_stats_changed_ms = app_millis();
}

void app_load_settings(void)
{
    nvs_handle_t handle;
    float value = 0.0f;
    uint8_t buzzer_u8 = APP_DEFAULT_BUZZER_ENABLED;
    size_t size;

    if (nvs_open("fish-tank", NVS_READWRITE, &handle) != ESP_OK) {
        app_log_event("NVS open failed, using defaults");
        update_relay_wear_warnings();
        return;
    }

    if (load_nvs_float(handle, "setpoint", &value) == ESP_OK) {
        g_runtime.setpoint = app_clamp_float(value, APP_MIN_SETPOINT, APP_MAX_SETPOINT);
    }
    if (load_nvs_float(handle, "hysteresis", &value) == ESP_OK) {
        g_runtime.hysteresis = app_clamp_float(value, APP_MIN_HYSTERESIS, APP_MAX_HYSTERESIS);
    }
    if (load_nvs_float(handle, "sd_alarm", &value) == ESP_OK) {
        g_runtime.sensor_diff_alarm = app_clamp_float(value, APP_MIN_SENSOR_DIFF_ALARM, APP_MAX_SENSOR_DIFF_ALARM);
    }
    if (nvs_get_u8(handle, "buzzer_en", &buzzer_u8) == ESP_OK) {
        g_runtime.buzzer_enabled = buzzer_u8 != 0;
    }
    (void)nvs_get_u32(handle, "heat_sw", &g_runtime.heat_relay_switch_count);
    (void)nvs_get_u32(handle, "cool_sw", &g_runtime.cool_relay_switch_count);

    size = sizeof(g_runtime.wifi_ssid);
    if (nvs_get_str(handle, "wifi_ssid", g_runtime.wifi_ssid, &size) != ESP_OK || g_runtime.wifi_ssid[0] == '\0') {
        strlcpy(g_runtime.wifi_ssid, APP_WIFI_SSID, sizeof(g_runtime.wifi_ssid));
    }

    size = sizeof(g_runtime.wifi_password);
    if (nvs_get_str(handle, "wifi_pwd", g_runtime.wifi_password, &size) != ESP_OK || g_runtime.wifi_password[0] == '\0') {
        strlcpy(g_runtime.wifi_password, APP_WIFI_PASSWORD, sizeof(g_runtime.wifi_password));
    }

    nvs_close(handle);
    update_relay_wear_warnings();
    app_log_event("Settings loaded: setpoint=%.1f hysteresis=%.1f diffAlarm=%.1f wifi=%s",
                  g_runtime.setpoint,
                  g_runtime.hysteresis,
                  g_runtime.sensor_diff_alarm,
                  g_runtime.wifi_ssid);
}

void app_save_settings_if_needed(void)
{
    bool should_save_settings = g_runtime.settings_dirty &&
                                (app_millis() - g_runtime.last_settings_changed_ms >= APP_SETTINGS_SAVE_DELAY_MS);
    bool should_save_relay_stats = g_runtime.relay_stats_dirty &&
                                   (app_millis() - g_runtime.last_relay_stats_changed_ms >= APP_RELAY_STATS_SAVE_DELAY_MS);
    nvs_handle_t handle;

    if (!should_save_settings && !should_save_relay_stats) {
        return;
    }

    if (nvs_open("fish-tank", NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed while saving");
        return;
    }

    if (should_save_settings) {
        if (save_nvs_float(handle, "setpoint", g_runtime.setpoint) == ESP_OK &&
            save_nvs_float(handle, "hysteresis", g_runtime.hysteresis) == ESP_OK &&
            save_nvs_float(handle, "sd_alarm", g_runtime.sensor_diff_alarm) == ESP_OK &&
            nvs_set_u8(handle, "buzzer_en", g_runtime.buzzer_enabled ? 1 : 0) == ESP_OK &&
            nvs_set_str(handle, "wifi_ssid", g_runtime.wifi_ssid) == ESP_OK &&
            nvs_set_str(handle, "wifi_pwd", g_runtime.wifi_password) == ESP_OK) {
            g_runtime.settings_dirty = false;
        }
    }

    if (should_save_relay_stats) {
        if (nvs_set_u32(handle, "heat_sw", g_runtime.heat_relay_switch_count) == ESP_OK &&
            nvs_set_u32(handle, "cool_sw", g_runtime.cool_relay_switch_count) == ESP_OK) {
            g_runtime.relay_stats_dirty = false;
        }
    }

    nvs_commit(handle);
    nvs_close(handle);
}

static bool relay_level_for(bool enabled)
{
    return enabled ? APP_RELAY_ACTIVE_LEVEL : !APP_RELAY_ACTIVE_LEVEL;
}

void app_core_set_buzzer(bool enabled)
{
    gpio_set_level(PIN_BUZZ_CTRL, enabled ? APP_BUZZER_ACTIVE_LEVEL : !APP_BUZZER_ACTIVE_LEVEL);
}

void app_core_pulse_buzzer(uint32_t duration_ms)
{
    if (!g_runtime.buzzer_enabled) {
        return;
    }

    app_core_set_buzzer(true);
    g_runtime.buzzer_off_ms = app_millis() + duration_ms;
}

void app_core_process_buzzer(void)
{
    if (g_runtime.buzzer_off_ms != 0 && app_millis() >= g_runtime.buzzer_off_ms) {
        app_core_set_buzzer(false);
        g_runtime.buzzer_off_ms = 0;
    }
}

static void set_last_fault(app_fault_code_t code)
{
    g_runtime.last_fault_code = code;
    strlcpy(g_runtime.last_fault_text, app_fault_code_to_text(code), sizeof(g_runtime.last_fault_text));
}

static void update_runtime_totals(void)
{
    uint32_t now = app_millis();
    uint32_t delta;

    if (g_runtime.last_stats_update_ms == 0) {
        g_runtime.last_stats_update_ms = now;
        return;
    }

    delta = now - g_runtime.last_stats_update_ms;
    g_runtime.last_stats_update_ms = now;

    if (g_runtime.heat_on) {
        g_runtime.total_heat_on_ms += delta;
    }
    if (g_runtime.cool_on) {
        g_runtime.total_cool_on_ms += delta;
    }
}

static void set_heat_output(bool enabled);
static void set_cool_output(bool enabled);

static void set_heat_output(bool enabled)
{
    if (enabled && g_runtime.cool_on) {
        set_cool_output(false);
    }
    if (g_runtime.heat_on == enabled) {
        return;
    }

    g_runtime.heat_on = enabled;
    g_runtime.heat_relay_switch_count++;
    app_mark_relay_stats_dirty();
    update_relay_wear_warnings();
    gpio_set_level(PIN_HEAT_CTRL, relay_level_for(enabled));
    g_runtime.heat_state_changed_ms = app_millis();
    app_log_event("Heat output -> %s", enabled ? "ON" : "OFF");
}

static void set_cool_output(bool enabled)
{
    if (enabled && g_runtime.heat_on) {
        set_heat_output(false);
    }
    if (g_runtime.cool_on == enabled) {
        return;
    }

    g_runtime.cool_on = enabled;
    g_runtime.cool_relay_switch_count++;
    app_mark_relay_stats_dirty();
    update_relay_wear_warnings();
    gpio_set_level(PIN_COOL_CTRL, relay_level_for(enabled));
    g_runtime.cool_state_changed_ms = app_millis();
    app_log_event("Cool output -> %s", enabled ? "ON" : "OFF");
}

static void stop_all_outputs(void)
{
    set_heat_output(false);
    set_cool_output(false);
}

static bool outputs_startup_inhibited(void)
{
    bool inhibited = (app_millis() - g_runtime.boot_ms) < APP_OUTPUT_STARTUP_INHIBIT_MS;
    g_runtime.alarms.output_startup_inhibit = inhibited;
    return inhibited;
}

static void update_safety_cutoffs(void)
{
    g_runtime.alarms.low_temp_cutoff = false;
    g_runtime.alarms.high_temp_cutoff = false;

    if (isnan(g_runtime.control_temp)) {
        return;
    }

    g_runtime.alarms.low_temp_cutoff = g_runtime.control_temp <= APP_ABS_LOW_TEMP_CUTOFF;
    g_runtime.alarms.high_temp_cutoff = g_runtime.control_temp >= APP_ABS_HIGH_TEMP_CUTOFF;
}

static void enter_latched_fault(app_fault_code_t code, app_state_t previous_state, const char *reason)
{
    stop_all_outputs();
    g_runtime.fault_latched = true;
    g_runtime.alarms.fault_latched = true;
    g_runtime.state = APP_STATE_FAULT_STOP;
    set_last_fault(code);

    if (previous_state != APP_STATE_FAULT_STOP) {
        g_runtime.fault_stop_count++;
        app_core_pulse_buzzer(APP_BUZZ_PULSE_MS * 3U);
    }
    app_log_event("%s", reason);
}

static bool is_temperature_plausible(float temp)
{
    return temp >= APP_MIN_VALID_TEMP && temp <= APP_MAX_VALID_TEMP;
}

static bool is_large_temperature_jump(const sensor_channel_t *channel, float value)
{
    return !isnan(channel->last_raw_temperature) && fabsf(value - channel->last_raw_temperature) > APP_MAX_SENSOR_STEP_DELTA;
}

static void accept_sensor_sample(sensor_channel_t *channel, float value)
{
    channel->last_raw_temperature = value;
    if (isnan(channel->temperature)) {
        channel->temperature = value;
    } else {
        channel->temperature += (value - channel->temperature) * APP_SENSOR_FILTER_ALPHA;
    }

    channel->fail_count = 0;
    if (channel->recover_count < 255U) {
        channel->recover_count++;
    }
    if (channel->recover_count >= APP_SENSOR_RECOVER_VALID_COUNT) {
        channel->valid = true;
    }
}

static void reject_sensor_sample(sensor_channel_t *channel)
{
    channel->recover_count = 0;
    if (channel->fail_count < 255U) {
        channel->fail_count++;
    }
    if (channel->fail_count >= APP_SENSOR_FAIL_LIMIT) {
        channel->valid = false;
        channel->temperature = NAN;
    }
}


static bool apply_adjust_step(int8_t direction)
{
    g_runtime.setpoint = app_clamp_float(g_runtime.setpoint + ((float)direction * 0.1f), APP_MIN_SETPOINT, APP_MAX_SETPOINT);
    return true;
}
static bool cooling_protected(void)
{
    uint32_t elapsed_off;

    if (g_runtime.cool_on) {
        g_runtime.alarms.compressor_protected = false;
        return false;
    }

    elapsed_off = app_millis() - g_runtime.cool_state_changed_ms;
    g_runtime.alarms.compressor_protected = elapsed_off < APP_COOL_MIN_OFF_MS;
    return g_runtime.alarms.compressor_protected;
}

void app_core_update_sensors(void)
{
    float value_a = NAN;
    float value_b = NAN;
    uint32_t now_ms = app_millis();
    bool valid_a = false;
    bool valid_b = false;
    bool sample_a_ready = false;
    bool sample_b_ready = false;
    esp_err_t sensor_a_err;
    esp_err_t sensor_b_err;
    sensor_channel_t prev_sensor_a;
    sensor_channel_t prev_sensor_b;

    if (!app_core_lock(pdMS_TO_TICKS(20))) {
        return;
    }
    prev_sensor_a = g_runtime.sensor_a;
    prev_sensor_b = g_runtime.sensor_b;
    app_core_unlock();

#if APP_USE_FAKE_SENSOR
    float phase = (float)(app_millis() % 60000U) / 60000.0f;
    value_a = 25.5f + sinf(phase * 6.28318f) * 1.2f;
    value_b = value_a + 0.2f;
    sample_a_ready = true;
    sample_b_ready = true;
    valid_a = true;
    valid_b = true;
#else
    if (s_sensor_a_dev.conversion_pending || s_sensor_b_dev.conversion_pending) {
        if (g_runtime.sensor_conversion_started_ms == 0U) {
            g_runtime.sensor_conversion_started_ms = now_ms;
        }
    }

    sensor_a_err = app_ds18b20_read_temperature(&s_sensor_a_dev, &value_a);
    sensor_b_err = app_ds18b20_read_temperature(&s_sensor_b_dev, &value_b);

    if (sensor_a_err == ESP_OK) {
        sample_a_ready = true;
        valid_a = true;
    } else if (sensor_a_err != ESP_ERR_NOT_FINISHED) {
        sample_a_ready = true;
        valid_a = false;
    }

    if (sensor_b_err == ESP_OK) {
        sample_b_ready = true;
        valid_b = true;
    } else if (sensor_b_err != ESP_ERR_NOT_FINISHED) {
        sample_b_ready = true;
        valid_b = false;
    }
#endif

    if (sample_a_ready && valid_a && (!is_temperature_plausible(value_a) || is_large_temperature_jump(&prev_sensor_a, value_a))) {
        valid_a = false;
    }
    if (sample_b_ready && valid_b && (!is_temperature_plausible(value_b) || is_large_temperature_jump(&prev_sensor_b, value_b))) {
        valid_b = false;
    }

    if (sample_a_ready || sample_b_ready) {
        if (g_runtime.last_sample_ready_ms != 0U) {
            g_runtime.last_sample_cycle_ms = now_ms - g_runtime.last_sample_ready_ms;
            if (g_runtime.last_sample_cycle_ms > g_runtime.max_sample_cycle_ms) {
                g_runtime.max_sample_cycle_ms = g_runtime.last_sample_cycle_ms;
            }
        }
        g_runtime.last_sample_ready_ms = now_ms;
        g_runtime.last_sample_ms = now_ms;
        g_runtime.sensor_conversion_started_ms = now_ms;
    }

    if (!app_core_lock(pdMS_TO_TICKS(20))) {
        return;
    }

    if (sample_a_ready) {
        if (valid_a) {
            accept_sensor_sample(&g_runtime.sensor_a, value_a);
        } else {
            reject_sensor_sample(&g_runtime.sensor_a);
        }
    }

    if (sample_b_ready) {
        if (valid_b) {
            accept_sensor_sample(&g_runtime.sensor_b, value_b);
        } else {
            reject_sensor_sample(&g_runtime.sensor_b);
        }
    }

    g_runtime.alarms.sensor_a_failed = !g_runtime.sensor_a.valid && g_runtime.sensor_a.fail_count >= APP_SENSOR_FAIL_LIMIT;
    g_runtime.alarms.sensor_b_failed = !g_runtime.sensor_b.valid && g_runtime.sensor_b.fail_count >= APP_SENSOR_FAIL_LIMIT;
    g_runtime.alarms.both_sensors_failed = g_runtime.alarms.sensor_a_failed && g_runtime.alarms.sensor_b_failed;

    if (g_runtime.sensor_a.valid && g_runtime.sensor_b.valid) {
        g_runtime.sensor_diff = fabsf(g_runtime.sensor_a.temperature - g_runtime.sensor_b.temperature);
        g_runtime.control_temp = (g_runtime.sensor_a.temperature + g_runtime.sensor_b.temperature) * 0.5f;
        g_runtime.alarms.sensor_mismatch = g_runtime.sensor_diff > g_runtime.sensor_diff_alarm;
    } else if (g_runtime.sensor_a.valid) {
        g_runtime.control_temp = g_runtime.sensor_a.temperature;
        g_runtime.sensor_diff = NAN;
        g_runtime.alarms.sensor_mismatch = false;
    } else if (g_runtime.sensor_b.valid) {
        g_runtime.control_temp = g_runtime.sensor_b.temperature;
        g_runtime.sensor_diff = NAN;
        g_runtime.alarms.sensor_mismatch = false;
    } else {
        g_runtime.control_temp = NAN;
        g_runtime.sensor_diff = NAN;
        g_runtime.alarms.sensor_mismatch = false;
    }

    app_core_unlock();
}

void app_core_update_state_machine(void)
{
    app_state_t previous_state = g_runtime.state;
    float lower_limit;
    float upper_limit;

    g_runtime.alarms.wifi_disconnected = !g_runtime.wifi_connected && !g_runtime.wifi_ap_mode;
    g_runtime.alarms.fault_latched = g_runtime.fault_latched;
    update_safety_cutoffs();

    if (g_runtime.fault_latched) {
        stop_all_outputs();
        g_runtime.state = APP_STATE_FAULT_STOP;
        if (g_runtime.alarms.high_temp_cutoff) {
            set_last_fault(APP_FAULT_HIGH_TEMP_CUTOFF);
        } else if (g_runtime.alarms.low_temp_cutoff) {
            set_last_fault(APP_FAULT_LOW_TEMP_CUTOFF);
        }
        return;
    }

    if (g_runtime.alarms.high_temp_cutoff) {
        enter_latched_fault(APP_FAULT_HIGH_TEMP_CUTOFF, previous_state, "Safety cutoff: control temp too high");
        return;
    }
    if (g_runtime.alarms.low_temp_cutoff) {
        enter_latched_fault(APP_FAULT_LOW_TEMP_CUTOFF, previous_state, "Safety cutoff: control temp too low");
        return;
    }

    if (g_runtime.alarms.both_sensors_failed) {
        set_last_fault(APP_FAULT_BOTH_SENSORS_FAILED);
    } else if (g_runtime.alarms.sensor_a_failed) {
        set_last_fault(APP_FAULT_SENSOR_A_FAILED);
    } else if (g_runtime.alarms.sensor_b_failed) {
        set_last_fault(APP_FAULT_SENSOR_B_FAILED);
    } else if (g_runtime.alarms.sensor_mismatch) {
        set_last_fault(APP_FAULT_SENSOR_MISMATCH);
    } else if (g_runtime.alarms.wifi_disconnected) {
        set_last_fault(APP_FAULT_WIFI_DISCONNECTED);
    } else {
        set_last_fault(APP_FAULT_NONE);
    }

    if (g_runtime.alarms.both_sensors_failed) {
        stop_all_outputs();
        g_runtime.state = APP_STATE_FAULT_STOP;
        if (previous_state != APP_STATE_FAULT_STOP) {
            g_runtime.fault_stop_count++;
            app_core_pulse_buzzer(APP_BUZZ_PULSE_MS * 3U);
            app_log_event("State -> %s", app_state_to_text(g_runtime.state));
        }
        return;
    }

    if (!g_runtime.sensor_a.valid || !g_runtime.sensor_b.valid) {
        g_runtime.state = APP_STATE_DEGRADED;
    } else if (g_runtime.alarms.sensor_mismatch || g_runtime.alarms.wifi_disconnected) {
        g_runtime.state = APP_STATE_ALARM;
    } else if (!g_runtime.heat_on && !g_runtime.cool_on) {
        g_runtime.state = APP_STATE_IDLE;
    }

    if (isnan(g_runtime.control_temp)) {
        stop_all_outputs();
        g_runtime.state = APP_STATE_FAULT_STOP;
        set_last_fault(APP_FAULT_CONTROL_TEMP_INVALID);
        if (previous_state != APP_STATE_FAULT_STOP) {
            g_runtime.fault_stop_count++;
            app_core_pulse_buzzer(APP_BUZZ_PULSE_MS * 3U);
            app_log_event("State -> %s", app_state_to_text(g_runtime.state));
        }
        return;
    }

    if (outputs_startup_inhibited()) {
        stop_all_outputs();
        if (g_runtime.state != APP_STATE_DEGRADED && g_runtime.state != APP_STATE_ALARM) {
            g_runtime.state = APP_STATE_IDLE;
        }
        if (g_runtime.state != previous_state) {
            app_log_event("State -> %s", app_state_to_text(g_runtime.state));
        }
        return;
    }

    lower_limit = g_runtime.setpoint - g_runtime.hysteresis;
    upper_limit = g_runtime.setpoint + g_runtime.hysteresis;

    if (g_runtime.control_temp < lower_limit) {
        if (g_runtime.cool_on && (app_millis() - g_runtime.cool_state_changed_ms) >= APP_COOL_MIN_ON_MS) {
            set_cool_output(false);
        }
        if (!g_runtime.cool_on) {
            if (!g_runtime.heat_on) {
                set_heat_output(true);
            }
            g_runtime.state = previous_state == APP_STATE_DEGRADED ? APP_STATE_DEGRADED : APP_STATE_HEATING;
        }
    } else if (g_runtime.control_temp > upper_limit) {
        if (g_runtime.heat_on && (app_millis() - g_runtime.heat_state_changed_ms) >= APP_HEAT_MIN_ON_MS) {
            set_heat_output(false);
        }
        if (!g_runtime.heat_on) {
            if (!cooling_protected() && !g_runtime.cool_on) {
                set_cool_output(true);
            }
            if (g_runtime.cool_on) {
                g_runtime.state = previous_state == APP_STATE_DEGRADED ? APP_STATE_DEGRADED : APP_STATE_COOLING;
            }
        }
    } else {
        if (g_runtime.heat_on && (app_millis() - g_runtime.heat_state_changed_ms) >= APP_HEAT_MIN_ON_MS) {
            set_heat_output(false);
        }
        if (g_runtime.cool_on && (app_millis() - g_runtime.cool_state_changed_ms) >= APP_COOL_MIN_ON_MS) {
            set_cool_output(false);
        }
        if (g_runtime.state != APP_STATE_DEGRADED && g_runtime.state != APP_STATE_ALARM) {
            g_runtime.state = APP_STATE_IDLE;
        }
    }

    if (g_runtime.state != previous_state) {
        if (g_runtime.state == APP_STATE_ALARM) {
            g_runtime.alarm_count++;
        } else if (g_runtime.state == APP_STATE_DEGRADED) {
            g_runtime.degraded_count++;
        }
        app_log_event("State -> %s", app_state_to_text(g_runtime.state));
    }
}

void app_core_handle_buttons(void)
{
    uint32_t now_ms;
    bool set_pressed;
    bool up_pressed;
    bool down_pressed;
    bool fault_reset_combo;
    bool stats_reset_combo;
    bool factory_reset_hold;
    bool adjusted;

    now_ms = app_millis();
    if (now_ms - g_runtime.last_button_ms < APP_BUTTON_SCAN_INTERVAL_MS) {
        return;
    }
    g_runtime.last_button_ms = now_ms;

    set_pressed = read_key_pressed(PIN_KEY_SET);
    up_pressed = read_key_pressed(PIN_KEY_UP);
    down_pressed = read_key_pressed(PIN_KEY_DOWN);

    fault_reset_combo = set_pressed && up_pressed && !down_pressed;
    stats_reset_combo = set_pressed && down_pressed && !up_pressed;
    factory_reset_hold = set_pressed && !up_pressed && !down_pressed &&
                         (now_ms - g_runtime.key_set_pressed_ms) >= APP_FACTORY_RESET_LONG_PRESS_MS;

    if (set_pressed && !g_runtime.key_set_prev) {
        g_runtime.key_set_pressed_ms = now_ms;
        g_runtime.maintenance_combo_handled = false;
        g_runtime.key_set_combo_seen = false;
    }

    if (set_pressed && (up_pressed || down_pressed)) {
        g_runtime.key_set_combo_seen = true;
    }

    if (!set_pressed) {
        g_runtime.maintenance_combo_handled = false;
    }

    if (!g_runtime.maintenance_combo_handled &&
        (now_ms - g_runtime.key_set_pressed_ms) >= APP_LONG_PRESS_MS) {
        if (fault_reset_combo) {
            app_core_clear_fault_latch();
            app_core_pulse_buzzer(APP_BUZZ_PULSE_MS * 2U);
            g_runtime.maintenance_combo_handled = true;
        } else if (stats_reset_combo) {
            app_core_clear_runtime_statistics();
            app_core_pulse_buzzer(APP_BUZZ_PULSE_MS * 2U);
            g_runtime.maintenance_combo_handled = true;
        } else if (factory_reset_hold) {
            app_core_factory_reset();
            g_runtime.maintenance_combo_handled = true;
        }
    }

    if (!set_pressed && g_runtime.key_set_prev) {
        if (g_runtime.maintenance_combo_handled) {
            g_runtime.key_set_prev = set_pressed;
            g_runtime.key_up_prev = up_pressed;
            g_runtime.key_down_prev = down_pressed;
            return;
        }

        if (!g_runtime.key_set_combo_seen &&
            (now_ms - g_runtime.key_set_pressed_ms) < APP_LONG_PRESS_MS) {
            g_runtime.buzzer_enabled = !g_runtime.buzzer_enabled;
            app_mark_settings_dirty();
            if (g_runtime.buzzer_enabled) {
                app_core_pulse_buzzer(APP_BUZZ_PULSE_MS);
            }
            app_log_event("Buzzer toggled by key: %s", g_runtime.buzzer_enabled ? "ON" : "OFF");
        }
    }

    if (up_pressed && !g_runtime.key_up_prev && !set_pressed && !down_pressed) {
        g_runtime.key_up_pressed_ms = now_ms;
        g_runtime.key_up_long_adjust_active = false;
        adjusted = apply_adjust_step(+1);
        if (adjusted) {
            app_mark_settings_dirty();
            app_core_pulse_buzzer(APP_BUZZ_PULSE_MS);
        }
    }

    if (down_pressed && !g_runtime.key_down_prev && !set_pressed && !up_pressed) {
        g_runtime.key_down_pressed_ms = now_ms;
        g_runtime.key_down_long_adjust_active = false;
        adjusted = apply_adjust_step(-1);
        if (adjusted) {
            app_mark_settings_dirty();
            app_core_pulse_buzzer(APP_BUZZ_PULSE_MS);
        }
    }

    if (up_pressed && !set_pressed && !down_pressed &&
        (now_ms - g_runtime.key_up_pressed_ms) >= APP_LONG_PRESS_MS) {
        if (!g_runtime.key_up_long_adjust_active) {
            g_runtime.key_up_long_adjust_active = true;
            g_runtime.last_fast_adjust_ms = now_ms;
        }
        if ((now_ms - g_runtime.last_fast_adjust_ms) >= APP_FAST_ADJUST_INTERVAL_MS) {
            if (apply_adjust_step(+1)) {
                g_runtime.fast_adjust_pending_commit = true;
            }
            g_runtime.last_fast_adjust_ms = now_ms;
        }
    }

    if (down_pressed && !set_pressed && !up_pressed &&
        (now_ms - g_runtime.key_down_pressed_ms) >= APP_LONG_PRESS_MS) {
        if (!g_runtime.key_down_long_adjust_active) {
            g_runtime.key_down_long_adjust_active = true;
            g_runtime.last_fast_adjust_ms = now_ms;
        }
        if ((now_ms - g_runtime.last_fast_adjust_ms) >= APP_FAST_ADJUST_INTERVAL_MS) {
            if (apply_adjust_step(-1)) {
                g_runtime.fast_adjust_pending_commit = true;
            }
            g_runtime.last_fast_adjust_ms = now_ms;
        }
    }

    if ((!up_pressed && g_runtime.key_up_prev && g_runtime.key_up_long_adjust_active) ||
        (!down_pressed && g_runtime.key_down_prev && g_runtime.key_down_long_adjust_active)) {
        g_runtime.key_up_long_adjust_active = false;
        g_runtime.key_down_long_adjust_active = false;
        if (g_runtime.fast_adjust_pending_commit) {
            app_mark_settings_dirty();
            app_core_pulse_buzzer(APP_BUZZ_PULSE_MS);
            g_runtime.fast_adjust_pending_commit = false;
        }
    }

    g_runtime.key_set_prev = set_pressed;
    g_runtime.key_up_prev = up_pressed;
    g_runtime.key_down_prev = down_pressed;
}

void app_core_update_display(void)
{
    uint32_t now_ms;
    display_snapshot_t current_snapshot;

    update_runtime_totals();

    if (!app_oled_is_ready()) {
        return;
    }
    now_ms = app_millis();
    if (now_ms - g_runtime.last_display_ms < APP_DISPLAY_UPDATE_INTERVAL_MS) {
        return;
    }

    build_display_snapshot(&current_snapshot);
    if (s_last_display_valid && display_snapshot_equal(&current_snapshot, &s_last_display_snapshot)) {
        return;
    }

    g_runtime.last_display_ms = now_ms;
    if (app_oled_render_status(&g_runtime, g_runtime.ip_text) == ESP_OK) {
        s_last_display_snapshot = current_snapshot;
        s_last_display_valid = true;
    }
}

void app_core_log_periodic_status(void)
{
    uint32_t conversion_age_ms = 0;

    if (app_millis() - g_runtime.last_debug_ms < APP_STATUS_LOG_INTERVAL_MS) {
        return;
    }

    g_runtime.last_debug_ms = app_millis();
    if (g_runtime.sensor_conversion_started_ms != 0U) {
        conversion_age_ms = app_millis() - g_runtime.sensor_conversion_started_ms;
    }
    ESP_LOGI(TAG,
             "STATE=%s T1=%.2f T2=%.2f TC=%.2f SET=%.1f HYS=%.1f HEAT=%s COOL=%s WIFI=%s IP=%s SAMPLE=%lums MAX=%lums CONV_AGE=%lums",
             app_state_to_text(g_runtime.state),
             g_runtime.sensor_a.temperature,
             g_runtime.sensor_b.temperature,
             g_runtime.control_temp,
             g_runtime.setpoint,
             g_runtime.hysteresis,
             g_runtime.heat_on ? "ON" : "OFF",
             g_runtime.cool_on ? "ON" : "OFF",
             g_runtime.wifi_ap_mode ? "AP" : (g_runtime.wifi_connected ? "OK" : "DISC"),
             app_network_current_ip_address(),
             (unsigned long)g_runtime.last_sample_cycle_ms,
             (unsigned long)g_runtime.max_sample_cycle_ms,
             (unsigned long)conversion_age_ms);
}

void app_core_clear_fault_latch(void)
{
    g_runtime.fault_latched = false;
    g_runtime.alarms.fault_latched = false;
    g_runtime.alarms.low_temp_cutoff = false;
    g_runtime.alarms.high_temp_cutoff = false;
    set_last_fault(APP_FAULT_NONE);
    if (g_runtime.state == APP_STATE_FAULT_STOP) {
        g_runtime.state = APP_STATE_IDLE;
    }
    app_log_event("Fault latch cleared by user");
}

void app_core_clear_runtime_statistics(void)
{
    nvs_handle_t handle;

    g_runtime.total_heat_on_ms = 0;
    g_runtime.total_cool_on_ms = 0;
    g_runtime.alarm_count = 0;
    g_runtime.degraded_count = 0;
    g_runtime.fault_stop_count = 0;
    g_runtime.heat_relay_switch_count = 0;
    g_runtime.cool_relay_switch_count = 0;
    g_runtime.relay_stats_dirty = false;
    g_runtime.last_stats_update_ms = app_millis();
    update_relay_wear_warnings();

    if (nvs_open("fish-tank", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u32(handle, "heat_sw", 0);
        nvs_set_u32(handle, "cool_sw", 0);
        nvs_commit(handle);
        nvs_close(handle);
    }

    app_log_event("Runtime statistics cleared by user");
}

void app_core_factory_reset(void)
{
    nvs_handle_t handle;
    esp_err_t err;
    bool nvs_cleared = false;

    stop_all_outputs();
    app_core_set_buzzer(false);
    g_runtime.buzzer_off_ms = 0;

    if (nvs_open("fish-tank", NVS_READWRITE, &handle) == ESP_OK) {
        err = nvs_erase_all(handle);
        if (err == ESP_OK) {
            err = nvs_commit(handle);
            nvs_cleared = err == ESP_OK;
        }
        nvs_close(handle);
    }

    g_runtime.setpoint = APP_DEFAULT_SETPOINT;
    g_runtime.hysteresis = APP_DEFAULT_HYSTERESIS;
    g_runtime.sensor_diff_alarm = APP_DEFAULT_SENSOR_DIFF_ALARM;
    g_runtime.buzzer_enabled = APP_DEFAULT_BUZZER_ENABLED;
    strlcpy(g_runtime.wifi_ssid, APP_WIFI_SSID, sizeof(g_runtime.wifi_ssid));
    strlcpy(g_runtime.wifi_password, APP_WIFI_PASSWORD, sizeof(g_runtime.wifi_password));
    g_runtime.wifi_reconnect_requested = true;

    g_runtime.sensor_a.temperature = NAN;
    g_runtime.sensor_a.last_raw_temperature = NAN;
    g_runtime.sensor_a.valid = false;
    g_runtime.sensor_a.fail_count = 0;
    g_runtime.sensor_a.recover_count = 0;
    g_runtime.sensor_b.temperature = NAN;
    g_runtime.sensor_b.last_raw_temperature = NAN;
    g_runtime.sensor_b.valid = false;
    g_runtime.sensor_b.fail_count = 0;
    g_runtime.sensor_b.recover_count = 0;
    g_runtime.control_temp = NAN;
    g_runtime.sensor_diff = NAN;

    memset(&g_runtime.alarms, 0, sizeof(g_runtime.alarms));
    g_runtime.fault_latched = false;
    g_runtime.state = APP_STATE_IDLE;
    set_last_fault(APP_FAULT_NONE);

    g_runtime.total_heat_on_ms = 0;
    g_runtime.total_cool_on_ms = 0;
    g_runtime.alarm_count = 0;
    g_runtime.degraded_count = 0;
    g_runtime.fault_stop_count = 0;
    g_runtime.heat_relay_switch_count = 0;
    g_runtime.cool_relay_switch_count = 0;
    g_runtime.relay_stats_dirty = false;
    g_runtime.settings_dirty = false;
    g_runtime.last_stats_update_ms = app_millis();
    update_relay_wear_warnings();

    g_runtime.key_set_combo_seen = false;
    g_runtime.key_up_long_adjust_active = false;
    g_runtime.key_down_long_adjust_active = false;
    g_runtime.fast_adjust_pending_commit = false;

    memset(g_runtime.event_log, 0, sizeof(g_runtime.event_log));
    g_runtime.event_log_count = 0;
    g_runtime.event_log_head = 0;

    app_log_event(nvs_cleared ? "Factory reset completed" : "Factory reset completed (NVS erase partial)");
    app_core_pulse_buzzer(APP_BUZZ_PULSE_MS * 3U);
}

static esp_err_t init_pins(void)
{
    gpio_config_t output_cfg = {
        .pin_bit_mask = (1ULL << PIN_HEAT_CTRL) | (1ULL << PIN_COOL_CTRL) | (1ULL << PIN_BUZZ_CTRL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config_t input_cfg = {
        .pin_bit_mask = (1ULL << PIN_KEY_SET) | (1ULL << PIN_KEY_UP) | (1ULL << PIN_KEY_DOWN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&output_cfg), TAG, "gpio output init failed");
    ESP_RETURN_ON_ERROR(gpio_config(&input_cfg), TAG, "gpio input init failed");
    gpio_set_level(PIN_HEAT_CTRL, relay_level_for(false));
    gpio_set_level(PIN_COOL_CTRL, relay_level_for(false));
    app_core_set_buzzer(false);
    return ESP_OK;
}

static esp_err_t init_sensors(void)
{
    ESP_RETURN_ON_ERROR(app_ds18b20_init(&s_sensor_a_dev, PIN_TEMP_A), TAG, "sensor A init failed");
    ESP_RETURN_ON_ERROR(app_ds18b20_init(&s_sensor_b_dev, PIN_TEMP_B), TAG, "sensor B init failed");
    return ESP_OK;
}

esp_err_t app_core_init(void)
{
    esp_err_t err;

    if (s_runtime_lock == NULL) {
        s_runtime_lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_runtime_lock != NULL, ESP_ERR_NO_MEM, TAG, "runtime lock create failed");
    }
    if (s_command_queue == NULL) {
        s_command_queue = xQueueCreate(APP_CMD_QUEUE_LEN, sizeof(app_command_t));
        ESP_RETURN_ON_FALSE(s_command_queue != NULL, ESP_ERR_NO_MEM, TAG, "command queue create failed");
    }

    g_runtime.boot_ms = app_millis();
    g_runtime.heat_state_changed_ms = g_runtime.boot_ms;
    g_runtime.cool_state_changed_ms = g_runtime.boot_ms;
    strlcpy(g_runtime.ip_text, "--", sizeof(g_runtime.ip_text));
    set_last_fault(APP_FAULT_NONE);

    app_load_settings();
    ESP_RETURN_ON_ERROR(init_pins(), TAG, "pin init failed");
    ESP_RETURN_ON_ERROR(init_sensors(), TAG, "sensor init failed");

    err = app_oled_init();
    if (err == ESP_OK) {
        (void)app_oled_show_boot();
    } else {
        ESP_LOGW(TAG, "OLED init failed: %s", esp_err_to_name(err));
    }

    g_runtime.state = APP_STATE_IDLE;
    app_log_event("Core init finished");
    return ESP_OK;
}