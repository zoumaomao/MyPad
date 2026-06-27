#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "serial_comm.h"
#include "ui.h"
#include "mbedtls/base64.h"
#include "display.h"
#include "cJSON.h"
#include "mbedtls/base64.h"

#define ICON_SIZE 80
#define ICON_RAW_BYTES (ICON_SIZE * ICON_SIZE * 2)

static const char *TAG = "serial";
static SemaphoreHandle_t uart_tx_mutex = NULL;
static FILE *transfer_file = NULL;

static void parse_env(const char *json)
{
    ESP_LOGI(TAG, "Parsing ENV: %s", json);
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGE(TAG, "ENV JSON parse failed");
        return;
    }
    
    cJSON *time = cJSON_GetObjectItem(root, "time");
    cJSON *date = cJSON_GetObjectItem(root, "date");
    cJSON *sub_date = cJSON_GetObjectItem(root, "sub_date");
    cJSON *temp = cJSON_GetObjectItem(root, "temp");
    cJSON *desc = cJSON_GetObjectItem(root, "desc");
    
    if (cJSON_IsString(time) && cJSON_IsString(date) && cJSON_IsString(sub_date) && cJSON_IsString(temp) && cJSON_IsString(desc)) {
        if (display_lock(500)) {
            ui_update_clock(time->valuestring);
            page_monitor_update_env(date->valuestring, sub_date->valuestring, temp->valuestring, desc->valuestring);
            display_unlock();
            ESP_LOGI(TAG, "ENV updated on UI: %s", time->valuestring);
        } else {
            ESP_LOGE(TAG, "ENV display_lock failed!");
        }
    } else {
        ESP_LOGE(TAG, "ENV JSON fields invalid");
    }
    cJSON_Delete(root);
}

static void parse_icon(const char *data)
{
    int slot = 0;
    const char *b64 = strchr(data, ':');
    if (!b64 || b64 - data != 1) return;
    slot = data[0] - '0';
    if (slot < 0 || slot >= SLOT_COUNT) return;
    b64++;

    uint8_t raw[ICON_RAW_BYTES + 16];
    size_t olen = 0;
    int ret = mbedtls_base64_decode(raw, sizeof(raw), &olen,
                                     (const uint8_t *)b64, strlen(b64));
    if (ret != 0 || olen < ICON_RAW_BYTES) {
        ESP_LOGW(TAG, "Icon base64 decode fail: ret=%d olen=%d", ret, (int)olen);
        return;
    }
    page_app_set_icon(slot, (const uint16_t *)raw);
    ESP_LOGI(TAG, "Icon slot %d loaded (%d bytes)", slot, (int)olen);
}

static void parse_slot(const char *data)
{
    int id = 0;
    char name[32] = {0};
    if (sscanf(data, "%d|%31[^|]", &id, name) >= 2) {
        if (id >= 1 && id <= SLOT_COUNT) {
            app_slot_t *slot = &app_slots[id - 1];
            strncpy(slot->name, name, sizeof(slot->name) - 1);
            slot->active = true;
            ESP_LOGI(TAG, "Slot %d: %s", id, name);
        }
    }
}

