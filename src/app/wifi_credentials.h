#pragma once

/*
 * Parser: JSON-String (wifi.json) → wifi_credentials_t.
 *
 * Reine Anwendungslogik, keine ESP-IDF-/Hardware-Aufrufe. Nativ testbar via
 * pio test -e native (esp_err.h über test/stubs, cJSON als native-Library).
 */

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MAX_NETWORKS   8
#define WIFI_SSID_LEN       33   /* 32 Zeichen + NUL                       */
#define WIFI_PASSWORD_LEN   65   /* WPA2 max. 63 Zeichen + NUL             */
#define WIFI_HOSTNAME_LEN   33

typedef struct
{
    char ssid[WIFI_SSID_LEN];
    char password[WIFI_PASSWORD_LEN];  /* leer = offenes Netz */
} wifi_network_t;

typedef struct
{
    char           hostname[WIFI_HOSTNAME_LEN];  /* mDNS-Name; Default "dashboard" */
    wifi_network_t networks[WIFI_MAX_NETWORKS];
    uint8_t        network_count;
} wifi_credentials_t;

/*
 * Parst wifi.json in wifi_credentials_t.
 *
 * @return ESP_OK                bei Erfolg,
 *         ESP_ERR_INVALID_ARG   bei Syntaxfehler oder fehlendem networks-Array,
 *         ESP_ERR_INVALID_SIZE  wenn mehr als WIFI_MAX_NETWORKS Netze angegeben sind.
 *
 * Fehlt "hostname", wird "dashboard" gesetzt. Fehlt "password" eines Netzes,
 * gilt es als offenes Netz (leerer String). Netze ohne "ssid" werden übersprungen.
 */
esp_err_t wifi_credentials_parse(const char *json_str,
                                 wifi_credentials_t *out,
                                 char *err_buf, size_t err_len);

#ifdef __cplusplus
}
#endif
