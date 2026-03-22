/*
 * mqtt_manager.c
 *
 * MQTT client for the smart home dashboard — ESP32-P4 / IDF 5.x
 *
 * CALL ORDER IN app_main()
 * ────────────────────────
 *   1. display_init()          hardware up
 *   2. lvgl_display_init()     LVGL ready
 *   3. dashboard_ui_create()   widgets built, lv_timer registered
 *   4. xTaskCreate(lvgl_task)  rendering running
 *   5. wifi_manager_start()    IP obtained           ← existing
 *   6. mqtt_manager_start()    broker connected      ← THIS FILE
 *
 * THREAD SAFETY
 * ─────────────
 * esp_mqtt_client_publish() is documented as thread-safe in IDF 5.x.
 * All other esp_mqtt_client_* calls happen only inside the MQTT event
 * handler, which runs in the MQTT task — no locking needed here.
 *
 * Incoming messages are forwarded to g_mqtt_queue (FreeRTOS queue).
 * The LVGL lv_timer in dashboard_ui.c drains the queue every 100 ms
 * and calls the appropriate dashboard_ui_update_*() functions.
 * No LVGL function is ever called from this file.
 */

#include "mqtt_manager.h"
#include "esp_log.h"
#include "esp_err.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdbool.h>

static const char *TAG = "MQTT";

/* ── Kconfig values ───────────────────────────────────────────────── */
#define BROKER_URI       CONFIG_MQTT_BROKER_URI
#define CLIENT_ID        CONFIG_MQTT_CLIENT_ID
#define KEEPALIVE_SEC    CONFIG_MQTT_KEEPALIVE_SEC

/* ── Public globals (declared in header) ─────────────────────────── */
QueueHandle_t    g_mqtt_queue    = NULL;
volatile bool    g_mqtt_connected = false;

/* ── Private ──────────────────────────────────────────────────────── */
static esp_mqtt_client_handle_t s_client = NULL;

#define MQTT_CONNECTED_BIT  BIT0
#define MQTT_QUEUE_DEPTH    32

static EventGroupHandle_t s_mqtt_event_group = NULL;

/* ── Topic list ───────────────────────────────────────────────────── */
static const char *SUBSCRIBE_TOPICS[] = {
    "/OWM/tempc",
    "/OWM/weather",
    "/OWM/detail",
    "/OWM/wicon",
    "/OWM/temp_minc",
    "/OWM/temp_maxc",
    "/OWM/humidity",
    "/OWM/windspeed",
    "/OWM/winddirection",
    "/OWM/sunrise_l",
    "/OWM/sunset_l",
    "/OWM/rain",
    "/OWM/location",
    "/HALL/watts",
    "/HALL/energy",
    "/HALL/lastkwh",
    "/HALL/powerfactor",
    "/HALL/voltage",
    "/BOI/power",
    "/BOI/mode",
    "/INTERNET/rtt",
    "/SYS/time",
    "/SYS/date",
    "/SYS/datel",
};

#define NUM_TOPICS  (sizeof(SUBSCRIBE_TOPICS) / sizeof(SUBSCRIBE_TOPICS[0]))

/* ── MQTT event handler ───────────────────────────────────────────── */
static void mqtt_event_handler(void *arg,
                                esp_event_base_t base,
                                int32_t event_id,
                                void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Connected to %s", BROKER_URI);
        g_mqtt_connected = true;
        xEventGroupSetBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);

        /* Subscribe to every topic at QoS 0 */
        for (size_t i = 0; i < NUM_TOPICS; i++) {
            int msg_id = esp_mqtt_client_subscribe(s_client,
                                                    SUBSCRIBE_TOPICS[i], 0);
            ESP_LOGD(TAG, "SUB %-30s  msg_id=%d",
                     SUBSCRIBE_TOPICS[i], msg_id);
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Disconnected — will retry automatically");
        g_mqtt_connected = false;
        break;

    case MQTT_EVENT_DATA: {
        /*
         * Copy topic + payload into an mqtt_message_t and post to the
         * queue.  We truncate silently if either field is too long —
         * dashboard payloads are always short ASCII strings.
         *
         * event->topic_len / event->data_len are NOT null-terminated;
         * we must copy with a length limit and add '\0' ourselves.
         */
        mqtt_message_t msg;
        memset(&msg, 0, sizeof(msg));

        int tlen = event->topic_len;
        if (tlen >= (int)sizeof(msg.topic))
            tlen = (int)sizeof(msg.topic) - 1;
        memcpy(msg.topic, event->topic, tlen);
        msg.topic[tlen] = '\0';

        int dlen = event->data_len;
        if (dlen >= (int)sizeof(msg.payload))
            dlen = (int)sizeof(msg.payload) - 1;
        memcpy(msg.payload, event->data, dlen);
        msg.payload[dlen] = '\0';

        /* Trim leading/trailing whitespace from payload — Node-RED
         * occasionally adds a trailing space or newline.             */
        char *p = msg.payload;
        int   l = strlen(p);
        while (l > 0 && (p[l-1] == ' ' || p[l-1] == '\n' || p[l-1] == '\r'))
            p[--l] = '\0';

        /* Post to queue — don't block; drop if full */
        if (xQueueSendToBack(g_mqtt_queue, &msg, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Queue full — dropped %s", msg.topic);
        }
        break;
    }

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;

    default:
        break;
    }
}

/* ── Public API ───────────────────────────────────────────────────── */

esp_err_t mqtt_manager_start(void)
{
    /* Create the message queue */
    g_mqtt_queue = xQueueCreate(MQTT_QUEUE_DEPTH, sizeof(mqtt_message_t));
    if (!g_mqtt_queue) {
        ESP_LOGE(TAG, "Failed to create message queue");
        return ESP_ERR_NO_MEM;
    }

    s_mqtt_event_group = xEventGroupCreate();
    if (!s_mqtt_event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri        = BROKER_URI,
        .credentials.client_id     = CLIENT_ID,
        .session.keepalive          = KEEPALIVE_SEC,
        .session.disable_clean_session = false,
        /* Reconnect automatically — IDF default is true, set
         * explicitly so future readers know it's intentional.        */
        .network.reconnect_timeout_ms = 5000,
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));

    ESP_ERROR_CHECK(esp_mqtt_client_start(s_client));

    ESP_LOGI(TAG, "Connecting to %s …", BROKER_URI);

    /* Wait up to 10 s for the broker to respond */
    EventBits_t bits = xEventGroupWaitBits(
        s_mqtt_event_group,
        MQTT_CONNECTED_BIT,
        pdFALSE,   /* don't clear on exit */
        pdTRUE,
        pdMS_TO_TICKS(10000));

    if (bits & MQTT_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Broker connected — %zu topics subscribed", NUM_TOPICS);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Broker not reached within 10 s — "
                  "client will keep retrying in background");
    return ESP_FAIL;
}

esp_err_t mqtt_manager_publish(const char *topic,
                                const char *payload,
                                int qos, bool retain)
{
    if (!s_client) {
        ESP_LOGE(TAG, "publish called before mqtt_manager_start()");
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_publish(s_client, topic, payload,
                                          0,   /* len 0 = use strlen */
                                          qos,
                                          retain ? 1 : 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "publish failed: topic=%s", topic);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "PUB %s  payload=%s  msg_id=%d", topic, payload, msg_id);
    return ESP_OK;
}
