/*
 * dashboard_ui.c
 *
 * Smart home dashboard for JC4880P443C  480×800  ESP32-P4.
 *
 * STYLE APPROACH — INLINE
 * ───────────────────────
 * Every visual property is set directly on the widget that owns it,
 * using lv_obj_set_style_*(obj, value, selector).
 *
 * The 'selector' argument is almost always 0, which means:
 *   LV_PART_MAIN | LV_STATE_DEFAULT
 * i.e. "the main visual part of this widget in its default state".
 *
 * Complex widgets (arc, bar) have multiple parts you style separately:
 *   LV_PART_MAIN      — the background track
 *   LV_PART_INDICATOR — the filled/active portion
 *   LV_PART_KNOB      — the draggable handle (we hide this)
 *
 * FONT SIZES USED
 * ───────────────
 *   lv_font_montserrat_14  — dim section labels, hour ticks
 *   lv_font_montserrat_16  — subtitles, device names, tile labels
 *   lv_font_montserrat_20  — primary labels, status values
 *   lv_font_montserrat_28  — big hero numbers (temperature, kWh)
 *
 * LAYOUT
 * ──────
 * Everything uses absolute positioning (lv_obj_set_pos) relative to
 * its parent. The parent chain is always:
 *   screen → card container → widget
 * Card containers are plain lv_obj_t with a coloured background.
 * We disable scrolling on every container — nothing scrolls yet.
 */

#include "dashboard_ui.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI";

/* ─────────────────────────────────────────────────────────────────────
 * COLOUR PALETTE
 * All hex values live here. To retheme: change these, recompile.
 * ───────────────────────────────────────────────────────────────────── */
#define C_BG           lv_color_hex(0x0F1117)  /* screen background        */
#define C_CARD         lv_color_hex(0x1A1D27)  /* card surface             */
#define C_CARD2        lv_color_hex(0x22263A)  /* slightly elevated surface */
#define C_BORDER       lv_color_hex(0x2A2E42)  /* subtle card border       */
#define C_ACCENT_BLUE  lv_color_hex(0x4A90D9)  /* active / on state        */
#define C_ACCENT_TEAL  lv_color_hex(0x2ECC8E)  /* ok / connected / positive */
#define C_ACCENT_WARM  lv_color_hex(0xE8834A)  /* heating / warning amber  */
#define C_TEXT_PRI     lv_color_hex(0xF0F0F5)  /* primary white text       */
#define C_TEXT_SEC     lv_color_hex(0x9094AB)  /* secondary muted text     */
#define C_TEXT_DIM     lv_color_hex(0x4A4F65)  /* dim labels               */
#define C_TILE_ON      lv_color_hex(0x162136)  /* active tile background   */
#define C_DOT_ON       lv_color_hex(0x2ECC8E)  /* online dot               */
#define C_DOT_OFF      lv_color_hex(0x2A2E42)  /* offline dot              */
#define C_BAR_NOW      lv_color_hex(0x4A90D9)  /* current-hour bar         */
#define C_BAR_PAST     lv_color_hex(0x2A3A5A)  /* past-hour bar            */
#define C_BAR_FUTURE   lv_color_hex(0x1A2030)  /* future bar               */

/* ─────────────────────────────────────────────────────────────────────
 * LAYOUT — all values in pixels, portrait 480 × 800
 * Tweak the _Y and _H constants if you need to shift zones.
 * ───────────────────────────────────────────────────────────────────── */
#define SCR_W      480
#define SCR_H      800
#define MARGIN     14     /* left / right screen margin         */
#define GAP        10     /* vertical gap between cards         */
#define CARD_W     (SCR_W - MARGIN * 2)   /* 452 px             */

/* Zone Y positions and heights */
#define Y_STATUS   0
#define H_STATUS   34

#define Y_HEADER   (Y_STATUS + H_STATUS)
#define H_HEADER   50

#define Y_CLIMATE  (Y_HEADER + H_HEADER + GAP)
#define H_CLIMATE  138

#define Y_TILES    (Y_CLIMATE + H_CLIMATE + GAP)
#define H_TILES    96

#define Y_ENERGY   (Y_TILES + H_TILES + GAP)
#define H_ENERGY   128

