/**
 * ESP32-P4 慧勤智远 7寸电容屏 —— 显示初始化 (调试版)
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/lock.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_ek79007.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_ldo_regulator.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "display.h"

static const char *TAG = "display";

/* LCD 引脚 */
#define LCD_BL_GPIO       21
#define LCD_RST_GPIO      22
#define LCD_PWR_GPIO      23

/* 触摸引脚 */
#define TOUCH_INT_GPIO    2
#define TOUCH_SDA_GPIO    3
#define TOUCH_SCL_GPIO    4
#define TOUCH_RST_GPIO    5

/* MIPI-DSI / EK79007 */
#define LCD_H_RES         1024
#define LCD_V_RES         600
#define DSI_LANES         2
#define DSI_BITRATE_MBPS  1000
#define PHY_LDO_CHAN      3
#define PHY_LDO_MV        2500
#define DPI_CLK_MHZ       48
#define HSYNC_PW          10
#define HSYNC_BP          120
#define HSYNC_FP          120
#define VSYNC_PW          1
#define VSYNC_BP          20
#define VSYNC_FP          10

/* LVGL */
#define DRAW_BUF_LINES    100
#define TICK_MS           2
#define TASK_STACK        (16 * 1024)

static SemaphoreHandle_t lvgl_mutex = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_touch_handle_t s_touch = NULL;
static int s_flush_count = 0;

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    s_flush_count++;
    if (s_flush_count <= 3) {
        ESP_LOGI(TAG, "flush #%d: (%ld,%ld)-(%ld,%ld)", s_flush_count,
                 (long)area->x1, (long)area->y1, (long)area->x2, (long)area->y2);
    }
    esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

static void touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint16_t x[1], y[1], str[1];
    uint8_t cnt = 0;
    esp_lcd_touch_read_data(s_touch);
    if (esp_lcd_touch_get_coordinates(s_touch, x, y, str, &cnt, 1) && cnt > 0) {
        data->point.x = x[0]; data->point.y = y[0];
        data->state = LV_INDEV_STATE_PRESSED;
        static int tc = 0;
        if (tc++ < 3) ESP_LOGI(TAG, "TOUCH: x=%d y=%d", x[0], y[0]);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void tick_cb(void *arg) { lv_tick_inc(TICK_MS); }

static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task running");
    while (1) {
        xSemaphoreTakeRecursive(lvgl_mutex, portMAX_DELAY);
        uint32_t t = lv_timer_handler();
        xSemaphoreGiveRecursive(lvgl_mutex);
        if (t > 500) t = 500;
        if (t < 10) t = 10;
        usleep(1000 * t);
    }
}

static lv_indev_t *s_touch_indev = NULL;

lv_indev_t *display_get_touch_indev(void) { return s_touch_indev; }

