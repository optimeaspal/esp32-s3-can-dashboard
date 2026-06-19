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

#if CONFIG_DASHBOARD_WIFI_ENABLE
#include "hal/waveshare_wifi_port.h"
#include "hal/web_server.h"
#include "app/wifi_credentials.h"
#endif

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

#if CONFIG_DASHBOARD_WIFI_ENABLE
/* Liest wifi.json, verbindet sich und startet den Upload-Server. Läuft im
 * Hintergrund, damit der Dashboard-Start nicht blockiert. */
static void network_task(void *arg)
{
    static char json_buf[2048];
    static wifi_credentials_t creds;
    size_t len = 0;

    esp_err_t rc = waveshare_sd_read_file(CONFIG_DASHBOARD_WIFI_PATH,
                                          json_buf, sizeof(json_buf), &len);
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "Keine wifi.json (%s) – Upload deaktiviert",
                 CONFIG_DASHBOARD_WIFI_PATH);
        vTaskDelete(NULL);
        return;
    }

    char err[160] = {0};
    if (wifi_credentials_parse(json_buf, &creds, err, sizeof(err)) != ESP_OK) {
        ESP_LOGW(TAG, "wifi.json ungültig: %s – Upload deaktiviert", err);
        vTaskDelete(NULL);
        return;
    }

    if (waveshare_wifi_port_start(&creds,
            CONFIG_DASHBOARD_WIFI_CONNECT_TIMEOUT_MS) == ESP_OK) {
        web_server_start(CONFIG_DASHBOARD_HTTP_PORT);
        ESP_LOGI(TAG, "Upload bereit: http://%s.local  (IP %s)",
                 creds.hostname, waveshare_wifi_port_get_ip());
    }
    vTaskDelete(NULL);
}
#endif

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

#if CONFIG_DASHBOARD_WIFI_ENABLE
    /* 5 KB Stack: WiFi/HTTP-Init + cJSON-Parse. Core 0, niedrige Priorität. */
    xTaskCreatePinnedToCore(network_task, "network", 5120, NULL, 3, NULL, 0);
#endif

    ESP_LOGI(TAG, "Initialisierung abgeschlossen – Dashboard läuft");
}
