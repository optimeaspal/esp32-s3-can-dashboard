#include "dashboard.h"
#include "widget_registry.h"
#include "nav_indicator.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdint.h>
#include <string.h>

static const char *TAG = "dashboard";

#define COLOR_BG lv_color_hex(0x1A1A2E)
#define MAX_LIVE_WIDGETS (CFG_MAX_PAGES * CFG_MAX_WIDGETS_PER_PAGE)

/* Ein gerendertes Widget mit seiner Signalbindung. */
typedef struct
{
    lv_obj_t                  *obj;
    const widget_descriptor_t *desc;
    int16_t                    signal_idx;
} live_widget_t;

static const dashboard_config_t *s_cfg;
static QueueHandle_t             s_queue;
static lv_obj_t                 *s_tileview;
static lv_obj_t                 *s_nav;

static live_widget_t s_widgets[MAX_LIVE_WIDGETS];
static size_t        s_widget_count;

static int64_t s_last_update_us[CFG_MAX_SIGNALS];
static bool    s_stale_flag[CFG_MAX_SIGNALS];

/* ── Navigation ───────────────────────────────────────────────────────────── */
static void on_page_changed(lv_event_t *e)
{
    lv_obj_t *tv  = lv_event_get_target(e);
    lv_obj_t *act = lv_tileview_get_tile_act(tv);
    if (!act) return;
    uint8_t idx = (uint8_t)(uintptr_t)lv_obj_get_user_data(act);
    nav_indicator_set_active(s_nav, idx);
}

static void on_dot_clicked(lv_event_t *e)
{
    uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    lv_obj_set_tile_id(s_tileview, idx, 0, LV_ANIM_ON);
}

/* ── Aufbau ───────────────────────────────────────────────────────────────── */
void dashboard_create(const dashboard_config_t *cfg, QueueHandle_t event_queue)
{
    s_cfg          = cfg;
    s_queue        = event_queue;
    s_widget_count = 0;
    memset(s_last_update_us, 0, sizeof(s_last_update_us));
    memset(s_stale_flag, 0, sizeof(s_stale_flag));

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_coord_t nav_h = (cfg->page_count >= 2) ? NAV_BAR_HEIGHT : 0;

    s_tileview = lv_tileview_create(scr);
    lv_obj_set_size(s_tileview, LV_HOR_RES, LV_VER_RES - nav_h);
    lv_obj_align(s_tileview, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(s_tileview, COLOR_BG, 0);
    lv_obj_add_event_cb(s_tileview, on_page_changed, LV_EVENT_VALUE_CHANGED, NULL);

    for (uint8_t p = 0; p < cfg->page_count; p++) {
        const page_config_t *page = &cfg->pages[p];
        lv_obj_t *tile = lv_tileview_add_tile(s_tileview, p, 0, LV_DIR_HOR);
        lv_obj_set_user_data(tile, (void *)(uintptr_t)p);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

        for (uint8_t w = 0; w < page->widget_count; w++) {
            const widget_config_t *wc = &page->widgets[w];
            const widget_descriptor_t *desc = widget_registry_find(wc->type);
            if (!desc) {
                ESP_LOGW(TAG, "Unbekannter Widget-Typ '%s' – übersprungen", wc->type);
                continue;   /* FR-002: andere Widgets bleiben */
            }
            const can_signal_t *sig =
                (wc->signal_idx >= 0 && wc->signal_idx < cfg->signal_count)
                    ? &cfg->signals[wc->signal_idx] : NULL;

            lv_obj_t *obj = desc->create(tile, wc, sig);
            if (!obj) continue;

            if (s_widget_count < MAX_LIVE_WIDGETS) {
                s_widgets[s_widget_count].obj        = obj;
                s_widgets[s_widget_count].desc       = desc;
                s_widgets[s_widget_count].signal_idx = wc->signal_idx;
                s_widget_count++;
            }
        }
    }

    /* Navigationsleiste nur bei >= 2 Seiten */
    s_nav = nav_indicator_create(scr, cfg->page_count);
    if (s_nav) {
        uint32_t n = lv_obj_get_child_cnt(s_nav);
        for (uint32_t i = 0; i < n; i++) {
            lv_obj_t *dot = lv_obj_get_child(s_nav, i);
            lv_obj_add_flag(dot, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(dot, on_dot_clicked, LV_EVENT_CLICKED,
                                (void *)(uintptr_t)i);
        }
    }

    ESP_LOGI(TAG, "Dashboard erstellt: %u Seite(n), %u Widget(s)",
             cfg->page_count, (unsigned)s_widget_count);
}

/* ── Laufzeit-Update ──────────────────────────────────────────────────────── */
void dashboard_tick(void)
{
    if (!s_cfg) return;

    can_value_event_t evt;
    while (xQueueReceive(s_queue, &evt, 0) == pdTRUE) {
        if (evt.signal_idx >= s_cfg->signal_count) continue;
        s_last_update_us[evt.signal_idx] = evt.timestamp_us;
        s_stale_flag[evt.signal_idx]     = false;

        for (size_t i = 0; i < s_widget_count; i++) {
            if (s_widgets[i].signal_idx == evt.signal_idx)
                s_widgets[i].desc->update(s_widgets[i].obj, evt.value);
        }
    }

    int64_t now = esp_timer_get_time();
    for (uint8_t s = 0; s < s_cfg->signal_count; s++) {
        uint32_t to = s_cfg->signals[s].timeout_ms;
        if (to == 0 || s_stale_flag[s] || s_last_update_us[s] == 0) continue;
        if ((now - s_last_update_us[s]) <= (int64_t)to * 1000) continue;

        s_stale_flag[s] = true;   /* Übergang in Stale nur einmal anwenden */
        for (size_t i = 0; i < s_widget_count; i++) {
            if (s_widgets[i].signal_idx == s)
                s_widgets[i].desc->set_stale(s_widgets[i].obj, true);
        }
    }
}
