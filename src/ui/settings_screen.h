#pragma once

#include "lvgl.h"
#include "app/config_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Übergibt der Settings-Seite einen Zeiger auf die geladene Konfiguration
 * (für die Geräte-Info-Karte). Einmalig beim Start aufrufen.
 */
void settings_screen_set_config(const dashboard_config_t *cfg);

/*
 * Öffnet den Settings-Screen (erzeugt ihn beim ersten Aufruf lazy) und lädt
 * ihn animiert. return_scr ist das Ziel des „Zurück"-Buttons.
 * Innerhalb des LVGL-Mutex aufrufen.
 */
void settings_screen_open(lv_obj_t *return_scr);

/*
 * Aktualisiert die Live-Werte (WLAN, Gerät, SD, CAN-Tabelle), solange der
 * Screen aktiv ist; no-op sonst. Periodisch aus dem LVGL-Task aufrufen.
 */
void settings_screen_tick(void);

#ifdef __cplusplus
}
#endif
