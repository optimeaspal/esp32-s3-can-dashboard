#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <stdio.h>
#include <assert.h>

#include "hal/waveshare_rgb_lcd_port.h"
#include "hal/waveshare_sd_port.h"
#include "hal/lvgl_port.h"
#include "app/config_types.h"
#include "app/config_loader.h"
#include "app/can_dispatcher.h"
#include "app/can_simulator.h"
#include "ui/dashboard.h"

static const char *TAG = "main";

/* Statisch: müssen für die gesamte Laufzeit gültig bleiben. */
static dashboard_config_t s_cfg;
static char               s_json_buf[16384];

/* Zeigt eine Fehlermeldung mittig auf dem Display (FR-009/FR-010). */
static void show_error_screen(const char *msg)
{
    lvgl_port_lock(-1);
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x300000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_HOR_RES - 80);
    lv_label_set_text(label, msg);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_center(label);
    lvgl_port_unlock();

    ESP_LOGE(TAG, "%s", msg);
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 CAN Dashboard – Start (JSON-Konfiguration)");

    /* Display + LVGL initialisieren, Backlight + CAN-Mux via CH422G setzen
     * (alle CH422G-Schreibzugriffe VOR Start des LVGL-Timers). */
    ESP_ERROR_CHECK(waveshare_rgb_lcd_init());
    ESP_ERROR_CHECK(waveshare_rgb_lcd_bl_on());
    ESP_ERROR_CHECK(waveshare_rgb_lcd_can_mux_enable());

    /* SD-Karte mounten (muss NACH rgb_lcd_init erfolgen – CH422G ist dann bereit). */
    esp_err_t rc = waveshare_sd_port_init();
    if (rc != ESP_OK) {
        show_error_screen("SD-Karte nicht gefunden.\n"
                          "Bitte FAT32-Karte mit dashboard.json einlegen und neu starten.");
        return;
    }

    /* dashboard.json lesen */
    size_t len = 0;
    rc = waveshare_sd_read_file(CONFIG_DASHBOARD_JSON_PATH, s_json_buf, sizeof(s_json_buf), &len);
    if (rc != ESP_OK) {
        show_error_screen("SD: dashboard.json nicht gefunden oder zu gross.");
        return;
    }

    /* JSON parsen */
    char err[160] = {0};
    rc = config_loader_parse(s_json_buf, &s_cfg, err, sizeof(err));
    if (rc != ESP_OK) {
        char msg[224];
        snprintf(msg, sizeof(msg), "Konfigurationsfehler:\n%s", err);
        show_error_screen(msg);
        return;
    }
    ESP_LOGI(TAG, "Konfiguration geladen: %u Signale, %u Seiten",
             s_cfg.signal_count, s_cfg.page_count);

    /* Shared Queue: CAN/Simulator → Dashboard */
    QueueHandle_t event_queue = xQueueCreate(CONFIG_CAN_RX_QUEUE_LEN, sizeof(can_value_event_t));
    assert(event_queue);

    /* Dashboard aufbauen + periodischen Tick registrieren (innerhalb LVGL-Mutex) */
    lvgl_port_lock(-1);
    dashboard_create(&s_cfg, event_queue);
    lv_timer_create((lv_timer_cb_t)dashboard_tick, 50, NULL);
    lvgl_port_unlock();

#if CONFIG_CAN_SIMULATOR_ENABLE
    ESP_ERROR_CHECK(can_simulator_start(s_cfg.signals, s_cfg.signal_count, event_queue));
#endif
    ESP_ERROR_CHECK(can_dispatcher_start(s_cfg.signals, s_cfg.signal_count, event_queue));

    ESP_LOGI(TAG, "Initialisierung abgeschlossen – Dashboard läuft");
}
