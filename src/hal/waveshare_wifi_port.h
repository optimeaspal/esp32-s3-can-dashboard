#pragma once

/*
 * HAL: WLAN im Station-Modus (STA). Verbindet sich der Reihe nach mit den in
 * wifi.json hinterlegten Netzwerken und registriert einen mDNS-Hostnamen.
 *
 * AP-Modus ist als spätere Erweiterung vorgesehen (hier noch nicht implementiert).
 */

#include "esp_err.h"
#include "app/wifi_credentials.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    WIFI_PORT_IDLE = 0,    /* noch nicht gestartet           */
    WIFI_PORT_CONNECTING,  /* Verbindungsaufbau läuft        */
    WIFI_PORT_CONNECTED,   /* verbunden, IP erhalten         */
    WIFI_PORT_FAILED       /* kein Netz erreichbar           */
} wifi_port_status_t;

/*
 * Initialisiert NVS, den TCP/IP-Stack und den WiFi-Treiber im STA-Modus und
 * versucht, sich der Reihe nach mit creds->networks zu verbinden. Blockiert bis
 * zur ersten erfolgreichen Verbindung oder bis alle Netze (je connect_timeout_ms)
 * fehlgeschlagen sind. Bei Erfolg wird mDNS (<hostname>.local) registriert und
 * die IP geloggt.
 *
 * @return ESP_OK wenn verbunden, ESP_FAIL wenn kein Netz erreichbar war.
 */
esp_err_t waveshare_wifi_port_start(const wifi_credentials_t *creds,
                                    uint32_t connect_timeout_ms);

/* Aktueller Verbindungsstatus (für späteres Status-Icon nutzbar). */
wifi_port_status_t waveshare_wifi_port_get_status(void);

/* Liefert die zuletzt erhaltene IP als String ("0.0.0.0" wenn keine). */
const char *waveshare_wifi_port_get_ip(void);

#ifdef __cplusplus
}
#endif
