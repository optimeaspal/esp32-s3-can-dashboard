# WLAN-Upload der dashboard.json — Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eine neue `dashboard.json` per Browser über lokales WLAN aufs Gerät laden, validieren-vor-ersetzen, dann Neustart.

**Architecture:** STA-WLAN aus `wifi.json` (SD) → Hintergrund-Task verbindet + registriert mDNS (`dashboard.local`) → `esp_http_server` liefert Web-Assets von SD (Flash-Fallback) und nimmt `POST /api/config` als Raw-Body entgegen → temp-Datei → `config_loader_parse` → bei Erfolg `rename` + `esp_restart`. Schichten gemäß Constitution: portable/testbare Logik in `app/`, ESP-IDF-Glue in `hal/`.

**Tech Stack:** C99, ESP-IDF ≥5.1 (`esp_wifi`, `esp_netif`, `nvs_flash`, `esp_http_server`, `espressif/mdns`), cJSON, FATFS, PlatformIO; native Unit-Tests via Unity (`pio test -e native`).

**Spec:** `docs/superpowers/specs/2026-06-19-wlan-dashboard-upload-design.md`
**Branch:** `002-wlan-config-upload` (bereits aktiv)

---

## File Structure

**Neu (App, nativ testbar):**
- `src/app/wifi_credentials.h` — Datenmodell + Parser-Deklaration für `wifi.json`
- `src/app/wifi_credentials.c` — cJSON-Parser (keine ESP-IDF-Abhängigkeit)
- `test/test_wifi_credentials/test_wifi_credentials.c` — Unity-Tests
- `test/fixtures/wifi_valid.json`, `wifi_open_network.json`, `wifi_no_hostname.json`, `wifi_empty.json`, `wifi_invalid_syntax.json` — Test-Fixtures

**Neu (HAL, ESP-IDF/Hardware):**
- `src/hal/waveshare_wifi_port.h` / `.c` — STA-Verbindung, AP-Liste durchprobieren, mDNS, Status
- `src/hal/web_server.h` / `.c` — `esp_http_server`: statische Assets + `POST /api/config`
- `src/hal/web_fallback_html.h` — eingebettetes Fallback-HTML (C-String)

**Neu (Web-Assets für SD):**
- `examples/www/index.html`, `examples/www/app.js`, `examples/www/style.css`
- `examples/wifi.json` — Beispiel-Zugangsdaten

**Modifiziert:**
- `src/hal/waveshare_sd_port.h` / `.c` — `waveshare_sd_write_file()` + `waveshare_sd_rename()` ergänzen
- `src/Kconfig.projbuild` — Menü „WLAN / Upload"
- `src/idf_component.yml` — `espressif/mdns` ergänzen
- `src/main.c` — WLAN/Webserver-Hintergrund-Task nach Dashboard-Aufbau starten

---

## Task 1: wifi_credentials — Datenmodell & Parser (TDD, nativ)

Reine cJSON-Parselogik, analog zu `config_loader`. Wird auf dem PC getestet.

**Files:**
- Create: `src/app/wifi_credentials.h`
- Create: `src/app/wifi_credentials.c`
- Create: `test/test_wifi_credentials/test_wifi_credentials.c`
- Create: `test/fixtures/wifi_valid.json`, `wifi_open_network.json`, `wifi_no_hostname.json`, `wifi_empty.json`, `wifi_invalid_syntax.json`

- [ ] **Step 1: Header mit Datenmodell und API anlegen**

Create `src/app/wifi_credentials.h`:

```c
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
```

- [ ] **Step 2: Fixtures anlegen**

Create `test/fixtures/wifi_valid.json`:

```json
{
  "version": "1.0",
  "hostname": "dashboard",
  "networks": [
    { "ssid": "Werkstatt-WLAN",  "password": "geheim123" },
    { "ssid": "Hotspot-Patrick", "password": "anderesPasswort" }
  ]
}
```

Create `test/fixtures/wifi_open_network.json`:

```json
{
  "version": "1.0",
  "hostname": "dashboard",
  "networks": [
    { "ssid": "OffenesNetz" }
  ]
}
```

Create `test/fixtures/wifi_no_hostname.json`:

