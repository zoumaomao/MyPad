#pragma once
#include "lvgl.h"

extern const lv_font_t font_cn_full_16;
extern lv_font_t *ui_font_cn_16;

#define FONT_CN_16  (*ui_font_cn_16)
#define FONT_ICON   lv_font_montserrat_28
