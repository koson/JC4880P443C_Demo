idf_component_register(
    SRCS
        "jc4880p443c_demo.c"
        "dashboard_ui.c"
        "wifi_manager.c"
    INCLUDE_DIRS "."
    REQUIRES
        jc4880p443c
        lvgl
        esp_lcd_st7701
        esp_timer
    PRIV_REQUIRES
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
)
