/*
 * dashboard_theme.h
 *
 * Extracted design theme from dashboard_ui.c / dashboard_ui.h
 * Single include — drop into any new page's build file.
 *
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  COLOUR PALETTE                                              ║
 * ╠══════════════════════════════════════════════════════════════╣
 * ║  Background layers                                           ║
 * ║    C_BG        #0F1117   deepest background (screen fill)   ║
 * ║    C_CARD      #1A1D27   card / panel surface               ║
 * ║    C_CARD2     #22263A   raised element on a card           ║
 * ║    C_BORDER    #2A2E42   card border, dividers              ║
 * ║                                                              ║
 * ║  Accent colours                                              ║
 * ║    C_BLUE      #4A90D9   energy / information               ║
 * ║    C_TEAL      #2ECC8E   success / online / good            ║
 * ║    C_WARM      #E8834A   active / heating / warning         ║
 * ║    C_GOLD      #E8C44A   on-state indicator dot / clear sky ║
 * ║    C_RED       #E05050   error / offline / danger           ║
 * ║                                                              ║
 * ║  Text hierarchy                                              ║
 * ║    C_PRI       #F0F0F5   primary text (hero numbers, names) ║
 * ║    C_SEC       #9094AB   secondary text (labels, hints)     ║
 * ║    C_DIM       #4A4F65   disabled / off-state / dim labels  ║
 * ║                                                              ║
 * ║  Tinted tile backgrounds (active state fills)               ║
 * ║    C_TILE_WARM #1E1A10   warm-active tile background        ║
 * ║    C_TILE_TEAL #121E13   teal-active tile background        ║
 * ║    C_BDR_WARM  #3A2E10   warm-active tile border            ║
 * ║    C_BDR_TEAL  #1A3020   teal-active tile border            ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  TYPOGRAPHY  (Montserrat — all weights regular)             ║
 * ╠══════════════════════════════════════════════════════════════╣
 * ║  48px   hero numbers    time, temperature, live watts       ║
 * ║  20px   sub-hero        status text, card sub-values        ║
 * ║  16px   body            tile names, value readouts, date    ║
 * ║  14px   label / hint    dim labels, status bar, hints       ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  LAYOUT GRID  (480 × 800 portrait)                          ║
 * ╠══════════════════════════════════════════════════════════════╣
 * ║  SCR_W  480    SCR_H   800                                  ║
 * ║  MARGIN  12    GAP       8    CARD_W  456 (SCR_W - 2×MARGIN)║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  COMPONENT RULES                                             ║
 * ╠══════════════════════════════════════════════════════════════╣
 * ║  Card          bg=C_CARD  border=C_BORDER 1px  radius=12   ║
 * ║  Tile          bg=C_CARD  border=C_BORDER 1px  radius=10   ║
 * ║                pad_all=10                                    ║
 * ║  Button        bg=C_CARD2 border=C_BDR_WARM 1px radius=8   ║
 * ║                pressed=0x2A2520                              ║
 * ║  Dot (off)     fill=C_DIM   size=10  circle                 ║
 * ║  Dot (on warm) fill=C_GOLD  size=10  circle                 ║
 * ║  Dot (on teal) fill=C_TEAL  size=10  circle                 ║
 * ║  Divider h/v   bg=C_BORDER  1px  no border  no radius      ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  STATE RULES  (how accent colours are used semantically)    ║
 * ╠══════════════════════════════════════════════════════════════╣
 * ║  WiFi signal                                                 ║
 * ║    > -70 dBm   C_TEAL    good                               ║
 * ║    > -85 dBm   C_WARM    marginal                           ║
 * ║    ≤ -85 dBm   C_RED     poor                               ║
 * ║    no signal   C_DIM     "No WiFi"                          ║
 * ║                                                              ║
 * ║  Internet RTT                                                ║
 * ║    < 50 ms     C_TEAL    fast                               ║
 * ║    < 200 ms    C_WARM    acceptable                         ║
 * ║    ≥ 200 ms    C_RED     slow                               ║
 * ║    0 / unknown C_DIM     "--"                               ║
 * ║                                                              ║
 * ║  Power factor                                                ║
 * ║    ≥ 0.90      C_TEAL    good                               ║
 * ║    ≥ 0.80      C_WARM    acceptable                         ║
 * ║    < 0.80      C_RED     poor                               ║
 * ║                                                              ║
 * ║  Rain mm                                                     ║
 * ║    0           C_DIM     dry                                 ║
 * ║    < 1         C_SEC     light                               ║
 * ║    < 5         C_BLUE    moderate                           ║
 * ║    ≥ 5         #7777FF   heavy                              ║
 * ║                                                              ║
 * ║  Active tile (warm — boiler, boost, lights on)              ║
 * ║    bg      → C_TILE_WARM                                    ║
 * ║    border  → C_BDR_WARM                                     ║
 * ║    dot     → C_WARM / C_GOLD                                ║
 * ║    text    → C_WARM                                         ║
 * ║                                                              ║
 * ║  Active tile (teal — internet ok)                           ║
 * ║    bg      → C_TILE_TEAL                                    ║
 * ║    border  → C_BDR_TEAL                                     ║
 * ║    dot     → C_TEAL                                         ║
 * ║                                                              ║
 * ║  Inactive / off tile                                         ║
 * ║    bg      → C_CARD                                         ║
 * ║    border  → C_BORDER                                       ║
 * ║    dot     → C_DIM                                          ║
 * ║    text    → C_DIM                                          ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  WEATHER ICON  colour map  (OWM icon code → dot colour)     ║
 * ╠══════════════════════════════════════════════════════════════╣
 * ║  01 (clear)        C_GOLD                                   ║
 * ║  02-04 (clouds)    C_SEC                                    ║
 * ║  09-10 (rain)      C_BLUE                                   ║
 * ║  11 (thunder)      #AA88FF                                  ║
 * ║  13 (snow)         #C8E0FF                                  ║
 * ║  else              C_DIM                                    ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Colour palette ──────────────────────────────────────────────── */

