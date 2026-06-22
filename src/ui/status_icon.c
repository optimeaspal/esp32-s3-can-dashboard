#include "status_icon.h"
#include "settings_screen.h"
#include "sdkconfig.h"

#if CONFIG_DASHBOARD_WIFI_ENABLE
#include "hal/waveshare_wifi_port.h"
#endif

static lv_obj_t *s_icon;       /* das Label mit dem Zahnrad-Symbol */
static lv_obj_t *s_parent_scr; /* Dashboard-Screen (Rückkehrziel)  */

static void on_icon_clicked(lv_event_t *e)
{
    (void)e;
    settings_screen_open(s_parent_scr);
}

void status_icon_create(lv_obj_t *parent_scr)
{
    s_parent_scr = parent_scr;

    /* Runde Klickfläche als Container, damit der Tap-Bereich großzügig ist. */
    lv_obj_t *btn = lv_obj_create(parent_scr);
    lv_obj_set_size(btn, 48, 48);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2B3A4D), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_80, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, on_icon_clicked, LV_EVENT_CLICKED, NULL);

    s_icon = lv_label_create(btn);
    lv_label_set_text(s_icon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(s_icon, &lv_font_montserrat_20, 0);
    lv_obj_center(s_icon);

    status_icon_tick();
}

void status_icon_tick(void)
{
    if (!s_icon) return;

    uint32_t color = 0x8895A5; /* grau = Default/deaktiviert */
#if CONFIG_DASHBOARD_WIFI_ENABLE
    switch (waveshare_wifi_port_get_status()) {
        case WIFI_PORT_CONNECTED:  color = 0x36D17F; break; /* grün  */
        case WIFI_PORT_CONNECTING: color = 0xE0B341; break; /* gelb  */
        case WIFI_PORT_FAILED:     color = 0xE0564B; break; /* rot   */
        case WIFI_PORT_IDLE:
        default:                   color = 0x8895A5; break; /* grau  */
    }
#endif
    lv_obj_set_style_text_color(s_icon, lv_color_hex(color), 0);
}
