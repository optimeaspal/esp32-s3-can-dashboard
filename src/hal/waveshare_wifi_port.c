#include "waveshare_wifi_port.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mdns.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "wifi_port";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t  s_wifi_events;
static wifi_port_status_t  s_status = WIFI_PORT_IDLE;
static char                s_ip[16] = "0.0.0.0";
static char                s_ssid[33]     = "";
static char                s_hostname[33] = "";
static esp_netif_t        *s_netif;

static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&evt->ip_info.ip));
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

/* Einmalige Treiber-Initialisierung (NVS, netif, Event-Loop, WiFi-STA). */
static esp_err_t wifi_init_once(void)
{
    esp_err_t rc = nvs_flash_init();
    if (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        rc = nvs_flash_init();
    }
    ESP_ERROR_CHECK(rc);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    return ESP_OK;
}

/* Versucht genau ein Netz, bis Timeout. true = verbunden. */
static bool try_connect(const wifi_network_t *net, uint32_t timeout_ms)
{
    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, net->ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, net->password, sizeof(wc.sta.password) - 1);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));

    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    ESP_LOGI(TAG, "Verbinde mit '%s' …", net->ssid);
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));

    if (bits & WIFI_CONNECTED_BIT) return true;
    esp_wifi_disconnect();
    return false;
}

static void start_mdns(const char *hostname, uint16_t port)
{
    if (mdns_init() != ESP_OK) {
        ESP_LOGW(TAG, "mDNS-Init fehlgeschlagen");
        return;
    }
    mdns_hostname_set(hostname);
    mdns_instance_name_set("CAN Dashboard");
    mdns_service_add(NULL, "_http", "_tcp", port, NULL, 0);
    ESP_LOGI(TAG, "mDNS aktiv: http://%s.local", hostname);
}

esp_err_t waveshare_wifi_port_start(const wifi_credentials_t *creds,
                                    uint32_t connect_timeout_ms)
{
    if (!creds || creds->network_count == 0) return ESP_ERR_INVALID_ARG;

    s_wifi_events = xEventGroupCreate();
    s_status = WIFI_PORT_CONNECTING;
    ESP_ERROR_CHECK(wifi_init_once());

    for (uint8_t i = 0; i < creds->network_count; i++) {
        if (try_connect(&creds->networks[i], connect_timeout_ms)) {
            s_status = WIFI_PORT_CONNECTED;
            strncpy(s_ssid, creds->networks[i].ssid, sizeof(s_ssid) - 1);
            strncpy(s_hostname, creds->hostname, sizeof(s_hostname) - 1);
            ESP_LOGI(TAG, "Verbunden mit '%s', IP %s",
                     creds->networks[i].ssid, s_ip);
            start_mdns(creds->hostname, CONFIG_DASHBOARD_HTTP_PORT);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "'%s' nicht erreichbar", creds->networks[i].ssid);
    }

    s_status = WIFI_PORT_FAILED;
    ESP_LOGW(TAG, "Kein WLAN erreichbar – Upload nicht verfügbar");
    return ESP_FAIL;
}

wifi_port_status_t waveshare_wifi_port_get_status(void) { return s_status; }
const char *waveshare_wifi_port_get_ip(void) { return s_ip; }
const char *waveshare_wifi_port_get_ssid(void)     { return s_ssid; }
const char *waveshare_wifi_port_get_hostname(void) { return s_hostname; }

int waveshare_wifi_port_get_rssi(void)
{
    if (s_status != WIFI_PORT_CONNECTED) return 0;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return 0;
    return ap.rssi;
}