static void parse_monitor(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;
    cJSON *v;
    if ((v = cJSON_GetObjectItem(root, "cpu_percent")))
        monitor_data.cpu_percent = (float)v->valuedouble;
    if ((v = cJSON_GetObjectItem(root, "cpu_temp")))
        monitor_data.cpu_temp = v->valueint;
    if ((v = cJSON_GetObjectItem(root, "cpu_model")) && v->valuestring)
        strncpy(monitor_data.cpu_model, v->valuestring, sizeof(monitor_data.cpu_model) - 1);
    if ((v = cJSON_GetObjectItem(root, "mem_percent")))
        monitor_data.mem_percent = (float)v->valuedouble;
    if ((v = cJSON_GetObjectItem(root, "mem_used")))
        monitor_data.mem_used = (float)v->valuedouble;
    if ((v = cJSON_GetObjectItem(root, "mem_total")))
        monitor_data.mem_total = (float)v->valuedouble;
    if ((v = cJSON_GetObjectItem(root, "gpu_percent")))
        monitor_data.gpu_percent = v->valueint;
    if ((v = cJSON_GetObjectItem(root, "gpu_temp")))
        monitor_data.gpu_temp = v->valueint;
    if ((v = cJSON_GetObjectItem(root, "disk_percent")))
        monitor_data.disk_percent = (float)v->valuedouble;
    if ((v = cJSON_GetObjectItem(root, "disk_used")))
        monitor_data.disk_used = (float)v->valuedouble;
    if ((v = cJSON_GetObjectItem(root, "disk_total")))
        monitor_data.disk_total = (float)v->valuedouble;
    if ((v = cJSON_GetObjectItem(root, "net_up")))
        monitor_data.net_up = (float)v->valuedouble;
    if ((v = cJSON_GetObjectItem(root, "net_down")))
        monitor_data.net_down = (float)v->valuedouble;
    /* 更新时钟 */
    if ((v = cJSON_GetObjectItem(root, "time_str")) && v->valuestring) {
        if (display_lock(50)) {
            ui_update_clock(v->valuestring);
            display_unlock();
        }
    }
    cJSON_Delete(root);
}

static void parse_finance(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return;
    finance_count = 0;
    memset(finance_items, 0, sizeof(finance_items));

    const char *keys[] = {"us_stocks", "kr_stocks", "crypto"};
    const char *types[] = {"\u7f8e\u80a1", "\u97e9\u80a1", "\u5e01"};
    for (int k = 0; k < 3 && finance_count < MAX_FINANCE; k++) {
        cJSON *section = cJSON_GetObjectItem(root, keys[k]);
        if (!section) continue;
        cJSON *item;
        cJSON_ArrayForEach(item, section) {
            if (finance_count >= MAX_FINANCE) break;
            finance_item_t *fi = &finance_items[finance_count];
            strncpy(fi->symbol, item->string, sizeof(fi->symbol) - 1);
            strncpy(fi->type, types[k], sizeof(fi->type) - 1);
            cJSON *p = cJSON_GetObjectItem(item, "price");
            if (p) fi->price = (float)p->valuedouble;
            cJSON *c = cJSON_GetObjectItem(item, "change");
            if (c) fi->change = (float)c->valuedouble;
            cJSON *rp = cJSON_GetObjectItem(item, "regular_price");
            if (rp) fi->regular_price = (float)rp->valuedouble;
            cJSON *rc = cJSON_GetObjectItem(item, "regular_change");
            if (rc) fi->regular_change = (float)rc->valuedouble;
            cJSON *pp = cJSON_GetObjectItem(item, "pre_price");
            if (pp && !cJSON_IsNull(pp)) fi->pre_price = (float)pp->valuedouble;
            cJSON *op = cJSON_GetObjectItem(item, "post_price");
            if (op && !cJSON_IsNull(op)) fi->post_price = (float)op->valuedouble;
            cJSON *n = cJSON_GetObjectItem(item, "name");
            if (n && n->valuestring)
                strncpy(fi->name, n->valuestring, sizeof(fi->name) - 1);
            cJSON *s = cJSON_GetObjectItem(item, "session");
            if (s && s->valuestring)
                strncpy(fi->session, s->valuestring, sizeof(fi->session) - 1);
            finance_count++;
        }
    }
    cJSON_Delete(root);
}

