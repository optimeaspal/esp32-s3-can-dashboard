#pragma once

/*
 * Gemeinsame Helfer für alle Widget-Implementierungen.
 * Jedes Widget legt einen eigenen Kontext (xxx_ctx_t) an, dessen erstes Feld
 * ein widget_base_t ist, und hängt ihn via lv_obj_set_user_data() ans
 * zurückgegebene Container-Objekt.
 */

#include "lvgl.h"
#include "app/config_types.h"
#include "app/can_signal.h"

#define WC_STALE_RGB    0x555577
#define WC_DEFAULT_WARN 0xFF4400
#define WC_TITLE_H      24   /* reservierte Höhe für den Titel oben */

/* Gemeinsame, an Stale/Warnung beteiligte Felder. */
typedef struct
{
    float      min;
    float      max;
    float      warn_threshold;
    bool       has_warning;
    lv_color_t normal_color;
    lv_color_t warning_color;
} widget_base_t;

static inline lv_color_t wc_color(uint32_t rgb, lv_color_t fallback)
{
    return rgb ? lv_color_hex(rgb) : fallback;
}

static inline float wc_clamp(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline void wc_base_init(widget_base_t *b, const widget_config_t *cfg,
                                const can_signal_t *sig, lv_color_t def_normal)
{
    b->min            = sig ? sig->min_value : 0.0f;
    b->max            = sig ? sig->max_value : 100.0f;
    b->warn_threshold = cfg->style.warning_threshold;
    b->has_warning    = cfg->style.has_warning;
    b->normal_color   = wc_color(cfg->style.normal_color, def_normal);
    b->warning_color  = wc_color(cfg->style.warning_color, lv_color_hex(WC_DEFAULT_WARN));
}

/* Farbe abhängig vom Wert (Normal- vs. Warnbereich). */
static inline lv_color_t wc_value_color(const widget_base_t *b, float v)
{
    if (b->has_warning && b->warn_threshold > 0.0f && v >= b->warn_threshold)
        return b->warning_color;
    return b->normal_color;
}

/*
 * Erstellt einen Container an Position/Größe aus cfg mit Hintergrundfarbe und
 * optionalem Titel-Label oben. Gibt den Container zurück.
 */
static inline lv_obj_t *wc_make_container(lv_obj_t *parent, const widget_config_t *cfg)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_pos(cont, cfg->x, cfg->y);
    lv_obj_set_size(cont, cfg->width, cfg->height);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(cont, 6, 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_radius(cont, 8, 0);

    lv_color_t bg = cfg->style.background_color
                        ? lv_color_hex(cfg->style.background_color)
                        : lv_color_hex(0x16213E);
    lv_obj_set_style_bg_color(cont, bg, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(cont, lv_color_hex(0x0F3460), 0);

    if (cfg->style.title[0] != '\0') {
        lv_obj_t *t = lv_label_create(cont);
        lv_label_set_text(t, cfg->style.title);
        lv_obj_set_style_text_color(t, lv_color_hex(0xECF0F1), 0);
        lv_obj_align(t, LV_ALIGN_TOP_MID, 0, -2);
    }
    return cont;
}
