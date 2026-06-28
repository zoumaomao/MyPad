#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "ui.h"
#include "display.h"
#include "fonts/font_chinese.h"

static lv_obj_t *idx_containers[MAX_INDICES];
static lv_obj_t *idx_names[MAX_INDICES];
static lv_obj_t *idx_prices[MAX_INDICES];
static lv_obj_t *idx_changes[MAX_INDICES];
static lv_obj_t *idx_sessions[MAX_INDICES];

void page_clock_init(lv_obj_t *parent)
{
    /* 暗色背景 */
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0f1722), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(parent, 20, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (int i = 0; i < MAX_INDICES; i++) {
        idx_containers[i] = lv_obj_create(parent);
        lv_obj_set_size(idx_containers[i], 310, 160);
        lv_obj_set_style_bg_color(idx_containers[i], lv_color_hex(0x1a2332), 0);
        lv_obj_set_style_border_width(idx_containers[i], 0, 0);
        lv_obj_set_style_radius(idx_containers[i], 12, 0);
        lv_obj_set_style_pad_all(idx_containers[i], 14, 0);
        lv_obj_set_flex_flow(idx_containers[i], LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_border_side(idx_containers[i], LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_color(idx_containers[i], lv_color_hex(0x34c759), 0);
        lv_obj_set_style_border_width(idx_containers[i], 3, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(idx_containers[i], LV_OPA_0, LV_PART_MAIN | LV_STATE_DEFAULT);

        idx_names[i] = lv_label_create(idx_containers[i]);
        lv_obj_set_style_text_color(idx_names[i], lv_color_hex(0x8899aa), 0);
        lv_obj_set_style_text_font(idx_names[i], &lv_font_montserrat_14, 0);
        lv_label_set_text(idx_names[i], "------");

        idx_sessions[i] = lv_label_create(idx_containers[i]);
        lv_obj_set_style_text_font(idx_sessions[i], &FONT_CN_16, 0);

        idx_prices[i] = lv_label_create(idx_containers[i]);
        lv_obj_set_style_text_color(idx_prices[i], lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(idx_prices[i], &lv_font_montserrat_28, 0);
        lv_label_set_text(idx_prices[i], "----.--");

        idx_changes[i] = lv_label_create(idx_containers[i]);
        lv_obj_set_style_text_font(idx_changes[i], &lv_font_montserrat_14, 0);
        lv_label_set_text(idx_changes[i], "--");
    }

    /* 加载壁纸（如果存在）做半透明背景 */
    lv_obj_t *wp = lv_image_create(lv_obj_get_parent(parent));
    lv_obj_set_size(wp, 1024, 600);
    lv_image_set_src(wp, "S:wallpaper.jpg");
    lv_obj_set_style_image_opa(wp, LV_OPA_10, 0);
    lv_obj_move_to_index(wp, 0);
}

void page_clock_update(void)
{
    /* Updated via page_clock_update_indices */
}

void page_clock_update_env(const char *time_str, const char *date, const char *sub_date,
                            const char *temp, const char *desc)
{
    /* Clock/env not shown on market page */
}

void page_clock_update_indices(void)
{
    for (int i = 0; i < MAX_INDICES; i++) {
        if (!idx_containers[i]) continue;
        if (i < index_count && index_items[i].name[0]) {
            index_item_t *ix = &index_items[i];

            lv_label_set_text(idx_names[i], ix->name);

            char price_buf[32];
            if (ix->price >= 1000)
                snprintf(price_buf, sizeof(price_buf), "%.0f", ix->price);
            else
                snprintf(price_buf, sizeof(price_buf), "%.2f", ix->price);
            lv_label_set_text(idx_prices[i], price_buf);

            char chg_buf[32];
            snprintf(chg_buf, sizeof(chg_buf), "%+.2f%%", ix->change);
            lv_label_set_text(idx_changes[i], chg_buf);
            lv_obj_set_style_text_color(idx_changes[i],
                ix->change >= 0 ? lv_color_hex(0x34c759) : lv_color_hex(0xff3b30), 0);

            /* 左边框颜色 */
            lv_obj_set_style_border_opa(idx_containers[i], LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(idx_containers[i],
                ix->change >= 0 ? lv_color_hex(0x34c759) : lv_color_hex(0xff3b30), 0);

            /* 市场时段标签 */
            lv_label_set_text(idx_sessions[i], ix->session);
            if (strcmp(ix->session, "盘中") == 0)
                lv_obj_set_style_text_color(idx_sessions[i], lv_color_hex(0x34c759), 0);
            else if (strcmp(ix->session, "休市") == 0)
                lv_obj_set_style_text_color(idx_sessions[i], lv_color_hex(0x556677), 0);
            else
                lv_obj_set_style_text_color(idx_sessions[i], lv_color_hex(0xff9500), 0);

            lv_obj_clear_flag(idx_containers[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(idx_sessions[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(idx_containers[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}
