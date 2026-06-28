#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "ui.h"
#include "serial_comm.h"
#include "fonts/font_chinese.h"

#define ICON_SIZE 80
#define ICON_BUF_BYTES (ICON_SIZE * ICON_SIZE * 2)

static lv_obj_t *slot_btns[SLOT_COUNT];
static lv_obj_t *slot_labels[SLOT_COUNT];
static lv_obj_t *slot_icons[SLOT_COUNT];
static lv_obj_t *slot_icon_labels[SLOT_COUNT];
static lv_obj_t *slot_canvases[SLOT_COUNT];
static uint16_t *icon_bufs[SLOT_COUNT] = {0};
static bool icon_loaded[SLOT_COUNT] = {false};

static int pressed_slot = -1;
static lv_timer_t *highlight_timers[SLOT_COUNT] = {0};

static void reset_slot_color_cb(lv_timer_t *t)
{
    int idx = (int)(intptr_t)lv_timer_get_user_data(t);
    highlight_timers[idx] = NULL;
    lv_obj_set_style_bg_color(slot_btns[idx], lv_color_hex(0xffffff), 0);
    lv_timer_del(t);
}
static int press_x = -1, press_y = -1;

static void container_press_cb(lv_event_t *e)
{
    /* 容器（非按钮区域）被按下时，清除 pressed_slot */
    pressed_slot = -1;
}

static void slot_press_cb(lv_event_t *e)
{
    if (ui_is_swiping()) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    pressed_slot = idx;
    lv_indev_t *indev = lv_indev_active();
    if (indev) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        press_x = p.x;
        press_y = p.y;
    }
}

static void slot_release_cb(lv_event_t *e)
{
    if (ui_is_swiping()) { pressed_slot = -1; return; }
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (pressed_slot != idx) { pressed_slot = -1; return; }
    /* 检查移动距离，超过 30px 视为拖拽而非点击 */
    if (press_x >= 0) {
        lv_indev_t *indev = lv_indev_active();
        if (indev) {
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            int dx = p.x - press_x;
            int dy = p.y - press_y;
            if (dx * dx + dy * dy > 900) { pressed_slot = -1; return; }
        }
    }
    if (app_slots[idx].active) {
        char buf[32];
        snprintf(buf, sizeof(buf), "LAUNCH:%d", idx + 1);
        serial_comm_send(buf);
        // 高亮反馈，500ms 后自动恢复
        lv_obj_set_style_bg_color(slot_btns[idx], lv_color_hex(0xd0e0ff), 0);
        if (highlight_timers[idx]) {
            lv_timer_del(highlight_timers[idx]);
        }
        highlight_timers[idx] = lv_timer_create(reset_slot_color_cb, 500, (void *)(intptr_t)idx);
    }
    pressed_slot = -1;
}

