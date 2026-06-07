#include "can_dispatcher.h"
#include "hal/waveshare_twai_port.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "can_disp";

typedef struct
{
    const can_signal_t *signals;
    size_t              count;
    QueueHandle_t       queue;
} dispatcher_ctx_t;

static void dispatcher_task(void *arg)
{
    dispatcher_ctx_t *ctx = (dispatcher_ctx_t *)arg;
    const uint32_t poll_ms = 100; // Alert-Wartezeit

    ESP_LOGI(TAG, "Dispatcher-Task gestartet (%zu Signale)", ctx->count);

    for (;;) {
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

        if (!(alerts & TWAI_ALERT_RX_DATA))
            continue;

        // Alle wartenden Frames verarbeiten
        twai_message_t msg;
        while (twai_receive(&msg, 0) == ESP_OK) {
            if (msg.rtr)
                continue; // RTR-Frames ignorieren

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
                // Nicht-blockierend senden; bei voller Queue wird der Frame verworfen
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
