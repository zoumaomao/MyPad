#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "lvgl.h"
#include "ui.h"
#include "serial_comm.h"
#include "fonts/font_chinese.h"

static lv_obj_t *cpu_val, *mem_val, *gpu_val, *disk_val;
static lv_obj_t *cpu_bar, *mem_bar, *gpu_bar, *disk_bar;
static lv_obj_t *cpu_detail, *gpu_detail, *disk_detail;
static lv_obj_t *net_up_val, *net_down_val;
static lv_obj_t *date_main, *date_sub;
static lv_obj_t *weather_temp, *weather_desc;
static lv_obj_t *finance_container;

static lv_obj_t *make_card(lv_obj_t *parent, const char *label, lv_color_t color,
                           lv_obj_t **val, lv_obj_t **bar, lv_obj_t **detail)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_height(card, LV_PCT(100));
    lv_obj_set_style_bg_color(card, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0xe2e8f0), 0);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_style_shadow_width(card, 1, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(card, 8, 0);

    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x999999), 0);
    lv_obj_set_style_text_font(lbl, &FONT_CN_16, 0);

    *val = lv_label_create(card);
    lv_label_set_text(*val, "0%");
    lv_obj_set_style_text_color(*val, color, 0);
    lv_obj_set_style_text_font(*val, &lv_font_montserrat_28, 0);

    *bar = lv_bar_create(card);
    lv_obj_set_size(*bar, LV_PCT(100), 4);
    lv_bar_set_range(*bar, 0, 100);
    lv_obj_set_style_bg_color(*bar, lv_color_hex(0xe2e8f0), 0);
    lv_obj_set_style_bg_color(*bar, color, LV_PART_INDICATOR);
    lv_obj_set_style_radius(*bar, 2, 0);
    lv_obj_set_style_radius(*bar, 2, LV_PART_INDICATOR);

    if (detail) {
        *detail = lv_label_create(card);
        lv_label_set_text(*detail, "");
        lv_obj_set_style_text_color(*detail, lv_color_hex(0xaaaaaa), 0);
        lv_obj_set_style_text_font(*detail, &FONT_CN_16, 0);
    }
    return card;
}

