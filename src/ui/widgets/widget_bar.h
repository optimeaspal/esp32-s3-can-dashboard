#pragma once
#include "lvgl.h"
#include "app/config_types.h"
#include "app/can_signal.h"

/* Bar (Balkenanzeige) auf Basis von lv_bar. */
lv_obj_t *bar_create(lv_obj_t *parent, const widget_config_t *cfg, const can_signal_t *sig);
void      bar_update(lv_obj_t *obj, float value);
void      bar_stale(lv_obj_t *obj, bool stale);
