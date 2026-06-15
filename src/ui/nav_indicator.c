#include "nav_indicator.h"

#define DOT_SIZE      12
#define DOT_GAP        8
#define COLOR_ACTIVE   lv_color_hex(0xECF0F1)
#define COLOR_INACTIVE lv_color_hex(0x555577)

lv_obj_t *nav_indicator_create(lv_obj_t *parent, uint8_t page_count)
{
    if (page_count < 2) return NULL;   /* FR-008: nur bei >= 2 Seiten */

    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_HOR_RES, NAV_BAR_HEIGHT);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    /* horizontale, zentrierte Anordnung der Punkte */
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, DOT_GAP, 0);

    for (uint8_t i = 0; i < page_count; i++) {
        lv_obj_t *dot = lv_obj_create(bar);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, DOT_SIZE, DOT_SIZE);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, (i == 0) ? COLOR_ACTIVE : COLOR_INACTIVE, 0);
    }
    return bar;
}

void nav_indicator_set_active(lv_obj_t *nav, uint8_t active_idx)
{
    if (!nav) return;
    uint32_t count = lv_obj_get_child_cnt(nav);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *dot = lv_obj_get_child(nav, i);
        lv_obj_set_style_bg_color(dot,
            (i == active_idx) ? COLOR_ACTIVE : COLOR_INACTIVE, 0);
    }
}