static void parse_calendar(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root || !cJSON_IsArray(root)) { cJSON_Delete(root); return; }
    calendar_count = 0;
    memset(calendar_items, 0, sizeof(calendar_items));
    int size = cJSON_GetArraySize(root);
    for (int i = 0; i < size && i < MAX_CALENDAR; i++) {
        cJSON *item = cJSON_GetArrayItem(root, i);
        calendar_item_t *ci = &calendar_items[calendar_count];
        cJSON *ev = cJSON_GetObjectItem(item, "event_zh");
        if (!ev || !ev->valuestring) ev = cJSON_GetObjectItem(item, "event");
        if (ev && ev->valuestring)
            strncpy(ci->event, ev->valuestring, sizeof(ci->event) - 1);
        cJSON *co = cJSON_GetObjectItem(item, "country");
        if (co && co->valuestring)
            strncpy(ci->country, co->valuestring, sizeof(ci->country) - 1);
        cJSON *im = cJSON_GetObjectItem(item, "impact");
        if (im && im->valuestring)
            strncpy(ci->impact, im->valuestring, sizeof(ci->impact) - 1);
        cJSON *dt = cJSON_GetObjectItem(item, "date");
        if (dt && dt->valuestring) {
            const char *s = dt->valuestring;
            /* 格式: "2026-06-27T14:30:00" → "06/27 14:30" */
            if (strlen(s) >= 16 && s[4] == '-' && s[7] == '-') {
                snprintf(ci->time_str, sizeof(ci->time_str), "%.5s %.5s", s + 5, s + 11);
            } else if (strlen(s) >= 5) {
                strncpy(ci->time_str, s, sizeof(ci->time_str) - 1);
            }
            ci->time_str[sizeof(ci->time_str) - 1] = '\0';
        }
        cJSON *fc = cJSON_GetObjectItem(item, "forecast");
        if (fc && fc->valuestring)
            strncpy(ci->forecast, fc->valuestring, sizeof(ci->forecast) - 1);
        cJSON *pv = cJSON_GetObjectItem(item, "previous");
        if (pv && pv->valuestring)
            strncpy(ci->previous, pv->valuestring, sizeof(ci->previous) - 1);
        calendar_count++;
    }
    cJSON_Delete(root);
}

static void parse_news(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root || !cJSON_IsArray(root)) { cJSON_Delete(root); return; }
    news_count = 0;
    memset(news_items, 0, sizeof(news_items));
    int size = cJSON_GetArraySize(root);
    for (int i = 0; i < size && i < MAX_NEWS; i++) {
        cJSON *item = cJSON_GetArrayItem(root, i);
        news_item_t *ni = &news_items[news_count];
        cJSON *t = cJSON_GetObjectItem(item, "title");
        if (t && t->valuestring)
            strncpy(ni->title, t->valuestring, sizeof(ni->title) - 1);
        cJSON *d = cJSON_GetObjectItem(item, "desc");
        if (d && d->valuestring)
            strncpy(ni->desc, d->valuestring, sizeof(ni->desc) - 1);
        cJSON *sr = cJSON_GetObjectItem(item, "source");
        if (sr && sr->valuestring)
            strncpy(ni->source, sr->valuestring, sizeof(ni->source) - 1);
        cJSON *tm = cJSON_GetObjectItem(item, "time");
        if (tm && tm->valuestring)
            strncpy(ni->time, tm->valuestring, sizeof(ni->time) - 1);
        news_count++;
    }
    cJSON_Delete(root);
}

