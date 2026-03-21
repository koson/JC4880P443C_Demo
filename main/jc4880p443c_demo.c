/*
 * jc4880p443c_demo.c
 *
 * CHANGES FROM YOUR ORIGINAL (marked ← NEW):
 *   1. #include "dashboard_ui.h"   added
 *   2. LVGL_TASK_STACK_SIZE        bumped 6 KB → 10 KB (more widgets)
 *   3. create_demo_ui() replaced   with dashboard_ui_create()
 *
 * Everything else — hardware init, LVGL port, backlight, MIPI DSI —
 * is byte-for-byte identical to your working original.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_st7701.h"
#include "esp_ldo_regulator.h"
#include "lvgl.h"
#include "jc4880p443c.h"
#include "dashboard_ui.h"
#include "wifi_manager.h"                      /* ← NEW */

static const char *TAG = "DEMO";

/* ── Hardware config (unchanged) ─────────────────────────────────────── */
uint16_t DISPLAY_H_RES, DISPLAY_V_RES;

#define PIN_LCD_RST                  (GPIO_NUM_5)
#define PIN_LCD_BACKLIGHT            (GPIO_NUM_23)
#define PIN_MIPI_PHY_PWR_LDO_CHAN    (3)
#define PIN_MIPI_PHY_PWR_VOLTAGE_MV  (2500)

#define BACKLIGHT_LEDC_CH            (0)
#define BACKLIGHT_LEDC_TIMER         (LEDC_TIMER_1)
#define BACKLIGHT_LEDC_MODE          (LEDC_LOW_SPEED_MODE)
#define BACKLIGHT_PWM_FREQ_HZ        (20000)
#define BACKLIGHT_PWM_RESOLUTION     (LEDC_TIMER_10_BIT)

#define LVGL_TICK_PERIOD_MS          (2)
#define LVGL_TASK_MAX_DELAY_MS       (500)
#define LVGL_TASK_MIN_DELAY_MS       (1)
#define LVGL_TASK_STACK_SIZE         (10 * 1024)  /* ← bumped: was 6 KB  */
#define LVGL_TASK_PRIORITY           (4)
#define LVGL_TASK_CORE               1
#define LVGL_BUFFER_SIZE             (480 * 50)

/* ── Backlight (unchanged) ───────────────────────────────────────────── */
static esp_err_t backlight_init(void)
{
    ESP_LOGI(TAG, "Initializing backlight");
    ledc_timer_config_t lt = {
        .speed_mode      = BACKLIGHT_LEDC_MODE,
        .duty_resolution = BACKLIGHT_PWM_RESOLUTION,
        .timer_num       = BACKLIGHT_LEDC_TIMER,
        .freq_hz         = BACKLIGHT_PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&lt), TAG, "LEDC timer");

    ledc_channel_config_t lc = {
        .gpio_num   = PIN_LCD_BACKLIGHT,
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .channel    = BACKLIGHT_LEDC_CH,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = BACKLIGHT_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&lc), TAG, "LEDC channel");
    return ESP_OK;
}

static esp_err_t backlight_set(int pct)
{
    if (pct > 100) pct = 100;
    if (pct < 0)   pct = 0;
    uint32_t duty = (1023 * pct) / 100;
    ESP_RETURN_ON_ERROR(ledc_set_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CH, duty),
                        TAG, "set duty");
    ESP_RETURN_ON_ERROR(ledc_update_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CH),
                        TAG, "update duty");
    return ESP_OK;
}

/* ── MIPI PHY power (unchanged) ─────────────────────────────────────── */
static esp_err_t mipi_phy_power_init(void)
{
    static esp_ldo_channel_handle_t phy_pwr_chan = NULL;
    esp_ldo_channel_config_t cfg = {
        .chan_id    = PIN_MIPI_PHY_PWR_LDO_CHAN,
        .voltage_mv = PIN_MIPI_PHY_PWR_VOLTAGE_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&cfg, &phy_pwr_chan),
                        TAG, "LDO acquire");
    return ESP_OK;
}

/* ── Display init (unchanged) ───────────────────────────────────────── */
static esp_err_t display_init(esp_lcd_panel_handle_t *ret_panel,
                               esp_lcd_panel_io_handle_t *ret_io)
{
    esp_err_t ret = ESP_OK;
    esp_lcd_dsi_bus_handle_t  dsi_bus = NULL;
    esp_lcd_panel_io_handle_t io      = NULL;
    esp_lcd_panel_handle_t    panel   = NULL;

    const st7701_lcd_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
    jc4880p443c_get_init_cmds(&init_cmds, &init_cmds_size);

    uint32_t lane_bit_rate; uint8_t num_lanes;
    jc4880p443c_get_dsi_config(&lane_bit_rate, &num_lanes);

    uint32_t pclk_mhz; uint16_t hbp, hfp, vbp, vfp;
    jc4880p443c_get_timing(&pclk_mhz, &hbp, &hfp, &vbp, &vfp);
    jc4880p443c_get_resolution(&DISPLAY_H_RES, &DISPLAY_V_RES);

    ESP_GOTO_ON_ERROR(backlight_init(),      err, TAG, "backlight");
    ESP_GOTO_ON_ERROR(mipi_phy_power_init(), err, TAG, "mipi pwr");

    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id             = 0,
        .num_data_lanes     = num_lanes,
        .phy_clk_src        = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = lane_bit_rate,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus), err, TAG, "dsi bus");

    esp_lcd_dbi_io_config_t dbi_cfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits    = 8,
        .lcd_param_bits  = 8,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &io),
                      err, TAG, "dbi io");

    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = pclk_mhz,
        .virtual_channel    = 0,
        .pixel_format       = LCD_COLOR_PIXEL_FORMAT_RGB888,
        .num_fbs            = 2,
        .video_timing = {
            .h_size            = DISPLAY_H_RES,
            .v_size            = DISPLAY_V_RES,
            .hsync_pulse_width = 12,
            .hsync_back_porch  = hbp,
            .hsync_front_porch = hfp,
            .vsync_pulse_width = 2,
            .vsync_back_porch  = vbp,
            .vsync_front_porch = vfp,
        },
        .flags.use_dma2d = true,
    };

    st7701_vendor_config_t vendor_cfg = {
        .init_cmds      = init_cmds,
        .init_cmds_size = init_cmds_size,
        .mipi_config    = { .dsi_bus = dsi_bus, .dpi_config = &dpi_cfg },
        .flags          = { .use_mipi_interface = 1 },
    };

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 24,
        .vendor_config  = &vendor_cfg,
    };

    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_st7701(io, &panel_cfg, &panel),
                      err, TAG, "st7701");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(panel),         err, TAG, "reset");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_init(panel),          err, TAG, "init");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), err, TAG, "on");

    *ret_panel = panel;
    *ret_io    = io;
    return ESP_OK;

