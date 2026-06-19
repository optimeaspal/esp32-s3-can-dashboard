#include "wifi_credentials.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static void set_err(char *buf, size_t len, const char *msg)
{
    if (buf && len) snprintf(buf, len, "%s", msg);
}

/* Kopiert einen JSON-String-Wert in ein festes Zielfeld (NUL-terminiert). */
static void copy_str(char *dst, size_t dst_len, const cJSON *item)
{
    dst[0] = '\0';
    if (cJSON_IsString(item) && item->valuestring)
        snprintf(dst, dst_len, "%s", item->valuestring);
}

esp_err_t wifi_credentials_parse(const char *json_str,
                                 wifi_credentials_t *out,
                                 char *err_buf, size_t err_len)
{
    if (!json_str || !out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        set_err(err_buf, err_len, "JSON-Syntaxfehler in wifi.json");
        return ESP_ERR_INVALID_ARG;
    }

    /* hostname (optional, Default "dashboard") */
    const cJSON *host = cJSON_GetObjectItemCaseSensitive(root, "hostname");
    if (cJSON_IsString(host) && host->valuestring && host->valuestring[0])
        snprintf(out->hostname, sizeof(out->hostname), "%s", host->valuestring);
    else
        snprintf(out->hostname, sizeof(out->hostname), "%s", "dashboard");

    /* networks (Pflicht, nicht leer) */
    const cJSON *nets = cJSON_GetObjectItemCaseSensitive(root, "networks");
    if (!cJSON_IsArray(nets) || cJSON_GetArraySize(nets) == 0) {
        set_err(err_buf, err_len, "wifi.json: 'networks' fehlt oder ist leer");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    if (cJSON_GetArraySize(nets) > WIFI_MAX_NETWORKS) {
        set_err(err_buf, err_len, "wifi.json: zu viele Netzwerke");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_SIZE;
    }

    const cJSON *net = NULL;
    cJSON_ArrayForEach(net, nets) {
        const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(net, "ssid");
        if (!cJSON_IsString(ssid) || !ssid->valuestring || !ssid->valuestring[0])
            continue;  /* Netz ohne ssid überspringen */

        wifi_network_t *n = &out->networks[out->network_count];
        copy_str(n->ssid, sizeof(n->ssid), ssid);
        copy_str(n->password, sizeof(n->password),
                 cJSON_GetObjectItemCaseSensitive(net, "password"));
        out->network_count++;
    }

    cJSON_Delete(root);

    if (out->network_count == 0) {
        set_err(err_buf, err_len, "wifi.json: kein gültiges Netzwerk (ssid fehlt)");
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}