static void process_line(const char *line)
{
    if (strncmp(line, "SLOT:", 5) == 0) {
        parse_slot(line + 5);
        if (display_lock(100)) {
            page_app_update();
            display_unlock();
        }
    } else if (strncmp(line, "MON:", 4) == 0) {
        parse_monitor(line + 4);
        if (ui_get_page() == 1 && display_lock(100)) {
            page_monitor_update();
            display_unlock();
        }
    } else if (strncmp(line, "FINANCE:", 8) == 0) {
        parse_finance(line + 8);
        if (display_lock(100)) {
            page_monitor_update_finance();
            display_unlock();
        }
    } else if (strncmp(line, "NEWS:", 5) == 0) {
        parse_news(line + 5);
        if (display_lock(100)) {
            page_news_update();
            page_news_update_flash();
            display_unlock();
        }
    } else if (strncmp(line, "CALENDAR:", 9) == 0) {
        parse_calendar(line + 9);
        if (display_lock(100)) {
            page_news_update_calendar();
            display_unlock();
        }
    } else if (strncmp(line, "ICON:", 5) == 0) {
        if (display_lock(200)) {
            parse_icon(line + 5);
            display_unlock();
        }
    } else if (strncmp(line, "BRIGHTNESS:", 11) == 0) {
        int val = atoi(line + 11);
        display_set_brightness(val);
        ESP_LOGI(TAG, "Brightness set to %d%%", val);
    } else if (strncmp(line, "TIMEOUT:", 8) == 0) {
        int val = atoi(line + 8);
        ui_set_screen_timeout(val);
        ESP_LOGI(TAG, "Screen timeout set to %ds", val);
    } else if (strcmp(line, "SYNC_START") == 0) {
        if (display_lock(200)) {
            memset(app_slots, 0, sizeof(app_slots));
            finance_count = 0;
            news_count = 0;
            calendar_count = 0;
            display_unlock();
        }
    } else if (strcmp(line, "SYNC_END") == 0) {
        if (display_lock(100)) {
            page_app_update();
            page_monitor_update_finance();
            page_news_update();
            page_news_update_flash();
            page_news_update_calendar();
            display_unlock();
        }
    } else if (strncmp(line, "ENV:", 4) == 0) {
        parse_env(line + 4);
    } else if (strncmp(line, "FILE_START:", 11) == 0) {
        char *p = strchr(line + 11, '|');
        if (p) {
            *p = '\0';
            char path[128];
            snprintf(path, sizeof(path), "/sdcard/%s", line + 11);
            if (transfer_file) fclose(transfer_file);
            transfer_file = fopen(path, "wb");
            if (transfer_file) {
                ESP_LOGI(TAG, "File transfer started: %s", path);
                serial_comm_send("FILE_ACK");
            } else {
                ESP_LOGE(TAG, "Failed to open file: %s", path);
                serial_comm_send("FILE_ERR");
            }
        }
    } else if (strncmp(line, "FILE_DATA:", 10) == 0) {
        if (transfer_file) {
            size_t len = strlen(line + 10);
            size_t olen = 0;
            unsigned char *buf = malloc(len);
            if (buf) {
                mbedtls_base64_decode(buf, len, &olen, (const unsigned char *)line + 10, len);
                fwrite(buf, 1, olen, transfer_file);
                free(buf);
            }
            serial_comm_send("FILE_ACK");
        }
    } else if (strcmp(line, "FILE_END") == 0) {
        if (transfer_file) {
            fclose(transfer_file);
            transfer_file = NULL;
            ESP_LOGI(TAG, "File transfer completed");
            serial_comm_send("FILE_ACK");
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        }
    } else if (strcmp(line, "PING") == 0) {
        serial_comm_send("PONG");
    }
}

#include "driver/usb_serial_jtag.h"

static void uart_rx_task(void *arg)
{
    char buf[128];
    char *line_buf = malloc(SERIAL_BUF_SIZE);
    if (!line_buf) {
        ESP_LOGE(TAG, "Failed to alloc line_buf");
        vTaskDelete(NULL);
        return;
    }
    int line_pos = 0;
    while (1) {
        int len = uart_read_bytes(UART_PORT, buf, sizeof(buf) - 1, pdMS_TO_TICKS(10));
        if (len <= 0) {
            len = usb_serial_jtag_read_bytes(buf, sizeof(buf) - 1, pdMS_TO_TICKS(10));
        }
        
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                char c = buf[i];
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buf[line_pos] = '\0';
                        process_line(line_buf);
                        line_pos = 0;
                    }
                } else if (line_pos < SERIAL_BUF_SIZE - 1) {
                    line_buf[line_pos++] = c;
                }
            }
        }
    }
}

void serial_comm_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_PORT, 32768, 1024, 0, NULL, 0);
    uart_param_config(UART_PORT, &uart_config);
    uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    
    usb_serial_jtag_driver_config_t jtag_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&jtag_cfg);

    uart_tx_mutex = xSemaphoreCreateMutex();
    assert(uart_tx_mutex);
    xTaskCreate(uart_rx_task, "uart_rx", 32768, NULL, 10, NULL);
    ESP_LOGI(TAG, "Serial init 115200 and USB-JTAG");
}

void serial_comm_send(const char *msg)
{
    if (xSemaphoreTake(uart_tx_mutex, pdMS_TO_TICKS(100))) {
        uart_write_bytes(UART_PORT, msg, strlen(msg));
        uart_write_bytes(UART_PORT, "\n", 1);
        usb_serial_jtag_write_bytes(msg, strlen(msg), pdMS_TO_TICKS(10));
        usb_serial_jtag_write_bytes("\n", 1, pdMS_TO_TICKS(10));
        xSemaphoreGive(uart_tx_mutex);
    }
}
