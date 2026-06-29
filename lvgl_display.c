#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "ui.h"
#include "lvglui/vars.h"
#include "lvglui/screens.h"
#include "flow_bridge.h"

// --- Pin definitions ---
#define LCD_MOSI     38
#define LCD_SCLK     39
#define LCD_CS       45
#define LCD_DC       42
#define LCD_RST      -1
#define LCD_BL       1

// --- Display resolution ---
#define LCD_H_RES    240
#define LCD_V_RES    320

// --- SPI ---
#define LCD_SPI_HOST    SPI2_HOST
#define LCD_SPI_CLK_HZ  (40 * 1000 * 1000)

// --- Physical buttons ---
#define PHYSICAL_NEXT_BTN_GPIO  GPIO_NUM_0   // BOOT button (active LOW) -> cycles highlight
#define PHYSICAL_OK_BTN_GPIO    GPIO_NUM_10  // TTP223 touch module (active HIGH) -> selects

static const char *TAG = "MAIN";
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static lv_display_t *display = NULL;

// --- NEXT: lets the Flow's own SetVariable node handle current_btn ---
static void trigger_next_button(void)
{
    lvgl_port_lock(0);
    if (objects.nextbtn != NULL) {
        lv_obj_send_event(objects.nextbtn, LV_EVENT_SHORT_CLICKED, NULL);
    }
    lvgl_port_unlock();

    // Flow queue runs on the next ui_tick() — value reflects a tick later, not synchronously
    int current_btn = eez_get_global_variable_int(FLOW_GLOBAL_VARIABLE_CURRENT_BTN);
    ESP_LOGI(TAG, "next pressed, current_btn (may lag one tick) = %d", current_btn);
}

// --- OK: confirms selection on current_btn ---
static void trigger_ok_button(void)
{
    int current = eez_get_global_variable_int(FLOW_GLOBAL_VARIABLE_CURRENT_BTN);
    ESP_LOGI(TAG, "OK pressed, current_btn = %d", current);

    lvgl_port_lock(0);
    if (objects.okbtn != NULL) {
        lv_obj_send_event(objects.okbtn, LV_EVENT_SHORT_CLICKED, NULL);
    }
    lvgl_port_unlock();
}


typedef void (*button_cb_t)(void);

typedef struct {
    gpio_num_t gpio;
    button_cb_t on_press;
    bool active_high; // true = TTP223 touch module, false = simple push button to GND
} button_task_arg_t;

// --- Generic debounced button poller, supports both active-low and active-high inputs ---
static void physical_button_task(void *pvParameter)
{
    button_task_arg_t *arg = (button_task_arg_t *)pvParameter;
    gpio_num_t gpio = arg->gpio;
    button_cb_t on_press = arg->on_press;
    bool active_high = arg->active_high;

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = active_high ? GPIO_PULLUP_DISABLE : GPIO_PULLUP_ENABLE,
        .pull_down_en = active_high ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    int idle_level = active_high ? 0 : 1;
    int pressed_level = active_high ? 1 : 0;
    int last_level = idle_level;

    while (true) {
        int level = gpio_get_level(gpio);

        if (last_level == idle_level && level == pressed_level) {
            vTaskDelay(pdMS_TO_TICKS(30)); // debounce
            if (gpio_get_level(gpio) == pressed_level) {
                ESP_LOGI(TAG, "GPIO %d press detected", gpio);
                on_press();
                while (gpio_get_level(gpio) == pressed_level) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            }
        }

        last_level = level;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// --- NEW: drives the EEZ Flow queue (SetVariable, Watch, etc. only run via this) ---
static void flow_tick_task(void *pvParameter)
{
    while (true) {
        lvgl_port_lock(0);
        ui_tick();
        lvgl_port_unlock();
        vTaskDelay(pdMS_TO_TICKS(33)); // ~60Hz
    }
}

// --- Display init ---
static void display_init(void)
{
    ESP_LOGI(TAG, "Backlight ON");
    gpio_set_direction(LCD_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_BL, 1);

    ESP_LOGI(TAG, "SPI bus init");
    spi_bus_config_t buscfg = {
        .mosi_io_num     = LCD_MOSI,
        .miso_io_num     = -1,
        .sclk_io_num     = LCD_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_H_RES * 20 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "LCD IO init");
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num       = LCD_DC,
        .cs_gpio_num       = LCD_CS,
        .pclk_hz           = LCD_SPI_CLK_HZ,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .spi_mode          = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_SPI_HOST,
            &io_config,
        &io_handle
    ));

    ESP_LOGI(TAG, "ST7789 panel init");
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST,
        .rgb_endian     = LCD_RGB_ENDIAN_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG, "Display init done");
}

// --- LVGL init via esp_lvgl_port ---
static void lvgl_init(void)
{
    ESP_LOGI(TAG, "LVGL port init");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 4;
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    ESP_LOGI(TAG, "Adding display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle     = io_handle,
        .panel_handle  = panel_handle,
        .buffer_size   = LCD_H_RES * 20,
        .double_buffer = false,
        .hres          = LCD_H_RES,
        .vres          = LCD_V_RES,
        .monochrome    = false,
        .rotation = {
            .swap_xy  = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma  = 1,
            .swap_bytes = 1,
        },
    };
    display = lvgl_port_add_disp(&display_cfg);
    assert(display != NULL);

    ESP_LOGI(TAG, "LVGL init done");
}

void app_main(void)
{
    display_init();
    esp_lcd_panel_invert_color(panel_handle, true);
    lvgl_init();

    lvgl_port_lock(0);
    ui_init();   // flow engine starts here

    // Make sure current_btn starts at a known value
    eez_set_global_variable_int(FLOW_GLOBAL_VARIABLE_CURRENT_BTN, 0);
    int current_btn = eez_get_global_variable_int(FLOW_GLOBAL_VARIABLE_CURRENT_BTN);
    ESP_LOGI(TAG, "current_btn initialized = %d", current_btn);

    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
    lvgl_port_unlock();

    // Start the Flow engine's tick driver — REQUIRED for SetVariable/Watch/etc. to ever run
    xTaskCreate(flow_tick_task, "flow_tick_task", 4096, NULL, 4, NULL);

    // Start physical button tasks AFTER ui_init() so objects.next/objects.ok exist
    static button_task_arg_t next_btn_arg = {
        .gpio = PHYSICAL_NEXT_BTN_GPIO,
        .on_press = trigger_next_button,
        .active_high = false,
    };
    static button_task_arg_t ok_btn_arg = {
        .gpio = PHYSICAL_OK_BTN_GPIO,
        .on_press = trigger_ok_button,
        .active_high = true,
    };

    xTaskCreate(physical_button_task, "next_btn_task", 4096, &next_btn_arg, 5, NULL);
    xTaskCreate(physical_button_task, "ok_btn_task", 4096, &ok_btn_arg, 5, NULL);
}