/* Background layers */
#define C_BG          lv_color_hex(0x0F1117)
#define C_CARD        lv_color_hex(0x1A1D27)
#define C_CARD2       lv_color_hex(0x22263A)
#define C_BORDER      lv_color_hex(0x2A2E42)

/* Accent colours */
#define C_BLUE        lv_color_hex(0x4A90D9)
#define C_TEAL        lv_color_hex(0x2ECC8E)
#define C_WARM        lv_color_hex(0xE8834A)
#define C_GOLD        lv_color_hex(0xE8C44A)
#define C_RED         lv_color_hex(0xE05050)

/* Text hierarchy */
#define C_PRI         lv_color_hex(0xF0F0F5)
#define C_SEC         lv_color_hex(0x9094AB)
#define C_DIM         lv_color_hex(0x4A4F65)

/* Tinted tile active-state fills */
#define C_TILE_WARM   lv_color_hex(0x1E1A10)
#define C_TILE_TEAL   lv_color_hex(0x121E13)
#define C_BDR_WARM    lv_color_hex(0x3A2E10)
#define C_BDR_TEAL    lv_color_hex(0x1A3020)

/* One-off colours (not promoted to named constants in original) */
#define C_BTN_PRESSED lv_color_hex(0x2A2520)   /* button pressed state   */
#define C_THUNDER     lv_color_hex(0xAA88FF)   /* OWM icon 11 thunder    */
#define C_SNOW        lv_color_hex(0xC8E0FF)   /* OWM icon 13 snow       */
#define C_RAIN_HEAVY  lv_color_hex(0x7777FF)   /* rain ≥ 5 mm            */

