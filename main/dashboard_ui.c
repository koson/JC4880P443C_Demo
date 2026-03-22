/*
 * dashboard_ui.c
 *
 * Portrait smart home dashboard — JC4880P443C  480x800  ESP32-P4.
 *
 * LAYOUT ZONES  (top → bottom, all values px)
 * ────────────────────────────────────────────
 *   Y=0    H=30   Status bar     full width, no radius
 *   Y=38   H=230  Weather+Clock  full width card
 *   Y=276  H=172  Energy         full width card
 *   Y=456  H=80   Tiles          4 equal tiles
 *   Y=544  H=100  Boiler card    full width card
 *   ─────────────────────────────
 *   Bottom edge: 644 px  (156 px spare on 800 px screen)
 *
 * FONT SIZES
 * ──────────
 *   montserrat_48  time, temperature, live watts  (hero numbers)
 *   montserrat_20  secondary values: kWh, power factor
 *   montserrat_16  tile names, card sub-values, date
 *   montserrat_14  dim labels, hints, status bar text
 *
 * WEATHER ICON
 * ────────────
 *   OWM wicon code → coloured circle:
 *   01=clear→gold  02-04=clouds→grey  09/10=rain→blue
 *   11=thunder→purple  13=snow→ice-blue  else→dim
 *
 * BOOST BUTTON
 * ────────────
 *   Tap publishes "1" to /BOI/home.
 *   2 s confirmation "Sent!" then reverts.
 *   Disabled automatically when mode=summer.
 *
 * MQTT DRAIN TIMER
 * ────────────────
 *   Fires every 100 ms inside the LVGL task.
 *   Drains g_mqtt_queue completely each tick.
 *   Each topic dispatches to the matching update function.
 *   Numeric payloads: atof()/atoi() — returns 0 on failure (safe).
 */

#include "dashboard_ui.h"
#include "mqtt_manager.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "UI";

/* ── Timing ───────────────────────────────────────────────────────── */
#define MQTT_DRAIN_MS      100
#define STATUS_POLL_MS    5000
#define BOOST_CONFIRM_MS  2000

/* ── Colour palette ───────────────────────────────────────────────── */
#define C_BG          lv_color_hex(0x0F1117)
#define C_CARD        lv_color_hex(0x1A1D27)
#define C_CARD2       lv_color_hex(0x22263A)
#define C_BORDER      lv_color_hex(0x2A2E42)
#define C_BLUE        lv_color_hex(0x4A90D9)
#define C_TEAL        lv_color_hex(0x2ECC8E)
#define C_WARM        lv_color_hex(0xE8834A)
#define C_GOLD        lv_color_hex(0xE8C44A)
#define C_PRI         lv_color_hex(0xF0F0F5)
#define C_SEC         lv_color_hex(0x9094AB)
#define C_DIM         lv_color_hex(0x4A4F65)
#define C_TILE_WARM   lv_color_hex(0x1E1A10)
#define C_TILE_TEAL   lv_color_hex(0x121E13)
#define C_BDR_WARM    lv_color_hex(0x3A2E10)
#define C_BDR_TEAL    lv_color_hex(0x1A3020)
#define C_RED         lv_color_hex(0xE05050)

/* ── Layout constants ─────────────────────────────────────────────── */
#define SCR_W       480
#define SCR_H       800
#define MARGIN       12
#define GAP           8
#define CARD_W      (SCR_W - MARGIN * 2)   /* 456 px */

#define Y_STATUS      0
#define H_STATUS     30

#define Y_WEATHER    (Y_STATUS + H_STATUS + GAP)
#define H_WEATHER    230

#define Y_ENERGY     (Y_WEATHER + H_WEATHER + GAP)
#define H_ENERGY     172

#define Y_TILES      (Y_ENERGY + H_ENERGY + GAP)
#define H_TILES       80
#define N_TILES        4
#define TILE_W       ((CARD_W - GAP * (N_TILES - 1)) / N_TILES)   /* ~108 px */

#define Y_BOILER     (Y_TILES + H_TILES + GAP)
#define H_BOILER     100

/* ── Widget handles ───────────────────────────────────────────────── */

/* Status bar */
static lv_obj_t *g_lbl_date;
static lv_obj_t *g_lbl_mqtt;
static lv_obj_t *g_lbl_wifi;

/* Weather + clock card */
static lv_obj_t *g_lbl_time;
static lv_obj_t *g_obj_icon;
static lv_obj_t *g_lbl_temp;
static lv_obj_t *g_lbl_condition;
static lv_obj_t *g_lbl_minmax;
static lv_obj_t *g_lbl_humidity;
static lv_obj_t *g_lbl_wind;
static lv_obj_t *g_lbl_suntime;

/* Energy card */
static lv_obj_t *g_lbl_watts;
static lv_obj_t *g_lbl_today;
static lv_obj_t *g_lbl_yesterday;
static lv_obj_t *g_lbl_pf;
static lv_obj_t *g_lbl_voltage;

