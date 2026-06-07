#pragma once

#include "can_signal.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Ereignis das der Dispatcher-Task für jedes dekodierte Signal an die UI sendet.
 */
typedef struct
{
    uint8_t  signal_idx;    // Index in der Signal-Tabelle (s. can_signals[] in main.c)
    float    value;         // Physikalischer Wert
    int64_t  timestamp_us;  // Zeitpunkt des Empfangs (esp_timer_get_time())
} can_value_event_t;

/*
 * Startet den CAN-Dispatcher-Task.
 *
 * @param signals      Zeiger auf die Signal-Tabelle
 * @param signal_count Anzahl der Signale
 * @param event_queue  FreeRTOS-Queue für can_value_event_t (vom Aufrufer erstellt)
 *
 * Alerts/Bitrate werden über Kconfig-Defaults gesetzt (CONFIG_CAN_BITRATE_KBPS).
 * Der Task läuft auf Core 0 mit Priorität 5.
 */
esp_err_t can_dispatcher_start(
    const can_signal_t *signals,
    size_t              signal_count,
    QueueHandle_t       event_queue);

#ifdef __cplusplus
}
#endif
