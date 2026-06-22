#include "settings_screen.h"
#include "app/can_dispatcher.h"
#include "app/can_monitor.h"
#include "sdkconfig.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>

#if CONFIG_DASHBOARD_WIFI_ENABLE
#include "hal/waveshare_wifi_port.h"
#endif
#include "hal/waveshare_sd_port.h"

/* LVGL 8.4: montserrat_14 ist nicht aktiviert (nur 12/16/20/24) → 16 nutzen. */
#define FONT_BODY  &lv_font_montserrat_16
#define FONT_TITLE &lv_font_montserrat_20

#define COLOR_BG       lv_color_hex(0x0F141A)
#define COLOR_CARD     lv_color_hex(0x172230)
#define COLOR_CARD_CAN lv_color_hex(0x101B14)
#define COLOR_TITLE    lv_color_hex(0x7FD1FF)
#define COLOR_TEXT     lv_color_hex(0xCDD6E0)
#define COLOR_CAN_TXT  lv_color_hex(0x8BE0A8)

#define CAN_ROWS_MAX 14   /* sichtbare CAN-Zeilen (Höhe ~ volle Spalte) */

static const dashboard_config_t *s_cfg;
static lv_obj_t *s_screen;
static lv_obj_t *s_return_scr;
static bool      s_active;

static lv_obj_t *s_wifi_val;
static lv_obj_t *s_dev_val;
static lv_obj_t *s_sd_val;
static lv_obj_t *s_can_header;
static lv_obj_t *s_can_rows[CAN_ROWS_MAX];

static void on_back_clicked(lv_event_t *e)
{
    (void)e;
    s_active = false;
    if (s_return_scr)
        lv_scr_load_anim(s_return_scr, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 250, 0, false);
}

static lv_obj_t *make_card(lv_obj_t *parent, const char *title)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2C3B4B), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *t = lv_label_create(card);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, COLOR_TITLE, 0);
    lv_obj_set_style_text_font(t, FONT_BODY, 0);

    lv_obj_t *v = lv_label_create(card);
    lv_label_set_long_mode(v, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(v, LV_PCT(100));
    lv_obj_set_style_text_color(v, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(v, FONT_BODY, 0);
    lv_label_set_text(v, "");
    return v;
}

static void build_screen(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hdr = lv_obj_create(s_screen);
    lv_obj_set_size(hdr, LV_HOR_RES, 56);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x18222E), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = lv_btn_create(hdr);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x27384A), 0);
    lv_obj_add_event_cb(back, on_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " Zurueck");
    lv_obj_center(bl);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "Einstellungen & Info");
    lv_obj_set_style_text_color(title, lv_color_hex(0xEAF2FB), 0);
    lv_obj_set_style_text_font(title, FONT_TITLE, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *body = lv_obj_create(s_screen);
    lv_obj_set_size(body, LV_HOR_RES, LV_VER_RES - 56);
    lv_obj_align(body, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 8, 0);
    lv_obj_set_style_pad_column(body, 8, 0);   /* Lücke zwischen den Spalten */
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);

    lv_obj_t *left = lv_obj_create(body);
    lv_obj_set_height(left, LV_PCT(100));
    lv_obj_set_width(left, LV_PCT(42));
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_set_style_pad_row(left, 8, 0);      /* Lücke zwischen den Karten */
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);

    s_wifi_val = make_card(left, "WLAN / Netzwerk");
    s_dev_val  = make_card(left, "Geraet / Firmware");
    s_sd_val   = make_card(left, "SD-Karte");

    lv_obj_t *can = lv_obj_create(body);
    lv_obj_set_height(can, LV_PCT(100));
    lv_obj_set_flex_grow(can, 1);
    lv_obj_set_style_bg_color(can, COLOR_CARD_CAN, 0);
    lv_obj_set_style_border_color(can, lv_color_hex(0x1F4030), 0);
    lv_obj_set_style_border_width(can, 1, 0);
    lv_obj_set_style_radius(can, 6, 0);
    lv_obj_set_style_pad_all(can, 8, 0);
    lv_obj_set_style_pad_row(can, 2, 0);       /* Lücke zwischen den Zeilen */
    lv_obj_clear_flag(can, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(can, LV_FLEX_FLOW_COLUMN);

    s_can_header = lv_label_create(can);
    lv_obj_set_style_text_color(s_can_header, lv_color_hex(0x36D17F), 0);
    lv_obj_set_style_text_font(s_can_header, FONT_BODY, 0);
    lv_label_set_text(s_can_header, "CAN-RX");

    for (int i = 0; i < CAN_ROWS_MAX; i++) {
        s_can_rows[i] = lv_label_create(can);
        lv_obj_set_style_text_color(s_can_rows[i], COLOR_CAN_TXT, 0);
        lv_obj_set_style_text_font(s_can_rows[i], FONT_BODY, 0);
        lv_label_set_text(s_can_rows[i], "");
    }
}

