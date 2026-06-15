#include "can_simulator.h"
#include "can_dispatcher.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "can_sim";

typedef struct {
    const can_signal_t *signals;
    size_t              signal_count;
    QueueHandle_t       event_queue;
} sim_ctx_t;

/*
 * Liefert einen synthetischen physikalischen Wert für ein beliebiges Signal.
 * Bereichsbasiert (min..max aus der JSON-Konfiguration), damit jede dynamisch
 * geladene Signaldefinition animiert wird. Die Periode variiert pro Index für
 * optisch unterscheidbare Verläufe.
 *
 * t = verstrichene Zeit in Sekunden seit Board-Start.
 */
static float sim_value(const can_signal_t *s, uint8_t idx, float t)
{
    float span   = s->max_value - s->min_value;
    float period = 8.0f + (float)(idx % 4) * 4.0f;   /* 8/12/16/20 s */
    float phase  = (float)idx * 0.7f;

    /* Binäre Signale (Bereich <= 1): als Rechteck-Toggle darstellen. */
    if (span <= 1.0f)
        return ((int)(t / 5.0f + idx) % 2) ? s->max_value : s->min_value;

    float frac = 0.5f * (1.0f + sinf(2.0f * (float)M_PI * t / period + phase));
    return s->min_value + span * frac;
}

static void sim_task(void *arg)
{
    sim_ctx_t   *ctx       = (sim_ctx_t *)arg;
    TickType_t   last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(100); // 10 Hz

    ESP_LOGI(TAG, "gestartet – %u Signal(e), 10 Hz", (unsigned)ctx->signal_count);

    while (1) {
        float t = (float)(esp_timer_get_time() / 1000) / 1000.0f; // ms → s

        for (uint8_t i = 0; i < (uint8_t)ctx->signal_count; i++) {
            if (!ctx->signals[i].is_simulated)
                continue;

            float v = sim_value(&ctx->signals[i], i, t);

            // Auf Signal-Bereich begrenzen
            if (v < ctx->signals[i].min_value) v = ctx->signals[i].min_value;
            if (v > ctx->signals[i].max_value) v = ctx->signals[i].max_value;

            can_value_event_t ev = {
                .signal_idx   = i,
                .value        = v,
                .timestamp_us = esp_timer_get_time(),
            };
            xQueueSend(ctx->event_queue, &ev, 0); // non-blocking: vollen Queue überspringen
        }

        vTaskDelayUntil(&last_wake, period);
    }
}

esp_err_t can_simulator_start(
    const can_signal_t *signals,
    size_t              signal_count,
    QueueHandle_t       event_queue)
{
    // static: Kontext überlebt den Stack-Frame
    static sim_ctx_t ctx;
    ctx.signals      = signals;
    ctx.signal_count = signal_count;
    ctx.event_queue  = event_queue;

    BaseType_t ok = xTaskCreatePinnedToCore(
        sim_task,
        "can_sim",
        4096,
        &ctx,
        4,    // Prio 4 – niedriger als LVGL (2), höher als Idle
        NULL,
        0);   // Core 0, identisch zum echten CAN-Dispatcher

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Task-Erstellung fehlgeschlagen");
        return ESP_FAIL;
    }
    return ESP_OK;
}
