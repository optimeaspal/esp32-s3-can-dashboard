#include "dashboard.h"
#include "app/can_signal.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>
#include <string.h>

static const char *TAG = "dashboard";

// Externe Signal-Tabelle (definiert in main.c)
extern const can_signal_t can_signals[];
extern const size_t       can_signal_count;

// ── Widget-Handles ────────────────────────────────────────────────────────────
static lv_obj_t *gauge_rpm;         // lv_meter  → Drehzahl
static lv_meter_indicator_t *gauge_rpm_needle;

static lv_obj_t *chart_temp;        // lv_chart  → Temperatur-Verlauf
static lv_chart_series_t *chart_temp_ser;

static lv_obj_t *bar_fuel;          // lv_bar    → Kraftstoff
static lv_obj_t *bar_fuel_label;

static lv_obj_t *led_warn;          // lv_led    → Warnleuchte
static lv_obj_t *led_warn_label;

// ── Stale-Tracking ───────────────────────────────────────────────────────────
#define MAX_SIGNALS 16
static int64_t last_update_us[MAX_SIGNALS];
static size_t  signal_count_local;
static QueueHandle_t evt_queue;

// ── Farben / Theme ───────────────────────────────────────────────────────────
#define COLOR_BG       lv_color_hex(0x1A1A2E)
#define COLOR_PANEL    lv_color_hex(0x16213E)
#define COLOR_ACCENT   lv_color_hex(0x0F3460)
#define COLOR_GREEN    lv_color_hex(0x00B894)
#define COLOR_RED      lv_color_hex(0xE74C3C)
#define COLOR_YELLOW   lv_color_hex(0xF1C40F)
#define COLOR_TEXT     lv_color_hex(0xECF0F1)
#define COLOR_STALE    lv_color_hex(0x555577)

// ── Hilfsfunktion: Panel mit Titel erstellen ─────────────────────────────────
static lv_obj_t *create_panel(lv_obj_t *parent, const char *title,
                               lv_coord_t x, lv_coord_t y,
                               lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 10, 0);

    lv_obj_t *label = lv_label_create(panel);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);

    return panel;
}