void page_monitor_init(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(parent, 10, 0);
    lv_obj_set_style_pad_gap(parent, 10, 0);

    /* ===== 左侧：系统监控 ===== */
    lv_obj_t *left = lv_obj_create(parent);
    lv_obj_set_flex_grow(left, 1);
    lv_obj_set_height(left, LV_PCT(100));
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_set_style_pad_gap(left, 6, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    /* 行1: CPU + 内存 */
    lv_obj_t *r1 = lv_obj_create(left);
    lv_obj_set_size(r1, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(r1, 1);
    lv_obj_set_style_bg_opa(r1, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r1, 0, 0);
    lv_obj_set_style_pad_all(r1, 0, 0);
    lv_obj_set_style_pad_gap(r1, 10, 0);
    lv_obj_set_flex_flow(r1, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(r1, LV_OBJ_FLAG_SCROLLABLE);
    make_card(r1, "CPU", lv_color_hex(0x007aff), &cpu_val, &cpu_bar, &cpu_detail);
    make_card(r1, "\u5185\u5b58", lv_color_hex(0xaf52de), &mem_val, &mem_bar, NULL);

    /* 行2: GPU + 磁盘 */
    lv_obj_t *r2 = lv_obj_create(left);
    lv_obj_set_size(r2, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(r2, 1);
    lv_obj_set_style_bg_opa(r2, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r2, 0, 0);
    lv_obj_set_style_pad_all(r2, 0, 0);
    lv_obj_set_style_pad_gap(r2, 10, 0);
    lv_obj_set_flex_flow(r2, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(r2, LV_OBJ_FLAG_SCROLLABLE);
    make_card(r2, "GPU", lv_color_hex(0x34c759), &gpu_val, &gpu_bar, &gpu_detail);
    make_card(r2, "\u78c1\u76d8", lv_color_hex(0xff9500), &disk_val, &disk_bar, &disk_detail);

    /* 网速 */
    lv_obj_t *net = lv_obj_create(left);
    lv_obj_set_size(net, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(net, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_radius(net, 14, 0);
    lv_obj_set_style_border_width(net, 1, 0);
    lv_obj_set_style_border_color(net, lv_color_hex(0xe2e8f0), 0);
    lv_obj_set_style_pad_all(net, 12, 0);
    lv_obj_clear_flag(net, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(net, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(net, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(net, 20, 0);

    lv_obj_t *up_sec = lv_obj_create(net);
    lv_obj_set_size(up_sec, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(up_sec, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(up_sec, 0, 0);
    lv_obj_set_style_pad_all(up_sec, 0, 0);
    lv_obj_clear_flag(up_sec, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(up_sec, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(up_sec, 8, 0);

    lv_obj_t *up_icon = lv_label_create(up_sec);
    lv_label_set_text(up_icon, LV_SYMBOL_UP);
    lv_obj_set_style_text_color(up_icon, lv_color_hex(0x34c759), 0);

    net_up_val = lv_label_create(up_sec);
    lv_label_set_text(net_up_val, "0 KB/s");
    lv_obj_set_style_text_font(net_up_val, &FONT_CN_16, 0);

    lv_obj_t *sep = lv_obj_create(net);
    lv_obj_set_size(sep, 1, 22);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0xe2e8f0), 0);
    lv_obj_set_style_border_width(sep, 0, 0);

    lv_obj_t *dn_sec = lv_obj_create(net);
    lv_obj_set_size(dn_sec, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(dn_sec, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dn_sec, 0, 0);
    lv_obj_set_style_pad_all(dn_sec, 0, 0);
    lv_obj_clear_flag(dn_sec, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(dn_sec, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(dn_sec, 8, 0);

    lv_obj_t *dn_icon = lv_label_create(dn_sec);
    lv_label_set_text(dn_icon, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(dn_icon, lv_color_hex(0x007aff), 0);

    net_down_val = lv_label_create(dn_sec);
    lv_label_set_text(net_down_val, "0 KB/s");
    lv_obj_set_style_text_font(net_down_val, &FONT_CN_16, 0);

    /* 日期天气 */
    lv_obj_t *dw = lv_obj_create(left);
    lv_obj_set_size(dw, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(dw, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_radius(dw, 14, 0);
    lv_obj_set_style_border_width(dw, 1, 0);
    lv_obj_set_style_border_color(dw, lv_color_hex(0xe2e8f0), 0);
    lv_obj_set_style_pad_all(dw, 12, 0);
    lv_obj_clear_flag(dw, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(dw, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dw, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *date_sec = lv_obj_create(dw);
    lv_obj_set_size(date_sec, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(date_sec, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(date_sec, 0, 0);
    lv_obj_set_style_pad_all(date_sec, 0, 0);
    lv_obj_clear_flag(date_sec, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(date_sec, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(date_sec, 2, 0);

    date_main = lv_label_create(date_sec);
    lv_label_set_text(date_main, "6月26日 周四");
    lv_obj_set_style_text_color(date_main, lv_color_hex(0x222222), 0);
    lv_obj_set_style_text_font(date_main, &FONT_CN_16, 0);

    date_sub = lv_label_create(date_sec);
    lv_label_set_text(date_sub, "2025年 · 第26周");
    lv_obj_set_style_text_color(date_sub, lv_color_hex(0x999999), 0);
    lv_obj_set_style_text_font(date_sub, &FONT_CN_16, 0);

    lv_obj_t *weather_sec = lv_obj_create(dw);
    lv_obj_set_size(weather_sec, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(weather_sec, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(weather_sec, 0, 0);
    lv_obj_set_style_pad_all(weather_sec, 0, 0);
    lv_obj_clear_flag(weather_sec, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(weather_sec, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(weather_sec, 10, 0);

    weather_temp = lv_label_create(weather_sec);
    lv_label_set_text(weather_temp, "28°");
    lv_obj_set_style_text_color(weather_temp, lv_color_hex(0x222222), 0);
    lv_obj_set_style_text_font(weather_temp, &lv_font_montserrat_24, 0);

    weather_desc = lv_label_create(weather_sec);
    lv_label_set_text(weather_desc, "多云");
    lv_obj_set_style_text_color(weather_desc, lv_color_hex(0x999999), 0);
    lv_obj_set_style_text_font(weather_desc, &FONT_CN_16, 0);

    /* ===== 右侧：行情 ===== */
    finance_container = lv_obj_create(parent);
    lv_obj_set_size(finance_container, 400, LV_PCT(100));
    lv_obj_set_style_bg_opa(finance_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(finance_container, 0, 0);
    lv_obj_set_style_pad_top(finance_container, 4, 0);
    lv_obj_set_style_pad_bottom(finance_container, 4, 0);
    lv_obj_set_style_pad_hor(finance_container, 0, 0);
    lv_obj_set_style_pad_gap(finance_container, 10, 0);
    lv_obj_set_flex_flow(finance_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(finance_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(finance_container, LV_DIR_VER);
}

void page_monitor_update(void)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "%.0f%%", monitor_data.cpu_percent);
    lv_label_set_text(cpu_val, buf);
    lv_bar_set_value(cpu_bar, (int)monitor_data.cpu_percent, LV_ANIM_ON);
    snprintf(buf, sizeof(buf), "%d°C · %s", monitor_data.cpu_temp,
             monitor_data.cpu_model[0] ? monitor_data.cpu_model : "CPU");
    lv_label_set_text(cpu_detail, buf);

    snprintf(buf, sizeof(buf), "%.0f%%", monitor_data.mem_percent);
    lv_label_set_text(mem_val, buf);
    lv_bar_set_value(mem_bar, (int)monitor_data.mem_percent, LV_ANIM_ON);

    snprintf(buf, sizeof(buf), "%d%%", monitor_data.gpu_percent);
    lv_label_set_text(gpu_val, buf);
    lv_bar_set_value(gpu_bar, monitor_data.gpu_percent, LV_ANIM_ON);
    snprintf(buf, sizeof(buf), "%d°C", monitor_data.gpu_temp);
    lv_label_set_text(gpu_detail, buf);

    snprintf(buf, sizeof(buf), "%.0f%%", monitor_data.disk_percent);
    lv_label_set_text(disk_val, buf);
    lv_bar_set_value(disk_bar, (int)monitor_data.disk_percent, LV_ANIM_ON);
    if (monitor_data.disk_total > 0)
        snprintf(buf, sizeof(buf), "%.0f / %.0f GB", monitor_data.disk_used, monitor_data.disk_total);
    else
        snprintf(buf, sizeof(buf), "%.0f%%", monitor_data.disk_percent);
    lv_label_set_text(disk_detail, buf);

    snprintf(buf, sizeof(buf), "%.1f KB/s", monitor_data.net_up);
    lv_label_set_text(net_up_val, buf);
    snprintf(buf, sizeof(buf), "%.1f KB/s", monitor_data.net_down);
    lv_label_set_text(net_down_val, buf);
}

void page_monitor_update_env(const char *date, const char *sub_date, const char *temp, const char *desc)
{
    if (date_main) lv_label_set_text(date_main, date);
    if (date_sub) lv_label_set_text(date_sub, sub_date);
    if (weather_temp) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%s°", temp);
        lv_label_set_text(weather_temp, buf);
    }
    if (weather_desc) lv_label_set_text(weather_desc, desc);
}

void page_monitor_update_finance(void)
{
    while (lv_obj_get_child_cnt(finance_container) > 0)
        lv_obj_del(lv_obj_get_child(finance_container, 0));

    const char *group_names[] = {"美股", "韩股", "加密货币"};
    const char *group_keys[] = {"美股", "韩股", "币"};
    const char *session_tags[] = {"", "", "24H"};
    lv_color_t tag_colors[] = {
        lv_color_hex(0xff9500), lv_color_hex(0x999999), lv_color_hex(0x34c759)
    };

    for (int g = 0; g < 3; g++) {
        int group_count = 0;
        for (int i = 0; i < finance_count; i++) {
            if (strcmp(finance_items[i].type, group_keys[g]) == 0) group_count++;
        }
        if (group_count == 0) continue;

        lv_obj_t *section = lv_obj_create(finance_container);
        lv_obj_set_size(section, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(section, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(section, 0, 0);
        lv_obj_set_style_pad_all(section, 0, 0);
        lv_obj_set_style_pad_gap(section, 4, 0);
        lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(section, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *sec_hdr = lv_obj_create(section);
        lv_obj_set_size(sec_hdr, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(sec_hdr, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(sec_hdr, 0, 0);
        lv_obj_set_style_pad_all(sec_hdr, 0, 0);
        lv_obj_clear_flag(sec_hdr, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(sec_hdr, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_gap(sec_hdr, 6, 0);

        lv_obj_t *sec_lbl = lv_label_create(sec_hdr);
        lv_label_set_text(sec_lbl, group_names[g]);
        lv_obj_set_style_text_color(sec_lbl, lv_color_hex(0x999999), 0);
        lv_obj_set_style_text_font(sec_lbl, &FONT_CN_16, 0);

        const char *tag_text = session_tags[g];
        if (g == 0) {
            for (int i = 0; i < finance_count; i++) {
                if (strcmp(finance_items[i].type, "美股") == 0 && finance_items[i].session[0]) {
                    tag_text = finance_items[i].session;
                    break;
                }
            }
        }
        if (tag_text[0]) {
            lv_obj_t *tag = lv_label_create(sec_hdr);
            lv_label_set_text(tag, tag_text);
            lv_obj_set_style_text_color(tag, tag_colors[g], 0);
            lv_obj_set_style_text_font(tag, &FONT_CN_16, 0);
            lv_obj_set_style_bg_color(tag, tag_colors[g], 0);
            lv_obj_set_style_bg_opa(tag, 20, 0);
            lv_obj_set_style_radius(tag, 3, 0);
            lv_obj_set_style_pad_hor(tag, 4, 0);
            lv_obj_set_style_pad_ver(tag, 1, 0);
        }

        for (int i = 0; i < finance_count; i++) {
            finance_item_t *fi = &finance_items[i];
            if (strcmp(fi->type, group_keys[g]) != 0) continue;

            lv_obj_t *item = lv_obj_create(section);
            lv_obj_set_size(item, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(item, lv_color_hex(0xffffff), 0);
            lv_obj_set_style_radius(item, 10, 0);
            lv_obj_set_style_border_width(item, 1, 0);
            lv_obj_set_style_border_color(item, lv_color_hex(0xe2e8f0), 0);
            lv_obj_set_style_pad_all(item, 7, 0);
            lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_gap(item, 3, 0);

            /* 第一行：代码 + 名称 + 时段标签 */
            lv_obj_t *row1 = lv_obj_create(item);
            lv_obj_set_size(row1, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(row1, 0, 0);
            lv_obj_set_style_pad_all(row1, 0, 0);
            lv_obj_clear_flag(row1, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_gap(row1, 4, 0);

            lv_obj_t *sym = lv_label_create(row1);
            lv_label_set_text(sym, fi->symbol);
            lv_obj_set_style_text_color(sym, lv_color_hex(0x222222), 0);
            lv_obj_set_style_text_font(sym, &FONT_CN_16, 0);

            if (fi->name[0]) {
                lv_obj_t *name_lbl = lv_label_create(row1);
                lv_label_set_text(name_lbl, fi->name);
                lv_obj_set_style_text_color(name_lbl, lv_color_hex(0xbbbbbb), 0);
                lv_obj_set_style_text_font(name_lbl, &FONT_CN_16, 0);
                lv_obj_set_flex_grow(name_lbl, 1);
                lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
            } else {
                lv_obj_t *spacer = lv_obj_create(row1);
                lv_obj_set_size(spacer, 0, 0);
                lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(spacer, 0, 0);
                lv_obj_set_style_pad_all(spacer, 0, 0);
                lv_obj_set_flex_grow(spacer, 1);
            }

            /* 时段标签 */
            if (fi->session[0] && strcmp(fi->session, "盘中") != 0) {
                lv_obj_t *stag = lv_label_create(row1);
                lv_label_set_text(stag, fi->session);
                lv_obj_set_style_text_font(stag, &FONT_CN_16, 0);
                lv_color_t sc = lv_color_hex(0x007aff);
                if (strcmp(fi->session, "盘前") == 0) sc = lv_color_hex(0xff9500);
                else if (strcmp(fi->session, "盘后") == 0) sc = lv_color_hex(0x4338ca);
                else if (strcmp(fi->session, "休市") == 0) sc = lv_color_hex(0x999999);
                lv_obj_set_style_text_color(stag, sc, 0);
                lv_obj_set_style_bg_color(stag, sc, 0);
                lv_obj_set_style_bg_opa(stag, 20, 0);
                lv_obj_set_style_radius(stag, 3, 0);
                lv_obj_set_style_pad_hor(stag, 4, 0);
                lv_obj_set_style_pad_ver(stag, 1, 0);
            }

            /* 第二行：盘中 价格 + 涨跌 */
            float show_price = fi->regular_price > 0 ? fi->regular_price : fi->price;
            float show_change = fi->regular_price > 0 ? fi->regular_change : fi->change;

            lv_obj_t *row2 = lv_obj_create(item);
            lv_obj_set_size(row2, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_opa(row2, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(row2, 0, 0);
            lv_obj_set_style_pad_all(row2, 0, 0);
            lv_obj_clear_flag(row2, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_gap(row2, 4, 0);

            /* "盘中" 标签 */
            if (g == 0) {
                lv_obj_t *lbl = lv_label_create(row2);
                lv_label_set_text(lbl, "盘中");
                lv_obj_set_style_text_color(lbl, lv_color_hex(0x999999), 0);
                lv_obj_set_style_text_font(lbl, &FONT_CN_16, 0);
            }

            char p[32];
            if (g == 2)
                snprintf(p, sizeof(p), "$%.0f", show_price);
            else if (show_price >= 1000)
                snprintf(p, sizeof(p), "$%.0f", show_price);
            else
                snprintf(p, sizeof(p), "$%.2f", show_price);
            lv_obj_t *price = lv_label_create(row2);
            lv_label_set_text(price, p);
            lv_obj_set_style_text_color(price, lv_color_hex(0x333333), 0);
            lv_obj_set_style_text_font(price, &FONT_CN_16, 0);

            bool up = show_change >= 0;
            char c[16]; snprintf(c, sizeof(c), "%s%.2f%%", up ? "+" : "", show_change);
            lv_obj_t *chg = lv_label_create(row2);
            lv_label_set_text(chg, c);
            lv_obj_set_style_text_color(chg, lv_color_hex(up ? 0x34c759 : 0xff3b30), 0);
            lv_obj_set_style_text_font(chg, &FONT_CN_16, 0);
            lv_obj_set_style_bg_color(chg, lv_color_hex(up ? 0x34c759 : 0xff3b30), 0);
            lv_obj_set_style_bg_opa(chg, 13, 0);
            lv_obj_set_style_radius(chg, 5, 0);
            lv_obj_set_style_pad_hor(chg, 6, 0);
            lv_obj_set_style_pad_ver(chg, 2, 0);

            /* 第三行（美股）：盘前或盘后价格 */
            if (g == 0) {
                float ext_price = 0;
                const char *ext_label = NULL;
                if (fi->pre_price > 0 && strcmp(fi->session, "盘前") == 0) {
                    ext_price = fi->pre_price;
                    ext_label = "盘前";
                } else if (fi->post_price > 0 && strcmp(fi->session, "盘后") == 0) {
                    ext_price = fi->post_price;
                    ext_label = "盘后";
                }
                if (ext_price > 0 && ext_label) {
                    float ext_chg = show_price > 0 ? ((ext_price - show_price) / show_price * 100) : 0;
                    lv_obj_t *row3 = lv_obj_create(item);
                    lv_obj_set_size(row3, LV_PCT(100), LV_SIZE_CONTENT);
                    lv_obj_set_style_bg_opa(row3, LV_OPA_TRANSP, 0);
                    lv_obj_set_style_border_width(row3, 0, 0);
                    lv_obj_set_style_pad_all(row3, 0, 0);
                    lv_obj_clear_flag(row3, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_set_flex_flow(row3, LV_FLEX_FLOW_ROW);
                    lv_obj_set_flex_align(row3, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
                    lv_obj_set_style_pad_gap(row3, 4, 0);

                    lv_obj_t *el = lv_label_create(row3);
                    lv_label_set_text(el, ext_label);
                    lv_obj_set_style_text_color(el, lv_color_hex(0x999999), 0);
                    lv_obj_set_style_text_font(el, &FONT_CN_16, 0);

                    char ep[32];
                    snprintf(ep, sizeof(ep), "$%.2f", ext_price);
                    lv_obj_t *ep_lbl = lv_label_create(row3);
                    lv_label_set_text(ep_lbl, ep);
                    lv_obj_set_style_text_color(ep_lbl, lv_color_hex(0x333333), 0);
                    lv_obj_set_style_text_font(ep_lbl, &FONT_CN_16, 0);

                    bool eup = ext_chg >= 0;
                    char ec[16]; snprintf(ec, sizeof(ec), "%s%.2f%%", eup ? "+" : "", ext_chg);
                    lv_obj_t *ec_lbl = lv_label_create(row3);
                    lv_label_set_text(ec_lbl, ec);
                    lv_obj_set_style_text_color(ec_lbl, lv_color_hex(eup ? 0x34c759 : 0xff3b30), 0);
                    lv_obj_set_style_text_font(ec_lbl, &FONT_CN_16, 0);
                    lv_obj_set_style_bg_color(ec_lbl, lv_color_hex(eup ? 0x34c759 : 0xff3b30), 0);
                    lv_obj_set_style_bg_opa(ec_lbl, 13, 0);
                    lv_obj_set_style_radius(ec_lbl, 5, 0);
                    lv_obj_set_style_pad_hor(ec_lbl, 6, 0);
                    lv_obj_set_style_pad_ver(ec_lbl, 2, 0);
                }
            }
        }
    }
}
