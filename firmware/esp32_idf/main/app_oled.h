#ifndef APP_OLED_H
#define APP_OLED_H

#include "esp_err.h"

#include "app_state.h"

esp_err_t app_oled_init(void);
esp_err_t app_oled_show_boot(void);
esp_err_t app_oled_render_status(const runtime_data_t *runtime, const char *ip_text);
bool app_oled_is_ready(void);

#endif
