#pragma once
#include "lvgl.h"
#include "app/config_types.h"
#include "app/can_signal.h"

/* Gauge (Rundinstrument mit Nadel) auf Basis von lv_meter. */
lv_obj_t *gauge_create(lv_obj_t *parent, const widget_config_t *cfg, const can_signal_t *sig);
void      gauge_update(lv_obj_t *obj, float value);
void      gauge_stale(lv_obj_t *obj, bool stale);