/* ── Typography — Montserrat ─────────────────────────────────────── */
/*
 * Use the LVGL built-in Montserrat sizes.
 * Enable in lv_conf.h:
 *   #define LV_FONT_MONTSERRAT_14  1
 *   #define LV_FONT_MONTSERRAT_16  1
 *   #define LV_FONT_MONTSERRAT_20  1
 *   #define LV_FONT_MONTSERRAT_48  1
 */
#define FONT_HERO     (&lv_font_montserrat_48)  /* time, temp, watts      */
#define FONT_SUB      (&lv_font_montserrat_20)  /* status, sub-values     */
#define FONT_BODY     (&lv_font_montserrat_16)  /* tile names, readouts   */
#define FONT_LABEL    (&lv_font_montserrat_14)  /* dim labels, hints      */

/* ── Layout grid ─────────────────────────────────────────────────── */
#define SCR_W         480
#define SCR_H         800
#define MARGIN         12
#define GAP             8
#define CARD_W        (SCR_W - MARGIN * 2)      /* 456 px                 */

/* ── Component style rules ───────────────────────────────────────── */
/*
 * Card
 *   bg_color   C_CARD
 *   border     C_BORDER  1 px
 *   radius     12
 *   pad_all    0
 *   scrollable false
 *
 * Tile  (small, in the 4-up / 2-col grids)
 *   bg_color   C_CARD
 *   border     C_BORDER  1 px
 *   radius     10
 *   pad_all    10
 *   scrollable false
 *
 * Button
 *   bg_color   C_CARD2
 *   border     C_BDR_WARM  1 px
 *   radius     8
 *   pad_all    0
 *   pressed bg C_BTN_PRESSED
 *   clickable  true
 *
 * Status dot
 *   size       10 × 10
 *   radius     LV_RADIUS_CIRCLE
 *   off        C_DIM
 *   on (warm)  C_GOLD
 *   on (teal)  C_TEAL
 *
 * Divider (horizontal or vertical)
 *   thickness  1 px
 *   bg_color   C_BORDER
 *   border     none
 *   radius     0
 */

/* ── Threshold helpers ───────────────────────────────────────────── */

/* Returns the appropriate colour for a WiFi RSSI value */
static inline lv_color_t theme_wifi_color(int8_t rssi)
{
    if (rssi <= -100 || rssi == 0) return C_DIM;
    if (rssi > -70)  return C_TEAL;
    if (rssi > -85)  return C_WARM;
    return C_RED;
}

/* Returns the appropriate colour for an internet RTT value (ms) */
static inline lv_color_t theme_rtt_color(uint32_t rtt_ms)
{
    if (rtt_ms == 0 || rtt_ms >= 2000) return C_DIM;
    if (rtt_ms < 50)  return C_TEAL;
    if (rtt_ms < 200) return C_WARM;
    return C_RED;
}

/* Returns the appropriate colour for a power factor value */
static inline lv_color_t theme_pf_color(float pf)
{
    if (pf >= 0.9f) return C_TEAL;
    if (pf >= 0.8f) return C_WARM;
    return C_RED;
}

/* Returns the appropriate colour for a rain value (mm) */
static inline lv_color_t theme_rain_color(float mm)
{
    if (mm == 0.0f)  return C_DIM;
    if (mm < 1.0f)   return C_SEC;
    if (mm < 5.0f)   return C_BLUE;
    return C_RAIN_HEAVY;
}

/* Returns the icon circle fill colour for an OWM wicon code string */
static inline lv_color_t theme_owm_icon_color(const char *code)
{
    if (!code || !code[0]) return C_DIM;
    int id = (int)(code[1] - '0') * 10 + (int)(code[2] - '0');
    if (id == 1)             return C_GOLD;
    if (id >= 2 && id <= 4)  return C_SEC;
    if (id == 9 || id == 10) return C_BLUE;
    if (id == 11)            return C_THUNDER;
    if (id == 13)            return C_SNOW;
    return C_DIM;
}

#ifdef __cplusplus
}
#endif
