#ifndef APP_NETWORK_H
#define APP_NETWORK_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t app_network_init(void);
void app_network_maintain(void);
esp_err_t app_network_connect_wifi(bool force_reconnect);
const char *app_network_current_ip_address(void);
const char *app_network_current_wifi_label(void);
uint16_t app_network_ap_client_count(void);

#endif