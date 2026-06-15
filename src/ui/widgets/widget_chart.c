#include "widget_chart.h"
#include "widget_common.h"
#include <stdlib.h>

#define CHART_POINTS 60

typedef struct
{
    widget_base_t      base;
    lv_obj_t          *chart;
    lv_chart_series_t *ser;
} chart_ctx_t;

lv_obj_t *chart_create(lv_obj_t *parent, const widget_config_t *cfg, const can_signal_t *sig)
{
    chart_ctx_t *ctx = (chart_ctx_t *)calloc(1, sizeof(chart_ctx_t));
    if (!ctx) return NULL;
    wc_base_init(&ctx->base, cfg, sig, lv_color_hex(0x0088FF));

    lv_obj_t *cont = wc_make_container(parent, cfg);

    lv_obj_t *chart = lv_chart_create(cont);
    ctx->chart = chart;
    lv_obj_set_size(chart, cfg->width - 24, cfg->height - 24 - WC_TITLE_H);
    lv_obj_align(chart, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(chart, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_border_color(chart, lv_color_hex(0x0F3460), 0);
    lv_obj_set_style_border_width(chart, 1, 0);
    lv_obj_set_style_line_color(chart, lv_color_hex(0x334466), LV_PART_MAIN);

    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, CHART_POINTS);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y,
                       (lv_coord_t)ctx->base.min, (lv_coord_t)ctx->base.max);
    lv_chart_set_div_line_count(chart, 5, 6);

    ctx->ser = lv_chart_add_series(chart, ctx->base.normal_color, LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_set_user_data(cont, ctx);
    return cont;
}

void chart_update(lv_obj_t *obj, float value)
{
    chart_ctx_t *ctx = (chart_ctx_t *)lv_obj_get_user_data(obj);
    if (!ctx) return;
    float v = wc_clamp(value, ctx->base.min, ctx->base.max);
    lv_chart_set_series_color(ctx->chart, ctx->ser, wc_value_color(&ctx->base, v));
    lv_chart_set_next_value(ctx->chart, ctx->ser, (lv_coord_t)v);
}

void chart_stale(lv_obj_t *obj, bool stale)
{
    chart_ctx_t *ctx = (chart_ctx_t *)lv_obj_get_user_data(obj);
    if (!ctx) return;
    lv_color_t c = stale ? lv_color_hex(WC_STALE_RGB) : ctx->base.normal_color;
    lv_chart_set_series_color(ctx->chart, ctx->ser, c);
}