// ── Gauge (lv_meter) – Drehzahl 0..6000 RPM ─────────────────────────────────
static void create_gauge(lv_obj_t *parent)
{
    lv_obj_t *panel = create_panel(parent, "Drehzahl", 10, 10, 370, 220);

    gauge_rpm = lv_meter_create(panel);
    lv_obj_set_size(gauge_rpm, 180, 180);
    lv_obj_align(gauge_rpm, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(gauge_rpm, COLOR_PANEL, 0);
    lv_obj_set_style_border_width(gauge_rpm, 0, 0);

    lv_meter_scale_t *scale = lv_meter_add_scale(gauge_rpm);
    lv_meter_set_scale_ticks(gauge_rpm, scale, 61, 2, 10,
                             lv_color_hex(0x444466));
    lv_meter_set_scale_major_ticks(gauge_rpm, scale, 10, 4, 15,
                                   COLOR_TEXT, 14);
    lv_meter_set_scale_range(gauge_rpm, scale, 0, 6000, 270, 135);

    // Grüner Bereich 0..4000
    lv_meter_add_arc(gauge_rpm, scale, 6, COLOR_GREEN, 0);
    lv_meter_indicator_t *arc_green = lv_meter_add_arc(gauge_rpm, scale, 6, COLOR_GREEN, 0);
    lv_meter_set_indicator_start_value(gauge_rpm, arc_green, 0);
    lv_meter_set_indicator_end_value(gauge_rpm, arc_green, 4000);

    // Roter Bereich 4000..6000
    lv_meter_indicator_t *arc_red = lv_meter_add_arc(gauge_rpm, scale, 6, COLOR_RED, 0);
    lv_meter_set_indicator_start_value(gauge_rpm, arc_red, 4000);
    lv_meter_set_indicator_end_value(gauge_rpm, arc_red, 6000);

    gauge_rpm_needle = lv_meter_add_needle_line(gauge_rpm, scale, 3, COLOR_YELLOW, -10);
    lv_meter_set_indicator_value(gauge_rpm, gauge_rpm_needle, 0);

    // Einheits-Label
    lv_obj_t *unit = lv_label_create(panel);
    lv_label_set_text(unit, "RPM");
    lv_obj_set_style_text_color(unit, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(unit, &lv_font_montserrat_12, 0);
    lv_obj_align(unit, LV_ALIGN_BOTTOM_MID, 0, -4);
}

// ── Chart (lv_chart) – Temperatur-Verlauf ────────────────────────────────────
static void create_chart(lv_obj_t *parent)
{
    lv_obj_t *panel = create_panel(parent, "Temperatur", 390, 10, 400, 220);

    chart_temp = lv_chart_create(panel);
    lv_obj_set_size(chart_temp, 360, 160);
    lv_obj_align(chart_temp, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(chart_temp, COLOR_BG, 0);
    lv_obj_set_style_border_color(chart_temp, COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(chart_temp, 1, 0);

    lv_chart_set_type(chart_temp, LV_CHART_TYPE_LINE);
    lv_chart_set_range(chart_temp, LV_CHART_AXIS_PRIMARY_Y, -40, 150);
    lv_chart_set_point_count(chart_temp, 60);
    lv_chart_set_div_line_count(chart_temp, 5, 5);
    lv_obj_set_style_line_color(chart_temp, lv_color_hex(0x334466), LV_PART_MAIN);

    chart_temp_ser = lv_chart_add_series(
        chart_temp, COLOR_GREEN, LV_CHART_AXIS_PRIMARY_Y);

    // Y-Achse Ticks
    lv_chart_set_axis_tick(chart_temp, LV_CHART_AXIS_PRIMARY_Y, 10, 5, 4, 1, true, 35);

    lv_obj_t *unit = lv_label_create(panel);
    lv_label_set_text(unit, "°C");
    lv_obj_set_style_text_color(unit, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(unit, &lv_font_montserrat_12, 0);
    lv_obj_align(unit, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
}

// ── Bar (lv_bar) – Kraftstoffstand 0..100% ───────────────────────────────────
static void create_bar(lv_obj_t *parent)
{
    lv_obj_t *panel = create_panel(parent, "Kraftstoff", 10, 245, 370, 220);

    bar_fuel = lv_bar_create(panel);
    lv_obj_set_size(bar_fuel, 320, 30);
    lv_obj_align(bar_fuel, LV_ALIGN_CENTER, 0, 10);
    lv_bar_set_range(bar_fuel, 0, 100);
    lv_bar_set_value(bar_fuel, 0, LV_ANIM_OFF);

    lv_obj_set_style_bg_color(bar_fuel, COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_fuel, COLOR_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_fuel, 4, 0);

    bar_fuel_label = lv_label_create(panel);
    lv_label_set_text(bar_fuel_label, "0 %");
    lv_obj_set_style_text_color(bar_fuel_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(bar_fuel_label, &lv_font_montserrat_20, 0);
    lv_obj_align_to(bar_fuel_label, bar_fuel, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
}

// ── LED (lv_led) – Warnleuchte ───────────────────────────────────────────────
static void create_led(lv_obj_t *parent)
{
    lv_obj_t *panel = create_panel(parent, "Warnung", 390, 245, 400, 220);

    led_warn = lv_led_create(panel);
    lv_obj_set_size(led_warn, 80, 80);
    lv_obj_align(led_warn, LV_ALIGN_CENTER, 0, 10);
    lv_led_set_color(led_warn, COLOR_RED);
    lv_led_off(led_warn);

    led_warn_label = lv_label_create(panel);
    lv_label_set_text(led_warn_label, "AUS");
    lv_obj_set_style_text_color(led_warn_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(led_warn_label, &lv_font_montserrat_20, 0);
    lv_obj_align_to(led_warn_label, led_warn, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
}

// ── Widget-Update-Funktionen (aufgerufen von dashboard_tick) ─────────────────

static void update_gauge(float value, bool stale)
{
    lv_color_t needle_color = stale ? COLOR_STALE : COLOR_YELLOW;
    lv_obj_set_style_line_color(gauge_rpm, needle_color, LV_PART_INDICATOR);
    if (!stale)
        lv_meter_set_indicator_value(gauge_rpm, gauge_rpm_needle, (int32_t)value);
}

static void update_chart(float value, bool stale)
{
    lv_color_t line_color = stale ? COLOR_STALE : COLOR_GREEN;
    lv_obj_set_style_line_color(chart_temp, line_color, LV_PART_ITEMS);
    if (!stale) {
        lv_chart_set_next_value(chart_temp, chart_temp_ser, (lv_coord_t)value);
        lv_chart_refresh(chart_temp);
    }
}

static void update_bar(float value, bool stale)
{
    lv_color_t ind_color = stale ? COLOR_STALE : COLOR_GREEN;
    lv_obj_set_style_bg_color(bar_fuel, ind_color, LV_PART_INDICATOR);
    if (!stale) {
        lv_bar_set_value(bar_fuel, (int32_t)value, LV_ANIM_ON);
        lv_label_set_text_fmt(bar_fuel_label, "%.0f %%", value);
    }
}

static void update_led(float value, bool stale)
{
    if (stale) {
        lv_led_set_brightness(led_warn, 60);
        lv_led_set_color(led_warn, COLOR_STALE);
        lv_label_set_text(led_warn_label, "---");
        return;
    }
    // Warnleuchte an wenn Wert > 0
    if (value > 0.0f) {
        lv_led_on(led_warn);
        lv_led_set_color(led_warn, COLOR_RED);
        lv_label_set_text(led_warn_label, "WARNUNG");
    } else {
        lv_led_off(led_warn);
        lv_led_set_color(led_warn, COLOR_GREEN);
        lv_label_set_text(led_warn_label, "OK");
    }
}

// ── Öffentliche API ───────────────────────────────────────────────────────────

void dashboard_create(QueueHandle_t event_queue, size_t sig_count)
{
    evt_queue = event_queue;
    signal_count_local = (sig_count < MAX_SIGNALS) ? sig_count : MAX_SIGNALS;
    memset(last_update_us, 0, sizeof(last_update_us));

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    create_gauge(scr);
    create_chart(scr);
    create_bar(scr);
    create_led(scr);

    ESP_LOGI(TAG, "Dashboard erstellt (800x480)");
}

void dashboard_tick(void)
{
    can_value_event_t evt;
    // Alle wartenden Events verarbeiten (nicht-blockierend)
    while (xQueueReceive(evt_queue, &evt, 0) == pdTRUE) {
        if (evt.signal_idx >= signal_count_local) continue;

        last_update_us[evt.signal_idx] = evt.timestamp_us;
        int64_t now = esp_timer_get_time();

        // Signal-Index → Widget-Zuweisung (POC: fest verdrahtet)
        switch (evt.signal_idx) {
        case 0: update_gauge(evt.value, false);  break;
        case 1: update_chart(evt.value, false);  break;
        case 2: update_bar(evt.value, false);    break;
        case 3: update_led(evt.value, false);    break;
        default: break;
        }

        (void)now;
    }

    // Stale-Überprüfung für alle Signale
    int64_t now = esp_timer_get_time();
    for (size_t i = 0; i < signal_count_local; i++) {
        if (can_signals[i].timeout_ms == 0) continue;
        int64_t timeout_us = (int64_t)can_signals[i].timeout_ms * 1000;
        bool stale = (last_update_us[i] > 0)
                     && ((now - last_update_us[i]) > timeout_us);
        if (!stale) continue;

        switch (i) {
        case 0: update_gauge(0, true); break;
        case 1: update_chart(0, true); break;
        case 2: update_bar(0, true);   break;
        case 3: update_led(0, true);   break;
        default: break;
        }
    }
}
