#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "lvgl.h"
#include "ui.h"
#include "serial_comm.h"
#include "fonts/font_chinese.h"
#include "display.h"

static lv_obj_t *pages[PAGE_COUNT];
static lv_obj_t *page_dots[PAGE_COUNT];
static lv_obj_t *clock_label;
static int current_page = 0;

static int touch_start_x = -1;
static int touch_start_y = -1;
static bool touch_was_pressed = false;
static bool swipe_triggered = false;
static lv_indev_t *s_indev = NULL;

/* 熄屏唤醒 */
static int screen_off_timeout_ms = 30000;
static uint32_t last_activity_tick = 0;
static bool screen_off = false;
static int saved_brightness = 80;

bool ui_is_swiping(void) { return swipe_triggered; }

static void update_indicator(void)
{
    for (int i = 0; i < PAGE_COUNT; i++) {
        lv_obj_set_width(page_dots[i], i == current_page ? 18 : 6);
        lv_obj_set_style_bg_color(page_dots[i], lv_color_hex(i == current_page ? 0x007aff : 0xcbd5e1), 0);
    }
}

void ui_set_page(int page)
{
    if (page < 0 || page >= PAGE_COUNT || page == current_page) return;
    lv_obj_add_flag(pages[current_page], LV_OBJ_FLAG_HIDDEN);
    current_page = page;
    lv_obj_clear_flag(pages[current_page], LV_OBJ_FLAG_HIDDEN);
    update_indicator();
    if (page == 0) page_app_update();
    else if (page == 1) { page_monitor_update(); page_monitor_update_finance(); }
    else if (page == 2) { page_news_update(); page_news_update_flash(); page_news_update_calendar(); }
}

int ui_get_page(void) { return current_page; }
void ui_update_finance(void) { if (ui_get_page() == 1) page_monitor_update_finance(); }
void ui_update_news(void) { if (ui_get_page() == 2) { page_news_update(); page_news_update_flash(); page_news_update_calendar(); } }

void ui_update_clock(const char *time_str)
{
    if (clock_label) lv_label_set_text(clock_label, time_str);
}

void ui_set_screen_timeout(int seconds)
{
    if (seconds < 5) seconds = 5;
    if (seconds > 300) seconds = 300;
    screen_off_timeout_ms = seconds * 1000;
}

static void swipe_reset_cb(lv_timer_t *t) { swipe_triggered = false; lv_timer_del(t); }

static void swipe_timer_cb(lv_timer_t *timer)
{
    if (!s_indev) return;
    bool pressed = (lv_indev_get_state(s_indev) == LV_INDEV_STATE_PRESSED);
    lv_point_t point;
    lv_indev_get_point(s_indev, &point);

    /* 熄屏状态：检测触控唤醒 */
    if (screen_off) {
        if (pressed) {
            screen_off = false;
            last_activity_tick = lv_tick_get();
            display_set_brightness(saved_brightness);
            /* 记录唤醒触控位置，以便后续检测滑动 */
            touch_start_x = point.x;
            touch_start_y = point.y;
            swipe_triggered = false;
            ESP_LOGI("ui", "Screen ON (touch wake)");
        }
        touch_was_pressed = pressed;
        return;
    }

    /* 亮屏状态：更新空闲计时 */
    if (pressed) {
        last_activity_tick = lv_tick_get();
    } else {
        uint32_t idle_ms = lv_tick_elaps(last_activity_tick);
        if (idle_ms >= (uint32_t)screen_off_timeout_ms) {
            screen_off = true;
            display_set_brightness(0);
            ESP_LOGI("ui", "Screen OFF (idle %dms)", (int)idle_ms);
            touch_was_pressed = pressed;
            return;
        }
    }

    if (pressed && !touch_was_pressed) {
        touch_start_x = point.x;
        touch_start_y = point.y;
        swipe_triggered = false;
    } else if (!pressed && touch_was_pressed) {
        if (touch_start_x >= 0) {
            int dx = point.x - touch_start_x;
            int dy = point.y - touch_start_y;
            if (dx < -50 && current_page < PAGE_COUNT - 1) {
                ui_set_page(current_page + 1);
                swipe_triggered = true;
            } else if (dx > 50 && current_page > 0) {
                ui_set_page(current_page - 1);
                swipe_triggered = true;
            } else if (abs(dx) > 10 || abs(dy) > 10) {
                swipe_triggered = true;
            }
        }
        touch_start_x = -1;
        if (swipe_triggered) lv_timer_create(swipe_reset_cb, 300, NULL);
    }
    touch_was_pressed = pressed;
}

static void brightness_cb(lv_event_t *e)
{
    int val = lv_slider_get_value(lv_event_get_target(e));
    saved_brightness = val;
    display_set_brightness(val);
}

lv_font_t *ui_font_cn_16 = (lv_font_t *)&font_cn_full_16;

