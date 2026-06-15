#include "widget_gauge.h"
#include "widget_common.h"
#include <stdlib.h>

typedef struct
{
    widget_base_t         base;
    lv_obj_t             *meter;
    lv_meter_indicator_t *needle;
} gauge_ctx_t;

lv_obj_t *gauge_create(lv_obj_t *parent, const widget_config_t *cfg, const can_signal_t *sig)
{
    gauge_ctx_t *ctx = (gauge_ctx_t *)calloc(1, sizeof(gauge_ctx_t));
    if (!ctx) return NULL;
    wc_base_init(&ctx->base, cfg, sig, lv_color_hex(0x00B894));

    lv_obj_t *cont = wc_make_container(parent, cfg);

    lv_obj_t *meter = lv_meter_create(cont);
    ctx->meter = meter;
    lv_coord_t side = LV_MIN(cfg->width, cfg->height) - 28;
    lv_obj_set_size(meter, side, side);
    lv_obj_align(meter, LV_ALIGN_CENTER, 0, WC_TITLE_H / 2);
    lv_obj_set_style_border_width(meter, 0, 0);
    lv_obj_set_style_bg_opa(meter, LV_OPA_TRANSP, 0);

    lv_meter_scale_t *scale = lv_meter_add_scale(meter);
    lv_meter_set_scale_ticks(meter, scale, 41, 2, 8, lv_color_hex(0x444466));
    lv_meter_set_scale_major_ticks(meter, scale, 8, 4, 12, lv_color_hex(0xECF0F1), 12);
    lv_meter_set_scale_range(meter, scale, (int32_t)ctx->base.min,
                             (int32_t)ctx->base.max, 270, 135);

    /* Warnbereich-Bogen */
    if (ctx->base.has_warning && ctx->base.warn_threshold > ctx->base.min) {
        lv_meter_indicator_t *arc =
            lv_meter_add_arc(meter, scale, 5, ctx->base.warning_color, 0);
        lv_meter_set_indicator_start_value(meter, arc, (int32_t)ctx->base.warn_threshold);
        lv_meter_set_indicator_end_value(meter, arc, (int32_t)ctx->base.max);
    }

    ctx->needle = lv_meter_add_needle_line(meter, scale, 4, ctx->base.normal_color, -10);
    lv_meter_set_indicator_value(meter, ctx->needle, (int32_t)ctx->base.min);

    lv_obj_set_style_text_color(meter, lv_color_hex(0xECF0F1), LV_PART_TICKS);
    lv_obj_set_user_data(cont, ctx);
    return cont;
}

void gauge_update(lv_obj_t *obj, float value)
{
    gauge_ctx_t *ctx = (gauge_ctx_t *)lv_obj_get_user_data(obj);
    if (!ctx) return;
    float v = wc_clamp(value, ctx->base.min, ctx->base.max);
    lv_meter_set_indicator_value(ctx->meter, ctx->needle, (int32_t)v);
    lv_obj_set_style_line_color(ctx->meter, wc_value_color(&ctx->base, v), LV_PART_INDICATOR);
}

void gauge_stale(lv_obj_t *obj, bool stale)
{
    gauge_ctx_t *ctx = (gauge_ctx_t *)lv_obj_get_user_data(obj);
    if (!ctx) return;
    lv_color_t c = stale ? lv_color_hex(WC_STALE_RGB) : ctx->base.normal_color;
    lv_obj_set_style_line_color(ctx->meter, c, LV_PART_INDICATOR);
}