```json
{
  "version": "1.0",
  "networks": [
    { "ssid": "NurEinNetz", "password": "pw" }
  ]
}
```

Create `test/fixtures/wifi_empty.json`:

```json
{
  "version": "1.0",
  "networks": []
}
```

Create `test/fixtures/wifi_invalid_syntax.json`:

```json
{ "version": "1.0", "networks": [ { "ssid": "x" ]
```

- [ ] **Step 3: Failing test schreiben**

Create `test/test_wifi_credentials/test_wifi_credentials.c`:

```c
/*
 * Unit-Tests für wifi_credentials_parse().  Native: pio test -e native
 * Fixtures aus test/fixtures/ (gleicher Lade-Mechanismus wie test_config_loader).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "unity.h"

#include "../../src/app/wifi_credentials.h"
#include "../../src/app/wifi_credentials.c"

void setUp(void) {}
void tearDown(void) {}

static wifi_credentials_t g_wc;
static char               g_err[160];

static const char *load_fixture(const char *name)
{
    static char buf[8192];
    const char *prefixes[] = {
        "test/fixtures/", "../../test/fixtures/", "fixtures/", "./test/fixtures/"
    };
    for (size_t p = 0; p < sizeof(prefixes) / sizeof(prefixes[0]); p++) {
        char path[256];
        snprintf(path, sizeof(path), "%s%s", prefixes[p], name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        buf[n] = '\0';
        return buf;
    }
    TEST_FAIL_MESSAGE("Fixture nicht gefunden – Arbeitsverzeichnis prüfen");
    return NULL;
}

void test_parse_valid(void)
{
    const char *json = load_fixture("wifi_valid.json");
    esp_err_t rc = wifi_credentials_parse(json, &g_wc, g_err, sizeof(g_err));
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, rc, g_err);
    TEST_ASSERT_EQUAL_UINT8(2, g_wc.network_count);
    TEST_ASSERT_EQUAL_STRING("dashboard", g_wc.hostname);
    TEST_ASSERT_EQUAL_STRING("Werkstatt-WLAN", g_wc.networks[0].ssid);
    TEST_ASSERT_EQUAL_STRING("geheim123", g_wc.networks[0].password);
    TEST_ASSERT_EQUAL_STRING("Hotspot-Patrick", g_wc.networks[1].ssid);
}

void test_open_network_has_empty_password(void)
{
    const char *json = load_fixture("wifi_open_network.json");
    esp_err_t rc = wifi_credentials_parse(json, &g_wc, g_err, sizeof(g_err));
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, rc, g_err);
    TEST_ASSERT_EQUAL_UINT8(1, g_wc.network_count);
    TEST_ASSERT_EQUAL_STRING("", g_wc.networks[0].password);
}

void test_default_hostname_when_missing(void)
{
    const char *json = load_fixture("wifi_no_hostname.json");
    esp_err_t rc = wifi_credentials_parse(json, &g_wc, g_err, sizeof(g_err));
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, rc, g_err);
    TEST_ASSERT_EQUAL_STRING("dashboard", g_wc.hostname);
}

void test_empty_networks_is_error(void)
{
    const char *json = load_fixture("wifi_empty.json");
    esp_err_t rc = wifi_credentials_parse(json, &g_wc, g_err, sizeof(g_err));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, rc);
}

void test_invalid_syntax_is_error(void)
{
    const char *json = load_fixture("wifi_invalid_syntax.json");
    esp_err_t rc = wifi_credentials_parse(json, &g_wc, g_err, sizeof(g_err));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, rc);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_valid);
    RUN_TEST(test_open_network_has_empty_password);
    RUN_TEST(test_default_hostname_when_missing);
    RUN_TEST(test_empty_networks_is_error);
    RUN_TEST(test_invalid_syntax_is_error);
    return UNITY_END();
}
```

- [ ] **Step 4: Test ausführen, Fehlschlag verifizieren**

Run: `pio test -e native -f test_wifi_credentials`
Expected: FAIL — Linker/Compile-Fehler, weil `wifi_credentials.c` noch nicht existiert.

- [ ] **Step 5: Parser implementieren**

Create `src/app/wifi_credentials.c`:

