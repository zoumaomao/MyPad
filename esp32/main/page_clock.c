#include <stdio.h>
#include "lvgl.h"
#include "ui.h"
#include "display.h"
#include "fonts/font_chinese.h"

static lv_obj_t *clock_time_label;
static lv_obj_t *clock_date_label;
static lv_obj_t *clock_sub_label;
static lv_obj_t *clock_temp_label;
static lv_obj_t *clock_bg;

void page_clock_init(lv_obj_t *parent)
{
    /* 背景：尝试加载壁纸，失败则用深色渐变 */
    clock_bg = lv_obj_create(parent);
    lv_obj_set_size(clock_bg, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(clock_bg, 0, 0);
    lv_obj_set_style_pad_all(clock_bg, 0, 0);
    lv_obj_set_style_radius(clock_bg, 0, 0);

    lv_obj_t *wallpaper_img = lv_image_create(clock_bg);
    lv_obj_set_size(wallpaper_img, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_border_width(wallpaper_img, 0, 0);
    lv_obj_set_style_radius(wallpaper_img, 0, 0);
    lv_image_set_src(wallpaper_img, "S:wallpaper.jpg");
    lv_obj_set_style_image_opa(wallpaper_img, LV_OPA_COVER, 0);

    /* 半透明遮罩层（让时间文字可读） */
    lv_obj_t *overlay = lv_obj_create(clock_bg);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_30, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);  /* 不穿透触摸 */

    /* Fallback: 渐变背景色（壁纸加载失败时可见） */
    lv_obj_set_style_bg_color(clock_bg, lv_color_hex(0x0b1721), 0);
    lv_obj_set_style_bg_grad_color(clock_bg, lv_color_hex(0x1a2a3a), 0);
    lv_obj_set_style_bg_grad_dir(clock_bg, LV_GRAD_DIR_VER, 0);

    /* 大时间 */
    clock_time_label = lv_label_create(clock_bg);
    lv_obj_set_style_text_color(clock_time_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(clock_time_label, &lv_font_montserrat_32, 0);
    lv_label_set_text(clock_time_label, "00:00");
    lv_obj_align(clock_time_label, LV_ALIGN_CENTER, 0, -60);

    /* 日期 */
    clock_date_label = lv_label_create(clock_bg);
    lv_obj_set_style_text_color(clock_date_label, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_text_font(clock_date_label, &FONT_CN_16, 0);
    lv_label_set_text(clock_date_label, "6月28日 周日");
    lv_obj_align(clock_date_label, LV_ALIGN_CENTER, 0, -20);

    /* 副行 */
    clock_sub_label = lv_label_create(clock_bg);
    lv_obj_set_style_text_color(clock_sub_label, lv_color_hex(0x999999), 0);
    lv_obj_set_style_text_font(clock_sub_label, &FONT_CN_16, 0);
    lv_label_set_text(clock_sub_label, "2026年 · 第26周");
    lv_obj_align(clock_sub_label, LV_ALIGN_CENTER, 0, 15);

    /* 天气 */
    clock_temp_label = lv_label_create(clock_bg);
    lv_obj_set_style_text_color(clock_temp_label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(clock_temp_label, &FONT_CN_16, 0);
    lv_label_set_text(clock_temp_label, "25°  晴");
    lv_obj_align(clock_temp_label, LV_ALIGN_CENTER, 0, 50);
}

void page_clock_update(void)
{
    /* Time updated via page_clock_update_env */
}

void page_clock_update_env(const char *time_str, const char *date, const char *sub_date,
                            const char *temp, const char *desc)
{
    if (clock_time_label) lv_label_set_text(clock_time_label, time_str);
    if (clock_date_label) lv_label_set_text(clock_date_label, date);
    if (clock_sub_label) lv_label_set_text(clock_sub_label, sub_date);
    if (clock_temp_label) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s°  %s", temp, desc);
        lv_label_set_text(clock_temp_label, buf);
    }
}