void ui_init(void)
{
    // Try to load SD card font
    lv_font_t *sd_font = lv_binfont_create("S:font.bin");
    if (sd_font) {
        ui_font_cn_16 = sd_font;
    }

    s_indev = display_get_touch_indev();
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xf5f5f7), 0);

    /* ===== 状态栏 (30px, 顶部) ===== */
    lv_obj_t *status = lv_obj_create(scr);
    lv_obj_set_size(status, SCREEN_WIDTH, 30);
    lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(status, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(status, 217, 0);
    lv_obj_set_style_border_width(status, 0, 0);
    lv_obj_set_style_radius(status, 0, 0);
    lv_obj_set_style_pad_all(status, 0, 0);
    lv_obj_clear_flag(status, LV_OBJ_FLAG_SCROLLABLE);

    /* 左：时钟 */
    clock_label = lv_label_create(status);
    lv_label_set_text(clock_label, "18:00");
    lv_obj_set_style_text_color(clock_label, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_font(clock_label, &lv_font_montserrat_14, 0);
    lv_obj_align(clock_label, LV_ALIGN_LEFT_MID, 12, 0);

    /* 中：亮度滑条 */
    lv_obj_t *slider = lv_slider_create(status);
    lv_obj_set_width(slider, 200);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, 0);
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider, 80, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xe2e8f0), 0);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x007aff), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xffffff), LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, lv_color_hex(0x007aff), LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 2, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 0, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 右：连接状态 */
    lv_obj_t *conn_lbl = lv_label_create(status);
    lv_label_set_text(conn_lbl, "MyPad 115200");
    lv_obj_set_style_text_color(conn_lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(conn_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(conn_lbl, LV_ALIGN_RIGHT_MID, -50, 0);

    lv_obj_t *dot = lv_obj_create(status);
    lv_obj_set_size(dot, 6, 6);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0x34c759), 0);
    lv_obj_set_style_radius(dot, 3, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_align(dot, LV_ALIGN_RIGHT_MID, -30, 0);

    lv_obj_t *conn_st = lv_label_create(status);
    lv_label_set_text(conn_st, "已连接");
    lv_obj_set_style_text_color(conn_st, lv_color_hex(0x34c759), 0);
    lv_obj_set_style_text_font(conn_st, &FONT_CN_16, 0);
    lv_obj_align(conn_st, LV_ALIGN_RIGHT_MID, -8, 0);

    /* ===== 页面容器 ===== */
    int content_y = 30;
    int content_h = SCREEN_HEIGHT - 30 - 22;
    lv_obj_t *page_area = lv_obj_create(scr);
    lv_obj_set_size(page_area, SCREEN_WIDTH, content_h);
    lv_obj_align(page_area, LV_ALIGN_TOP_MID, 0, content_y);
    lv_obj_set_style_bg_opa(page_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page_area, 0, 0);
    lv_obj_set_style_pad_all(page_area, 0, 0);
    lv_obj_clear_flag(page_area, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < PAGE_COUNT; i++) {
        pages[i] = lv_obj_create(page_area);
        lv_obj_set_size(pages[i], SCREEN_WIDTH, content_h);
        lv_obj_set_style_bg_color(pages[i], lv_color_hex(0xf5f5f7), 0);
        lv_obj_set_style_border_width(pages[i], 0, 0);
        lv_obj_set_style_radius(pages[i], 0, 0);
        lv_obj_set_style_pad_all(pages[i], 0, 0);
        lv_obj_clear_flag(pages[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(pages[i], LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    }

    page_app_init(pages[0]);
    page_monitor_init(pages[1]);
    page_news_init(pages[2]);
    lv_obj_clear_flag(pages[0], LV_OBJ_FLAG_HIDDEN);

    /* ===== 底部指示器 (22px) ===== */
    lv_obj_t *ind_bar = lv_obj_create(scr);
    lv_obj_set_size(ind_bar, SCREEN_WIDTH, 22);
    lv_obj_align(ind_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(ind_bar, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(ind_bar, 204, 0);
    lv_obj_set_style_border_width(ind_bar, 0, 0);
    lv_obj_set_style_radius(ind_bar, 0, 0);
    lv_obj_set_style_pad_all(ind_bar, 0, 0);
    lv_obj_clear_flag(ind_bar, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < PAGE_COUNT; i++) {
        page_dots[i] = lv_obj_create(ind_bar);
        lv_obj_set_size(page_dots[i], 6, 6);
        lv_obj_set_style_radius(page_dots[i], 3, 0);
        lv_obj_set_style_bg_color(page_dots[i], lv_color_hex(i == 0 ? 0x007aff : 0xcbd5e1), 0);
        lv_obj_set_style_border_width(page_dots[i], 0, 0);
        lv_obj_align(page_dots[i], LV_ALIGN_CENTER, (i - 1) * 24, 0);
    }
    update_indicator();

    /* 滑动检测定时器 */
    last_activity_tick = lv_tick_get();
    lv_timer_create(swipe_timer_cb, 10, NULL);
}
