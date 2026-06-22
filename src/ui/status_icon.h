#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Erzeugt das Zahnrad-Icon oben rechts auf parent_scr (dem Dashboard-Screen).
 * Klick öffnet den Settings-Screen (Rückkehr zu parent_scr).
 * Muss innerhalb des LVGL-Mutex aufgerufen werden.
 */
void status_icon_create(lv_obj_t *parent_scr);

/*
 * Aktualisiert die Icon-Farbe anhand des aktuellen WLAN-Status.
 * Periodisch aus dem LVGL-Task aufrufen (innerhalb des Mutex).
 */
void status_icon_tick(void);

#ifdef __cplusplus
}
#endif
