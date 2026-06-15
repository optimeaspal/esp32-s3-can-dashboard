#include "widget_bar.h"
#include "widget_common.h"
#include <stdlib.h>
#include <stdio.h>

typedef struct
{
    widget_base_t base;
    lv_obj_t     *bar;
    lv_obj_t     *value_label;
    char          unit[CAN_SIGNAL_UNIT_LEN];
} bar_ctx_t;

lv_obj_t *bar_create(lv_obj_t *parent, const widget_config_t *cfg, const can_signal_t *sig)
{
    bar_ctx_t *ctx = (bar_ctx_t *)calloc(1, sizeof(bar_ctx_t));
    if (!ctx) return NULL;
    wc_base_init(&ctx->base, cfg, sig, lv_color_hex(0x00B894));
    snprintf(ctx->unit, sizeof(ctx->unit), "%s", sig ? sig->unit : "");

    lv_obj_t *cont = wc_make_container(parent, cfg);

    lv_obj_t *bar = lv_bar_create(cont);
    ctx->bar = bar;
    lv_obj_set_size(bar, cfg->width - 28, 26);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 0);
    lv_bar_set_range(bar, (int32_t)ctx->base.min, (int32_t)ctx->base.max);
    lv_bar_set_value(bar, (int32_t)ctx->base.min, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, 4, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0F3460), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, ctx->base.normal_color, LV_PART_INDICATOR);

    ctx->value_label = lv_label_create(cont);
    lv_label_set_text(ctx->value_label, "--");
    lv_obj_set_style_text_color(ctx->value_label, lv_color_hex(0xECF0F1), 0);
    lv_obj_align_to(ctx->value_label, bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    lv_obj_set_user_data(cont, ctx);
    return cont;
}

void bar_update(lv_obj_t *obj, float value)
{
    bar_ctx_t *ctx = (bar_ctx_t *)lv_obj_get_user_data(obj);
    if (!ctx) return;
    float v = wc_clamp(value, ctx->base.min, ctx->base.max);
    lv_obj_set_style_bg_color(ctx->bar, wc_value_color(&ctx->base, v), LV_PART_INDICATOR);
    lv_bar_set_value(ctx->bar, (int32_t)v, LV_ANIM_ON);
    lv_label_set_text_fmt(ctx->value_label, "%d %s", (int)v, ctx->unit);
}

void bar_stale(lv_obj_t *obj, bool stale)
{
    bar_ctx_t *ctx = (bar_ctx_t *)lv_obj_get_user_data(obj);
    if (!ctx) return;
    lv_color_t c = stale ? lv_color_hex(WC_STALE_RGB) : ctx->base.normal_color;
    lv_obj_set_style_bg_color(ctx->bar, c, LV_PART_INDICATOR);
    if (stale) lv_label_set_text(ctx->value_label, "---");
}