void page_app_init(lv_obj_t *parent)
{
    /* 从 PSRAM 分配图标 buffer */
    for (int i = 0; i < SLOT_COUNT; i++) {
        icon_bufs[i] = heap_caps_malloc(ICON_BUF_BYTES, MALLOC_CAP_SPIRAM);
        assert(icon_bufs[i]);
        memset(icon_bufs[i], 0, ICON_BUF_BYTES);
    }

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(parent, 14, 0);
    lv_obj_set_style_pad_all(parent, 18, 0);
    lv_obj_add_event_cb(parent, container_press_cb, LV_EVENT_PRESSED, NULL);

    for (int i = 0; i < SLOT_COUNT; i++) {
        slot_btns[i] = lv_btn_create(parent);
        lv_obj_set_size(slot_btns[i], 310, 220);
        lv_obj_set_style_bg_color(slot_btns[i], lv_color_hex(0xffffff), 0);
        lv_obj_set_style_radius(slot_btns[i], 18, 0);
        lv_obj_set_style_border_width(slot_btns[i], 1, 0);
        lv_obj_set_style_border_color(slot_btns[i], lv_color_hex(0xe2e8f0), 0);
        lv_obj_set_style_shadow_width(slot_btns[i], 2, 0);
        lv_obj_set_style_shadow_color(slot_btns[i], lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(slot_btns[i], 10, 0);
        lv_obj_set_flex_flow(slot_btns[i], LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(slot_btns[i], LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(slot_btns[i], 12, 0);
        lv_obj_add_event_cb(slot_btns[i], slot_press_cb, LV_EVENT_PRESSED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(slot_btns[i], slot_release_cb, LV_EVENT_RELEASED, (void *)(intptr_t)i);

        /* 图标区域 (80x80) */
        slot_icons[i] = lv_obj_create(slot_btns[i]);
        lv_obj_set_size(slot_icons[i], 80, 80);
        lv_obj_set_style_bg_opa(slot_icons[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_radius(slot_icons[i], 18, 0);
        lv_obj_set_style_border_width(slot_icons[i], 0, 0);
        lv_obj_clear_flag(slot_icons[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(slot_icons[i], 0, 0);

        /* Canvas 图标（初始隐藏） */
        slot_canvases[i] = lv_canvas_create(slot_icons[i]);
        lv_canvas_set_buffer(slot_canvases[i], icon_bufs[i], ICON_SIZE, ICON_SIZE, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_size(slot_canvases[i], ICON_SIZE, ICON_SIZE);
        lv_obj_center(slot_canvases[i]);
        lv_obj_add_flag(slot_canvases[i], LV_OBJ_FLAG_HIDDEN);

        /* 文字图标（初始显示） */
        slot_icon_labels[i] = lv_label_create(slot_icons[i]);
        lv_label_set_text(slot_icon_labels[i], "+");
        lv_obj_set_style_text_color(slot_icon_labels[i], lv_color_hex(0x007aff), 0);
        lv_obj_set_style_text_font(slot_icon_labels[i], &lv_font_montserrat_28, 0);
        lv_obj_center(slot_icon_labels[i]);

        /* 应用名 */
        slot_labels[i] = lv_label_create(slot_btns[i]);
        lv_label_set_text(slot_labels[i], "...");
        lv_obj_set_style_text_color(slot_labels[i], lv_color_hex(0x333333), 0);
        lv_obj_set_style_text_font(slot_labels[i], &FONT_CN_16, 0);
    }
}

void page_app_set_icon(int slot, const uint16_t *rgb565)
{
    if (slot < 0 || slot >= SLOT_COUNT) return;
    memcpy(icon_bufs[slot], rgb565, ICON_BUF_BYTES);
    icon_loaded[slot] = true;
    lv_obj_clear_flag(slot_canvases[slot], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(slot_icon_labels[slot], LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(slot_canvases[slot]);
}

void page_app_update(void)
{
    for (int i = 0; i < SLOT_COUNT; i++) {
        if (app_slots[i].active) {
            lv_label_set_text(slot_labels[i], app_slots[i].name);
            if (!icon_loaded[i]) {
                /* 无图标时用首字符 */
                char ch[2] = {0, 0};
                ch[0] = app_slots[i].name[0];
                if (ch[0] >= 'a' && ch[0] <= 'z') ch[0] -= 32;
                lv_label_set_text(slot_icon_labels[i], ch);
                lv_obj_set_style_text_color(slot_icon_labels[i], lv_color_hex(0x007aff), 0);
                lv_obj_set_style_bg_color(slot_icons[i], lv_color_hex(0xf0f4ff), 0);
                lv_obj_set_style_bg_opa(slot_icons[i], LV_OPA_100, 0);
                lv_obj_clear_flag(slot_icon_labels[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(slot_canvases[i], LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_set_style_border_color(slot_btns[i], lv_color_hex(0xe2e8f0), 0);
        } else {
            lv_label_set_text(slot_labels[i], "...");
            lv_label_set_text(slot_icon_labels[i], "+");
            lv_obj_set_style_text_color(slot_icon_labels[i], lv_color_hex(0x007aff), 0);
            lv_obj_set_style_bg_opa(slot_icons[i], LV_OPA_TRANSP, 0);
            lv_obj_clear_flag(slot_icon_labels[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(slot_canvases[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_border_color(slot_btns[i], lv_color_hex(0xe2e8f0), 0);
            icon_loaded[i] = false;
        }
    }
}