err:
    if (panel)   esp_lcd_panel_del(panel);
    if (io)      esp_lcd_panel_io_del(io);
    if (dsi_bus) esp_lcd_del_dsi_bus(dsi_bus);
    return ret;
}

/* ── LVGL port (unchanged except stack size above) ───────────────────── */
static void lvgl_tick_cb(void *arg)       { lv_tick_inc(LVGL_TICK_PERIOD_MS); }

static esp_err_t lvgl_tick_init(void)
{
    const esp_timer_create_args_t args = { .callback = lvgl_tick_cb,
                                            .name     = "lvgl_tick" };
    esp_timer_handle_t t = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&args, &t));
    return esp_timer_start_periodic(t, LVGL_TICK_PERIOD_MS * 1000);
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area,
                           uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = lv_display_get_user_data(disp);
    esp_lcd_panel_draw_bitmap(panel,
                              area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task on core %d", xPortGetCoreID());
    uint32_t d = LVGL_TASK_MAX_DELAY_MS;
    while (1) {
        d = lv_timer_handler();
        if (d > LVGL_TASK_MAX_DELAY_MS) d = LVGL_TASK_MAX_DELAY_MS;
        if (d < LVGL_TASK_MIN_DELAY_MS) d = LVGL_TASK_MIN_DELAY_MS;
        vTaskDelay(pdMS_TO_TICKS(d));
    }
}

static lv_display_t *lvgl_display_init(esp_lcd_panel_handle_t panel)
{
    lv_init();
    ESP_ERROR_CHECK(lvgl_tick_init());

    lv_display_t *disp = lv_display_create(DISPLAY_H_RES, DISPLAY_V_RES);
    assert(disp);

    void *buf1 = heap_caps_malloc(LVGL_BUFFER_SIZE * sizeof(lv_color_t),
                                   MALLOC_CAP_DMA);
    void *buf2 = heap_caps_malloc(LVGL_BUFFER_SIZE * sizeof(lv_color_t),
                                   MALLOC_CAP_DMA);
    assert(buf1 && buf2);

    lv_display_set_buffers(disp, buf1, buf2,
                           LVGL_BUFFER_SIZE * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_user_data(disp, panel);
    return disp;
}

/* ════════════════════════════════════════════════════════════════════
 * app_main
 * ════════════════════════════════════════════════════════════════════ */
void app_main(void)
{
    ESP_LOGI(TAG, "Smart home dashboard — ESP32-P4");

    esp_lcd_panel_handle_t    panel = NULL;
    esp_lcd_panel_io_handle_t io    = NULL;

    if (display_init(&panel, &io) != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed — halting");
        return;
    }

    ESP_ERROR_CHECK(backlight_set(90));

    lv_display_t *disp = lvgl_display_init(panel);
    assert(disp);

    /* ── Build the dashboard ────────────────────────────────────────
     * Single call. All widget creation happens inside dashboard_ui.c.
     * To update data later: call dashboard_ui_update_*() from inside
     * an lv_timer callback, never from another FreeRTOS task.       */
    dashboard_ui_create(disp);

    /* ── Start LVGL task FIRST ──────────────────────────────────────
     * LVGL must be running before WiFi starts:
     *   1. wifi_manager_start() blocks for ~2 s — LVGL must tick
     *      during that time or the screen freezes.
     *   2. The status bar lv_timer (registered in dashboard_ui_create)
     *      will call dashboard_ui_update_status() safely on its own
     *      5 s tick once WiFi is up — no direct LVGL calls needed
     *      from app_main at all.                                     */
    xTaskCreatePinnedToCore(lvgl_task, "lvgl",
                             LVGL_TASK_STACK_SIZE, NULL,
                             LVGL_TASK_PRIORITY, NULL,
                             LVGL_TASK_CORE);

    /* Give LVGL one tick to render the initial dashboard before
     * wifi_manager_start() blocks this task for ~2 s.               */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* ── Connect to WiFi ────────────────────────────────────────────
     * Blocks until connected or retries exhausted.
     * On success g_wifi_state.connected = true and rssi is set.
     * The lv_timer in dashboard_ui.c polls g_wifi_state every 5 s
     * and updates the status bar from within the LVGL task safely.  */
    wifi_manager_start();

    ESP_LOGI(TAG, "Running — %dx%d", DISPLAY_H_RES, DISPLAY_V_RES);
}
