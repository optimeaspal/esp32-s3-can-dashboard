#pragma once
#include "lvgl.h"
#include "app/config_types.h"
#include "app/can_signal.h"

/* LED (binäre Statusanzeige) auf Basis von lv_led. */
lv_obj_t *led_create(lv_obj_t *parent, const widget_config_t *cfg, const can_signal_t *sig);
void      led_update(lv_obj_t *obj, float value);
void      led_stale(lv_obj_t *obj, bool stale);