```c
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
```

- [ ] **Step 6: Test ausführen, Erfolg verifizieren**

Run: `pio test -e native -f test_wifi_credentials`
Expected: PASS (5 Tests). Falls der Runner alle Suiten baut, auch `pio test -e native` grün halten.

- [ ] **Step 7: Commit**

```bash
git add src/app/wifi_credentials.h src/app/wifi_credentials.c \
        test/test_wifi_credentials/ test/fixtures/wifi_*.json
git commit -m "feat: wifi_credentials-Parser für wifi.json (nativ getestet)"
```

---

## Task 2: SD-Port um Schreiben & Umbenennen erweitern

`waveshare_sd_port` kann bisher nur lesen. Für validieren-vor-ersetzen brauchen wir Schreiben in eine temp-Datei und atomares Umbenennen.

**Files:**
- Modify: `src/hal/waveshare_sd_port.h`
- Modify: `src/hal/waveshare_sd_port.c`

- [ ] **Step 1: Header um zwei Funktionen erweitern**

In `src/hal/waveshare_sd_port.h`, vor `#ifdef __cplusplus`-Abschluss einfügen (nach `waveshare_sd_read_file`):

```c
/*
 * Schreibt buf (len Bytes) vollständig in eine Datei (überschreibt vorhandene).
 *
 * @return ESP_OK bei Erfolg, ESP_FAIL bei Schreib-/Öffnen-Fehler.
 */
esp_err_t waveshare_sd_write_file(const char *path, const char *buf, size_t len);

/*
 * Benennt eine Datei um (atomar ersetzen). Ein vorhandenes Ziel wird zuvor
 * entfernt (FATFS-rename schlägt sonst fehl, wenn das Ziel existiert).
 *
 * @return ESP_OK bei Erfolg, ESP_FAIL sonst.
 */
esp_err_t waveshare_sd_rename(const char *from_path, const char *to_path);
```

- [ ] **Step 2: Implementierung ergänzen**

Am Ende von `src/hal/waveshare_sd_port.c` (vor evtl. abschließenden Kommentaren) einfügen. Stelle sicher, dass `#include <stdio.h>` und `#include "esp_log.h"` oben vorhanden sind (sonst ergänzen):

```c
esp_err_t waveshare_sd_write_file(const char *path, const char *buf, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Kann %s nicht zum Schreiben öffnen", path);
        return ESP_FAIL;
    }
    size_t written = fwrite(buf, 1, len, f);
    fclose(f);
    if (written != len) {
        ESP_LOGE(TAG, "Schreibfehler %s (%u/%u Bytes)",
                 path, (unsigned)written, (unsigned)len);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t waveshare_sd_rename(const char *from_path, const char *to_path)
{
    remove(to_path);  /* FATFS: rename schlägt fehl, wenn Ziel existiert */
    if (rename(from_path, to_path) != 0) {
        ESP_LOGE(TAG, "rename %s -> %s fehlgeschlagen", from_path, to_path);
        return ESP_FAIL;
    }
    return ESP_OK;
}
```

Falls `TAG` in der Datei anders heißt, den dort definierten Logger-Tag verwenden (mit Grep prüfen: `static const char *TAG`).

- [ ] **Step 3: Build verifizieren**

Run: `pio run -e esp32-s3-touch-lcd-7`
Expected: SUCCESS (Compile/Link ohne Fehler). Die neuen Funktionen werden noch nicht aufgerufen — reiner Build-Check.

- [ ] **Step 4: Commit**

```bash
git add src/hal/waveshare_sd_port.h src/hal/waveshare_sd_port.c
git commit -m "feat: SD-Port um write_file und rename erweitern"
```

---

## Task 3: WiFi-HAL — STA verbinden, AP-Liste, mDNS, Status

ESP-IDF-Glue. Nicht nativ testbar → Verifikation auf Hardware (Task 8). Hier nur Build-Korrektheit pro Schritt.

**Files:**
- Create: `src/hal/waveshare_wifi_port.h`
- Create: `src/hal/waveshare_wifi_port.c`
- Modify: `src/idf_component.yml` (mDNS-Komponente)

- [ ] **Step 1: mDNS-Komponente als Abhängigkeit eintragen**

