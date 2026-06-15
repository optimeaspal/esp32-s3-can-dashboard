#pragma once

/*
 * Widget-Registry: Funktionstabelle type_name → create/update/stale.
 *
 * Erfüllt FR-014: neue Widget-Typen (auch zukünftige CAN-TX-Input-Widgets)
 * werden durch einen Tabelleneintrag ergänzt, ohne die Rendering-Schicht
 * (dashboard.c) umzubauen.
 */

#include "lvgl.h"
#include "app/config_types.h"
#include "app/can_signal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * create: erstellt das Widget auf parent gemäß cfg (Position/Größe/Style) und
 *         sig (Wertebereich/Einheit). Gibt das Haupt-LVGL-Objekt zurück
 *         (Handle für update/stale). Widget-spezifischer Kontext wird via
 *         lv_obj_set_user_data am Objekt hinterlegt.
 * update: setzt den aktuellen physikalischen Wert (intern geclampt).
 * set_stale: schaltet den Stale-Zustand (ausgegraut) ein/aus.
 */
typedef lv_obj_t *(*widget_create_fn_t)(lv_obj_t *parent,
                                        const widget_config_t *cfg,
                                        const can_signal_t *sig);
typedef void (*widget_update_fn_t)(lv_obj_t *obj, float value);
typedef void (*widget_stale_fn_t)(lv_obj_t *obj, bool stale);

typedef struct
{
    const char        *type_name;   /* "gauge", "chart", ... */
    widget_create_fn_t create;
    widget_update_fn_t update;
    widget_stale_fn_t  set_stale;
} widget_descriptor_t;

/*
 * Sucht den Deskriptor zu einem Typ-Namen (lineare Suche).
 * @return Zeiger auf den Deskriptor oder NULL bei unbekanntem Typ (FR-002).
 */
const widget_descriptor_t *widget_registry_find(const char *type_name);

#ifdef __cplusplus
}
#endif
