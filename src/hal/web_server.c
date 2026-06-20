#include "web_server.h"
#include "web_fallback_html.h"
#include "waveshare_sd_port.h"
#include "app/config_types.h"
#include "app/config_loader.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "web_server";

#define WWW_DIR        "/sdcard/www"
#define UPLOAD_MAX     16384                       /* = s_json_buf in main.c       */
#define TMP_PATH       "/sdcard/dashboard.json.tmp"
#define DEST_PATH      CONFIG_DASHBOARD_JSON_PATH  /* "/sdcard/dashboard.json"     */

/* Asset-Lesepuffer: editor-core.js + app.js überschreiten die alten 8 KB.
 * Einzelne www-Dateien dürfen damit bis WWW_FILE_MAX-1 Bytes groß sein. */
#define WWW_FILE_MAX   (48 * 1024)

/* Skelett, wenn noch keine dashboard.json existiert (Offline-Erststart). */
static const char SKELETON_JSON[] =
    "{\"version\":\"1.0\",\"signals\":[],"
    "\"pages\":[{\"title\":\"Seite 1\",\"widgets\":[]}],\"tx_commands\":[]}";

/* Upload-Puffer + validierender Parse-Scratch. static: nicht auf den Task-Stack. */
static char               s_upload[UPLOAD_MAX + 1];
static dashboard_config_t s_validate_cfg;

/* MIME-Typ aus Dateiendung. */
static const char *mime_for(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (!strcmp(dot, ".html")) return "text/html";
    if (!strcmp(dot, ".js"))   return "application/javascript";
    if (!strcmp(dot, ".css"))  return "text/css";
    if (!strcmp(dot, ".json")) return "application/json";
    return "application/octet-stream";
}

/* Reboot verzögert nach dem Senden der HTTP-Antwort. */
static void reboot_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
}

/* GET-Handler für statische Assets + Root mit Flash-Fallback. */
static esp_err_t get_handler(httpd_req_t *req)
{
    /* Path-Traversal verhindern: ".." dürfte sonst /sdcard/www verlassen. */
    if (strstr(req->uri, "..")) {
        httpd_resp_send_404(req);
        return ESP_OK;
    }

    /* Browser fragt automatisch /favicon.ico an – still mit 204 beantworten,
     * statt bei jedem Seitenaufruf eine SD-Fehlersuche samt ERROR-Log auszulösen. */
    if (!strcmp(req->uri, "/favicon.ico")) {
        httpd_resp_set_status(req, "204 No Content");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    char path[160];
    if (!strcmp(req->uri, "/"))
        snprintf(path, sizeof(path), "%s/index.html", WWW_DIR);
    else
        snprintf(path, sizeof(path), "%s%.140s", WWW_DIR, req->uri);

    static char filebuf[WWW_FILE_MAX];
    size_t len = 0;
    esp_err_t rc = waveshare_sd_read_file(path, filebuf, sizeof(filebuf), &len);

    if (rc != ESP_OK) {
        /* Kein Asset auf SD: nur für Root das eingebettete Fallback liefern. */
        if (!strcmp(req->uri, "/")) {
            httpd_resp_set_type(req, "text/html");
            return httpd_resp_send(req, WEB_FALLBACK_HTML,
                                   strlen(WEB_FALLBACK_HTML));
        }
        httpd_resp_send_404(req);
        return ESP_OK;
    }

    httpd_resp_set_type(req, mime_for(path));
    return httpd_resp_send(req, filebuf, len);
}

/* GET /api/config: aktuelle dashboard.json liefern; fehlt sie → Skelett. */
static esp_err_t get_config_handler(httpd_req_t *req)
{
    static char cfgbuf[UPLOAD_MAX + 1];
    size_t len = 0;
    esp_err_t rc = waveshare_sd_read_file(DEST_PATH, cfgbuf, sizeof(cfgbuf), &len);

    httpd_resp_set_type(req, "application/json");
    if (rc == ESP_OK) {
        return httpd_resp_send(req, cfgbuf, len);
    }
    /* Nicht gefunden o. ä.: leeres Skelett, damit der Editor sauber startet. */
    ESP_LOGI(TAG, "GET /api/config: keine dashboard.json, sende Skelett");
    return httpd_resp_send(req, SKELETON_JSON, strlen(SKELETON_JSON));
}

/* POST /api/config: Raw-Body → temp → validieren → rename → Neustart. */
static esp_err_t post_config_handler(httpd_req_t *req)
{
    if (req->content_len > UPLOAD_MAX) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_sendstr(req, "Datei zu groß (max. 16 KB)");
        return ESP_OK;
    }

    /* Body (kommt in Häppchen) vollständig einlesen. */
    int received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, s_upload + received,
                               req->content_len - received);
        if (r <= 0) {
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_sendstr(req, "Empfang abgebrochen");
            return ESP_OK;
        }
        received += r;
    }
    s_upload[received] = '\0';

    /* Validieren mit demselben Parser wie beim Boot. */
    char err[160] = {0};
    esp_err_t rc = config_loader_parse(s_upload, &s_validate_cfg, err, sizeof(err));
    if (rc != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, err[0] ? err : "Ungültige Konfiguration");
        return ESP_OK;
    }

    /* Temp schreiben, dann atomar ersetzen. */
    if (waveshare_sd_write_file(TMP_PATH, s_upload, received) != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "SD-Schreibfehler");
        return ESP_OK;
    }
    if (waveshare_sd_rename(TMP_PATH, DEST_PATH) != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Ersetzen fehlgeschlagen");
        return ESP_OK;
    }

    httpd_resp_sendstr(req, "Konfiguration übernommen, Neustart …");
    ESP_LOGI(TAG, "Neue dashboard.json übernommen, Neustart");
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}

esp_err_t web_server_start(uint16_t port)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.uri_match_fn = httpd_uri_match_wildcard;  /* Wildcard-Asset-Pfade */
    config.stack_size = 8192;  /* config_loader_parse (cJSON, rekursiv) läuft
                                  im httpd-Task → Default 4 KB reicht nicht */

    httpd_handle_t server = NULL;
    esp_err_t rc = httpd_start(&server, &config);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start fehlgeschlagen: %s", esp_err_to_name(rc));
        return rc;
    }

    httpd_uri_t post_cfg = {
        .uri = "/api/config", .method = HTTP_POST,
        .handler = post_config_handler,
    };
    httpd_register_uri_handler(server, &post_cfg);

    httpd_uri_t get_cfg = {
        .uri = "/api/config", .method = HTTP_GET,
        .handler = get_config_handler,
    };
    httpd_register_uri_handler(server, &get_cfg);

    httpd_uri_t get_any = {
        .uri = "/*", .method = HTTP_GET,
        .handler = get_handler,
    };
    httpd_register_uri_handler(server, &get_any);

    ESP_LOGI(TAG, "Webserver gestartet auf Port %u", port);
    return ESP_OK;
}
