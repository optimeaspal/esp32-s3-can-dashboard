#include "widget_led.h"
#include "widget_common.h"
#include <stdlib.h>

typedef struct
{
    widget_base_t base;
    lv_obj_t     *led;
    lv_obj_t     *state_label;
    float         on_threshold;  /* Wert ab dem die LED "an" ist */
} led_ctx_t;

lv_obj_t *led_create(lv_obj_t *parent, const widget_config_t *cfg, const can_signal_t *sig)
{
    led_ctx_t *ctx = (led_ctx_t *)calloc(1, sizeof(led_ctx_t));
    if (!ctx) return NULL;
    wc_base_init(&ctx->base, cfg, sig, lv_color_hex(0xE74C3C));
    /* Schaltschwelle: warning_threshold falls gesetzt, sonst > 0 */
    ctx->on_threshold = (cfg->style.warning_threshold > 0.0f)
                            ? cfg->style.warning_threshold : 0.5f;

    lv_obj_t *cont = wc_make_container(parent, cfg);

    lv_obj_t *led = lv_led_create(cont);
    ctx->led = led;
    lv_coord_t d = LV_MIN(cfg->width, cfg->height) - 40;
    if (d < 20) d = 20;
    lv_obj_set_size(led, d, d);
    lv_obj_align(led, LV_ALIGN_CENTER, 0, WC_TITLE_H / 2);
    lv_led_set_color(led, ctx->base.normal_color);
    lv_led_off(led);

    ctx->state_label = lv_label_create(cont);
    lv_label_set_text(ctx->state_label, "AUS");
    lv_obj_set_style_text_color(ctx->state_label, lv_color_hex(0xECF0F1), 0);
    lv_obj_align_to(ctx->state_label, led, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    lv_obj_set_user_data(cont, ctx);
    return cont;
}

void led_update(lv_obj_t *obj, float value)
{
    led_ctx_t *ctx = (led_ctx_t *)lv_obj_get_user_data(obj);
    if (!ctx) return;
    if (value >= ctx->on_threshold) {
        lv_led_on(ctx->led);
        lv_led_set_color(ctx->led, ctx->base.normal_color);
        lv_label_set_text(ctx->state_label, "AN");
    } else {
        lv_led_off(ctx->led);
        lv_label_set_text(ctx->state_label, "AUS");
    }
}

void led_stale(lv_obj_t *obj, bool stale)
{
    led_ctx_t *ctx = (led_ctx_t *)lv_obj_get_user_data(obj);
    if (!ctx) return;
    if (stale) {
        lv_led_set_color(ctx->led, lv_color_hex(WC_STALE_RGB));
        lv_led_set_brightness(ctx->led, 80);
        lv_label_set_text(ctx->state_label, "---");
    }
}
