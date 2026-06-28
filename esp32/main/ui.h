#pragma once
#include "lvgl.h"

#define SCREEN_WIDTH    1024
#define SCREEN_HEIGHT   600
#define PAGE_COUNT      4
#define SLOT_COUNT      6
#define SERIAL_BUF_SIZE 20480

typedef struct {
    char name[32];
    bool active;
} app_slot_t;

typedef struct {
    float cpu_percent;
    int   cpu_temp;
    char  cpu_model[64];
    float mem_percent;
    float mem_used;
    float mem_total;
    int   gpu_percent;
    int   gpu_temp;
    float disk_percent;
    float disk_used;
    float disk_total;
    float net_up;
    float net_down;
} monitor_data_t;

typedef struct {
    char symbol[16];
    char name[64];
    float price;
    float change;
    float regular_price;
    float regular_change;
    float pre_price;
    float post_price;
    char session[8];
    char type[8];
} finance_item_t;

typedef struct {
    char title[256];
    char desc[256];
    char source[32];
    char time[64];
} news_item_t;

typedef struct {
    char event[128];
    char country[8];
    char impact[8];
    char time_str[16];
    char forecast[32];
    char previous[32];
} calendar_item_t;

#define MAX_FINANCE 12
#define MAX_NEWS    20
#define MAX_CALENDAR 10

extern app_slot_t app_slots[SLOT_COUNT];
extern monitor_data_t monitor_data;
extern finance_item_t finance_items[MAX_FINANCE];
extern int finance_count;
extern news_item_t news_items[MAX_NEWS];
extern int news_count;
extern calendar_item_t calendar_items[MAX_CALENDAR];
extern int calendar_count;
extern bool sdcard_ready;

void ui_init(void);
void ui_set_page(int page);
int  ui_get_page(void);
void ui_update_finance(void);
void ui_update_news(void);
bool ui_is_swiping(void);
void ui_update_clock(const char *time_str);
void ui_set_screen_timeout(int seconds);

void page_app_init(lv_obj_t *parent);
void page_app_update(void);
void page_app_set_icon(int slot, const uint16_t *rgb565);
void page_monitor_init(lv_obj_t *parent);
void page_monitor_update(void);
void page_monitor_update_finance(void);
void page_monitor_update_env(const char *date, const char *sub_date, const char *temp, const char *desc);
void page_news_init(lv_obj_t *parent);
void page_news_update(void);
void page_news_update_flash(void);
void page_news_update_calendar(void);
void page_clock_init(lv_obj_t *parent);
void page_clock_update(void);
void page_clock_update_env(const char *time_str, const char *date, const char *sub_date,
                            const char *temp, const char *desc);

void serial_comm_init(void);
void serial_comm_send(const char *msg);