/* Tiles */
static lv_obj_t *g_tile_boiler;
static lv_obj_t *g_dot_boiler;
static lv_obj_t *g_lbl_boiler_state;
static lv_obj_t *g_lbl_boiler_badge;

static lv_obj_t *g_tile_boost;
static lv_obj_t *g_dot_boost;
static lv_obj_t *g_lbl_boost_state;

static lv_obj_t *g_tile_inet;
static lv_obj_t *g_dot_inet;
static lv_obj_t *g_lbl_inet_rtt;

static lv_obj_t *g_lbl_rain;

/* Boiler detail card */
static lv_obj_t *g_lbl_boi_status;
static lv_obj_t *g_lbl_boi_mode;
static lv_obj_t *g_btn_boost;
static lv_obj_t *g_lbl_boost_btn;

/* Internal state */
static bool s_boiler_on   = false;
static bool s_winter_mode = true;
static bool s_boost_active = false;

/* ── Helpers ──────────────────────────────────────────────────────── */

static lv_obj_t *make_card(lv_obj_t *parent,
                            int32_t x, int32_t y,
                            int32_t w, int32_t h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, C_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, C_BORDER, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(c, LV_SCROLLBAR_MODE_OFF);
    return c;
}

static lv_obj_t *make_label(lv_obj_t *parent,
                             const char *text,
                             const lv_font_t *font,
                             lv_color_t color,
                             int32_t x, int32_t y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

static void make_hdiv(lv_obj_t *parent, int32_t x, int32_t y, int32_t w)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_set_size(d, w, 1);
    lv_obj_set_pos(d, x, y);
    lv_obj_set_style_bg_color(d, C_BORDER, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(d, 0, 0);
    lv_obj_set_style_radius(d, 0, 0);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
}

static void make_vdiv(lv_obj_t *parent, int32_t x, int32_t y, int32_t h)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_set_size(d, 1, h);
    lv_obj_set_pos(d, x, y);
    lv_obj_set_style_bg_color(d, C_BORDER, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(d, 0, 0);
    lv_obj_set_style_radius(d, 0, 0);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *make_dot(lv_obj_t *parent, lv_color_t color,
                           int32_t x, int32_t y, int32_t sz)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_set_size(d, sz, sz);
    lv_obj_set_pos(d, x, y);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, color, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(d, 0, 0);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
    return d;
}

static const char *deg_to_compass(uint16_t deg)
{
    static const char *pts[16] = {
        "N","NNE","NE","ENE","E","ESE","SE","SSE",
        "S","SSW","SW","WSW","W","WNW","NW","NNW"
    };
    return pts[((uint16_t)(deg / 22.5f + 0.5f)) % 16];
}

static lv_color_t icon_to_color(const char *code)
{
    if (!code || !code[0]) return C_DIM;
    int id = atoi(code + 1);
    if (id == 1)             return C_GOLD;
    if (id >= 2 && id <= 4)  return C_SEC;
    if (id == 9 || id == 10) return C_BLUE;
    if (id == 11)            return lv_color_hex(0xAA88FF);
    if (id == 13)            return lv_color_hex(0xC8E0FF);
    return C_DIM;
}

/* ═════════════════════════════════════════════════════════════════════
 * BUILD: STATUS BAR
 * ═════════════════════════════════════════════════════════════════════ */
static void build_status_bar(lv_obj_t *scr)
{
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, SCR_W, H_STATUS);
    lv_obj_set_style_bg_color(bar, C_CARD, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    g_lbl_date = lv_label_create(bar);
    lv_label_set_text(g_lbl_date, "---");
    lv_obj_set_style_text_font(g_lbl_date, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_lbl_date, C_SEC, 0);
    lv_obj_align(g_lbl_date, LV_ALIGN_LEFT_MID, MARGIN, 0);

    g_lbl_mqtt = lv_label_create(bar);
    lv_label_set_text(g_lbl_mqtt, "MQTT --");
    lv_obj_set_style_text_font(g_lbl_mqtt, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_lbl_mqtt, C_DIM, 0);
    lv_obj_align(g_lbl_mqtt, LV_ALIGN_CENTER, 0, 0);

    g_lbl_wifi = lv_label_create(bar);
    lv_label_set_text(g_lbl_wifi, "WiFi --");
    lv_obj_set_style_text_font(g_lbl_wifi, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_lbl_wifi, C_DIM, 0);
    lv_obj_align(g_lbl_wifi, LV_ALIGN_RIGHT_MID, -MARGIN, 0);
}

/* ═════════════════════════════════════════════════════════════════════
 * BUILD: WEATHER + CLOCK HERO CARD
 *
 * Internal layout (CARD_W x H_WEATHER = 456 x 230):
 *
 *   y=14          [TIME  48px]                       left=16
 *   y=70          "local time" hint 14px dim
 *   y=84          ── hdiv ──
 *   y=96  [ICON 52x52]  [TEMP 48px]  [condition 20px]
 *   y=160         [condition / detail  20px]
 *   y=184         ── hdiv ──
 *   y=194  [min/max] | [humidity] | [wind] | [rise/set]   16px
 * ═════════════════════════════════════════════════════════════════════ */
static void build_weather_card(lv_obj_t *scr)
{
    lv_obj_t *card = make_card(scr, MARGIN, Y_WEATHER, CARD_W, H_WEATHER);

    /* ── Big time ── */
    g_lbl_time = lv_label_create(card);
    lv_label_set_text(g_lbl_time, "--:--");
    lv_obj_set_style_text_font(g_lbl_time, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(g_lbl_time, C_PRI, 0);
    lv_obj_set_pos(g_lbl_time, 16, 14);

    make_label(card, "local time", &lv_font_montserrat_14, C_DIM, 20, 72);

    make_hdiv(card, 16, 92, CARD_W - 32);

    /* ── Icon + temperature row ── */
    g_obj_icon = lv_obj_create(card);
    lv_obj_set_size(g_obj_icon, 52, 52);
    lv_obj_set_pos(g_obj_icon, 16, 104);
    lv_obj_set_style_radius(g_obj_icon, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_obj_icon, C_GOLD, 0);
    lv_obj_set_style_bg_opa(g_obj_icon, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_obj_icon, 0, 0);
    lv_obj_clear_flag(g_obj_icon, LV_OBJ_FLAG_SCROLLABLE);

    g_lbl_temp = lv_label_create(card);
    lv_label_set_text(g_lbl_temp, "--\xC2\xB0");
    lv_obj_set_style_text_font(g_lbl_temp, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(g_lbl_temp, C_PRI, 0);
    lv_obj_set_pos(g_lbl_temp, 82, 100);

    /* ── Condition + location ── */
    g_lbl_condition = lv_label_create(card);
    lv_label_set_text(g_lbl_condition, "---");
    lv_obj_set_style_text_font(g_lbl_condition, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_lbl_condition, C_SEC, 0);
    lv_obj_set_pos(g_lbl_condition, 16, 164);

    make_label(card, "High Wycombe",
               &lv_font_montserrat_14, C_DIM, 16, 190);

    make_hdiv(card, 16, H_WEATHER - 54, CARD_W - 32);

    /* ── Four stat columns ── */
    int32_t sw  = (CARD_W - 32) / 4;
    int32_t sy0 = H_WEATHER - 44;   /* label row */
    int32_t sy1 = H_WEATHER - 26;   /* value row */

    make_label(card, "min / max",
               &lv_font_montserrat_14, C_DIM, 16, sy0);
    g_lbl_minmax = make_label(card, "--\xC2\xB0 / --\xC2\xB0",
                               &lv_font_montserrat_16, C_PRI, 16, sy1);

    make_vdiv(card, 16 + sw, sy0 - 4, 52);

    make_label(card, "humidity",
               &lv_font_montserrat_14, C_DIM, 20 + sw, sy0);
    g_lbl_humidity = make_label(card, "--%",
                                 &lv_font_montserrat_16, C_PRI,
                                 20 + sw, sy1);

    make_vdiv(card, 16 + sw * 2, sy0 - 4, 52);

    make_label(card, "wind",
               &lv_font_montserrat_14, C_DIM, 20 + sw * 2, sy0);
    g_lbl_wind = make_label(card, "-- m/s",
                             &lv_font_montserrat_16, C_PRI,
                             20 + sw * 2, sy1);

    make_vdiv(card, 16 + sw * 3, sy0 - 4, 52);

    make_label(card, "rise / set",
               &lv_font_montserrat_14, C_DIM, 20 + sw * 3, sy0);
    g_lbl_suntime = make_label(card, "--:-- --:--",
                                &lv_font_montserrat_16, C_PRI,
                                20 + sw * 3, sy1);
}

/* ═════════════════════════════════════════════════════════════════════
 * BUILD: ENERGY CARD
 *
 * Internal layout (CARD_W x H_ENERGY = 456 x 172):
 *
 *   y=14   "Whole-house power"  dim 14px
 *   y=30   [WATTS  48px  blue]
 *   y=88   "watts live"  sec 14px
 *   y=106  ── hdiv ──
 *   y=118  [today 20px] | [yesterday 16px] | [PF 20px] | [voltage 16px]
 *   y=142  values row
 * ═════════════════════════════════════════════════════════════════════ */
static void build_energy_card(lv_obj_t *scr)
{
    lv_obj_t *card = make_card(scr, MARGIN, Y_ENERGY, CARD_W, H_ENERGY);

    make_label(card, "Whole-house power",
               &lv_font_montserrat_14, C_DIM, 16, 14);

    g_lbl_watts = lv_label_create(card);
    lv_label_set_text(g_lbl_watts, "---");
    lv_obj_set_style_text_font(g_lbl_watts, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(g_lbl_watts, C_BLUE, 0);
    lv_obj_set_pos(g_lbl_watts, 16, 30);

    make_label(card, "watts live",
               &lv_font_montserrat_14, C_SEC, 16, 90);

    make_hdiv(card, 16, 110, CARD_W - 32);

    /* Four stat columns */
    int32_t ew  = (CARD_W - 32) / 4;
    int32_t ey0 = 120;   /* label */
    int32_t ey1 = 140;   /* value */

    make_label(card, "today",
               &lv_font_montserrat_14, C_DIM, 16, ey0);
    g_lbl_today = make_label(card, "--.- kWh",
                              &lv_font_montserrat_20, C_PRI, 16, ey1);

    make_vdiv(card, 16 + ew, ey0 - 4, 52);

    make_label(card, "yesterday",
               &lv_font_montserrat_14, C_DIM, 20 + ew, ey0);
    g_lbl_yesterday = make_label(card, "--.- kWh",
                                  &lv_font_montserrat_16, C_SEC,
                                  20 + ew, ey1);

    make_vdiv(card, 16 + ew * 2, ey0 - 4, 52);

    make_label(card, "pwr factor",
               &lv_font_montserrat_14, C_DIM, 20 + ew * 2, ey0);
    g_lbl_pf = make_label(card, "-.--",
                           &lv_font_montserrat_20, C_TEAL,
                           20 + ew * 2, ey1);

    make_vdiv(card, 16 + ew * 3, ey0 - 4, 52);

    make_label(card, "voltage",
               &lv_font_montserrat_14, C_DIM, 20 + ew * 3, ey0);
    g_lbl_voltage = make_label(card, "--- V",
                                &lv_font_montserrat_16, C_SEC,
                                20 + ew * 3, ey1);
}

/* ═════════════════════════════════════════════════════════════════════
 * BUILD: CONTROL TILES
 * ═════════════════════════════════════════════════════════════════════ */
static lv_obj_t *make_tile(lv_obj_t *scr, int idx)
{
    int32_t x = MARGIN + idx * (TILE_W + GAP);
    lv_obj_t *t = lv_obj_create(scr);
    lv_obj_set_pos(t, x, Y_TILES);
    lv_obj_set_size(t, TILE_W, H_TILES);
    lv_obj_set_style_bg_color(t, C_CARD, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(t, C_BORDER, 0);
    lv_obj_set_style_border_width(t, 1, 0);
    lv_obj_set_style_radius(t, 10, 0);
    lv_obj_set_style_pad_all(t, 10, 0);
    lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    return t;
}

static void build_tiles(lv_obj_t *scr)
{
    /* ── Tile 0: Boiler ── */
    g_tile_boiler = make_tile(scr, 0);
    g_dot_boiler  = make_dot(g_tile_boiler, C_DIM, 0, 0, 10);

    g_lbl_boiler_badge = lv_label_create(g_tile_boiler);
    lv_label_set_text(g_lbl_boiler_badge, "winter");
    lv_obj_set_style_text_font(g_lbl_boiler_badge,
                                &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_lbl_boiler_badge, C_WARM, 0);
    lv_obj_align(g_lbl_boiler_badge, LV_ALIGN_TOP_RIGHT, 0, 0);

    make_label(g_tile_boiler, "Boiler",
               &lv_font_montserrat_16, C_PRI, 0, 24);

    g_lbl_boiler_state = make_label(g_tile_boiler, "Off",
                                     &lv_font_montserrat_14, C_DIM, 0, 46);

    /* ── Tile 1: Boost ── */
    g_tile_boost = make_tile(scr, 1);
    g_dot_boost  = make_dot(g_tile_boost, C_DIM, 0, 0, 10);

    make_label(g_tile_boost, "Boost",
               &lv_font_montserrat_16, C_PRI, 0, 24);

    g_lbl_boost_state = make_label(g_tile_boost, "Inactive",
                                    &lv_font_montserrat_14, C_DIM, 0, 46);

    /* ── Tile 2: Internet ── */
    g_tile_inet = make_tile(scr, 2);
    g_dot_inet  = make_dot(g_tile_inet, C_DIM, 0, 0, 10);

    make_label(g_tile_inet, "Internet",
               &lv_font_montserrat_16, C_PRI, 0, 24);

    g_lbl_inet_rtt = make_label(g_tile_inet, "-- ms",
                                 &lv_font_montserrat_14, C_DIM, 0, 46);

    /* ── Tile 3: Rain ── */
    lv_obj_t *tile_rain = make_tile(scr, 3);
    make_dot(tile_rain, C_BLUE, 0, 0, 10);

    make_label(tile_rain, "Rain",
               &lv_font_montserrat_16, C_PRI, 0, 24);

    g_lbl_rain = make_label(tile_rain, "-- mm",
                             &lv_font_montserrat_14, C_DIM, 0, 46);
}

/* ═════════════════════════════════════════════════════════════════════
 * BUILD: BOILER DETAIL CARD
 * ═════════════════════════════════════════════════════════════════════ */
static void boost_revert_cb(lv_timer_t *t)
{
    s_boost_active = false;
    lv_label_set_text(g_lbl_boost_btn, "Tap for 1 hr boost");
    lv_obj_set_style_text_color(g_lbl_boost_btn, C_SEC, 0);
    lv_obj_clear_state(g_btn_boost, LV_STATE_DISABLED);
    lv_timer_del(t);
}

static void boost_btn_cb(lv_event_t *e)
{
    (void)e;
    if (s_boost_active) return;

    if (!s_winter_mode) {
        lv_label_set_text(g_lbl_boost_btn, "Summer mode");
        lv_obj_set_style_text_color(g_lbl_boost_btn, C_DIM, 0);
        lv_timer_create(boost_revert_cb, BOOST_CONFIRM_MS, NULL);
        return;
    }

    s_boost_active = true;
    mqtt_manager_publish("/BOI/home", "1", 0, false);
    lv_label_set_text(g_lbl_boost_btn, "Sent!");
    lv_obj_set_style_text_color(g_lbl_boost_btn, C_TEAL, 0);
    lv_obj_add_state(g_btn_boost, LV_STATE_DISABLED);
    lv_timer_create(boost_revert_cb, BOOST_CONFIRM_MS, NULL);
}

static void build_boiler_card(lv_obj_t *scr)
{
    lv_obj_t *card = make_card(scr, MARGIN, Y_BOILER, CARD_W, H_BOILER);

    make_label(card, "Boiler",
               &lv_font_montserrat_14, C_DIM, 16, 10);

    g_lbl_boi_status = lv_label_create(card);
    lv_label_set_text(g_lbl_boi_status, "Idle");
    lv_obj_set_style_text_font(g_lbl_boi_status, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_lbl_boi_status, C_DIM, 0);
    lv_obj_set_pos(g_lbl_boi_status, 16, 28);

    g_lbl_boi_mode = lv_label_create(card);
    lv_label_set_text(g_lbl_boi_mode, "Mode: winter");
    lv_obj_set_style_text_font(g_lbl_boi_mode, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_lbl_boi_mode, C_DIM, 0);
    lv_obj_set_pos(g_lbl_boi_mode, 16, 54);

    make_vdiv(card, CARD_W / 2, 10, H_BOILER - 20);

    /* Boost tap button — right half */
    g_btn_boost = lv_obj_create(card);
    lv_obj_set_size(g_btn_boost, CARD_W / 2 - 24, 54);
    lv_obj_set_pos(g_btn_boost, CARD_W / 2 + 12, 22);
    lv_obj_set_style_bg_color(g_btn_boost, C_CARD2, 0);
    lv_obj_set_style_bg_opa(g_btn_boost, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_btn_boost, C_BDR_WARM, 0);
    lv_obj_set_style_border_width(g_btn_boost, 1, 0);
    lv_obj_set_style_radius(g_btn_boost, 8, 0);
    lv_obj_set_style_pad_all(g_btn_boost, 0, 0);
    lv_obj_clear_flag(g_btn_boost, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_btn_boost, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(g_btn_boost, lv_color_hex(0x2A2520),
                               LV_STATE_PRESSED);
    lv_obj_add_event_cb(g_btn_boost, boost_btn_cb, LV_EVENT_CLICKED, NULL);

    g_lbl_boost_btn = lv_label_create(g_btn_boost);
    lv_label_set_text(g_lbl_boost_btn, "Tap  for 1 hr boost");
    lv_obj_set_style_text_font(g_lbl_boost_btn, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_lbl_boost_btn, C_SEC, 0);
    lv_obj_center(g_lbl_boost_btn);
}

/* ═════════════════════════════════════════════════════════════════════
 * MQTT DRAIN TIMER
 * ═════════════════════════════════════════════════════════════════════ */
static void mqtt_drain_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!g_mqtt_queue) return;

    mqtt_message_t msg;
    while (xQueueReceive(g_mqtt_queue, &msg, 0) == pdTRUE) {
        const char *t = msg.topic;
        const char *p = msg.payload;

        /* Clock / date */
        if      (strcmp(t, "/SYS/time")  == 0) dashboard_ui_update_time(p);
        else if (strcmp(t, "/SYS/datel") == 0) dashboard_ui_update_date(p);

        /* Weather */
        else if (strcmp(t, "/OWM/tempc")         == 0)
            dashboard_ui_update_weather(atof(p),0,0,0,0,0,NULL,NULL,NULL,NULL,NULL);
        else if (strcmp(t, "/OWM/temp_minc")     == 0)
            dashboard_ui_update_weather(0,atof(p),0,0,0,0,NULL,NULL,NULL,NULL,NULL);
        else if (strcmp(t, "/OWM/temp_maxc")     == 0)
            dashboard_ui_update_weather(0,0,atof(p),0,0,0,NULL,NULL,NULL,NULL,NULL);
        else if (strcmp(t, "/OWM/humidity")      == 0)
            dashboard_ui_update_weather(0,0,0,(uint8_t)atoi(p),0,0,NULL,NULL,NULL,NULL,NULL);
        else if (strcmp(t, "/OWM/windspeed")     == 0)
            dashboard_ui_update_weather(0,0,0,0,atof(p),0,NULL,NULL,NULL,NULL,NULL);
        else if (strcmp(t, "/OWM/winddirection") == 0)
            dashboard_ui_update_weather(0,0,0,0,0,(uint16_t)atoi(p),NULL,NULL,NULL,NULL,NULL);
        else if (strcmp(t, "/OWM/weather")       == 0)
            dashboard_ui_update_weather(0,0,0,0,0,0,p,NULL,NULL,NULL,NULL);
        else if (strcmp(t, "/OWM/detail")        == 0)
            dashboard_ui_update_weather(0,0,0,0,0,0,NULL,p,NULL,NULL,NULL);
        else if (strcmp(t, "/OWM/wicon")         == 0)
            dashboard_ui_update_weather(0,0,0,0,0,0,NULL,NULL,p,NULL,NULL);
        else if (strcmp(t, "/OWM/sunrise_l")     == 0)
            dashboard_ui_update_weather(0,0,0,0,0,0,NULL,NULL,NULL,p,NULL);
        else if (strcmp(t, "/OWM/sunset_l")      == 0)
            dashboard_ui_update_weather(0,0,0,0,0,0,NULL,NULL,NULL,NULL,p);

        /* Energy */
        else if (strcmp(t, "/HALL/watts")        == 0)
            dashboard_ui_update_energy(atof(p),0,0,0,0);
        else if (strcmp(t, "/HALL/energy")       == 0)
            dashboard_ui_update_energy(0,atof(p),0,0,0);
        else if (strcmp(t, "/HALL/lastkwh")      == 0)
            dashboard_ui_update_energy(0,0,atof(p),0,0);
        else if (strcmp(t, "/HALL/powerfactor")  == 0)
            dashboard_ui_update_energy(0,0,0,atof(p),0);
        else if (strcmp(t, "/HALL/voltage")      == 0)
            dashboard_ui_update_energy(0,0,0,0,atof(p));

        /* Boiler */
        else if (strcmp(t, "/BOI/power") == 0)
            dashboard_ui_update_boiler((atoi(p) != 0), s_winter_mode);
        else if (strcmp(t, "/BOI/mode")  == 0)
            dashboard_ui_update_boiler(s_boiler_on, strcmp(p,"winter") == 0);

        /* Internet */
        else if (strcmp(t, "/INTERNET/rtt") == 0)
            dashboard_ui_update_internet((uint32_t)atoi(p));

        /* Rain */
        else if (strcmp(t, "/OWM/rain") == 0)
            dashboard_ui_update_rain(atof(p));
    }
}

/* ═════════════════════════════════════════════════════════════════════
 * STATUS POLL TIMER
 * ═════════════════════════════════════════════════════════════════════ */
static void status_poll_cb(lv_timer_t *timer)
{
    (void)timer;
    dashboard_ui_update_status(wifi_manager_get_rssi(), g_mqtt_connected);
}

/* ═════════════════════════════════════════════════════════════════════
 * PUBLIC — dashboard_ui_create
 * ═════════════════════════════════════════════════════════════════════ */
void dashboard_ui_create(lv_display_t *disp)
{
    ESP_LOGI(TAG, "Building portrait dashboard %dx%d", SCR_W, SCR_H);

    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    build_status_bar(scr);
    build_weather_card(scr);
    build_energy_card(scr);
    build_tiles(scr);
    build_boiler_card(scr);

    lv_timer_create(mqtt_drain_cb,  MQTT_DRAIN_MS,  NULL);
    lv_timer_create(status_poll_cb, STATUS_POLL_MS, NULL);

    ESP_LOGI(TAG, "Dashboard built — bottom: %d px  screen: %d px  spare: %d px",
             Y_BOILER + H_BOILER, SCR_H,
             SCR_H - (Y_BOILER + H_BOILER));
}

/* ═════════════════════════════════════════════════════════════════════
 * PUBLIC — update functions
 * ═════════════════════════════════════════════════════════════════════ */

void dashboard_ui_update_status(int8_t rssi, bool mqtt_ok)
{
    if (!g_lbl_wifi) return;
    char buf[28];

    if (rssi <= -100 || rssi == 0) {
        lv_label_set_text(g_lbl_wifi, "No WiFi");
        lv_obj_set_style_text_color(g_lbl_wifi, C_DIM, 0);
    } else {
        snprintf(buf, sizeof(buf), "WiFi %d dBm", (int)rssi);
        lv_label_set_text(g_lbl_wifi, buf);
        lv_color_t c = rssi > -70 ? C_TEAL : rssi > -85 ? C_WARM : C_RED;
        lv_obj_set_style_text_color(g_lbl_wifi, c, 0);
    }

    lv_label_set_text(g_lbl_mqtt, mqtt_ok ? "MQTT ok" : "MQTT --");
    lv_obj_set_style_text_color(g_lbl_mqtt, mqtt_ok ? C_TEAL : C_DIM, 0);
}

void dashboard_ui_update_time(const char *time_str)
{
    if (!g_lbl_time || !time_str || !time_str[0]) return;
    lv_label_set_text(g_lbl_time, time_str);
}

void dashboard_ui_update_date(const char *date_str)
{
    if (!g_lbl_date || !date_str || !date_str[0]) return;
    lv_label_set_text(g_lbl_date, date_str);
}

/* Persistent weather state */
static float    s_temp=0, s_min=0, s_max=0;
static float    s_wind_mps=0;
static uint16_t s_wind_deg=0;
static uint8_t  s_hum=0;
static char     s_cond[48]   = "---";
static char     s_sunrise[8] = "--:--";
static char     s_sunset[8]  = "--:--";
static char     s_icon[8]    = "";

void dashboard_ui_update_weather(float temp_c,
                                  float min_c, float max_c,
                                  uint8_t humidity,
                                  float wind_mps, uint16_t wind_deg,
                                  const char *condition,
                                  const char *detail,
                                  const char *icon_code,
                                  const char *sunrise,
                                  const char *sunset)
{
    if (!g_lbl_temp) return;
    char buf[48];

    if (temp_c   != 0.0f) s_temp     = temp_c;
    if (min_c    != 0.0f) s_min      = min_c;
    if (max_c    != 0.0f) s_max      = max_c;
    if (humidity != 0)    s_hum      = humidity;
    if (wind_mps != 0.0f) s_wind_mps = wind_mps;
    if (wind_deg != 0)    s_wind_deg = wind_deg;

    if (condition && condition[0])
        snprintf(s_cond, sizeof(s_cond), "%s", condition);
    if (detail && detail[0])
        snprintf(s_cond, sizeof(s_cond), "%s", detail);
    if (sunrise && sunrise[0])
        snprintf(s_sunrise, sizeof(s_sunrise), "%s", sunrise);
    if (sunset && sunset[0])
        snprintf(s_sunset,  sizeof(s_sunset),  "%s", sunset);
    if (icon_code && icon_code[0]) {
        snprintf(s_icon, sizeof(s_icon), "%s", icon_code);
        lv_obj_set_style_bg_color(g_obj_icon, icon_to_color(s_icon), 0);
    }

    snprintf(buf, sizeof(buf), "%.1f\xC2\xB0", s_temp);
    lv_label_set_text(g_lbl_temp, buf);

    lv_label_set_text(g_lbl_condition, s_cond);

    snprintf(buf, sizeof(buf), "%.1f\xC2\xB0 / %.1f\xC2\xB0", s_min, s_max);
    lv_label_set_text(g_lbl_minmax, buf);

    snprintf(buf, sizeof(buf), "%u%%", s_hum);
    lv_label_set_text(g_lbl_humidity, buf);

    snprintf(buf, sizeof(buf), "%.1f %s",
             s_wind_mps, deg_to_compass(s_wind_deg));
    lv_label_set_text(g_lbl_wind, buf);

    snprintf(buf, sizeof(buf), "%s  %s", s_sunrise, s_sunset);
    lv_label_set_text(g_lbl_suntime, buf);
}

/* Persistent energy state */
static float s_watts=0, s_today=0, s_yest=0, s_pf=0, s_volt=0;

void dashboard_ui_update_energy(float watts,
                                 float today_kwh,
                                 float yesterday_kwh,
                                 float power_factor,
                                 float voltage)
{
    if (!g_lbl_watts) return;
    char buf[24];

    if (watts         != 0.0f) s_watts = watts;
    if (today_kwh     != 0.0f) s_today = today_kwh;
    if (yesterday_kwh != 0.0f) s_yest  = yesterday_kwh;
    if (power_factor  != 0.0f) s_pf    = power_factor;
    if (voltage       != 0.0f) s_volt  = voltage;

    snprintf(buf, sizeof(buf), "%.0f", s_watts);
    lv_label_set_text(g_lbl_watts, buf);

    snprintf(buf, sizeof(buf), "%.2f kWh", s_today);
    lv_label_set_text(g_lbl_today, buf);

    snprintf(buf, sizeof(buf), "%.2f kWh", s_yest);
    lv_label_set_text(g_lbl_yesterday, buf);

    snprintf(buf, sizeof(buf), "%.2f", s_pf);
    lv_label_set_text(g_lbl_pf, buf);
    lv_color_t pfc = s_pf >= 0.9f ? C_TEAL : s_pf >= 0.8f ? C_WARM : C_RED;
    lv_obj_set_style_text_color(g_lbl_pf, pfc, 0);

    snprintf(buf, sizeof(buf), "%.0f V", s_volt);
    lv_label_set_text(g_lbl_voltage, buf);
}

void dashboard_ui_update_boiler(bool power_on, bool winter_mode)
{
    if (!g_tile_boiler) return;

    s_boiler_on   = power_on;
    s_winter_mode = winter_mode;

    lv_obj_set_style_bg_color(g_tile_boiler,
                               power_on ? C_TILE_WARM : C_CARD, 0);
    lv_obj_set_style_border_color(g_tile_boiler,
                                   power_on ? C_BDR_WARM : C_BORDER, 0);
    lv_obj_set_style_bg_color(g_dot_boiler,
                               power_on ? C_WARM : C_DIM, 0);

    lv_label_set_text(g_lbl_boiler_badge,
                      winter_mode ? "winter" : "summer");
    lv_obj_set_style_text_color(g_lbl_boiler_badge,
                                 winter_mode ? C_WARM : C_SEC, 0);

    lv_label_set_text(g_lbl_boiler_state, power_on ? "On" : "Off");
    lv_obj_set_style_text_color(g_lbl_boiler_state,
                                 power_on ? C_WARM : C_DIM, 0);

    lv_obj_set_style_bg_color(g_tile_boost,
                               (power_on && winter_mode) ? C_TILE_WARM : C_CARD, 0);
    lv_obj_set_style_border_color(g_tile_boost,
                                   (power_on && winter_mode) ? C_BDR_WARM : C_BORDER, 0);
    lv_obj_set_style_bg_color(g_dot_boost,
                               winter_mode ? C_WARM : C_DIM, 0);
    lv_label_set_text(g_lbl_boost_state,
                      winter_mode ? "Available" : "Summer");
    lv_obj_set_style_text_color(g_lbl_boost_state,
                                 winter_mode ? C_SEC : C_DIM, 0);

    lv_label_set_text(g_lbl_boi_status, power_on ? "Heating" : "Idle");
    lv_obj_set_style_text_color(g_lbl_boi_status,
                                 power_on ? C_WARM : C_DIM, 0);

    char mbuf[20];
    snprintf(mbuf, sizeof(mbuf), "Mode: %s",
             winter_mode ? "winter" : "summer");
    lv_label_set_text(g_lbl_boi_mode, mbuf);
    lv_obj_set_style_text_color(g_lbl_boi_mode,
                                 winter_mode ? C_SEC : C_DIM, 0);

    if (!winter_mode) {
        lv_obj_add_state(g_btn_boost, LV_STATE_DISABLED);
        lv_obj_set_style_border_color(g_btn_boost, C_BORDER, 0);
    } else if (!s_boost_active) {
        lv_obj_clear_state(g_btn_boost, LV_STATE_DISABLED);
        lv_obj_set_style_border_color(g_btn_boost, C_BDR_WARM, 0);
    }
}

void dashboard_ui_update_internet(uint32_t rtt_ms)
{
    if (!g_lbl_inet_rtt) return;
    char buf[16];
    bool ok = rtt_ms > 0 && rtt_ms < 2000;

    lv_obj_set_style_bg_color(g_tile_inet, ok ? C_TILE_TEAL : C_CARD, 0);
    lv_obj_set_style_border_color(g_tile_inet,
                                   ok ? C_BDR_TEAL : C_BORDER, 0);
    lv_obj_set_style_bg_color(g_dot_inet, ok ? C_TEAL : C_DIM, 0);

    if (rtt_ms == 0) {
        lv_label_set_text(g_lbl_inet_rtt, "-- ms");
        lv_obj_set_style_text_color(g_lbl_inet_rtt, C_DIM, 0);
    } else {
        snprintf(buf, sizeof(buf), "%lu ms", (unsigned long)rtt_ms);
        lv_label_set_text(g_lbl_inet_rtt, buf);
        lv_color_t c = rtt_ms < 50 ? C_TEAL : rtt_ms < 200 ? C_WARM : C_RED;
        lv_obj_set_style_text_color(g_lbl_inet_rtt, c, 0);
    }
}

void dashboard_ui_update_rain(float rain_mm)
{
    if (!g_lbl_rain) return;
    char buf[16];
    if (rain_mm == 0.0f) {
        lv_label_set_text(g_lbl_rain, "0 mm");
        lv_obj_set_style_text_color(g_lbl_rain, C_DIM, 0);
    } else {
        snprintf(buf, sizeof(buf), "%.1f mm", rain_mm);
        lv_label_set_text(g_lbl_rain, buf);
        lv_color_t c = rain_mm < 1.0f ? C_SEC :
                       rain_mm < 5.0f ? C_BLUE : lv_color_hex(0x7777FF);
        lv_obj_set_style_text_color(g_lbl_rain, c, 0);
    }
}
