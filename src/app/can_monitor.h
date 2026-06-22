#pragma once

/*
 * Reine, hardware-unabhängige Pro-ID-Statistik für empfangene CAN-Frames.
 * Keine ESP-IDF-Abhängigkeiten → vollständig nativ testbar (pio test -e native).
 *
 * Nicht thread-safe: Der Aufrufer (can_dispatcher) serialisiert Schreib- und
 * Lesezugriffe über einen Spinlock.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_MONITOR_MAX_IDS 32

/* Statistik zu genau einer CAN-ID. */
typedef struct
{
    uint32_t id;            /* CAN-Identifier                                  */
    bool     extended;      /* true = 29-Bit Extended Frame                    */
    uint8_t  data[8];       /* zuletzt empfangene Datenbytes                   */
    uint8_t  dlc;           /* Data Length Code des letzten Frames             */
    uint32_t count;         /* kumulative Frame-Anzahl seit reset              */
    uint32_t window_count;  /* Frames seit letztem fps-Fenster (intern)        */
    uint32_t fps;           /* Frames im letzten abgeschlossenen 1-s-Fenster   */
    int64_t  last_us;       /* Zeitstempel des letzten Frames (µs)             */
} can_monitor_entry_t;

/* Monitor-Zustand. Vom Aufrufer instanziiert (z. B. statisch im Dispatcher). */
typedef struct
{
    can_monitor_entry_t entries[CAN_MONITOR_MAX_IDS];
    size_t              count;                 /* belegte Einträge             */
    int64_t             fps_window_start_us;   /* Beginn des aktuellen Fensters */
} can_monitor_t;

/* Setzt den Monitor auf leer zurück. */
void can_monitor_reset(can_monitor_t *m);

/*
 * Registriert einen empfangenen Frame. Legt bei neuer ID einen Eintrag an
 * (sofern < CAN_MONITOR_MAX_IDS), sonst wird der Frame einer vollen Tabelle
 * verworfen. dlc wird auf 8 begrenzt.
 */
void can_monitor_record(can_monitor_t *m, uint32_t id, bool extended,
                        const uint8_t *data, uint8_t dlc, int64_t timestamp_us);

/*
 * Aktualisiert die fps-Werte, wenn seit fps_window_start_us >= 1 s vergangen
 * ist: setzt je Eintrag fps = window_count und nullt window_count.
 * Vor Ablauf der Sekunde passiert nichts.
 */
void can_monitor_update_fps(can_monitor_t *m, int64_t now_us);

/*
 * Kopiert bis zu max Einträge sortiert nach (extended, id) nach out.
 * @return Anzahl kopierter Einträge (= min(m->count, max)).
 */
size_t can_monitor_snapshot(const can_monitor_t *m,
                            can_monitor_entry_t *out, size_t max);

/* Summe aller Eintrags-fps (Gesamt-Framerate). */
uint32_t can_monitor_total_fps(const can_monitor_t *m);

#ifdef __cplusplus
}
#endif
