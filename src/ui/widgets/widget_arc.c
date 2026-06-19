#include "widget_arc.h"
#include "widget_common.h"
#include <stdlib.h>
#include <stdio.h>

typedef struct
{
    widget_base_t base;
    lv_obj_t     *arc;
    lv_obj_t     *value_label;
    char          unit[CAN_SIGNAL_UNIT_LEN];
} arc_ctx_t;

lv_obj_t *arc_create(lv_obj_t *parent, const widget_config_t *cfg, const can_signal_t *sig)
{
    arc_ctx_t *ctx = (arc_ctx_t *)calloc(1, sizeof(arc_ctx_t));
    if (!ctx) return NULL;
    wc_base_init(&ctx->base, cfg, sig, lv_color_hex(0xFFCC00));
    snprintf(ctx->unit, sizeof(ctx->unit), "%s", sig ? sig->unit : "");

    lv_obj_t *cont = wc_make_container(parent, cfg);

    lv_obj_t *arc = lv_arc_create(cont);
    ctx->arc = arc;
    lv_coord_t side = LV_MIN(cfg->width, cfg->height) - 28;
    lv_obj_set_size(arc, side, side);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, WC_TITLE_H / 2);

    /* Halbkreis-Optik, nicht interaktiv */
    lv_arc_set_bg_angles(arc, 135, 45);
    lv_arc_set_range(arc, (int16_t)ctx->base.min, (int16_t)ctx->base.max);
    lv_arc_set_value(arc, (int16_t)ctx->base.min);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x0F3460), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, ctx->base.normal_color, LV_PART_INDICATOR);

    ctx->value_label = lv_label_create(cont);
    lv_label_set_text(ctx->value_label, "--");
    lv_obj_set_style_text_color(ctx->value_label, lv_color_hex(0xECF0F1), 0);
    lv_obj_align(ctx->value_label, LV_ALIGN_CENTER, 0, WC_TITLE_H / 2);

    lv_obj_set_user_data(cont, ctx);
    return cont;
}

void arc_update(lv_obj_t *obj, float value)
{
    arc_ctx_t *ctx = (arc_ctx_t *)lv_obj_get_user_data(obj);
    if (!ctx) return;
    float v = wc_clamp(value, ctx->base.min, ctx->base.max);
    lv_obj_set_style_arc_color(ctx->arc, wc_value_color(&ctx->base, v), LV_PART_INDICATOR);
    lv_arc_set_value(ctx->arc, (int16_t)v);
    /* newlib-snprintf statt lv_label_set_text_fmt: LVGLs internes printf kann %f
     * nur bei CONFIG_LV_SPRINTF_USE_FLOAT und crasht sonst. */
    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f %s", v, ctx->unit);
    lv_label_set_text(ctx->value_label, buf);
}

void arc_stale(lv_obj_t *obj, bool stale)
{
    arc_ctx_t *ctx = (arc_ctx_t *)lv_obj_get_user_data(obj);
    if (!ctx) return;
    lv_color_t c = stale ? lv_color_hex(WC_STALE_RGB) : ctx->base.normal_color;
    lv_obj_set_style_arc_color(ctx->arc, c, LV_PART_INDICATOR);
    if (stale) lv_label_set_text(ctx->value_label, "---");
}
