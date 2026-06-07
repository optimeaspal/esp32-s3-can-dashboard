#pragma once

#include "app/can_dispatcher.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Erstellt den statischen Dashboard-Screen mit allen 4 Widget-Typen:
 *   - lv_meter  → Gauge  (analoger Zeiger)
 *   - lv_chart  → Chart  (Zeitverlauf)
 *   - lv_bar    → Bar    (Füllstand)
 *   - lv_led    → LED    (Statusanzeige)
 *
 * Muss innerhalb des LVGL-Mutex aufgerufen werden (lvgl_port_lock/unlock).
 *
 * @param event_queue  Queue mit can_value_event_t aus dem Dispatcher
 * @param signal_count Anzahl der Signale (für Stale-Überprüfung)
 */
void dashboard_create(QueueHandle_t event_queue, size_t signal_count);

/*
 * Muss periodisch aus dem LVGL-Task aufgerufen werden (innerhalb des Mutex).
 * Verarbeitet wartende can_value_event_t aus der Queue und aktualisiert Widgets.
 * Setzt ausgefüllte Signale auch auf "stale" (ausgegraut) wenn timeout_ms abgelaufen.
 */
void dashboard_tick(void);

#ifdef __cplusplus
}
#endif
