#pragma once

#include "app/config_types.h"
#include "app/can_dispatcher.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Baut das Dashboard aus der geladenen Konfiguration auf:
 *   - lv_tileview mit einer Kachel pro Seite (horizontale Swipe-Navigation)
 *   - Widgets je Seite über die Widget-Registry (unbekannte Typen übersprungen)
 *   - Navigationspunkte unten (nur bei >= 2 Seiten)
 *
 * Muss innerhalb des LVGL-Mutex aufgerufen werden (lvgl_port_lock/unlock).
 * cfg MUSS für die Laufzeit gültig bleiben (z. B. statisch in main.c).
 */
void dashboard_create(const dashboard_config_t *cfg, QueueHandle_t event_queue);

/*
 * Periodisch aus dem LVGL-Task aufrufen (innerhalb des Mutex).
 * Verarbeitet wartende can_value_event_t und aktualisiert die zugehörigen
 * Widgets; graut Widgets aus, deren Signal sein stale_timeout überschritten hat.
 */
void dashboard_tick(void);

#ifdef __cplusplus
}
#endif
