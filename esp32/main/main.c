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
index_item_t index_items[MAX_INDICES] = {0};
int index_count = 0;

#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "driver/gpio.h"

static bool init_sdcard(void)
{
    const char mount_point[] = "/sdcard";

    ESP_LOGI(TAG, "Mounting internal flash storage...");

    // 使用内部 flash 的 storage FAT 分区（13.2MB，无需 SD 卡）
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "storage"
    );
    if (part) {
        wl_handle_t wl_handle;
        esp_vfs_fat_mount_config_t mount_config = {
            .format_if_mount_failed = true,
            .max_files = 5,
            .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
        };
        esp_err_t ret = esp_vfs_fat_spiflash_mount_rw_wl(
            mount_point, "storage", &mount_config, &wl_handle
        );
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Internal storage mounted at %s (%.1fMB free)", mount_point,
                     (float)(part->size - 0xCF0000 + part->size) / (1024 * 1024));
            return true;
        }
        ESP_LOGW(TAG, "Internal storage mount failed (%s), trying SD card...", esp_err_to_name(ret));
    } else {
        ESP_LOGW(TAG, "No 'storage' partition found, trying SD card...");
    }

    // 回退：SD 卡
    gpio_set_direction(GPIO_NUM_46, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_46, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

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

    esp_vfs_fat_sdmmc_mount_config_t sd_mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    sdmmc_card_t *card;

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &sd_mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed (%s), trying format...", esp_err_to_name(ret));
        host.max_freq_khz = SDMMC_FREQ_PROBING;
        sd_mount_config.format_if_mount_failed = true;
        ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &sd_mount_config, &card);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SD card mount failed (%s)", esp_err_to_name(ret));
            return false;
        }
        ESP_LOGI(TAG, "SD card formatted and mounted at %s", mount_point);
    } else {
        ESP_LOGI(TAG, "SD card mounted at %s", mount_point);
    }
    return true;
}

bool sdcard_ready = false;

static void sdcard_task(void *arg)
{
    sdcard_ready = init_sdcard();
    ESP_LOGI(TAG, "SD card init done, ready=%d", sdcard_ready);
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "MyPad starting...");
    nvs_flash_init();

    serial_comm_init();
    display_init();
    ui_init();
    display_start_lvgl_task();

    // SD 卡后台初始化（字库/壁纸需先通过读卡器写入 TF 卡）
    xTaskCreate(sdcard_task, "sdcard", 8192, NULL, 5, NULL);

    serial_comm_send("READY");
    ESP_LOGI(TAG, "MyPad ready");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
