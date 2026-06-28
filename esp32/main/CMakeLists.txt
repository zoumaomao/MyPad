idf_component_register(
    SRCS "main.c" "display.c" "ui.c" "serial_comm.c" "page_app.c" "page_monitor.c" "page_news.c" "page_clock.c" "fonts/font_cn_full_16.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl esp_lcd esp_timer driver nvs_flash json mbedtls
    PRIV_REQUIRES fatfs sdmmc
)
