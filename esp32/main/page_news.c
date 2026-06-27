#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "lvgl.h"
#include "ui.h"
#include "serial_comm.h"
#include "fonts/font_chinese.h"

static lv_obj_t *news_container;
static lv_obj_t *flash_container;
static lv_obj_t *calendar_container;

void page_news_init(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(parent, 14, 0);
    lv_obj_set_style_pad_gap(parent, 12, 0);

    /* 左侧：资讯 */
    lv_obj_t *left = lv_obj_create(parent);
    lv_obj_set_flex_grow(left, 1);
    lv_obj_set_height(left, LV_PCT(100));
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_set_style_pad_gap(left, 6, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(left, LV_DIR_VER);

    lv_obj_t *hdr = lv_obj_create(left);
    lv_obj_set_size(hdr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(hdr, 6, 0);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "\u8d44\u8baf");
    lv_obj_set_style_text_color(title, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_font(title, &FONT_CN_16, 0);

    lv_obj_t *hint = lv_label_create(hdr);
    lv_label_set_text(hint, "Yahoo \u00b7 CNBC");
    lv_obj_set_style_text_color(hint, lv_color_hex(0xbbbbbb), 0);
    lv_obj_set_style_text_font(hint, &FONT_CN_16, 0);

    news_container = left;

    /* 右侧：快讯 + 经济日历 */
    lv_obj_t *right = lv_obj_create(parent);
    lv_obj_set_size(right, 340, LV_PCT(100));
    lv_obj_set_style_bg_color(right, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_radius(right, 14, 0);
    lv_obj_set_style_border_width(right, 1, 0);
    lv_obj_set_style_border_color(right, lv_color_hex(0xe2e8f0), 0);
    lv_obj_set_style_pad_all(right, 0, 0);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    /* 快讯区域 */
    lv_obj_t *flash_sec = lv_obj_create(right);
    lv_obj_set_width(flash_sec, LV_PCT(100));
    lv_obj_set_flex_grow(flash_sec, 1);
    lv_obj_set_style_bg_opa(flash_sec, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(flash_sec, 0, 0);
    lv_obj_set_style_pad_all(flash_sec, 10, 0);
    lv_obj_set_style_pad_gap(flash_sec, 0, 0);
    lv_obj_set_flex_flow(flash_sec, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(flash_sec, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(flash_sec, LV_DIR_VER);

    lv_obj_t *flash_hdr = lv_obj_create(flash_sec);
    lv_obj_set_size(flash_hdr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(flash_hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(flash_hdr, 0, 0);
    lv_obj_set_style_pad_all(flash_hdr, 0, 0);
    lv_obj_set_style_pad_bottom(flash_hdr, 8, 0);
    lv_obj_clear_flag(flash_hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(flash_hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(flash_hdr, 6, 0);

    lv_obj_t *live_dot = lv_obj_create(flash_hdr);
    lv_obj_set_size(live_dot, 6, 6);
    lv_obj_set_style_bg_color(live_dot, lv_color_hex(0xff3b30), 0);
    lv_obj_set_style_radius(live_dot, 3, 0);
    lv_obj_set_style_border_width(live_dot, 0, 0);

    lv_obj_t *flash_lbl = lv_label_create(flash_hdr);
    lv_label_set_text(flash_lbl, "LIVE \u5feb\u8baf");
    lv_obj_set_style_text_color(flash_lbl, lv_color_hex(0xff3b30), 0);
    lv_obj_set_style_text_font(flash_lbl, &FONT_CN_16, 0);

    lv_obj_t *flash_src = lv_label_create(flash_hdr);
    lv_label_set_text(flash_src, "Financial Juice");
    lv_obj_set_style_text_color(flash_src, lv_color_hex(0xbbbbbb), 0);
    lv_obj_set_style_text_font(flash_src, &FONT_CN_16, 0);

    /* 分割线 */
    lv_obj_t *sep = lv_obj_create(flash_hdr);
    lv_obj_set_size(sep, 1, 16);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0xe2e8f0), 0);
    lv_obj_set_style_border_width(sep, 0, 0);

    flash_container = flash_sec;

    /* 经济日历区域 */
    lv_obj_t *cal_sec = lv_obj_create(right);
    lv_obj_set_width(cal_sec, LV_PCT(100));
    lv_obj_set_flex_grow(cal_sec, 1);
    lv_obj_set_style_bg_opa(cal_sec, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cal_sec, 1, 0);
    lv_obj_set_style_border_side(cal_sec, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(cal_sec, lv_color_hex(0xe2e8f0), 0);
    lv_obj_set_style_pad_all(cal_sec, 8, 0);
    lv_obj_set_style_pad_gap(cal_sec, 3, 0);
    lv_obj_set_flex_flow(cal_sec, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(cal_sec, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(cal_sec, LV_DIR_VER);

    lv_obj_t *cal_hdr = lv_obj_create(cal_sec);
    lv_obj_set_size(cal_hdr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(cal_hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cal_hdr, 0, 0);
    lv_obj_set_style_pad_all(cal_hdr, 0, 0);
    lv_obj_clear_flag(cal_hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cal_lbl = lv_label_create(cal_hdr);
    lv_label_set_text(cal_lbl, "\u7ecf\u6d4e\u65e5\u5386");
    lv_obj_set_style_text_color(cal_lbl, lv_color_hex(0x999999), 0);
    lv_obj_set_style_text_font(cal_lbl, &FONT_CN_16, 0);

    calendar_container = cal_sec;
}

void page_news_update(void)
{
    int cnt = lv_obj_get_child_cnt(news_container);
    while (cnt > 1) {
        lv_obj_del(lv_obj_get_child(news_container, cnt - 1));
        cnt--;
    }

    int shown = 0;
    for (int i = 0; i < news_count && shown < 10; i++) {
        news_item_t *ni = &news_items[i];
        if (strcmp(ni->source, "FJ\u5feb\u8baf") == 0) continue;
        lv_obj_t *card = lv_obj_create(news_container);
        lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(0xe2e8f0), 0);
        lv_obj_set_style_pad_all(card, 10, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_gap(card, 3, 0);

        lv_obj_t *top = lv_obj_create(card);
        lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(top, 0, 0);
        lv_obj_set_style_pad_all(top, 0, 0);
        lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_gap(top, 6, 0);

        lv_obj_t *tag = lv_label_create(top);
        lv_label_set_text(tag, ni->source);
        lv_obj_set_style_text_color(tag, lv_color_hex(0x007aff), 0);
        lv_obj_set_style_text_font(tag, &FONT_CN_16, 0);
        lv_obj_set_style_bg_color(tag, lv_color_hex(0x007aff), 0);
        lv_obj_set_style_bg_opa(tag, 13, 0);
        lv_obj_set_style_radius(tag, 4, 0);
        lv_obj_set_style_pad_hor(tag, 4, 0);

        lv_obj_t *time_lbl = lv_label_create(top);
        lv_label_set_text(time_lbl, ni->time);
        lv_obj_set_style_text_color(time_lbl, lv_color_hex(0xbbbbbb), 0);
        lv_obj_set_style_text_font(time_lbl, &FONT_CN_16, 0);

        lv_obj_t *t = lv_label_create(card);
        lv_label_set_text(t, ni->title);
        lv_obj_set_width(t, LV_PCT(100));
        lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(t, lv_color_hex(0x222222), 0);
        lv_obj_set_style_text_font(t, &FONT_CN_16, 0);

        if (ni->desc[0]) {
            lv_obj_t *d = lv_label_create(card);
            lv_label_set_text(d, ni->desc);
            lv_obj_set_width(d, LV_PCT(100));
            lv_label_set_long_mode(d, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_color(d, lv_color_hex(0x999999), 0);
            lv_obj_set_style_text_font(d, &FONT_CN_16, 0);
        }
        shown++;
    }
}

void page_news_update_flash(void)
{
    int cnt = lv_obj_get_child_cnt(flash_container);
    while (cnt > 1) {
        lv_obj_del(lv_obj_get_child(flash_container, cnt - 1));
        cnt--;
    }

    int shown = 0;
    for (int i = 0; i < news_count && shown < 15; i++) {
        news_item_t *ni = &news_items[i];
        if (strcmp(ni->source, "FJ\u5feb\u8baf") != 0) continue;

        lv_obj_t *row = lv_obj_create(flash_container);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 3, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_gap(row, 1, 0);

        lv_obj_t *title = lv_label_create(row);
        lv_label_set_text(title, ni->title);
        lv_obj_set_width(title, LV_PCT(100));
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(title, lv_color_hex(0x333333), 0);
        lv_obj_set_style_text_font(title, &FONT_CN_16, 0);

        if (ni->time[0]) {
            lv_obj_t *t = lv_label_create(row);
            lv_label_set_text(t, ni->time);
            lv_obj_set_style_text_color(t, lv_color_hex(0xbbbbbb), 0);
            lv_obj_set_style_text_font(t, &lv_font_montserrat_12, 0);
        }
        shown++;
    }

    if (shown == 0) {
        lv_obj_t *empty = lv_label_create(flash_container);
        lv_label_set_text(empty, "\u6682\u65e0\u5feb\u8baf");
        lv_obj_set_style_text_color(empty, lv_color_hex(0xbbbbbb), 0);
        lv_obj_set_style_text_font(empty, &FONT_CN_16, 0);
    }
}

void page_news_update_calendar(void)
{
    int cnt = lv_obj_get_child_cnt(calendar_container);
    while (cnt > 1) {
        lv_obj_del(lv_obj_get_child(calendar_container, cnt - 1));
        cnt--;
    }

    for (int i = 0; i < calendar_count && i < MAX_CALENDAR; i++) {
        calendar_item_t *ci = &calendar_items[i];

        lv_obj_t *row = lv_obj_create(calendar_container);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xf8f8f8), 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 4, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(row, 5, 0);

        lv_obj_t *time_lbl = lv_label_create(row);
        lv_label_set_text(time_lbl, ci->time_str[0] ? ci->time_str : "--:--");
        lv_obj_set_style_text_color(time_lbl, lv_color_hex(0x999999), 0);
        lv_obj_set_style_text_font(time_lbl, &FONT_CN_16, 0);

        lv_obj_t *flag = lv_label_create(row);
        lv_label_set_text(flag, ci->country);
        lv_obj_set_style_text_font(flag, &FONT_CN_16, 0);
        lv_color_t flag_bg = lv_color_hex(0x007aff);
        if (strcmp(ci->country, "EUR") == 0) flag_bg = lv_color_hex(0xff9500);
        else if (strcmp(ci->country, "JPY") == 0) flag_bg = lv_color_hex(0xaf52de);
        else if (strcmp(ci->country, "CNY") == 0) flag_bg = lv_color_hex(0xff3b30);
        lv_obj_set_style_text_color(flag, flag_bg, 0);
        lv_obj_set_style_bg_color(flag, flag_bg, 0);
        lv_obj_set_style_bg_opa(flag, 13, 0);
        lv_obj_set_style_radius(flag, 3, 0);
        lv_obj_set_style_pad_hor(flag, 3, 0);

        lv_obj_t *ev = lv_label_create(row);
        lv_label_set_text(ev, ci->event);
        lv_obj_set_flex_grow(ev, 1);
        lv_label_set_long_mode(ev, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(ev, lv_color_hex(0x555555), 0);
        lv_obj_set_style_text_font(ev, &FONT_CN_16, 0);

        if (strcmp(ci->impact, "High") == 0) {
            lv_obj_t *imp = lv_label_create(row);
            lv_label_set_text(imp, "!!!");
            lv_obj_set_style_text_color(imp, lv_color_hex(0xff3b30), 0);
            lv_obj_set_style_text_font(imp, &FONT_CN_16, 0);
        }
    }
}