In `src/idf_component.yml` unter `dependencies:` ergänzen (gleiche Einrückung wie `esp_lcd_touch_gt911`):

```yaml
  # mDNS: Gerät unter <hostname>.local erreichbar machen
  espressif/mdns: "^1.2"
```

- [ ] **Step 2: Header anlegen**

Create `src/hal/waveshare_wifi_port.h`:

```c
#pragma once

/*
 * HAL: WLAN im Station-Modus (STA). Verbindet sich der Reihe nach mit den in
 * wifi.json hinterlegten Netzwerken und registriert einen mDNS-Hostnamen.
 *
 * AP-Modus ist als spätere Erweiterung vorgesehen (hier noch nicht implementiert).
 */

#include "esp_err.h"
#include "wifi_credentials.h"

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
```

- [ ] **Step 3: Implementierung anlegen**

Create `src/hal/waveshare_wifi_port.c`:

```c
#include "waveshare_wifi_port.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "wifi_port";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t  s_wifi_events;
static wifi_port_status_t  s_status = WIFI_PORT_IDLE;
static char                s_ip[16] = "0.0.0.0";
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
    snprintf((char *)wc.sta.ssid, sizeof(wc.sta.ssid), "%s", net->ssid);
    snprintf((char *)wc.sta.password, sizeof(wc.sta.password), "%s", net->password);
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

static void start_mdns(const char *hostname)
{
    if (mdns_init() != ESP_OK) {
        ESP_LOGW(TAG, "mDNS-Init fehlgeschlagen");
        return;
    }
    mdns_hostname_set(hostname);
    mdns_instance_name_set("CAN Dashboard");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
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
            ESP_LOGI(TAG, "Verbunden mit '%s', IP %s",
                     creds->networks[i].ssid, s_ip);
            start_mdns(creds->hostname);
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
```

- [ ] **Step 4: Build verifizieren**

Run: `pio run -e esp32-s3-touch-lcd-7`
Expected: SUCCESS. Beim ersten Build lädt der Component-Manager `espressif/mdns` herunter (Netzverbindung des Build-PCs nötig). Noch kein Aufruf der Funktionen — reiner Compile/Link.

- [ ] **Step 5: Commit**

```bash
git add src/hal/waveshare_wifi_port.h src/hal/waveshare_wifi_port.c src/idf_component.yml
git commit -m "feat: WiFi-HAL (STA-Verbindung, AP-Liste, mDNS, Status)"
```

---

## Task 4: Web-Server-HAL — statische Assets + POST /api/config

`esp_http_server`: liefert `/www/*` von der SD (Flash-Fallback für `/`), nimmt den Upload als Raw-Body, validiert ihn mit `config_loader` und startet bei Erfolg neu.

**Files:**
- Create: `src/hal/web_fallback_html.h`
- Create: `src/hal/web_server.h`
- Create: `src/hal/web_server.c`

- [ ] **Step 1: Fallback-HTML als C-String anlegen**

Create `src/hal/web_fallback_html.h`:

```c
#pragma once

/*
 * Minimale Upload-Seite, in den Flash eingebettet. Wird ausgeliefert, wenn auf
 * der SD-Karte kein /sdcard/www/index.html liegt. Sendet die Datei per Raw-POST
 * (kein multipart) an /api/config.
 */
static const char WEB_FALLBACK_HTML[] =
"<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>Dashboard-Upload</title></head><body style=\"font-family:sans-serif;max-width:40em;margin:2em auto\">"
"<h1>Dashboard-Konfiguration hochladen</h1>"
"<p>Wähle eine <code>dashboard.json</code> und lade sie hoch. Das Gerät prüft "
"die Datei und startet bei Erfolg neu.</p>"
"<input type=\"file\" id=\"f\" accept=\".json,application/json\">"
"<button id=\"b\">Hochladen</button>"
"<pre id=\"out\"></pre>"
"<script>"
"document.getElementById('b').onclick=async()=>{"
"const o=document.getElementById('out');const f=document.getElementById('f').files[0];"
"if(!f){o.textContent='Bitte zuerst eine Datei wählen.';return;}"
"o.textContent='Lade hoch …';"
"try{const r=await fetch('/api/config',{method:'POST',body:f});"
"const t=await r.text();"
"o.textContent=(r.ok?'OK: ':'Fehler '+r.status+': ')+t;}"
"catch(e){o.textContent='Netzwerkfehler: '+e;}};"
"</script></body></html>";
```

