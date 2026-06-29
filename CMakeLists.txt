idf_component_register(
    SRCS 
        "lvgl_display.c"
        "flow_bridge.cpp"
        "lvglui/eez-flow.cpp"
        "lvglui/screens.c"
        "lvglui/styles.c"
        "lvglui/images.c"
        "lvglui/ui.c"
        "lvglui/ui_image_battery.c"
        "lvglui/ui_image_camera.c"
        "lvglui/ui_image_garden.c"
        "lvglui/ui_image_music.c"
        "lvglui/ui_image_setting.c"
        "lvglui/ui_image_stopwatch.c"
        "lvglui/ui_image_calender.c"
        "lvglui/ui_image_clock.c"
        "lvglui/ui_image_moon.c"
        "lvglui/ui_image_walking.c"
    INCLUDE_DIRS "." "lvglui"
    REQUIRES esp_lcd driver esp_lvgl_port lvgl
)