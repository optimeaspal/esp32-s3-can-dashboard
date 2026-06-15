#pragma once
#include "lvgl.h"
#include "app/config_types.h"
#include "app/can_signal.h"

/* Arc (Halbkreis-Gauge) auf Basis von lv_arc. */
lv_obj_t *arc_create(lv_obj_t *parent, const widget_config_t *cfg, const can_signal_t *sig);
void      arc_update(lv_obj_t *obj, float value);
void      arc_stale(lv_obj_t *obj, bool stale);
