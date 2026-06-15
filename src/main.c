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
    // Signal 0 → Gauge: Drehzahl 0..6000 RPM (simuliert)
    {
        .can_id        = 0x102,
        .extended_id   = false,
        .byte_offset   = 0,
        .byte_length   = 2,
        .little_endian = true,
        .is_signed     = false,
        .is_simulated  = true,
        .scale         = 1.0f,
        .offset        = 0.0f,
        .min_value     = 0.0f,
        .max_value     = 6000.0f,
        .timeout_ms    = CONFIG_CAN_SIGNAL_STALE_MS,
        .name          = "RPM",
        .unit          = "1/min",
    },
    // Signal 1 → Chart: Temperatur – IEEE-754 float, 32 Bit, Little-Endian (CAN ID 0x2A)
    {
        .can_id        = 0x02A,
        .extended_id   = false,
        .byte_offset   = 0,
        .byte_length   = 4,
        .little_endian = true,
        .is_signed     = false,
        .is_float      = true,
        .is_simulated  = false,
        .scale         = 1.0f,
        .offset        = 0.0f,
        .min_value     = 0.0f,
        .max_value     = 100.0f,
        .timeout_ms    = CONFIG_CAN_SIGNAL_STALE_MS,
        .name          = "Temperatur",
        .unit          = "C",
    },
    // Signal 2 → Bar: Kraftstoffstand 0..100% (simuliert)
    {
        .can_id        = 0x103,
        .extended_id   = false,
        .byte_offset   = 0,
        .byte_length   = 1,
        .little_endian = true,
        .is_signed     = false,
        .is_simulated  = true,
        .scale         = 100.0f / 255.0f,
        .offset        = 0.0f,
        .min_value     = 0.0f,
        .max_value     = 100.0f,
        .timeout_ms    = CONFIG_CAN_SIGNAL_STALE_MS,
        .name          = "Kraftstoff",
        .unit          = "%",
    },
    // Signal 3 → LED: Rechteck 0/1 – 1=ON (CAN ID 0x2B)
    {
        .can_id        = 0x02B,
        .extended_id   = false,
        .byte_offset   = 0,
        .byte_length   = 1,
        .little_endian = true,
        .is_signed     = false,
        .is_simulated  = false,
        .scale         = 1.0f,
        .offset        = 0.0f,
        .min_value     = 0.0f,
        .max_value     = 1.0f,
        .timeout_ms    = CONFIG_CAN_SIGNAL_STALE_MS,
        .name          = "Status",
        .unit          = "",
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

    // Display + LVGL initialisieren, Backlight + CAN-Mux via CH422G setzen
    // (alle CH422G-Schreibzugriffe VOR Start des LVGL-Timers, kein I2C-Race mit GT911)
    ESP_ERROR_CHECK(waveshare_rgb_lcd_init());
    ESP_ERROR_CHECK(waveshare_rgb_lcd_bl_on());
    ESP_ERROR_CHECK(waveshare_rgb_lcd_can_mux_enable());

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

    // Simulator für is_simulated-Signale + Dispatcher für echte CAN-Signale
    ESP_ERROR_CHECK(can_simulator_start(can_signals, can_signal_count, event_queue));
    ESP_ERROR_CHECK(can_dispatcher_start(can_signals, can_signal_count, event_queue));

    ESP_LOGI(TAG, "Initialisierung abgeschlossen – Dashboard läuft");
    // Haupttask beendet sich; LVGL-Task und CAN-Task laufen weiter
}
