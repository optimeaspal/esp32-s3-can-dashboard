#include "widget_label.h"
#include "widget_common.h"
#include <stdlib.h>
#include <stdio.h>

typedef struct
{
    widget_base_t base;
    lv_obj_t     *value_label;
    char          unit[CAN_SIGNAL_UNIT_LEN];
} label_ctx_t;

lv_obj_t *label_create(lv_obj_t *parent, const widget_config_t *cfg, const can_signal_t *sig)
{
    label_ctx_t *ctx = (label_ctx_t *)calloc(1, sizeof(label_ctx_t));
    if (!ctx) return NULL;
    wc_base_init(&ctx->base, cfg, sig, lv_color_hex(0xECF0F1));
    snprintf(ctx->unit, sizeof(ctx->unit), "%s", sig ? sig->unit : "");

    lv_obj_t *cont = wc_make_container(parent, cfg);

    ctx->value_label = lv_label_create(cont);
    lv_label_set_text(ctx->value_label, "--");
    lv_obj_set_style_text_color(ctx->value_label, ctx->base.normal_color, 0);
    lv_obj_set_style_text_font(ctx->value_label, &lv_font_montserrat_24, 0);
    lv_obj_align(ctx->value_label, LV_ALIGN_CENTER, 0, WC_TITLE_H / 2);

    lv_obj_set_user_data(cont, ctx);
    return cont;
}

void label_update(lv_obj_t *obj, float value)
{
    label_ctx_t *ctx = (label_ctx_t *)lv_obj_get_user_data(obj);
    if (!ctx) return;
    float v = wc_clamp(value, ctx->base.min, ctx->base.max);
    lv_obj_set_style_text_color(ctx->value_label, wc_value_color(&ctx->base, v), 0);
    /* Mit newlib-snprintf formatieren statt lv_label_set_text_fmt: LVGLs internes
     * printf kann %f nur bei CONFIG_LV_SPRINTF_USE_FLOAT und crasht sonst (das %f
     * verschiebt die va_list, das folgende %s liest einen Müll-Zeiger). */
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f %s", v, ctx->unit);
    lv_label_set_text(ctx->value_label, buf);
}

void label_stale(lv_obj_t *obj, bool stale)
{
    label_ctx_t *ctx = (label_ctx_t *)lv_obj_get_user_data(obj);
    if (!ctx) return;
    lv_color_t c = stale ? lv_color_hex(WC_STALE_RGB) : ctx->base.normal_color;
    lv_obj_set_style_text_color(ctx->value_label, c, 0);
    if (stale) lv_label_set_text(ctx->value_label, "---");
}
