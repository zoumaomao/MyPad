#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

void display_init(void);
void display_start_lvgl_task(void);
bool display_lock(uint32_t timeout_ms);
void display_unlock(void);
void display_set_brightness(int percent);
lv_indev_t *display_get_touch_indev(void);