void settings_screen_set_config(const dashboard_config_t *cfg)
{
    s_cfg = cfg;
}

void settings_screen_open(lv_obj_t *return_scr)
{
    s_return_scr = return_scr;
    if (!s_screen) build_screen();
    s_active = true;
    settings_screen_tick();
    lv_scr_load_anim(s_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
}

/* ── Laufzeit-Update ──────────────────────────────────────────────────────── */
static void format_hex(char *dst, size_t dstsz, const uint8_t *d, uint8_t dlc)
{
    size_t pos = 0;
    for (uint8_t i = 0; i < dlc && pos + 3 < dstsz; i++)
        pos += snprintf(dst + pos, dstsz - pos, "%02X ", d[i]);
    if (pos > 0) dst[pos - 1] = '\0';   /* abschließendes Leerzeichen kappen */
    else if (dstsz) dst[0] = '\0';
}

static void update_info_cards(void)
{
    char buf[160];

#if CONFIG_DASHBOARD_WIFI_ENABLE
    const char *status;
    switch (waveshare_wifi_port_get_status()) {
        case WIFI_PORT_CONNECTED:  status = "verbunden";   break;
        case WIFI_PORT_CONNECTING: status = "verbindet…";  break;
        case WIFI_PORT_FAILED:     status = "getrennt";    break;
        default:                   status = "inaktiv";     break;
    }
    snprintf(buf, sizeof(buf),
             "SSID: %s\n%s\nIP: %s\n%s.local\nRSSI: %d dBm",
             waveshare_wifi_port_get_ssid()[0] ? waveshare_wifi_port_get_ssid() : "-",
             status,
             waveshare_wifi_port_get_ip(),
             waveshare_wifi_port_get_hostname()[0] ? waveshare_wifi_port_get_hostname() : "-",
             waveshare_wifi_port_get_rssi());
#else
    snprintf(buf, sizeof(buf), "WLAN deaktiviert");
#endif
    lv_label_set_text(s_wifi_val, buf);

    uint32_t up_s    = (uint32_t)(esp_timer_get_time() / 1000000);
    uint32_t heap_k  = (uint32_t)(esp_get_free_heap_size() / 1024);
    uint32_t psram_k = (uint32_t)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    snprintf(buf, sizeof(buf),
             "FW %d.%d  (%s)\nUptime: %lu s\nHeap frei: %lu k\nPSRAM frei: %lu k\nSeiten: %u",
             s_cfg ? s_cfg->version_major : 0,
             s_cfg ? s_cfg->version_minor : 0,
             __DATE__,
             (unsigned long)up_s, (unsigned long)heap_k, (unsigned long)psram_k,
             s_cfg ? s_cfg->page_count : 0);
    lv_label_set_text(s_dev_val, buf);

    snprintf(buf, sizeof(buf),
             "%s\ndashboard.json: %s\nwifi.json: %s",
             waveshare_sd_port_is_mounted() ? "erkannt" : "nicht erkannt",
             waveshare_sd_port_file_exists("/sdcard/dashboard.json") ? "ja" : "nein",
             waveshare_sd_port_file_exists("/sdcard/wifi.json") ? "ja" : "nein");
    lv_label_set_text(s_sd_val, buf);
}

static void update_can_table(void)
{
    static can_monitor_entry_t entries[CAN_MONITOR_MAX_IDS];
    uint32_t total_fps = 0;
    size_t n = can_dispatcher_get_monitor(entries, CAN_MONITOR_MAX_IDS, &total_fps);

    char hdr[64];
    snprintf(hdr, sizeof(hdr), "CAN-RX  %u IDs  %lu fps",
             (unsigned)n, (unsigned long)total_fps);
    lv_label_set_text(s_can_header, hdr);

    for (int i = 0; i < CAN_ROWS_MAX; i++) {
        if ((size_t)i < n) {
            char hex[32];
            format_hex(hex, sizeof(hex), entries[i].data, entries[i].dlc);
            char row[64];
            snprintf(row, sizeof(row), "0x%03lX  %-23s %lu",
                     (unsigned long)entries[i].id, hex,
                     (unsigned long)entries[i].fps);
            lv_label_set_text(s_can_rows[i], row);
        } else {
            lv_label_set_text(s_can_rows[i], "");
        }
    }
}

void settings_screen_tick(void)
{
    if (!s_active || !s_screen) return;
    update_info_cards();
    update_can_table();
}
