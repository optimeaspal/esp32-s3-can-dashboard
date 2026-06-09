#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "hal/waveshare_rgb_lcd_port.h"
#include "hal/lvgl_port.h"
#include "app/can_signal.h"
#include "app/can_dispatcher.h"
#include "app/can_simulator.h"
#include "ui/dashboard.h"

static const char *TAG = "main";

// ── CAN-Signaltabelle (POC: statisch, 4 Signale für je ein Widget-Typ) ────────
//
// Anpassung für eigene CAN-Frames:
//   - can_id, byte_offset, byte_length, scale, offset, min_value, max_value ändern
//   - timeout_ms = 0 deaktiviert Stale-Erkennung für das Signal
//
const can_signal_t can_signals[] = {
    // Signal 0 → Gauge: Drehzahl 0..6000 RPM
    // CAN 0x101, Bytes 0-1, uint16 LE, scale=1.0 → direkt in RPM
    {
        .can_id       = 0x101,
        .extended_id  = false,
        .byte_offset  = 0,
        .byte_length  = 2,
        .little_endian = true,
        .is_signed    = false,
        .scale        = 1.0f,
        .offset       = 0.0f,
        .min_value    = 0.0f,
        .max_value    = 6000.0f,
        .timeout_ms   = CONFIG_CAN_SIGNAL_STALE_MS,
        .name         = "RPM",
        .unit         = "1/min",
    },
    // Signal 1 → Chart: Motortemperatur -40..150°C
    // CAN 0x102, Byte 0, int8, scale=1.0, offset=-40
    {
        .can_id       = 0x102,
        .extended_id  = false,
        .byte_offset  = 0,
        .byte_length  = 1,
        .little_endian = true,
        .is_signed    = true,
        .scale        = 1.0f,
        .offset       = -40.0f,
        .min_value    = -40.0f,
        .max_value    = 150.0f,
        .timeout_ms   = CONFIG_CAN_SIGNAL_STALE_MS,
        .name         = "Motortemperatur",
        .unit         = "C",
    },
    // Signal 2 → Bar: Kraftstoffstand 0..100%
    // CAN 0x103, Byte 0, uint8, scale=100/255
    {
        .can_id       = 0x103,
        .extended_id  = false,
        .byte_offset  = 0,
        .byte_length  = 1,
        .little_endian = true,
        .is_signed    = false,
        .scale        = 100.0f / 255.0f,
        .offset       = 0.0f,
        .min_value    = 0.0f,
        .max_value    = 100.0f,
        .timeout_ms   = CONFIG_CAN_SIGNAL_STALE_MS,
        .name         = "Kraftstoff",
        .unit         = "%",
    },
    // Signal 3 → LED: Warnzustand (0=OK, >0=Warnung)
    // CAN 0x104, Byte 0, uint8, scale=1.0
    {
        .can_id       = 0x104,
        .extended_id  = false,
        .byte_offset  = 0,
        .byte_length  = 1,
        .little_endian = true,
        .is_signed    = false,
        .scale        = 1.0f,
        .offset       = 0.0f,
        .min_value    = 0.0f,
        .max_value    = 255.0f,
        .timeout_ms   = CONFIG_CAN_SIGNAL_STALE_MS,
        .name         = "Warnung",
        .unit         = "",
    },
};
const size_t can_signal_count = sizeof(can_signals) / sizeof(can_signals[0]);

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 CAN Dashboard – Start");

    // Shared Queue: CAN-Dispatcher → Dashboard
    QueueHandle_t event_queue = xQueueCreate(
        CONFIG_CAN_RX_QUEUE_LEN, sizeof(can_value_event_t));
    assert(event_queue);

    // Display + LVGL initialisieren (Backlight nach Init einschalten)
    ESP_ERROR_CHECK(waveshare_rgb_lcd_init());
    ESP_ERROR_CHECK(waveshare_rgb_lcd_bl_on());

    // Dashboard-Screen erstellen (innerhalb LVGL-Mutex)
    lvgl_port_lock(-1);
    dashboard_create(event_queue, can_signal_count);
    lvgl_port_unlock();

    // LVGL-Timer registrieren: dashboard_tick() aufrufen
    lvgl_port_lock(-1);
    lv_timer_create(
        (lv_timer_cb_t)dashboard_tick, // LVGL ruft dashboard_tick() periodisch auf
        50,                            // alle 50 ms
        NULL);
    lvgl_port_unlock();

    // CAN-Quelle starten: Simulator (Testbetrieb) oder echter TWAI-Dispatcher
#if CONFIG_CAN_SIMULATOR_ENABLE
    ESP_LOGW(TAG, "CAN-Simulator aktiv – keine TWAI-Hardware erforderlich");
    ESP_ERROR_CHECK(can_simulator_start(
        can_signals, can_signal_count, event_queue));
#else
    ESP_ERROR_CHECK(can_dispatcher_start(
        can_signals, can_signal_count, event_queue));
#endif

    ESP_LOGI(TAG, "Initialisierung abgeschlossen – Dashboard läuft");
    // Haupttask beendet sich; LVGL-Task und CAN-Task laufen weiter
}
