#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "nvs_flash.h"

#include "app_config.h"
#include "app_core.h"
#include "app_network.h"

static const char *TAG = "fish_tank_idf";

static void app_log_effective_hw_config(void)
{
    uint32_t flash_size = 0;
    esp_err_t flash_err = esp_flash_get_size(NULL, &flash_size);
    size_t psram_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    bool psram_inited = psram_size > 0;
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t spiram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t spiram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    esp_partition_iterator_t it = NULL;

    if (flash_err == ESP_OK) {
        ESP_LOGI(TAG,
                 "Flash check: detected=%lu bytes (%.1f MB), sdkconfig=%s",
                 (unsigned long)flash_size,
                 (double)flash_size / (1024.0 * 1024.0),
                 CONFIG_ESPTOOLPY_FLASHSIZE);
    } else {
        ESP_LOGW(TAG, "Flash check failed: %s, sdkconfig=%s", esp_err_to_name(flash_err), CONFIG_ESPTOOLPY_FLASHSIZE);
    }

    ESP_LOGI(TAG,
             "PSRAM check: enabled=%s inited=%s size=%lu bytes (%.1f MB)",
#if CONFIG_SPIRAM
             "yes",
#else
             "no",
#endif
             psram_inited ? "yes" : "no",
             (unsigned long)psram_size,
             (double)psram_size / (1024.0 * 1024.0));

    ESP_LOGI(TAG,
             "Heap check: internal_free=%lu internal_largest=%lu spiram_free=%lu spiram_largest=%lu",
             (unsigned long)internal_free,
             (unsigned long)internal_largest,
             (unsigned long)spiram_free,
             (unsigned long)spiram_largest);

    ESP_LOGI(TAG, "Partition table check: file=%s", CONFIG_PARTITION_TABLE_FILENAME);
    it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it != NULL) {
        const esp_partition_t *p = esp_partition_get(it);
        if (p != NULL) {
            ESP_LOGI(TAG,
                     "Partition: label=%s type=%u subtype=0x%02x offset=0x%lx size=0x%lx",
                     p->label,
                     (unsigned int)p->type,
                     (unsigned int)p->subtype,
                     (unsigned long)p->address,
                     (unsigned long)p->size);
        }
        it = esp_partition_next(it);
    }
}

static void app_control_task(void *arg)
{
    (void)arg;

    while (true) {
        if (app_core_lock(pdMS_TO_TICKS(50))) {
            app_core_process_pending_commands();
            app_core_handle_buttons();
            app_core_process_buzzer();
            app_save_settings_if_needed();
            app_core_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void app_sampling_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        app_core_update_sensors();

        if (app_core_lock(pdMS_TO_TICKS(50))) {
            app_core_update_state_machine();
            app_core_unlock();
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_SENSOR_POLL_INTERVAL_MS));
    }
}

static void app_network_task(void *arg)
{
    (void)arg;

    while (true) {
        app_network_maintain();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void app_display_task(void *arg)
{
    (void)arg;

    while (true) {
        if (app_core_lock(pdMS_TO_TICKS(100))) {
            app_core_update_display();
            app_core_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void app_log_task(void *arg)
{
    (void)arg;

    while (true) {
        if (app_core_lock(pdMS_TO_TICKS(100))) {
            app_core_log_periodic_status();
            app_core_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "Booting fish tank controller (ESP-IDF)");
    app_log_effective_hw_config();
    ESP_ERROR_CHECK(app_core_init());
    ESP_ERROR_CHECK(app_network_init());

    xTaskCreate(app_control_task, "app_ctrl", 4096, NULL, 7, NULL);
    xTaskCreate(app_sampling_task, "app_sample", 4096, NULL, 8, NULL);
    xTaskCreate(app_network_task, "app_net", 4096, NULL, 6, NULL);
    xTaskCreate(app_display_task, "app_disp", 4096, NULL, 5, NULL);
    xTaskCreate(app_log_task, "app_log", 3072, NULL, 4, NULL);

    vTaskDelete(NULL);
}