#pragma once

#include "lvgl.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NAV_BAR_HEIGHT 32

/*
 * Erstellt eine Navigationsleiste mit page_count Punkten am unteren Rand von
 * parent. Gibt NULL zurück (und erstellt nichts) wenn page_count < 2 –
 * gemäß FR-008 erscheint die Leiste nur bei >= 2 Seiten.
 */
lv_obj_t *nav_indicator_create(lv_obj_t *parent, uint8_t page_count);

/* Markiert den Punkt active_idx als aktiv (weiß), die übrigen grau. */
void nav_indicator_set_active(lv_obj_t *nav, uint8_t active_idx);

#ifdef __cplusplus
}
#endif
