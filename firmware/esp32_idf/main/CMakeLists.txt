idf_component_register(SRCS "main.c" "app_ds18b20.c" "app_oled.c" "app_core.c" "app_network.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_server esp_timer driver esp_driver_gpio esp_driver_i2c spi_flash)
