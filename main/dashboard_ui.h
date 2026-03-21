/*
 * dashboard_ui.h
 *
 * Public interface for the smart home dashboard UI.
 *
 * HOW IT WORKS
 * ------------
 * The UI is built once with dashboard_ui_create().
 * After that, call the dashboard_ui_update_*() functions from
 * anything that runs INSIDE the LVGL task (e.g. an lv_timer callback).
 *
 * Never call LVGL functions from another FreeRTOS task directly —
 * that causes race conditions. When we add MQTT later, the transport
 * layer will post events to a queue; a single lv_timer drains it and
 * calls these update functions safely.
 */

#pragma once

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Build the entire dashboard on the active screen.
 * Call once, after lv_init() and display creation.                   */
void dashboard_ui_create(lv_display_t *disp);

/* Climate card update.
 * temp_c   : current temperature °C        e.g. 21.5
 * target_c : thermostat setpoint °C        e.g. 22.0
 * humidity : relative humidity 0-100 %     e.g. 58
 * co2_ppm  : CO₂ concentration in ppm      e.g. 412                 */
void dashboard_ui_update_climate(float temp_c, float target_c,
                                 uint8_t humidity, uint16_t co2_ppm);

/* Toggle tile update.
 * idx : 0 = Lights   1 = Fan   2 = TV   3 = Lock
 * on  : true = active/on, false = inactive/off                       */
void dashboard_ui_update_tile(uint8_t idx, bool on);

/* Energy bar chart update.
 * values[]     : exactly 7 Wh values (one per hour slot)
 * current_hour : index 0-6 of the bar to highlight as "now"
 * total_kwh    : summary figure shown in the top-right corner         */
void dashboard_ui_update_energy(const uint16_t *values,
                                uint8_t current_hour, float total_kwh);

/* Device row update.
 * idx     : 0 = TV   1 = Thermostat   2 = Lights   3 = Fan
 * online  : true = green dot, false = grey dot
 * status  : short subtitle string, keep under ~22 chars              */
void dashboard_ui_update_device(uint8_t idx, bool online,
                                const char *status);

/* Status bar update.
 * wifi_rssi : signal dBm, e.g. -65  (pass -127 to show "No WiFi")
 * mqtt_ok   : true = MQTT broker is connected                        */
void dashboard_ui_update_status(int8_t wifi_rssi, bool mqtt_ok);

#ifdef __cplusplus
}
#endif
