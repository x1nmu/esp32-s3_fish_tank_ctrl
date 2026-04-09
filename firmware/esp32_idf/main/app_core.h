#ifndef APP_CORE_H
#define APP_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"

#include "esp_err.h"

#include "app_state.h"

typedef enum {
	APP_CMD_UPDATE_SETTINGS = 0,
	APP_CMD_SET_SETPOINT,
	APP_CMD_CLEAR_FAULT,
	APP_CMD_CLEAR_STATS,
} app_cmd_type_t;

typedef struct {
	app_cmd_type_t type;
	float setpoint;
	float hysteresis;
	float sensor_diff_alarm;
	bool buzzer_enabled;
	bool update_wifi;
	char wifi_ssid[APP_WIFI_SSID_MAX_LEN];
	char wifi_password[APP_WIFI_PASSWORD_MAX_LEN];
} app_command_t;

extern runtime_data_t g_runtime;

uint32_t app_millis(void);
float app_clamp_float(float value, float min_value, float max_value);
const char *app_state_to_text(app_state_t state);
const char *app_fault_code_to_text(app_fault_code_t code);
const char *app_fault_code_to_description(app_fault_code_t code);
void app_format_float(char *buffer, size_t size, float value, uint8_t digits);
void app_log_event(const char *fmt, ...);
bool app_core_lock(TickType_t wait_ticks);
void app_core_unlock(void);
void app_core_get_runtime_snapshot(runtime_data_t *snapshot);
esp_err_t app_core_post_command(const app_command_t *cmd, TickType_t wait_ticks);
void app_core_process_pending_commands(void);
void app_mark_settings_dirty(void);
void app_mark_relay_stats_dirty(void);
void app_load_settings(void);
void app_save_settings_if_needed(void);
esp_err_t app_core_init(void);
void app_core_handle_buttons(void);
void app_core_update_sensors(void);
void app_core_update_state_machine(void);
void app_core_update_display(void);
void app_core_log_periodic_status(void);
void app_core_process_buzzer(void);
void app_core_set_buzzer(bool enabled);
void app_core_pulse_buzzer(uint32_t duration_ms);
void app_core_clear_fault_latch(void);
void app_core_clear_runtime_statistics(void);

#endif