- [ ] **Step 2: Header anlegen**

Create `src/hal/web_server.h`:

```c
#pragma once

/*
 * HAL: HTTP-Server für den drahtlosen Upload der dashboard.json.
 *   GET  /              → /sdcard/www/index.html  (Fallback: eingebettetes HTML)
 *   GET  /<asset>       → /sdcard/www/<asset>      (app.js, style.css, …)
 *   POST /api/config    → Raw-Body → temp → validieren → rename → Neustart
 */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Startet den HTTP-Server auf Port `port`. */
esp_err_t web_server_start(uint16_t port);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 3: Implementierung anlegen**

Create `src/hal/web_server.c`:

```c
#include "web_server.h"
#include "web_fallback_html.h"
#include "waveshare_sd_port.h"
#include "config_types.h"
#include "config_loader.h"
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
    char path[160];
    if (!strcmp(req->uri, "/"))
        snprintf(path, sizeof(path), "%s/index.html", WWW_DIR);
    else
        snprintf(path, sizeof(path), "%s%s", WWW_DIR, req->uri);

    static char filebuf[8192];
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
    config.uri_match_fn = httpd_uri_match_wildcard;  /* für /* Asset-Pfade */
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

    httpd_uri_t get_any = {
        .uri = "/*", .method = HTTP_GET,
        .handler = get_handler,
    };
    httpd_register_uri_handler(server, &get_any);

    ESP_LOGI(TAG, "Webserver gestartet auf Port %u", port);
    return ESP_OK;
}
```

- [ ] **Step 4: Build verifizieren**

Run: `pio run -e esp32-s3-touch-lcd-7`
Expected: SUCCESS. Noch kein Aufruf von `web_server_start` — reiner Compile/Link.

- [ ] **Step 5: Commit**

```bash
git add src/hal/web_server.h src/hal/web_server.c src/hal/web_fallback_html.h
git commit -m "feat: HTTP-Server (Asset-Serving + validierender Raw-POST-Upload)"
```

---

## Task 5: Kconfig-Optionen ergänzen

**Files:**
- Modify: `src/Kconfig.projbuild`

- [ ] **Step 1: Menü „WLAN / Upload" einfügen**

In `src/Kconfig.projbuild`, direkt vor der abschließenden `endmenu`-Zeile (die das äußere „CAN Dashboard Configuration"-Menü schließt), einfügen:

```
    menu "WLAN / Upload"
        config DASHBOARD_WIFI_ENABLE
            bool "Drahtlosen Upload via WLAN aktivieren"
            default y
            help
                Verbindet sich beim Start (Hintergrund-Task) mit den in
                /sdcard/wifi.json hinterlegten Netzen und startet einen
                HTTP-Server zum Hochladen der dashboard.json.

        config DASHBOARD_WIFI_PATH
            string "Pfad zur WLAN-Konfigurationsdatei auf der SD-Karte"
            default "/sdcard/wifi.json"
            depends on DASHBOARD_WIFI_ENABLE

        config DASHBOARD_HTTP_PORT
            int "HTTP-Server Port"
            default 80
            depends on DASHBOARD_WIFI_ENABLE

        config DASHBOARD_WIFI_CONNECT_TIMEOUT_MS
            int "Verbindungs-Timeout pro Netzwerk (ms)"
            default 8000
            depends on DASHBOARD_WIFI_ENABLE
    endmenu
