#include "can_dispatcher.h"
#include "can_monitor.h"
#include "hal/waveshare_twai_port.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// DEBUG: Zeile auskommentieren um Sekunden-Statistik zu deaktivieren
#define CAN_DEBUG_STATS 1

static const char *TAG = "can_disp";

static can_monitor_t   s_monitor;
static portMUX_TYPE    s_monitor_mux = portMUX_INITIALIZER_UNLOCKED;

typedef struct
{
    const can_signal_t *signals;
    size_t              count;
    QueueHandle_t       queue;
} dispatcher_ctx_t;

static void dispatcher_task(void *arg)
{
    dispatcher_ctx_t *ctx = (dispatcher_ctx_t *)arg;
    const uint32_t poll_ms = 10; // Alert-Wartezeit

    ESP_LOGI(TAG, "Dispatcher-Task gestartet (%zu Signale)", ctx->count);

#ifdef CAN_DEBUG_STATS
    #define DBG_MAX_IDS 32
    static uint32_t dbg_ids[DBG_MAX_IDS];
    static uint32_t dbg_cnt[DBG_MAX_IDS];
    static int      dbg_id_count = 0;
    int64_t         dbg_next_us  = 0;
#endif

    for (;;) {
        {
            int64_t now = esp_timer_get_time();
            portENTER_CRITICAL(&s_monitor_mux);
            can_monitor_update_fps(&s_monitor, now);
            portEXIT_CRITICAL(&s_monitor_mux);
        }

        uint32_t alerts = 0;
        twai_read_alerts(&alerts, pdMS_TO_TICKS(poll_ms));

        if (alerts & TWAI_ALERT_ERR_PASS)
            ESP_LOGW(TAG, "TWAI: Error Passive");

        if (alerts & TWAI_ALERT_BUS_ERROR) {
            twai_status_info_t st;
            twai_get_status_info(&st);
            ESP_LOGW(TAG, "TWAI: Bus error (count=%" PRIu32 ")", st.bus_error_count);
        }

        if (alerts & TWAI_ALERT_RX_QUEUE_FULL) {
            twai_status_info_t st;
            twai_get_status_info(&st);
            ESP_LOGW(TAG, "TWAI: RX queue full (missed=%" PRIu32 ")", st.rx_missed_count);
        }

#ifdef CAN_DEBUG_STATS
        {
            int64_t now = esp_timer_get_time();
            if (now >= dbg_next_us) {
                dbg_next_us = now + 1000000;
                if (dbg_id_count == 0) {
                    ESP_LOGI(TAG, "Stats: keine Frames empfangen");
                } else {
                    for (int i = 0; i < dbg_id_count; i++) {
                        ESP_LOGI(TAG, "Stats: 0x%03" PRIX32 " -> %" PRIu32 " Frames/s",
                                 dbg_ids[i], dbg_cnt[i]);
                        dbg_cnt[i] = 0;
                    }
                }
            }
        }
#endif

        if (!(alerts & (TWAI_ALERT_RX_DATA | TWAI_ALERT_RX_QUEUE_FULL)))
            continue;

        // Alle wartenden Frames verarbeiten
        twai_message_t msg;
        while (twai_receive(&msg, 0) == ESP_OK) {
            if (msg.rtr)
                continue;

            {
                int64_t now = esp_timer_get_time();
                portENTER_CRITICAL(&s_monitor_mux);
                can_monitor_record(&s_monitor, msg.identifier, (bool)msg.extd,
                                   msg.data, msg.data_length_code, now);
                portEXIT_CRITICAL(&s_monitor_mux);
            }

#ifdef CAN_DEBUG_STATS
            {
                int slot = -1;
                for (int i = 0; i < dbg_id_count; i++) {
                    if (dbg_ids[i] == msg.identifier) { slot = i; break; }
                }
                if (slot == -1 && dbg_id_count < DBG_MAX_IDS) {
                    slot = dbg_id_count++;
                    dbg_ids[slot] = msg.identifier;
                    dbg_cnt[slot] = 0;
                }
                if (slot != -1) dbg_cnt[slot]++;
            }
#endif

            bool extended = (bool)msg.extd;
            int64_t ts = esp_timer_get_time();

            for (size_t i = 0; i < ctx->count; i++) {
                if (!can_signal_matches(&ctx->signals[i], msg.identifier, extended))
                    continue;

                float value = can_signal_decode(&ctx->signals[i], msg.data, msg.data_length_code);
                if (isnan(value))
                    continue;

                can_value_event_t evt = {
                    .signal_idx   = (uint8_t)i,
                    .value        = value,
                    .timestamp_us = ts,
                };
                xQueueSend(ctx->queue, &evt, 0);
            }
        }
    }
}

esp_err_t can_dispatcher_start(
    const can_signal_t *signals,
    size_t              signal_count,
    QueueHandle_t       event_queue)
{
    ESP_ERROR_CHECK(waveshare_twai_init(CONFIG_CAN_BITRATE_KBPS));

    static dispatcher_ctx_t ctx;
    ctx.signals = signals;
    ctx.count   = signal_count;
    ctx.queue   = event_queue;

    BaseType_t ret = xTaskCreatePinnedToCore(
        dispatcher_task, "can_disp",
        4096, &ctx,
        5, NULL, 0); // Core 0, Priorität 5

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Task-Erstellung fehlgeschlagen");
        return ESP_FAIL;
    }
    return ESP_OK;
}

size_t can_dispatcher_get_monitor(can_monitor_entry_t *out, size_t max,
                                  uint32_t *out_total_fps)
{
    portENTER_CRITICAL(&s_monitor_mux);
    size_t n = can_monitor_snapshot(&s_monitor, out, max);
    if (out_total_fps) *out_total_fps = can_monitor_total_fps(&s_monitor);
    portEXIT_CRITICAL(&s_monitor_mux);
    return n;
}
