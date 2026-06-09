#pragma once

#include "can_signal.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Startet den CAN-Simulator-Task.
 *
 * Ersetzt can_dispatcher_start() wenn CONFIG_CAN_SIMULATOR_ENABLE gesetzt ist.
 * Der Task sendet alle 100 ms synthetische can_value_event_t-Ereignisse
 * für alle Signale in der Tabelle — ohne TWAI-Hardware.
 *
 * Signalmuster:
 *   Index 0 (RPM)            – Sinuswelle 800..5500 RPM, Periode 10 s
 *   Index 1 (Motortemperatur) – Sägezahn   20..110 °C, Periode 30 s
 *   Index 2 (Kraftstoff)     – Fallend   100..0 %,    Periode 60 s
 *   Index 3 (Warnung)        – Toggle 0/1,             Periode  5 s
 *   Index >3                 – konstant 0.0
 */
esp_err_t can_simulator_start(
    const can_signal_t *signals,
    size_t              signal_count,
    QueueHandle_t       event_queue);

#ifdef __cplusplus
}
#endif