```

- [ ] **Step 2: Build verifizieren**

Run: `pio run -e esp32-s3-touch-lcd-7`
Expected: SUCCESS. Die neuen `CONFIG_DASHBOARD_*`-Symbole sind ab jetzt in `sdkconfig`/`sdkconfig.h` verfügbar.

- [ ] **Step 3: Commit**

```bash
git add src/Kconfig.projbuild
git commit -m "feat: Kconfig-Optionen für WLAN-Upload (Feature-Flag, Pfad, Port, Timeout)"
```

---

## Task 6: Integration in main.c — Hintergrund-Task

WLAN + Webserver dürfen den Dashboard-Start (< 3 s) nicht blockieren → eigener Task, gestartet **nach** dem Dashboard-Aufbau.

**Files:**
- Modify: `src/main.c`

- [ ] **Step 1: Includes + Netzwerk-Task ergänzen**

In `src/main.c` bei den Includes ergänzen (nach `#include "ui/dashboard.h"`):

```c
#if CONFIG_DASHBOARD_WIFI_ENABLE
#include "hal/waveshare_wifi_port.h"
#include "hal/web_server.h"
#include "app/wifi_credentials.h"
#endif
```

Vor `app_main` (nach `show_error_screen`) einfügen:

```c
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
```

- [ ] **Step 2: Task am Ende von app_main starten**

In `src/main.c`, unmittelbar vor der letzten Log-Zeile `ESP_LOGI(TAG, "Initialisierung abgeschlossen – Dashboard läuft");` einfügen:

```c
#if CONFIG_DASHBOARD_WIFI_ENABLE
    /* 5 KB Stack: WiFi/HTTP-Init + cJSON-Parse. Core 0, niedrige Priorität. */
    xTaskCreatePinnedToCore(network_task, "network", 5120, NULL, 3, NULL, 0);
#endif
```

- [ ] **Step 3: Build verifizieren**

Run: `pio run -e esp32-s3-touch-lcd-7`
Expected: SUCCESS. Firmware enthält jetzt den vollständigen WLAN-Upload-Pfad.

- [ ] **Step 4: Commit**

```bash
git add src/main.c
git commit -m "feat: WLAN/Upload-Hintergrund-Task in main.c integrieren"
```

---

## Task 7: Web-Assets & Beispiel-Dateien für die SD

Reichere Upload-Seite (von SD ausgeliefert) + Beispiel-`wifi.json`. Die Seite nutzt denselben Raw-POST wie das Fallback und ist später zum Konfigurator ausbaubar.

**Files:**
- Create: `examples/www/index.html`
- Create: `examples/www/app.js`
- Create: `examples/www/style.css`
- Create: `examples/wifi.json`

- [ ] **Step 1: index.html anlegen**

Create `examples/www/index.html`:

```html
<!doctype html>
<html lang="de">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>CAN Dashboard – Konfiguration</title>
  <link rel="stylesheet" href="style.css">
</head>
<body>
  <main>
    <h1>CAN Dashboard</h1>
    <section class="card">
      <h2>dashboard.json hochladen</h2>
      <p>Datei wählen und hochladen. Das Gerät prüft die Konfiguration und
         startet bei Erfolg neu.</p>
      <input type="file" id="file" accept=".json,application/json">
      <button id="upload">Hochladen</button>
      <pre id="status"></pre>
    </section>
  </main>
  <script src="app.js"></script>
</body>
</html>
```

- [ ] **Step 2: app.js anlegen**

Create `examples/www/app.js`:

```javascript
// Sendet die gewählte dashboard.json als Raw-Body an /api/config.
// (Kein multipart/form-data – der Server liest den Body direkt als Datei.)
document.getElementById('upload').addEventListener('click', async () => {
  const out = document.getElementById('status');
  const file = document.getElementById('file').files[0];
  if (!file) { out.textContent = 'Bitte zuerst eine Datei wählen.'; return; }
  out.textContent = 'Lade hoch …';
  try {
    const res = await fetch('/api/config', { method: 'POST', body: file });
    const text = await res.text();
    out.textContent = (res.ok ? 'OK: ' : 'Fehler ' + res.status + ': ') + text;
  } catch (e) {
    out.textContent = 'Netzwerkfehler: ' + e;
  }
});
```

- [ ] **Step 3: style.css anlegen**

Create `examples/www/style.css`:

