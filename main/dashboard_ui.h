/*
 * dashboard_ui.h
 *
 * Public interface for the landscape smart home dashboard.
 *
 * LAYOUT (800 x 480, landscape)
 * ─────────────────────────────
 *   Status bar     30 px   date (left) · MQTT (centre) · WiFi (right)
 *   ┌─────────────────────────┬──────────────────────┐
 *   │  Weather + Clock hero   │   Energy hero        │  ~290 px
 *   │  time 48px | temp 48px  │   watts 48px         │
 *   │  condition · stats row  │   kWh · PF · volts   │
 *   └─────────────────────────┴──────────────────────┘
 *   ┌──────┬──────┬──────┬──────┬────────────────────┐
 *   │Boiler│Boost │ Net  │ Rain │  Boiler detail      │  ~148 px
 *   └──────┴──────┴──────┴──────┴────────────────────┘
 *
 * THREAD SAFETY
 * ─────────────
 * All dashboard_ui_update_*() functions must be called from within
 * the LVGL task only (lv_timer or lv_event callbacks).
 * The MQTT drain timer (100 ms) and WiFi poll timer (5 s) are both
 * registered inside dashboard_ui_create() and run in the LVGL task.
 */

#pragma once

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Build the entire dashboard. Call once after lv_init(), display
 * creation, and mqtt_manager_start() (queue must exist).            */
void dashboard_ui_create(lv_display_t *disp);

/* ── Update functions — LVGL task only ───────────────────────────── */

/* Status bar */
void dashboard_ui_update_status(int8_t wifi_rssi, bool mqtt_ok);

/* Clock — from /SYS/time  e.g. "20:04"                             */
void dashboard_ui_update_time(const char *time_str);

/* Date  — from /SYS/datel e.g. "Saturday 21 March"                 */
void dashboard_ui_update_date(const char *date_str);

/* Weather hero card
 * temp_c        : outdoor °C                       e.g.  8.2
 * min_c/max_c   : daily min/max °C                 e.g.  7.2 / 11.1
 * humidity      : 0-100 %                          e.g. 61
 * wind_mps      : wind speed m/s                   e.g.  2.76
 * wind_deg      : bearing 0-360°                   e.g. 33
 * condition     : short word                       e.g. "Clear"
 * detail        : longer phrase (overrides cond.)  e.g. "clear sky"
 * icon_code     : OWM wicon string                 e.g. "w01n"
 * sunrise/set   : time strings                     e.g. "06:03"    */
void dashboard_ui_update_weather(float temp_c,
                                  float min_c, float max_c,
                                  uint8_t humidity,
                                  float wind_mps, uint16_t wind_deg,
                                  const char *condition,
                                  const char *detail,
                                  const char *icon_code,
                                  const char *sunrise,
                                  const char *sunset);

/* Energy card
 * watts         : live W                           e.g. 553.7
 * today_kwh     : cumulative from midnight         e.g.   9.924
 * yesterday_kwh : previous day total               e.g.  17.210
 * power_factor  : 0.0 – 1.0                        e.g.   0.940
 * voltage       : mains V                          e.g. 251.4      */
void dashboard_ui_update_energy(float watts,
                                 float today_kwh,
                                 float yesterday_kwh,
                                 float power_factor,
                                 float voltage);

/* Boiler tile + detail card
 * power_on      : boiler firing
 * winter_mode   : true = winter, false = summer                    */
void dashboard_ui_update_boiler(bool power_on, bool winter_mode);

/* Internet tile
 * rtt_ms        : round-trip ms  (0 = unknown)                     */
void dashboard_ui_update_internet(uint32_t rtt_ms);

/* Rain tile
 * rain_mm       : precipitation mm                e.g. 0.13        */
void dashboard_ui_update_rain(float rain_mm);

#ifdef __cplusplus
}
#endif
