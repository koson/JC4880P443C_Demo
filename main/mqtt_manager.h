/*
 * mqtt_manager.h
 *
 * Thin MQTT client for the smart home dashboard.
 *
 * DESIGN
 * ──────
 * mqtt_manager_start() connects to the broker defined in Kconfig and
 * subscribes to every topic the dashboard needs.  It must be called
 * after wifi_manager_start() returns ESP_OK.
 *
 * Incoming messages are NOT processed here.  Instead, each message is
 * packed into an mqtt_message_t and posted to a FreeRTOS queue
 * (g_mqtt_queue).  An lv_timer created inside dashboard_ui_create()
 * drains that queue every 100 ms from within the LVGL task — the only
 * safe place to call any lv_* function.
 *
 * mqtt_manager_publish() may be called from any task (e.g. when the
 * user taps the boost button inside an LVGL event callback).  It uses
 * esp_mqtt_client_publish() which is thread-safe per IDF docs.
 *
 * TOPIC LIST (all QoS 0, subscribe-only unless noted)
 * ─────────────────────────────────────────────────────
 *   /OWM/tempc          outdoor temperature °C
 *   /OWM/weather        condition word  ("Clear")
 *   /OWM/detail         detail phrase   ("clear sky")
 *   /OWM/wicon          icon code       ("w01n")
 *   /OWM/temp_minc      daily min °C
 *   /OWM/temp_maxc      daily max °C
 *   /OWM/humidity       humidity %
 *   /OWM/windspeed      wind speed m/s
 *   /OWM/winddirection  wind bearing °
 *   /OWM/sunrise_l      sunrise string  ("06:03")
 *   /OWM/sunset_l       sunset string   ("18:16")
 *   /OWM/rain           rain mm
 *   /OWM/location       location name
 *   /HALL/watts         live power W
 *   /HALL/energy        today kWh (from midnight)
 *   /HALL/lastkwh       yesterday total kWh
 *   /HALL/powerfactor   power factor 0–1
 *   /HALL/voltage       mains voltage V
 *   /BOI/power          boiler on/off  (0|1)
 *   /BOI/mode           boiler mode    ("winter"|"summer")
 *   /BOI/home           boost button   (publish "1" to trigger)  ← PUBLISH
 *   /INTERNET/rtt       latency to Google ms
 */

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Message envelope posted to g_mqtt_queue ──────────────────────── */
#define MQTT_TOPIC_MAX_LEN   48
#define MQTT_PAYLOAD_MAX_LEN 64

typedef struct {
    char topic  [MQTT_TOPIC_MAX_LEN];
    char payload[MQTT_PAYLOAD_MAX_LEN];
} mqtt_message_t;

/* Queue handle — declared here, defined in mqtt_manager.c.
 * The LVGL timer in dashboard_ui.c reads from this queue.
 * Queue depth: 32 messages.  If the broker floods us faster than the
 * 100 ms drain rate the oldest unread messages are dropped silently — 
 * dashboard data is always "latest value wins" so this is fine.       */
extern QueueHandle_t g_mqtt_queue;

/* true once the broker has acknowledged our CONNECT packet            */
extern volatile bool g_mqtt_connected;

/* Connect to the broker (URI from Kconfig), subscribe to all topics.
 * Blocks briefly to confirm the connection before returning.
 * Returns ESP_OK on success, ESP_FAIL if the broker is unreachable.  */
esp_err_t mqtt_manager_start(void);

/* Publish a single message.  Thread-safe — may be called from any
 * task, including LVGL event callbacks.
 * topic   : null-terminated topic string
 * payload : null-terminated payload string
 * qos     : 0 or 1  (use 0 for the boost button)
 * retain  : false for momentary button presses                        */
esp_err_t mqtt_manager_publish(const char *topic,
                                const char *payload,
                                int qos, bool retain);

#ifdef __cplusplus
}
#endif