#define Y_DEVICES  (Y_ENERGY + H_ENERGY + GAP)
#define H_DEV_ROW  56
#define H_DEVICES  (H_DEV_ROW * 4 + GAP * 3)

/* Tile geometry: 4 equal tiles with gaps */
#define TILE_W     ((CARD_W - GAP * 3) / 4)   /* ~102 px          */

/* ─────────────────────────────────────────────────────────────────────
 * WIDGET HANDLES — pointers to objects we update after build
 * Static globals scoped to this file only (no leakage).
 * ───────────────────────────────────────────────────────────────────── */

/* Status bar */
static lv_obj_t *g_lbl_wifi;
static lv_obj_t *g_lbl_mqtt;

/* Climate card */
static lv_obj_t *g_arc_temp;
static lv_obj_t *g_lbl_temp_big;     /* "21°"          */
static lv_obj_t *g_lbl_temp_target;  /* "target  22°"  */
static lv_obj_t *g_lbl_heat_state;   /* "Heating"      */
static lv_obj_t *g_lbl_humidity;     /* "58%"          */
static lv_obj_t *g_lbl_co2;          /* "412 ppm"      */

/* Toggle tiles */
static lv_obj_t *g_tile[4];
static lv_obj_t *g_tile_dot[4];
static lv_obj_t *g_tile_state[4];    /* "On" / "Off"   */

/* Energy chart */
static lv_obj_t *g_bar[7];
static lv_obj_t *g_lbl_kwh;          /* "3.2 kWh"      */
static lv_obj_t *g_lbl_delta;        /* "-12% vs avg"  */

/* Device rows */
static lv_obj_t *g_dev_dot[4];
static lv_obj_t *g_dev_status[4];

/* ─────────────────────────────────────────────────────────────────────
 * SMALL HELPERS
 * ───────────────────────────────────────────────────────────────────── */

/*
 * make_card — create a plain rectangle used as a card background.
 *
 * parent : the screen (or another container)
 * x, y   : position relative to parent's top-left (ignoring parent padding
 *          because we set pad_all=0 on the screen)
 * w, h   : size in pixels
 *
 * Returns the lv_obj_t* so you can add children to it.
 */
static lv_obj_t *make_card(lv_obj_t *parent,
                            int32_t x, int32_t y,
                            int32_t w, int32_t h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);

    /* Visual */
    lv_obj_set_style_bg_color(c, C_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, C_BORDER, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_radius(c, 12, 0);

    /* Remove all internal padding — we position children ourselves */
    lv_obj_set_style_pad_all(c, 0, 0);

    /* Disable scrolling and hide the scrollbar */
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(c, LV_SCROLLBAR_MODE_OFF);

    return c;
}

/*
 * make_label — create a text label at an absolute position.
 *
 * parent : container the label lives in
 * text   : initial string (copied by LVGL)
 * font   : one of lv_font_montserrat_14 / 16 / 20 / 28
 * color  : text colour
 * x, y   : position relative to parent top-left
 */
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

/* ─────────────────────────────────────────────────────────────────────
 * SECTION: STATUS BAR
 * ───────────────────────────────────────────────────────────────────── */
