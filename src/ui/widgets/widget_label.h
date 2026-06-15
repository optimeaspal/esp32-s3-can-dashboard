#pragma once
#include "lvgl.h"
#include "app/config_types.h"
#include "app/can_signal.h"

/* Label (numerische Textwert-Anzeige) auf Basis von lv_label. */
lv_obj_t *label_create(lv_obj_t *parent, const widget_config_t *cfg, const can_signal_t *sig);
void      label_update(lv_obj_t *obj, float value);
void      label_stale(lv_obj_t *obj, bool stale);
