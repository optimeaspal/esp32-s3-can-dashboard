#pragma once
#include "lvgl.h"
#include "app/config_types.h"
#include "app/can_signal.h"

/* Chart (Zeitreihen-Liniengraph) auf Basis von lv_chart. */
lv_obj_t *chart_create(lv_obj_t *parent, const widget_config_t *cfg, const can_signal_t *sig);
void      chart_update(lv_obj_t *obj, float value);
void      chart_stale(lv_obj_t *obj, bool stale);