static void build_status_bar(lv_obj_t *screen)
{
    /* Full-width strip, no radius */
    lv_obj_t *bar = lv_obj_create(screen);
    lv_obj_set_pos(bar, 0, Y_STATUS);
    lv_obj_set_size(bar, SCR_W, H_STATUS);
    lv_obj_set_style_bg_color(bar, C_CARD, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    /* Clock — left */
    /* Static, not stored — we don't update it from MQTT */
    lv_obj_t *lbl_clock = lv_label_create(bar);
    lv_label_set_text(lbl_clock, "09:41");
    lv_obj_set_style_text_font(lbl_clock, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_clock, C_TEXT_SEC, 0);
    lv_obj_align(lbl_clock, LV_ALIGN_LEFT_MID, MARGIN, 0);

    /* MQTT status — centre */
    g_lbl_mqtt = lv_label_create(bar);
    lv_label_set_text(g_lbl_mqtt, "MQTT --");
    lv_obj_set_style_text_font(g_lbl_mqtt, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_lbl_mqtt, C_TEXT_DIM, 0);
    lv_obj_align(g_lbl_mqtt, LV_ALIGN_CENTER, 0, 0);

    /* WiFi — right */
    g_lbl_wifi = lv_label_create(bar);
    lv_label_set_text(g_lbl_wifi, "WiFi --");
    lv_obj_set_style_text_font(g_lbl_wifi, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_lbl_wifi, C_TEXT_DIM, 0);
    lv_obj_align(g_lbl_wifi, LV_ALIGN_RIGHT_MID, -MARGIN, 0);
}

/* ─────────────────────────────────────────────────────────────────────
 * SECTION: HEADER  (greeting + outdoor pill)
 * ───────────────────────────────────────────────────────────────────── */
static void build_header(lv_obj_t *screen)
{
    /* Greeting — montserrat_20, primary white */
    make_label(screen, "Good morning",
               &lv_font_montserrat_20, C_TEXT_PRI,
               MARGIN, Y_HEADER + 6);

    /* Date — montserrat_14, muted */
    make_label(screen, "Monday, 21 March",
               &lv_font_montserrat_14, C_TEXT_SEC,
               MARGIN, Y_HEADER + 30);

    /* Outdoor pill — right side */
    lv_obj_t *pill = lv_obj_create(screen);
    lv_obj_set_size(pill, 108, 28);
    lv_obj_set_pos(pill, SCR_W - MARGIN - 108, Y_HEADER + 11);
    lv_obj_set_style_bg_color(pill, C_CARD, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(pill, C_BORDER, 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_radius(pill, 14, 0);  /* fully rounded sides */
    lv_obj_set_style_pad_all(pill, 0, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_out = lv_label_create(pill);
    lv_label_set_text(lbl_out, "12\xC2\xB0""C  Cloudy"); /* ° = UTF-8 0xC2 0xB0 */
    lv_obj_set_style_text_font(lbl_out, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_out, C_TEXT_SEC, 0);
    lv_obj_center(lbl_out);
}

/* ─────────────────────────────────────────────────────────────────────
 * SECTION: CLIMATE CARD
 *
 * Three columns inside the card:
 *
 *   Col A (x=14..114)  : arc gauge + big temperature number centred on it
 *   Col B (x=120..280) : state label + "Temperature" label + target
 *   Col C (x=290..438) : Humidity row + CO₂ row
 *
 * lv_arc notes:
 *   set_bg_angles(start, end) — the arc sweeps from start° to end°.
 *   Angles: 0=right 90=bottom 180=left 270=top (clockwise).
 *   So 135→45 gives a 270° arc, opening at the bottom — like a gauge.
 *   set_range(min, max)  — the value range the arc represents.
 *   set_value(v)         — moves the coloured indicator end.
 * ───────────────────────────────────────────────────────────────────── */
static void build_climate_card(lv_obj_t *screen)
{
    lv_obj_t *card = make_card(screen, MARGIN, Y_CLIMATE, CARD_W, H_CLIMATE);

    /* ── Arc gauge ──────────────────────────────────────────────────── */
    g_arc_temp = lv_arc_create(card);
    lv_obj_set_size(g_arc_temp, 104, 104);
    lv_obj_set_pos(g_arc_temp, 10, 14);

    lv_arc_set_bg_angles(g_arc_temp, 135, 45);  /* 270° sweep         */
    lv_arc_set_range(g_arc_temp, 10, 35);        /* 10°C – 35°C        */
    lv_arc_set_value(g_arc_temp, 21);            /* placeholder: 21°C  */

    /* Track (unlit background arc) */
    lv_obj_set_style_arc_color(g_arc_temp, C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_arc_temp, 9, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_arc_temp, LV_OPA_TRANSP, LV_PART_MAIN);

    /* Indicator (the lit coloured portion) */
    lv_obj_set_style_arc_color(g_arc_temp, C_ACCENT_WARM, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(g_arc_temp, 9, LV_PART_INDICATOR);

    /* Hide the knob — display-only arc */
    lv_obj_set_style_opa(g_arc_temp, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(g_arc_temp, LV_OBJ_FLAG_CLICKABLE);

    /* Big temperature label — centred inside the arc circle.
     * lv_obj_align_to(obj, base, align, x_ofs, y_ofs)
     * aligns 'obj' relative to 'base' using the chosen anchor point.  */
    g_lbl_temp_big = lv_label_create(card);
    lv_label_set_text(g_lbl_temp_big, "21\xC2\xB0");
    lv_obj_set_style_text_font(g_lbl_temp_big, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(g_lbl_temp_big, C_TEXT_PRI, 0);
    lv_obj_align_to(g_lbl_temp_big, g_arc_temp, LV_ALIGN_CENTER, 0, -4);

    /* ── Middle column ──────────────────────────────────────────────── */
    int32_t cx = 124;  /* left edge of middle column */

    /* Heating state — warm amber when active */
    g_lbl_heat_state = lv_label_create(card);
    lv_label_set_text(g_lbl_heat_state, "Heating");
    lv_obj_set_style_text_font(g_lbl_heat_state, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_lbl_heat_state, C_ACCENT_WARM, 0);
    lv_obj_set_pos(g_lbl_heat_state, cx, 18);

    /* "Temperature" section label — dim */
    lv_obj_t *lbl_t_label = lv_label_create(card);
    lv_label_set_text(lbl_t_label, "Temperature");
    lv_obj_set_style_text_font(lbl_t_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_t_label, C_TEXT_DIM, 0);
    lv_obj_set_pos(lbl_t_label, cx, 42);

    /* Target temp */
    g_lbl_temp_target = lv_label_create(card);
    lv_label_set_text(g_lbl_temp_target, "target  22\xC2\xB0");
    lv_obj_set_style_text_font(g_lbl_temp_target, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_lbl_temp_target, C_TEXT_SEC, 0);
    lv_obj_set_pos(g_lbl_temp_target, cx, 64);

    /* Small divider line between columns — a thin rectangle */
    lv_obj_t *div = lv_obj_create(card);
    lv_obj_set_size(div, 1, H_CLIMATE - 28);
    lv_obj_set_pos(div, CARD_W - 110, 14);
    lv_obj_set_style_bg_color(div, C_BORDER, 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_set_style_radius(div, 0, 0);
    lv_obj_clear_flag(div, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Right column: Humidity + CO₂ ──────────────────────────────── */
    int32_t rx = CARD_W - 100;  /* left edge of right column */

    lv_obj_t *lbl_hum_label = lv_label_create(card);
    lv_label_set_text(lbl_hum_label, "Humidity");
    lv_obj_set_style_text_font(lbl_hum_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_hum_label, C_TEXT_DIM, 0);
    lv_obj_set_pos(lbl_hum_label, rx, 18);

    g_lbl_humidity = lv_label_create(card);
    lv_label_set_text(g_lbl_humidity, "58%");
    lv_obj_set_style_text_font(g_lbl_humidity, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_lbl_humidity, C_TEXT_PRI, 0);
    lv_obj_set_pos(g_lbl_humidity, rx, 36);

    lv_obj_t *lbl_co2_label = lv_label_create(card);
    /* CO₂ — subscript 2 via UTF-8 U+2082 → 0xE2 0x82 0x82 */
    lv_label_set_text(lbl_co2_label, "CO\xE2\x82\x82");
    lv_obj_set_style_text_font(lbl_co2_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_co2_label, C_TEXT_DIM, 0);
    lv_obj_set_pos(lbl_co2_label, rx, 74);

    g_lbl_co2 = lv_label_create(card);
    lv_label_set_text(g_lbl_co2, "412 ppm");
    lv_obj_set_style_text_font(g_lbl_co2, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_lbl_co2, C_TEXT_PRI, 0);
    lv_obj_set_pos(g_lbl_co2, rx, 92);
}

/* ─────────────────────────────────────────────────────────────────────
 * SECTION: TOGGLE TILES
 *
 * Four equal tiles in a row.  Each tile has:
 *   • A small coloured dot (top-left of tile)
 *   • A device name label (middle, montserrat_16)
 *   • A state label "On"/"Off" (bottom, montserrat_14, dim)
 *
 * Background changes colour when active (C_TILE_ON vs C_CARD).
 * Touch events and MQTT commands will be wired here in a later step.
 * ───────────────────────────────────────────────────────────────────── */
static const char *TILE_NAMES[4]    = {"Lights", "Fan",  "TV",   "Lock"};
static const bool  TILE_DEFAULTS[4] = {true,     false,  true,   false};

static void build_tiles(lv_obj_t *screen)
{
    for (int i = 0; i < 4; i++) {
        bool on = TILE_DEFAULTS[i];
        int32_t x = MARGIN + i * (TILE_W + GAP);

        /* Tile container */
        g_tile[i] = lv_obj_create(screen);
        lv_obj_set_pos(g_tile[i], x, Y_TILES);
        lv_obj_set_size(g_tile[i], TILE_W, H_TILES);
        lv_obj_set_style_bg_color(g_tile[i], on ? C_TILE_ON : C_CARD, 0);
        lv_obj_set_style_bg_opa(g_tile[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(g_tile[i], C_BORDER, 0);
        lv_obj_set_style_border_width(g_tile[i], 1, 0);
        lv_obj_set_style_radius(g_tile[i], 10, 0);
        lv_obj_set_style_pad_all(g_tile[i], 10, 0);
        lv_obj_clear_flag(g_tile[i], LV_OBJ_FLAG_SCROLLABLE);

        /* Coloured dot — top-left */
        g_tile_dot[i] = lv_obj_create(g_tile[i]);
        lv_obj_set_size(g_tile_dot[i], 8, 8);
        lv_obj_set_pos(g_tile_dot[i], 0, 0);
        lv_obj_set_style_radius(g_tile_dot[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(g_tile_dot[i],
                                   on ? C_ACCENT_BLUE : C_TEXT_DIM, 0);
        lv_obj_set_style_bg_opa(g_tile_dot[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(g_tile_dot[i], 0, 0);

        /* Device name — middle of tile, montserrat_16 */
        lv_obj_t *lbl_name = lv_label_create(g_tile[i]);
        lv_label_set_text(lbl_name, TILE_NAMES[i]);
        lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl_name, C_TEXT_PRI, 0);
        lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 0, 4);

        /* State label — bottom-left, montserrat_14 */
        g_tile_state[i] = lv_label_create(g_tile[i]);
        lv_label_set_text(g_tile_state[i], on ? "On" : "Off");
        lv_obj_set_style_text_font(g_tile_state[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(g_tile_state[i],
                                     on ? C_ACCENT_BLUE : C_TEXT_DIM, 0);
        lv_obj_align(g_tile_state[i], LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
}

/* ─────────────────────────────────────────────────────────────────────
 * SECTION: ENERGY BAR CHART
 *
 * Seven lv_bar widgets, arranged horizontally.
 * lv_bar is a rectangular progress indicator:
 *   LV_PART_MAIN      = the grey track (full height)
 *   LV_PART_INDICATOR = the coloured filled portion (grows from bottom)
 *
 * lv_bar_set_range(bar, min, max) — sets the value scale.
 * lv_bar_set_value(bar, val, LV_ANIM_OFF) — sets current fill level.
 *
 * Right column shows the total kWh and a delta vs average.
 * ───────────────────────────────────────────────────────────────────── */
static const char    *HOUR_LABELS[7]    = {"6am","7","8","9","10","11","12"};
static const uint16_t ENERGY_VALS[7]    = {120, 215, 280, 175, 345, 310, 255};
static const uint8_t  ENERGY_NOW        = 4;   /* "10am" = index 4   */
//static const float    ENERGY_TOTAL      = 3.2f;

static void build_energy_card(lv_obj_t *screen)
{
    lv_obj_t *card = make_card(screen, MARGIN, Y_ENERGY, CARD_W, H_ENERGY);

    /* Section label — top-left, montserrat_14 dim */
    lv_obj_t *lbl_sec = lv_label_create(card);
    lv_label_set_text(lbl_sec, "Energy today");
    lv_obj_set_style_text_font(lbl_sec, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_sec, C_TEXT_DIM, 0);
    lv_obj_set_pos(lbl_sec, 14, 12);

    /* Total kWh — top-right, montserrat_20 white */
    g_lbl_kwh = lv_label_create(card);
    lv_label_set_text(g_lbl_kwh, "3.2 kWh");
    lv_obj_set_style_text_font(g_lbl_kwh, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_lbl_kwh, C_TEXT_PRI, 0);
    lv_obj_align(g_lbl_kwh, LV_ALIGN_TOP_RIGHT, -14, 10);

    /* Delta — below total, teal */
    g_lbl_delta = lv_label_create(card);
    lv_label_set_text(g_lbl_delta, "-12% vs avg");
    lv_obj_set_style_text_font(g_lbl_delta, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_lbl_delta, C_ACCENT_TEAL, 0);
    lv_obj_align(g_lbl_delta, LV_ALIGN_TOP_RIGHT, -14, 34);

    /*
     * Bar layout:
     *   Available width for bars = CARD_W - 14(left pad) - 14(right pad) - 96(right stat col)
     *                            = 452 - 28 - 96 = 328 px
     *   7 bars with 4 px gaps between them:
     *   bar_w = (328 - 6*4) / 7 = (328-24)/7 = 304/7 ≈ 43 px
     */
    int32_t bars_x     = 14;
    int32_t bars_y     = 36;
    int32_t bars_avail = CARD_W - 28 - 96;
    int32_t bar_gap    = 4;
    int32_t bar_w      = (bars_avail - bar_gap * 6) / 7;  /* ~43 px */
    int32_t bar_h      = H_ENERGY - bars_y - 26;          /* leave 26px for hour labels */

    for (int i = 0; i < 7; i++) {
        int32_t bx = bars_x + i * (bar_w + bar_gap);

        g_bar[i] = lv_bar_create(card);
        lv_obj_set_size(g_bar[i], bar_w, bar_h);
        lv_obj_set_pos(g_bar[i], bx, bars_y);

        lv_bar_set_range(g_bar[i], 0, 400);
        lv_bar_set_value(g_bar[i], ENERGY_VALS[i], LV_ANIM_OFF);

        /* Track */
        lv_obj_set_style_bg_color(g_bar[i], C_CARD2, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(g_bar[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(g_bar[i], 4, LV_PART_MAIN);
        lv_obj_set_style_border_width(g_bar[i], 0, LV_PART_MAIN);

        /* Indicator — colour by time relative to now */
        lv_color_t ic = (i < ENERGY_NOW)  ? C_BAR_PAST
                       :(i == ENERGY_NOW) ? C_BAR_NOW
                                          : C_BAR_FUTURE;
        lv_obj_set_style_bg_color(g_bar[i], ic, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(g_bar[i], LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(g_bar[i], 4, LV_PART_INDICATOR);

        /* Hour label below bar — montserrat_14, dim */
        lv_obj_t *lbl_h = lv_label_create(card);
        lv_label_set_text(lbl_h, HOUR_LABELS[i]);
        lv_obj_set_style_text_font(lbl_h, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl_h,
                                     i == ENERGY_NOW ? C_ACCENT_BLUE : C_TEXT_DIM, 0);
        lv_obj_set_pos(lbl_h, bx + bar_w / 2 - 8, bars_y + bar_h + 4);
    }
}

/* ─────────────────────────────────────────────────────────────────────
 * SECTION: DEVICE LIST
 *
 * Four rows inside one card. Each row is a C_CARD2 rounded rectangle:
 *   [dot 10px]  [device name  montserrat_16  white]
 *               [status sub   montserrat_14  muted]
 *
 * The dot colour signals online (teal) vs offline (dim).
 * ───────────────────────────────────────────────────────────────────── */
static const char *DEV_NAMES[4]    = {"Living room TV",   "Smart thermostat",
                                       "Ceiling lights",   "Ceiling fan"};
/* Middle dot U+00B7 = 0xC2 0xB7 in UTF-8 */
static const char *DEV_STATUS[4]   = {"On \xC2\xB7 Netflix \xC2\xB7 65W",
                                       "Heating \xC2\xB7 Target 22\xC2\xB0",
                                       "On \xC2\xB7 70% \xC2\xB7 14W",
                                       "Off \xC2\xB7 Living room"};
static const bool  DEV_ONLINE[4]   = {true, true, true, false};

static void build_devices_card(lv_obj_t *screen)
{
    lv_obj_t *card = make_card(screen, MARGIN, Y_DEVICES, CARD_W, H_DEVICES);

    /* Section label */
    lv_obj_t *lbl_sec = lv_label_create(card);
    lv_label_set_text(lbl_sec, "Devices");
    lv_obj_set_style_text_font(lbl_sec, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_sec, C_TEXT_DIM, 0);
    lv_obj_set_pos(lbl_sec, 14, 10);

    int32_t inner_w = CARD_W - 28;  /* CARD_W minus left+right 14px */

    for (int i = 0; i < 4; i++) {
        bool online = DEV_ONLINE[i];
        int32_t ry = 30 + i * (H_DEV_ROW + GAP);

        /* Row background */
        lv_obj_t *row = lv_obj_create(card);
        lv_obj_set_size(row, inner_w, H_DEV_ROW);
        lv_obj_set_pos(row, 14, ry);
        lv_obj_set_style_bg_color(row, C_CARD2, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        /* Online/offline dot */
        g_dev_dot[i] = lv_obj_create(row);
        lv_obj_set_size(g_dev_dot[i], 10, 10);
        lv_obj_align(g_dev_dot[i], LV_ALIGN_LEFT_MID, 12, 0);
        lv_obj_set_style_radius(g_dev_dot[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(g_dev_dot[i],
                                   online ? C_DOT_ON : C_DOT_OFF, 0);
        lv_obj_set_style_bg_opa(g_dev_dot[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(g_dev_dot[i], 0, 0);

        /* Device name — montserrat_16 white */
        lv_obj_t *lbl_name = lv_label_create(row);
        lv_label_set_text(lbl_name, DEV_NAMES[i]);
        lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl_name, C_TEXT_PRI, 0);
        lv_obj_set_pos(lbl_name, 30, 8);

        /* Status subtitle — montserrat_14, muted/dim based on online */
        g_dev_status[i] = lv_label_create(row);
        lv_label_set_text(g_dev_status[i], DEV_STATUS[i]);
        lv_obj_set_style_text_font(g_dev_status[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(g_dev_status[i],
                                     online ? C_TEXT_SEC : C_TEXT_DIM, 0);
        lv_obj_set_pos(g_dev_status[i], 30, 30);
    }
}

/* ═════════════════════════════════════════════════════════════════════
 * STATUS BAR TIMER CALLBACK
 * Called by lv_timer every 5 s, from inside the LVGL task.
 * Reads g_wifi_state (set by the WiFi event handler) and updates
 * the status bar labels. mqtt_ok is false until MQTT is added.
 * ═════════════════════════════════════════════════════════════════════ */
static void status_bar_timer_cb(lv_timer_t *timer)
{
    /* wifi_manager_get_rssi() does a fresh esp_wifi_sta_get_ap_info()
     * call each time so the displayed value stays current.            */
    int8_t rssi = wifi_manager_get_rssi();
    dashboard_ui_update_status(rssi, false); /* mqtt_ok=false for now */
}

/* ═════════════════════════════════════════════════════════════════════
 * PUBLIC — dashboard_ui_create
 * ═════════════════════════════════════════════════════════════════════ */
void dashboard_ui_create(lv_display_t *disp)
{
    ESP_LOGI(TAG, "Building dashboard — total height budget: %d px", SCR_H);

    /* Get the active screen and zero out its default styling */
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Build every zone top to bottom */
    build_status_bar(scr);
    build_header(scr);
    build_climate_card(scr);
    build_tiles(scr);
    build_energy_card(scr);
    build_devices_card(scr);

    /* Log the bottom of the last zone so you can verify nothing clips */
    /* ── Status bar poll timer ──────────────────────────────────────
     * Runs every 5 s inside the LVGL task — the only safe place to
     * call LVGL functions.  Reads g_wifi_state (written by the WiFi
     * event handler) and refreshes the status bar labels.
     *
     * lv_timer_create(callback, period_ms, user_data)
     * The callback receives an lv_timer_t* — cast user_data if needed. */
    lv_timer_create(status_bar_timer_cb, 5000, NULL);

    ESP_LOGI(TAG, "UI built. Bottom of devices zone: %d px (screen: %d px)",
             Y_DEVICES + H_DEVICES, SCR_H);
}

/* ═════════════════════════════════════════════════════════════════════
 * PUBLIC — update functions (called later from lv_timer / MQTT events)
 * ═════════════════════════════════════════════════════════════════════ */

void dashboard_ui_update_climate(float temp_c, float target_c,
                                 uint8_t humidity, uint16_t co2_ppm)
{
    if (!g_arc_temp) return;

    /* Arc position — clamp to declared range */
    int32_t arc_val = (int32_t)temp_c;
    if (arc_val < 10) arc_val = 10;
    if (arc_val > 35) arc_val = 35;
    lv_arc_set_value(g_arc_temp, arc_val);

    /* Big temperature number */
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f\xC2\xB0", temp_c);
    lv_label_set_text(g_lbl_temp_big, buf);

    /* Heating vs idle */
    bool heating = (temp_c < target_c - 0.2f); /* 0.2° deadband */
    lv_label_set_text(g_lbl_heat_state, heating ? "Heating" : "Idle");
    lv_obj_set_style_text_color(g_lbl_heat_state,
                                 heating ? C_ACCENT_WARM : C_TEXT_DIM, 0);

    /* Target */
    snprintf(buf, sizeof(buf), "target  %.0f\xC2\xB0", target_c);
    lv_label_set_text(g_lbl_temp_target, buf);

    /* Humidity */
    snprintf(buf, sizeof(buf), "%u%%", humidity);
    lv_label_set_text(g_lbl_humidity, buf);

    /* CO₂ */
    snprintf(buf, sizeof(buf), "%u ppm", co2_ppm);
    lv_label_set_text(g_lbl_co2, buf);
}

void dashboard_ui_update_tile(uint8_t idx, bool on)
{
    if (idx >= 4 || !g_tile[idx]) return;

    lv_obj_set_style_bg_color(g_tile[idx], on ? C_TILE_ON : C_CARD, 0);
    lv_obj_set_style_bg_color(g_tile_dot[idx],
                               on ? C_ACCENT_BLUE : C_TEXT_DIM, 0);
    lv_label_set_text(g_tile_state[idx], on ? "On" : "Off");
    lv_obj_set_style_text_color(g_tile_state[idx],
                                 on ? C_ACCENT_BLUE : C_TEXT_DIM, 0);
}

void dashboard_ui_update_energy(const uint16_t *values,
                                uint8_t current_hour, float total_kwh)
{
    if (!g_bar[0]) return;

    for (int i = 0; i < 7; i++) {
        lv_bar_set_value(g_bar[i], values[i], LV_ANIM_ON);
        lv_color_t ic = (i < current_hour)  ? C_BAR_PAST
                       :(i == current_hour) ? C_BAR_NOW
                                            : C_BAR_FUTURE;
        lv_obj_set_style_bg_color(g_bar[i], ic, LV_PART_INDICATOR);
    }

    char buf[20];
    snprintf(buf, sizeof(buf), "%.1f kWh", total_kwh);
    lv_label_set_text(g_lbl_kwh, buf);
}

void dashboard_ui_update_device(uint8_t idx, bool online, const char *status)
{
    if (idx >= 4 || !g_dev_dot[idx]) return;

    lv_obj_set_style_bg_color(g_dev_dot[idx],
                               online ? C_DOT_ON : C_DOT_OFF, 0);
    lv_label_set_text(g_dev_status[idx], status ? status : "");
    lv_obj_set_style_text_color(g_dev_status[idx],
                                 online ? C_TEXT_SEC : C_TEXT_DIM, 0);
}

void dashboard_ui_update_status(int8_t wifi_rssi, bool mqtt_ok)
{
    if (!g_lbl_wifi) return;

    char buf[24];
    if (wifi_rssi <= -100) {
        lv_label_set_text(g_lbl_wifi, "No WiFi");
        lv_obj_set_style_text_color(g_lbl_wifi, C_TEXT_DIM, 0);
    } else {
        snprintf(buf, sizeof(buf), "WiFi %ddBm", (int)wifi_rssi);
        lv_label_set_text(g_lbl_wifi, buf);
        /* Green > -70, amber > -85, red below */
        lv_color_t c = (wifi_rssi > -70) ? C_ACCENT_TEAL
                      :(wifi_rssi > -85) ? C_ACCENT_WARM
                                         : lv_color_hex(0xE05050);
        lv_obj_set_style_text_color(g_lbl_wifi, c, 0);
    }

    lv_label_set_text(g_lbl_mqtt, mqtt_ok ? "MQTT ok" : "MQTT --");
    lv_obj_set_style_text_color(g_lbl_mqtt,
                                 mqtt_ok ? C_ACCENT_TEAL : C_TEXT_DIM, 0);
}
