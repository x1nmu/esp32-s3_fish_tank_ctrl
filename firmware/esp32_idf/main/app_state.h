#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"

typedef enum {
    APP_STATE_INIT = 0,
    APP_STATE_IDLE,
    APP_STATE_HEATING,
    APP_STATE_COOLING,
    APP_STATE_DEGRADED,
    APP_STATE_ALARM,
    APP_STATE_FAULT_STOP,
} app_state_t;

typedef enum {
    APP_FAULT_NONE = 0,
    APP_FAULT_SENSOR_A_FAILED,
    APP_FAULT_SENSOR_B_FAILED,
    APP_FAULT_BOTH_SENSORS_FAILED,
    APP_FAULT_SENSOR_MISMATCH,
    APP_FAULT_WIFI_DISCONNECTED,
    APP_FAULT_CONTROL_TEMP_INVALID,
    APP_FAULT_LOW_TEMP_CUTOFF,
    APP_FAULT_HIGH_TEMP_CUTOFF,
} app_fault_code_t;

typedef enum {
    APP_MENU_SETPOINT = 0,
    APP_MENU_HYSTERESIS,
    APP_MENU_SENSOR_DIFF_ALARM,
    APP_MENU_BUZZER,
    APP_MENU_COUNT,
} app_menu_item_t;

typedef struct {
    float temperature;
    float last_raw_temperature;
    bool valid;
    uint8_t fail_count;
    uint8_t recover_count;
} sensor_channel_t;

typedef struct {
    bool sensor_a_failed;
    bool sensor_b_failed;
    bool both_sensors_failed;
    bool sensor_mismatch;
    bool wifi_disconnected;
    bool low_temp_cutoff;
    bool high_temp_cutoff;
    bool fault_latched;
    bool heat_relay_wear_warning;
    bool cool_relay_wear_warning;
    bool compressor_protected;
    bool output_startup_inhibit;
} alarm_flags_t;

typedef struct {
    float setpoint;
    float hysteresis;
    float sensor_diff_alarm;
    float control_temp;
    float sensor_diff;
    sensor_channel_t sensor_a;
    sensor_channel_t sensor_b;
    alarm_flags_t alarms;
    app_state_t state;
    bool heat_on;
    bool cool_on;
    bool buzzer_enabled;
    bool show_second_page;
    bool local_menu_active;
    bool wifi_reconnect_requested;
    bool wifi_ap_mode;
    bool settings_dirty;
    bool relay_stats_dirty;
    bool maintenance_combo_handled;
    bool key_set_combo_seen;
    bool key_set_prev;
    bool key_up_prev;
    bool key_down_prev;
    bool wifi_connected;
    bool fault_latched;
    uint8_t menu_index;
    uint32_t last_sample_ms;
    uint32_t last_display_ms;
    uint32_t last_button_ms;
    uint32_t last_debug_ms;
    uint32_t last_settings_changed_ms;
    uint32_t last_relay_stats_changed_ms;
    uint32_t heat_state_changed_ms;
    uint32_t cool_state_changed_ms;
    uint32_t last_stats_update_ms;
    uint32_t buzzer_off_ms;
    uint32_t boot_ms;
    uint32_t key_set_pressed_ms;
    uint32_t key_up_pressed_ms;
    uint32_t key_down_pressed_ms;
    uint32_t last_fast_adjust_ms;
    uint32_t last_wifi_retry_ms;
    uint32_t sensor_conversion_started_ms;
    uint32_t last_sample_cycle_ms;
    uint32_t max_sample_cycle_ms;
    uint32_t last_sample_ready_ms;
    uint32_t wifi_ap_started_ms;
    uint32_t total_heat_on_ms;
    uint32_t total_cool_on_ms;
    uint16_t alarm_count;
    uint16_t degraded_count;
    uint16_t fault_stop_count;
    uint32_t heat_relay_switch_count;
    uint32_t cool_relay_switch_count;
    app_fault_code_t last_fault_code;
    char wifi_ssid[APP_WIFI_SSID_MAX_LEN];
    char wifi_password[APP_WIFI_PASSWORD_MAX_LEN];
    char wifi_ap_ssid[APP_WIFI_AP_SSID_MAX_LEN];
    char ip_text[APP_IP_TEXT_MAX_LEN];
    char last_fault_text[APP_FAULT_TEXT_MAX_LEN];
    char event_log[APP_EVENT_LOG_SIZE][APP_EVENT_LOG_TEXT_LEN];
    uint8_t event_log_count;
    uint8_t event_log_head;
    bool key_up_long_adjust_active;
    bool key_down_long_adjust_active;
    bool fast_adjust_pending_commit;
} runtime_data_t;

#endif