```css
body { font-family: system-ui, sans-serif; margin: 0; background: #14142a; color: #ecf0f1; }
main { max-width: 40em; margin: 2em auto; padding: 0 1em; }
h1 { font-weight: 600; }
.card { background: #1f1f3a; border-radius: 12px; padding: 1.5em; }
input[type=file] { display: block; margin: 1em 0; color: #ecf0f1; }
button { background: #00aaff; color: #fff; border: 0; border-radius: 8px;
         padding: 0.6em 1.4em; font-size: 1em; cursor: pointer; }
button:hover { background: #0088cc; }
pre { white-space: pre-wrap; margin-top: 1em; min-height: 1.5em; }
```

- [ ] **Step 4: Beispiel-wifi.json anlegen**

Create `examples/wifi.json`:

```json
{
  "version": "1.0",
  "hostname": "dashboard",
  "networks": [
    { "ssid": "MEIN-WLAN", "password": "BITTE-AENDERN" }
  ]
}
```

- [ ] **Step 5: Commit**

```bash
git add examples/www/ examples/wifi.json
git commit -m "feat: Web-Assets und Beispiel-wifi.json für SD-Karte"
```

---

## Task 8: Hardware-Validierung (Quickstart)

Native Tests decken `wifi_credentials` ab; der ESP-IDF-Glue wird auf Hardware geprüft.

**Files:** keine (Validierung)

- [ ] **Step 1: SD-Karte vorbereiten**

Auf die FAT32-SD-Karte kopieren:
- `examples/dashboard.json` → `/dashboard.json`
- `examples/www/` → `/www/` (mit `index.html`, `app.js`, `style.css`)
- `examples/wifi.json` → `/wifi.json`, darin echte SSID/Passwort eintragen

- [ ] **Step 2: Native Tests grün halten**

Run: `pio test -e native`
Expected: alle Suiten PASS (inkl. `test_wifi_credentials`).

- [ ] **Step 3: Flashen & Monitor**

Run: `pio run -e esp32-s3-touch-lcd-7 -t upload` (seriellen Monitor vorher schließen)
Dann: `pio device monitor`
Expected im Log: `wifi_port: Verbunden mit '<SSID>', IP <x.x.x.x>`, `mDNS aktiv: http://dashboard.local`, `Upload bereit: …`. Dashboard erscheint wie gewohnt in < 3 s (vor dem WLAN-Log).

- [ ] **Step 4: Upload über Browser testen**

- Browser öffnen: `http://dashboard.local` (Fallback: die geloggte IP).
- Seite muss laden (Assets von SD). Eine geänderte `dashboard.json` wählen, hochladen.
- Erwartung: „OK: Konfiguration übernommen, Neustart …", Gerät rebootet, neues Dashboard erscheint.

- [ ] **Step 5: Fehlerfälle prüfen**

- Ungültige JSON hochladen (z. B. `signals`-Key entfernt): Erwartung HTTP 400 + Fehlermeldung im Browser, **kein** Reboot, altes Dashboard bleibt aktiv.
- `/www` von SD entfernen, neu booten: `http://dashboard.local` zeigt das eingebettete Fallback-Formular.
- `wifi.json` entfernen, neu booten: Log „Keine wifi.json – Upload deaktiviert", Dashboard läuft normal.

- [ ] **Step 6: Ergebnis dokumentieren & Abschluss-Commit**

Beobachtungen (verbundene SSID, IP, mDNS ja/nein, Upload ok, Fehlerfälle) festhalten.

```bash
git add -A
git commit -m "test: WLAN-Upload auf Hardware validiert (Quickstart Task 8)"
```

---

## Hinweise für die Umsetzung

- **mDNS-Komponente:** Der erste Build nach Task 3 lädt `espressif/mdns` über den Component-Manager (Internet am Build-PC nötig). Schlägt der Download fehl, Version in `src/idf_component.yml` prüfen.
- **Reihenfolge:** Tasks 1–7 sind reine Build-/Unit-Test-Schritte und können ohne Hardware abgeschlossen werden. Erst Task 8 braucht das Board.
- **Stack-Größen** (`network_task` 5 KB, `reboot_task` 2 KB) sind konservativ geschätzt; bei `Stack canary`-Panics im Monitor erhöhen.
- **Upload-Limit** (`UPLOAD_MAX` 16384) ist bewusst gleich `s_json_buf` in `main.c` gewählt — beide zusammen ändern, falls größere Configs nötig werden.
