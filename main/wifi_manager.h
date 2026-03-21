/*
 * wifi_manager.h
 *
 * Thin wrapper around the IDF WiFi station driver.
 *
 * DESIGN
 * ──────
 * wifi_manager_start() initialises WiFi and blocks until either:
 *   • the station gets an IP address  → returns ESP_OK
 *   • retries are exhausted           → returns ESP_FAIL
 *
 * After a successful start, wifi_manager_get_rssi() can be called
 * at any time to get the current signal strength.
 *
 * The status bar on the dashboard is updated via a shared state struct
 * (wifi_manager_state_t) that is written here and read by an lv_timer
 * inside the LVGL task — so LVGL is never touched from this module.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Connection state — written by wifi_manager, read by the LVGL timer */
typedef struct {
    bool    connected;   /* true once we have an IP address          */
    int8_t  rssi;        /* last measured RSSI in dBm  (0 = unknown) */
} wifi_manager_state_t;

/* Global state — declared here, defined in wifi_manager.c.
 * The LVGL status-bar timer reads this without a mutex because:
 *   • connected is written once (false→true) and never goes back
 *     during normal operation.
 *   • rssi is an int8_t — single-byte reads are atomic on any CPU.
 * If you later add reconnection logic, wrap reads with a spinlock.  */
extern volatile wifi_manager_state_t g_wifi_state;

/* Initialise NVS, bring up the TCP/IP stack, connect to the AP
 * configured via menuconfig (CONFIG_ESP_WIFI_SSID / _PASSWORD).
 * Blocks until connected or max retries reached.
 * Call this from app_main BEFORE starting the LVGL task.           */
esp_err_t wifi_manager_start(void);

/* Returns current RSSI in dBm, or 0 if not connected / unavailable */
int8_t wifi_manager_get_rssi(void);

#ifdef __cplusplus
}
#endif
