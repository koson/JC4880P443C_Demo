/*
 * wifi_manager.c
 *
 * WiFi station manager for the smart home dashboard.
 * Adapted from the standard IDF wifi/getting_started/station example.
 *
 * HOW IT FITS INTO THE PROJECT
 * ────────────────────────────
 * app_main() call order:
 *   1. display_init()          — hardware up, panel on
 *   2. backlight_set(90)       — screen lit
 *   3. lvgl_display_init()     — LVGL initialised
 *   4. dashboard_ui_create()   — widgets built (shows "WiFi --")
 *   5. wifi_manager_start()    — connects to AP  ← THIS FILE
 *   6. xTaskCreate(lvgl_task)  — rendering starts
 *
 * After step 5, g_wifi_state.connected flips to true and
 * g_wifi_state.rssi is populated.  An lv_timer (registered in
 * dashboard_ui.c) polls g_wifi_state every 5 s and calls
 * dashboard_ui_update_status() to refresh the status bar.
 *
 * IDF EVENT LOOP PATTERN (used here)
 * ───────────────────────────────────
 * esp_event_loop_create_default() creates a single system-wide event
 * loop task.  We register two handlers on it:
 *
 *   WIFI_EVENT / ESP_EVENT_ANY_ID
 *     • STA_START       → call esp_wifi_connect()
 *     • STA_DISCONNECTED→ retry or set WIFI_FAIL_BIT
 *
 *   IP_EVENT / IP_EVENT_STA_GOT_IP
 *     • got an IP       → set WIFI_CONNECTED_BIT, update g_wifi_state
 *
 * wifi_manager_start() then blocks on xEventGroupWaitBits() until
 * one of those bits is set.
 */

#include "wifi_manager.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"

static const char *TAG = "WIFI";

/* ── Kconfig values (set via idf.py menuconfig) ───────────────────── */
#define WIFI_SSID        CONFIG_ESP_WIFI_SSID
#define WIFI_PASS        CONFIG_ESP_WIFI_PASSWORD
#define WIFI_MAX_RETRY   CONFIG_ESP_MAXIMUM_RETRY

/* ── WPA3 SAE mode — mirrors the example Kconfig exactly ─────────── */
#if CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HUNT_AND_PECK
  #define WIFI_SAE_MODE       WPA3_SAE_PWE_HUNT_AND_PECK
  #define WIFI_H2E_IDENTIFIER ""
#elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HASH_TO_ELEMENT
  #define WIFI_SAE_MODE       WPA3_SAE_PWE_HASH_TO_ELEMENT
  #define WIFI_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_BOTH
  #define WIFI_SAE_MODE       WPA3_SAE_PWE_BOTH
  #define WIFI_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif

/* ── Auth mode threshold — mirrors the example Kconfig ───────────── */
#if CONFIG_ESP_WIFI_AUTH_OPEN
  #define WIFI_SCAN_AUTH_MODE WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
  #define WIFI_SCAN_AUTH_MODE WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
  #define WIFI_SCAN_AUTH_MODE WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
  #define WIFI_SCAN_AUTH_MODE WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
  #define WIFI_SCAN_AUTH_MODE WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
  #define WIFI_SCAN_AUTH_MODE WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
  #define WIFI_SCAN_AUTH_MODE WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
  #define WIFI_SCAN_AUTH_MODE WIFI_AUTH_WAPI_PSK
#endif

/* ── Event group bits ─────────────────────────────────────────────── */
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

/* ── Module-private state ─────────────────────────────────────────── */
static EventGroupHandle_t s_wifi_event_group;
static int                s_retry_num = 0;

/* ── Public state (read by the LVGL status-bar timer) ─────────────── */
volatile wifi_manager_state_t g_wifi_state = {
    .connected = false,
    .rssi      = 0,
};

/* ════════════════════════════════════════════════════════════════════
 * EVENT HANDLER
 * Called by the IDF default event loop task — not our task.
 * Keep it fast: no blocking, no LVGL calls.
 * ════════════════════════════════════════════════════════════════════ */
