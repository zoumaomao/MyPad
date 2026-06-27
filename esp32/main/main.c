#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "ui.h"
#include "serial_comm.h"
#include "display.h"

static const char *TAG = "mypad";

app_slot_t app_slots[SLOT_COUNT] = {0};
monitor_data_t monitor_data = {0};
finance_item_t finance_items[MAX_FINANCE] = {0};
int finance_count = 0;
news_item_t news_items[MAX_NEWS] = {0};
int news_count = 0;
calendar_item_t calendar_items[MAX_CALENDAR] = {0};
int calendar_count = 0;

#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "driver/gpio.h"

static void init_sdcard(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    sdmmc_card_t *card;
    const char mount_point[] = "/sdcard";

    ESP_LOGI(TAG, "Initializing SD card");

    // Enable SD card power via GPIO46
    gpio_set_direction(GPIO_NUM_46, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_46, 1);
    vTaskDelay(pdMS_TO_TICKS(100)); // wait for power to stabilize

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.clk = GPIO_NUM_43;
    slot_config.cmd = GPIO_NUM_44;
    slot_config.d0 = GPIO_NUM_39;
    slot_config.d1 = GPIO_NUM_40;
    slot_config.d2 = GPIO_NUM_41;
    slot_config.d3 = GPIO_NUM_42;
    slot_config.cd = GPIO_NUM_45;

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card (%s)", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "SD card mounted at %s", mount_point);
}

void app_main(void)
{
    ESP_LOGI(TAG, "MyPad starting...");
    nvs_flash_init();
    
    init_sdcard();
    
    serial_comm_init();

    display_init();
    ui_init();
    display_start_lvgl_task();

    serial_comm_send("READY");
    ESP_LOGI(TAG, "MyPad ready");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