void display_init(void)
{
    ESP_LOGI(TAG, "===== display_init start =====");

    /* 创建 LVGL 递归互斥锁（timer 回调内可能重入） */
    lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    assert(lvgl_mutex);

    /* 关闭 NS4150B 音频功放 (GPIO20 PA_CTRL) 消除蜂鸣 */
    gpio_config_t pa_gc = { .pin_bit_mask = (1ULL << 20), .mode = GPIO_MODE_OUTPUT };
    gpio_config(&pa_gc); gpio_set_level(20, 0);

    /* LCD 电源 */
    gpio_config_t gc = { .pin_bit_mask = (1ULL << LCD_PWR_GPIO), .mode = GPIO_MODE_OUTPUT };
    gpio_config(&gc); gpio_set_level(LCD_PWR_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* LCD 复位 */
    gc.pin_bit_mask = (1ULL << LCD_RST_GPIO);
    gpio_config(&gc);
    gpio_set_level(LCD_RST_GPIO, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LCD_RST_GPIO, 1); vTaskDelay(pdMS_TO_TICKS(50));

    /* 背光 PWM (GPIO21) */
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 20000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&ledc_timer);
    ledc_channel_config_t ledc_ch = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .gpio_num = LCD_BL_GPIO,
        .duty = 820,
        .hpoint = 0,
    };
    ledc_channel_config(&ledc_ch);
    ESP_LOGI(TAG, "Backlight PWM OK");

    /* MIPI PHY LDO */
    esp_ldo_channel_handle_t ldo;
    esp_ldo_channel_config_t lc = { .chan_id = PHY_LDO_CHAN, .voltage_mv = PHY_LDO_MV };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&lc, &ldo));
    ESP_LOGI(TAG, "PHY LDO OK");

    /* DSI bus */
    esp_lcd_dsi_bus_handle_t bus;
    esp_lcd_dsi_bus_config_t bc = {
        .bus_id = 0, .num_data_lanes = DSI_LANES, .lane_bit_rate_mbps = DSI_BITRATE_MBPS,
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bc, &bus));
    ESP_LOGI(TAG, "DSI bus OK");

    /* DBI IO */
    esp_lcd_panel_io_handle_t dbi;
    esp_lcd_dbi_io_config_t dc = { .virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8 };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(bus, &dc, &dbi));

    /* EK79007 panel */
    esp_lcd_dpi_panel_config_t dpi = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = DPI_CLK_MHZ,
        .in_color_format = LCD_COLOR_FMT_RGB888,
        .video_timing = {
            .h_size = LCD_H_RES, .v_size = LCD_V_RES,
            .hsync_pulse_width = HSYNC_PW,
            .hsync_back_porch = HSYNC_BP,
            .hsync_front_porch = HSYNC_FP,
            .vsync_pulse_width = VSYNC_PW,
            .vsync_back_porch = VSYNC_BP,
            .vsync_front_porch = VSYNC_FP,
        },
        .flags.use_dma2d = false,
    };
    ek79007_vendor_config_t vc = { .mipi_config = { .dsi_bus = bus, .dpi_config = &dpi } };
    esp_lcd_panel_dev_config_t pc = {
        .reset_gpio_num = -1, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 24, .vendor_config = &vc,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ek79007(dbi, &pc, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_LOGI(TAG, "EK79007 panel OK");

    /* I2C + GT911 (触摸初始化失败不阻塞) */
    i2c_master_bus_handle_t i2c;
    i2c_master_bus_config_t ic = {
        .clk_source = I2C_CLK_SRC_DEFAULT, .i2c_port = I2C_NUM_0,
        .scl_io_num = TOUCH_SCL_GPIO, .sda_io_num = TOUCH_SDA_GPIO,
        .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&ic, &i2c);
    if (ret == ESP_OK) {
        esp_lcd_panel_io_handle_t tp_io;
        esp_lcd_panel_io_i2c_config_t tpio = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
        /* 尝试 0x5D，失败后试 0x14 */
        ret = esp_lcd_new_panel_io_i2c(i2c, &tpio, &tp_io);
        if (ret == ESP_OK) {
            esp_lcd_touch_config_t tc = { .x_max = LCD_H_RES, .y_max = LCD_V_RES,
                                          .rst_gpio_num = TOUCH_RST_GPIO, .int_gpio_num = TOUCH_INT_GPIO };
            ret = esp_lcd_touch_new_i2c_gt911(tp_io, &tc, &s_touch);
        }
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "GT911 at 0x5D failed, trying 0x14...");
            tpio.dev_addr = 0x14;
            ret = esp_lcd_new_panel_io_i2c(i2c, &tpio, &tp_io);
            if (ret == ESP_OK) {
                esp_lcd_touch_config_t tc = { .x_max = LCD_H_RES, .y_max = LCD_V_RES,
                                              .rst_gpio_num = TOUCH_RST_GPIO, .int_gpio_num = TOUCH_INT_GPIO };
                ret = esp_lcd_touch_new_i2c_gt911(tp_io, &tc, &s_touch);
            }
        }
    }
    if (ret == ESP_OK && s_touch) {
        ESP_LOGI(TAG, "GT911 touch OK");
    } else {
        ESP_LOGW(TAG, "Touch init failed (0x%x), continuing without touch", ret);
        s_touch = NULL;
    }

    /* LVGL */
    lv_init();
    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_user_data(disp, s_panel);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB888);
    lv_display_set_flush_cb(disp, flush_cb);

    size_t buf_sz = LCD_H_RES * DRAW_BUF_LINES * sizeof(lv_color_t);
    void *buf1 = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM);
    void *buf2 = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "LVGL buf: sz=%d buf1=%p buf2=%p", buf_sz, buf1, buf2);
    assert(buf1 && buf2);
    lv_display_set_buffers(disp, buf1, buf2, buf_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);

    /* 触摸输入 (仅在触摸初始化成功时注册) */
    if (s_touch) {
        s_touch_indev = lv_indev_create();
        lv_indev_set_type(s_touch_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(s_touch_indev, touch_cb);
        ESP_LOGI(TAG, "Touch indev registered");
    }

    /* tick 定时器 */
    esp_timer_handle_t th;
    esp_timer_create_args_t ta = { .callback = tick_cb, .name = "tick" };
    ESP_ERROR_CHECK(esp_timer_create(&ta, &th));
    ESP_ERROR_CHECK(esp_timer_start_periodic(th, TICK_MS * 1000));

    ESP_LOGI(TAG, "===== display_init done (LVGL task not started yet) =====");
}

void display_start_lvgl_task(void)
{
    xTaskCreate(lvgl_task, "lvgl", TASK_STACK, NULL, 2, NULL);
    ESP_LOGI(TAG, "LVGL task started");
}

bool display_lock(uint32_t ms)
{
    if (!lvgl_mutex) return false;
    return xSemaphoreTakeRecursive(lvgl_mutex, pdMS_TO_TICKS(ms)) == pdTRUE;
}
void display_unlock(void)
{
    if (lvgl_mutex) xSemaphoreGiveRecursive(lvgl_mutex);
}

/* 亮度控制 (LEDC PWM, 0-100%) */
void display_set_brightness(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint32_t duty = percent * 1023 / 100;
    if (display_lock(200)) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        display_unlock();
    }
}