static void wifi_event_handler(void *arg,
                                esp_event_base_t event_base,
                                int32_t          event_id,
                                void            *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        /* WiFi driver is up — kick off the first connection attempt */
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {

        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retrying connection (%d/%d)…",
                     s_retry_num, WIFI_MAX_RETRY);
        } else {
            /* Exhausted retries — signal failure to wifi_manager_start() */
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGW(TAG, "Connection failed after %d retries", WIFI_MAX_RETRY);
        }

        /* Keep public state accurate */
        g_wifi_state.connected = false;
        g_wifi_state.rssi      = 0;

    } else if (event_base == IP_EVENT &&
               event_id == IP_EVENT_STA_GOT_IP) {

        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        s_retry_num            = 0;
        g_wifi_state.connected = true;

        /* Read initial RSSI now that we're associated */
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            g_wifi_state.rssi = ap_info.rssi;
            ESP_LOGI(TAG, "RSSI: %d dBm", g_wifi_state.rssi);
        }

        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* ════════════════════════════════════════════════════════════════════
 * PUBLIC — wifi_manager_start
 * ════════════════════════════════════════════════════════════════════ */
esp_err_t wifi_manager_start(void)
{
    /* ── NVS init ────────────────────────────────────────────────────
     * WiFi driver stores calibration data in NVS flash.
     * Erase and reinitialise if the partition is full or version changed. */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition problem — erasing and reinitialising");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* ── TCP/IP stack and event loop ─────────────────────────────────
     * esp_netif_init()            — initialises the lwIP TCP/IP stack.
     * esp_event_loop_create_default() — creates the system event loop
     *   task that calls our wifi_event_handler when events fire.
     * esp_netif_create_default_wifi_sta() — creates a default station
     *   network interface bound to the event loop.                    */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    /* ── WiFi driver init ────────────────────────────────────────────
     * WIFI_INIT_CONFIG_DEFAULT() fills in all driver defaults.        */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* ── Register event handlers ─────────────────────────────────────
     * We use two separate registrations:
     *   1. All WIFI_EVENTs  (start, disconnect, etc.)
     *   2. Specifically IP_EVENT_STA_GOT_IP
     * Both route to the same handler function for simplicity.         */
    s_wifi_event_group = xEventGroupCreate();

    esp_event_handler_instance_t h_wifi;
    esp_event_handler_instance_t h_ip;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,
        &wifi_event_handler, NULL, &h_wifi));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP,
        &wifi_event_handler, NULL, &h_ip));

    /* ── Configure station mode ──────────────────────────────────────
     * Credentials come from Kconfig — never hardcoded.                */
    wifi_config_t wifi_config = {
        .sta = {
            .ssid              = WIFI_SSID,
            .password          = WIFI_PASS,
            .threshold.authmode = WIFI_SCAN_AUTH_MODE,
            .sae_pwe_h2e       = WIFI_SAE_MODE,
            .sae_h2e_identifier = WIFI_H2E_IDENTIFIER,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to \"%s\"…", WIFI_SSID);

    /* ── Block until connected or failed ─────────────────────────────
     * xEventGroupWaitBits() suspends this task until the event handler
     * sets either WIFI_CONNECTED_BIT or WIFI_FAIL_BIT.
     * pdFALSE, pdFALSE = don't clear bits, don't require all bits.   */
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,   /* don't clear on exit */
        pdFALSE,   /* wait for either bit, not both */
        portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to \"%s\"  RSSI: %d dBm",
                 WIFI_SSID, g_wifi_state.rssi);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Failed to connect to \"%s\"", WIFI_SSID);
    return ESP_FAIL;
}

/* ════════════════════════════════════════════════════════════════════
 * PUBLIC — wifi_manager_get_rssi
 * Reads a fresh RSSI sample. Safe to call from any task after start.
 * ════════════════════════════════════════════════════════════════════ */
int8_t wifi_manager_get_rssi(void)
{
    if (!g_wifi_state.connected) return 0;

    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        g_wifi_state.rssi = ap.rssi;
    }
    return g_wifi_state.rssi;
}
