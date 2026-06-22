# Feature 005 – Settings-/Info-Screen Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ein status-farbiges Zahnrad-Icon im Dashboard öffnet einen eigenen, read-only Settings-/Info-Screen mit WLAN-, Geräte- und SD-Infos sowie einem live CAN-RX-Monitor (gruppiert pro CAN-ID).

**Architecture:** Die testbare Kernlogik wird als reines, ESP-IDF-freies Modul `app/can_monitor` herausgezogen (native Unit-Tests). Der `can_dispatcher` füttert dieses Modul in seiner RX-Schleife unter einem Spinlock und bietet einen thread-sicheren Snapshot-Getter. Die LVGL-UI besteht aus zwei neuen Modulen: `ui/status_icon` (Zahnrad auf dem Dashboard-Screen, färbt sich nach WLAN-Status, Klick öffnet Settings) und `ui/settings_screen` (eigener `lv_screen`, Zwei-Spalten-Layout, „Zurück"-Navigation, pollt Snapshot im Tick). Die UI-Module werden auf Hardware validiert (LVGL ist nicht nativ testbar).

**Tech Stack:** C, ESP-IDF (espidf via PlatformIO), LVGL 9, FreeRTOS, Unity (native Tests), `pio test -e native` / `pio run`.

**Referenz-Spec:** `docs/superpowers/specs/2026-06-22-settings-info-screen-design.md`

---

## File Structure

**Neu:**
- `src/app/can_monitor.h` – reine Datenstruktur + API für die Pro-ID-Monitor-Tabelle (keine ESP-Abhängigkeiten).
- `src/app/can_monitor.c` – Implementierung (native testbar).
- `test/test_can_monitor/test_can_monitor.c` – Unity-Tests für `can_monitor`.
- `src/ui/status_icon.h` / `src/ui/status_icon.c` – Zahnrad-Icon, WLAN-Statusfarbe, Klick öffnet Settings.
- `src/ui/settings_screen.h` / `src/ui/settings_screen.c` – Settings-Screen (Layout, Info-Karten, CAN-Tabelle, Tick).

**Geändert:**
- `src/app/can_dispatcher.c` – füttert `can_monitor` in der RX-Schleife (unter Spinlock), bietet thread-sicheren Getter.
- `src/app/can_dispatcher.h` – deklariert den Monitor-Getter.
- `src/hal/waveshare_wifi_port.c` / `.h` – Getter für SSID, Hostname, RSSI.
- `src/hal/waveshare_sd_port.c` / `.h` – `waveshare_sd_port_is_mounted()` + `waveshare_sd_port_file_exists()`.
- `src/main.c` – Status-Icon nach `dashboard_create` erstellen, periodische Timer für Icon-Farbe + `settings_screen_tick`.

---

## Task 1: `can_monitor` – reine Pro-ID-Tabelle mit native Tests

Das Herzstück: eine feste Tabelle, die je CAN-ID die letzten Datenbytes, einen kumulativen Zähler und eine gefensterte Frames/s-Rate hält. Komplett ohne ESP-IDF, daher voll nativ testbar (TDD).

**Files:**
- Create: `src/app/can_monitor.h`
- Create: `src/app/can_monitor.c`
- Test: `test/test_can_monitor/test_can_monitor.c`

- [ ] **Step 1: Header anlegen**

Create `src/app/can_monitor.h`:

```c
#pragma once

/*
 * Reine, hardware-unabhängige Pro-ID-Statistik für empfangene CAN-Frames.
 * Keine ESP-IDF-Abhängigkeiten → vollständig nativ testbar (pio test -e native).
 *
 * Nicht thread-safe: Der Aufrufer (can_dispatcher) serialisiert Schreib- und
 * Lesezugriffe über einen Spinlock.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_MONITOR_MAX_IDS 32

/* Statistik zu genau einer CAN-ID. */
typedef struct
{
    uint32_t id;            /* CAN-Identifier                                  */
    bool     extended;      /* true = 29-Bit Extended Frame                    */
    uint8_t  data[8];       /* zuletzt empfangene Datenbytes                   */
    uint8_t  dlc;           /* Data Length Code des letzten Frames             */
    uint32_t count;         /* kumulative Frame-Anzahl seit reset              */
    uint32_t window_count;  /* Frames seit letztem fps-Fenster (intern)        */
    uint32_t fps;           /* Frames im letzten abgeschlossenen 1-s-Fenster   */
    int64_t  last_us;       /* Zeitstempel des letzten Frames (µs)             */
} can_monitor_entry_t;

/* Monitor-Zustand. Vom Aufrufer instanziiert (z. B. statisch im Dispatcher). */
typedef struct
{
    can_monitor_entry_t entries[CAN_MONITOR_MAX_IDS];
    size_t              count;                 /* belegte Einträge             */
    int64_t             fps_window_start_us;   /* Beginn des aktuellen Fensters */
} can_monitor_t;

/* Setzt den Monitor auf leer zurück. */
void can_monitor_reset(can_monitor_t *m);

/*
 * Registriert einen empfangenen Frame. Legt bei neuer ID einen Eintrag an
 * (sofern < CAN_MONITOR_MAX_IDS), sonst wird der Frame einer vollen Tabelle
 * verworfen. dlc wird auf 8 begrenzt.
 */
void can_monitor_record(can_monitor_t *m, uint32_t id, bool extended,
                        const uint8_t *data, uint8_t dlc, int64_t timestamp_us);

/*
 * Aktualisiert die fps-Werte, wenn seit fps_window_start_us >= 1 s vergangen
 * ist: setzt je Eintrag fps = window_count und nullt window_count.
 * Vor Ablauf der Sekunde passiert nichts.
 */
void can_monitor_update_fps(can_monitor_t *m, int64_t now_us);

/*
 * Kopiert bis zu max Einträge sortiert nach (extended, id) nach out.
 * @return Anzahl kopierter Einträge (= min(m->count, max)).
 */
size_t can_monitor_snapshot(const can_monitor_t *m,
                            can_monitor_entry_t *out, size_t max);

/* Summe aller Eintrags-fps (Gesamt-Framerate). */
uint32_t can_monitor_total_fps(const can_monitor_t *m);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Failing test schreiben**

Create `test/test_can_monitor/test_can_monitor.c`:

```c
/*
 * Unit-Tests für die Pro-ID-CAN-Monitor-Tabelle.
 * Native: pio test -e native
 */
#include <stdint.h>
#include <string.h>
#include "unity.h"

#include "../../src/app/can_monitor.h"
#include "../../src/app/can_monitor.c"

static can_monitor_t m;

void setUp(void)    { can_monitor_reset(&m); }
void tearDown(void) {}

/* Mehrere Frames derselben ID → ein Eintrag, letzte Daten + Zähler stimmen. */
void test_same_id_aggregates(void)
{
    uint8_t d1[] = {0x01, 0x02};
    uint8_t d2[] = {0x03, 0x04};
    can_monitor_record(&m, 0x100, false, d1, 2, 1000);
    can_monitor_record(&m, 0x100, false, d2, 2, 2000);

    TEST_ASSERT_EQUAL_UINT(1, m.count);
    can_monitor_entry_t out[CAN_MONITOR_MAX_IDS];
    size_t n = can_monitor_snapshot(&m, out, CAN_MONITOR_MAX_IDS);
    TEST_ASSERT_EQUAL_UINT(1, n);
    TEST_ASSERT_EQUAL_UINT32(0x100, out[0].id);
    TEST_ASSERT_EQUAL_UINT32(2, out[0].count);
    TEST_ASSERT_EQUAL_UINT8(0x03, out[0].data[0]);
    TEST_ASSERT_EQUAL_UINT8(0x04, out[0].data[1]);
    TEST_ASSERT_EQUAL_INT64(2000, out[0].last_us);
}

/* Verschiedene IDs → Snapshot sortiert aufsteigend nach ID. */
void test_distinct_ids_sorted(void)
{
    uint8_t d[] = {0xAA};
    can_monitor_record(&m, 0x200, false, d, 1, 10);
    can_monitor_record(&m, 0x100, false, d, 1, 20);
    can_monitor_record(&m, 0x180, false, d, 1, 30);

    can_monitor_entry_t out[CAN_MONITOR_MAX_IDS];
    size_t n = can_monitor_snapshot(&m, out, CAN_MONITOR_MAX_IDS);
    TEST_ASSERT_EQUAL_UINT(3, n);
    TEST_ASSERT_EQUAL_UINT32(0x100, out[0].id);
    TEST_ASSERT_EQUAL_UINT32(0x180, out[1].id);
    TEST_ASSERT_EQUAL_UINT32(0x200, out[2].id);
}

/* Volle Tabelle: weitere neue ID wird verworfen, vorhandene weiter gezählt. */
void test_table_full_drops_new_ids(void)
{
    uint8_t d[] = {0x00};
    for (uint32_t i = 0; i < CAN_MONITOR_MAX_IDS; i++)
        can_monitor_record(&m, 0x100 + i, false, d, 1, i);
    TEST_ASSERT_EQUAL_UINT(CAN_MONITOR_MAX_IDS, m.count);

    can_monitor_record(&m, 0x999, false, d, 1, 9999);          /* neue ID */
    TEST_ASSERT_EQUAL_UINT(CAN_MONITOR_MAX_IDS, m.count);

    can_monitor_record(&m, 0x100, false, d, 1, 8888);          /* vorhandene */
    can_monitor_entry_t out[CAN_MONITOR_MAX_IDS];
    can_monitor_snapshot(&m, out, CAN_MONITOR_MAX_IDS);
    TEST_ASSERT_EQUAL_UINT32(2, out[0].count);                 /* 0x100 zweimal */
}

/* dlc > 8 wird auf 8 begrenzt (kein Überlauf). */
void test_dlc_clamped(void)
{
    uint8_t d[8] = {1,2,3,4,5,6,7,8};
    can_monitor_record(&m, 0x100, false, d, 200, 1);
    can_monitor_entry_t out[CAN_MONITOR_MAX_IDS];
    can_monitor_snapshot(&m, out, CAN_MONITOR_MAX_IDS);
    TEST_ASSERT_EQUAL_UINT8(8, out[0].dlc);
}

/* fps: vor 1 s keine Änderung, nach 1 s window_count → fps, dann Reset. */
void test_fps_window(void)
{
    uint8_t d[] = {0x00};
    for (int i = 0; i < 5; i++)
        can_monitor_record(&m, 0x100, false, d, 1, 100);

    can_monitor_update_fps(&m, 500000);     /* 0,5 s → noch nichts */
    can_monitor_entry_t out[CAN_MONITOR_MAX_IDS];
    can_monitor_snapshot(&m, out, CAN_MONITOR_MAX_IDS);
    TEST_ASSERT_EQUAL_UINT32(0, out[0].fps);

    can_monitor_update_fps(&m, 1000000);    /* 1,0 s → fps = 5 */
    can_monitor_snapshot(&m, out, CAN_MONITOR_MAX_IDS);
    TEST_ASSERT_EQUAL_UINT32(5, out[0].fps);
    TEST_ASSERT_EQUAL_UINT32(5, can_monitor_total_fps(&m));

    can_monitor_record(&m, 0x100, false, d, 1, 1000100);  /* neues Fenster */
    can_monitor_update_fps(&m, 2000000);
    can_monitor_snapshot(&m, out, CAN_MONITOR_MAX_IDS);
    TEST_ASSERT_EQUAL_UINT32(1, out[0].fps);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_same_id_aggregates);
    RUN_TEST(test_distinct_ids_sorted);
    RUN_TEST(test_table_full_drops_new_ids);
    RUN_TEST(test_dlc_clamped);
    RUN_TEST(test_fps_window);
    return UNITY_END();
}
```

- [ ] **Step 3: Test ausführen, Fehlschlag verifizieren**

Run: `pio test -e native -f test_can_monitor`
Expected: FAIL – Linkerfehler/„undefined reference" zu `can_monitor_*`, da `can_monitor.c` noch leer ist.

- [ ] **Step 4: Minimale Implementierung schreiben**

Create `src/app/can_monitor.c`:

```c
#include "can_monitor.h"
#include <string.h>

void can_monitor_reset(can_monitor_t *m)
{
    memset(m, 0, sizeof(*m));
}

static can_monitor_entry_t *find_slot(can_monitor_t *m, uint32_t id, bool extended)
{
    for (size_t i = 0; i < m->count; i++) {
        if (m->entries[i].id == id && m->entries[i].extended == extended)
            return &m->entries[i];
    }
    if (m->count >= CAN_MONITOR_MAX_IDS)
        return NULL;
    can_monitor_entry_t *e = &m->entries[m->count++];
    memset(e, 0, sizeof(*e));
    e->id       = id;
    e->extended = extended;
    return e;
}

void can_monitor_record(can_monitor_t *m, uint32_t id, bool extended,
                        const uint8_t *data, uint8_t dlc, int64_t timestamp_us)
{
    can_monitor_entry_t *e = find_slot(m, id, extended);
    if (!e) return;

    if (dlc > 8) dlc = 8;
    e->dlc = dlc;
    memset(e->data, 0, sizeof(e->data));
    if (data && dlc) memcpy(e->data, data, dlc);

    e->count++;
    e->window_count++;
    e->last_us = timestamp_us;
}

void can_monitor_update_fps(can_monitor_t *m, int64_t now_us)
{
    if (now_us - m->fps_window_start_us < 1000000)
        return;
    for (size_t i = 0; i < m->count; i++) {
        m->entries[i].fps          = m->entries[i].window_count;
        m->entries[i].window_count = 0;
    }
    m->fps_window_start_us = now_us;
}

size_t can_monitor_snapshot(const can_monitor_t *m,
                            can_monitor_entry_t *out, size_t max)
{
    size_t n = (m->count < max) ? m->count : max;
    memcpy(out, m->entries, n * sizeof(can_monitor_entry_t));

    /* Insertion-Sort nach (extended, id) – n ist klein (<= 32). */
    for (size_t i = 1; i < n; i++) {
        can_monitor_entry_t key = out[i];
        size_t j = i;
        while (j > 0 &&
               ((out[j-1].extended > key.extended) ||
                (out[j-1].extended == key.extended && out[j-1].id > key.id))) {
            out[j] = out[j-1];
            j--;
        }
        out[j] = key;
    }
    return n;
}

uint32_t can_monitor_total_fps(const can_monitor_t *m)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < m->count; i++)
        sum += m->entries[i].fps;
    return sum;
}
```

- [ ] **Step 5: Test ausführen, Erfolg verifizieren**

Run: `pio test -e native -f test_can_monitor`
Expected: PASS – alle 5 Tests grün.

- [ ] **Step 6: Gesamte native Suite laufen lassen (Regression)**

Run: `pio test -e native`
Expected: PASS – alle bestehenden Tests + die neuen bleiben grün.

- [ ] **Step 7: Commit**

```bash
git add src/app/can_monitor.h src/app/can_monitor.c test/test_can_monitor/test_can_monitor.c
git commit -m "feat(can): can_monitor – Pro-ID-Frame-Tabelle mit native Tests"
```

---

## Task 2: `can_dispatcher` füttert den Monitor (thread-sicher)

Der Dispatcher schreibt jeden empfangenen Frame in einen statischen `can_monitor_t` unter einem Spinlock und aktualisiert die fps einmal pro Sekunde. Ein neuer Getter kopiert einen Snapshot für die UI.

**Files:**
- Modify: `src/app/can_dispatcher.h`
- Modify: `src/app/can_dispatcher.c`

- [ ] **Step 1: Getter im Header deklarieren**

In `src/app/can_dispatcher.h`, nach den vorhandenen Includes `#include "can_monitor.h"` ergänzen und vor dem schließenden `#ifdef __cplusplus`-Block hinzufügen:

```c
/*
 * Kopiert einen thread-sicheren Snapshot der CAN-RX-Monitor-Tabelle.
 *
 * @param out        Zielarray (Aufrufer stellt bereit)
 * @param max        Kapazität von out
 * @param out_total_fps  (optional) Gesamt-Framerate; darf NULL sein
 * @return Anzahl kopierter Einträge (sortiert nach (extended, id)).
 */
size_t can_dispatcher_get_monitor(can_monitor_entry_t *out, size_t max,
                                  uint32_t *out_total_fps);
```

Außerdem oben den Include ergänzen:

```c
#include "can_monitor.h"
```

- [ ] **Step 2: Monitor + Spinlock im Dispatcher anlegen**

In `src/app/can_dispatcher.c`, Include ergänzen und statischen Zustand vor `dispatcher_task` einfügen:

```c
#include "can_monitor.h"
```

```c
static can_monitor_t   s_monitor;
static portMUX_TYPE    s_monitor_mux = portMUX_INITIALIZER_UNLOCKED;
```

- [ ] **Step 3: In der RX-Schleife jeden Frame registrieren**

In `dispatcher_task`, innerhalb `while (twai_receive(&msg, 0) == ESP_OK) { ... }`, direkt nach `if (msg.rtr) continue;` einfügen:

```c
            {
                int64_t now = esp_timer_get_time();
                portENTER_CRITICAL(&s_monitor_mux);
                can_monitor_record(&s_monitor, msg.identifier, (bool)msg.extd,
                                   msg.data, msg.data_length_code, now);
                portEXIT_CRITICAL(&s_monitor_mux);
            }
```

- [ ] **Step 4: fps einmal pro Sekunde aktualisieren**

In `dispatcher_task`, am Anfang des `for (;;)`-Schleifenkörpers (vor `twai_read_alerts`), einfügen:

```c
        {
            int64_t now = esp_timer_get_time();
            portENTER_CRITICAL(&s_monitor_mux);
            can_monitor_update_fps(&s_monitor, now);
            portEXIT_CRITICAL(&s_monitor_mux);
        }
```

- [ ] **Step 5: Getter implementieren**

In `src/app/can_dispatcher.c`, ans Dateiende (nach `can_dispatcher_start`) anfügen:

```c
size_t can_dispatcher_get_monitor(can_monitor_entry_t *out, size_t max,
                                  uint32_t *out_total_fps)
{
    portENTER_CRITICAL(&s_monitor_mux);
    size_t n = can_monitor_snapshot(&s_monitor, out, max);
    if (out_total_fps) *out_total_fps = can_monitor_total_fps(&s_monitor);
    portEXIT_CRITICAL(&s_monitor_mux);
    return n;
}
```

- [ ] **Step 6: Firmware bauen**

Run: `pio run`
Expected: SUCCESS – Build läuft fehlerfrei durch.

- [ ] **Step 7: Commit**

```bash
git add src/app/can_dispatcher.h src/app/can_dispatcher.c
git commit -m "feat(can): Dispatcher fuettert can_monitor + thread-sicherer Getter"
```

---

## Task 3: HAL-Getter für WLAN- und SD-Infos

Die Info-Karten brauchen SSID, Hostname, RSSI (WLAN) sowie Mount-Status und Datei-Existenz (SD). Diese Werte sind aktuell nicht abrufbar.

**Files:**
- Modify: `src/hal/waveshare_wifi_port.h`
- Modify: `src/hal/waveshare_wifi_port.c`
- Modify: `src/hal/waveshare_sd_port.h`
- Modify: `src/hal/waveshare_sd_port.c`

- [ ] **Step 1: WLAN-Getter deklarieren**

In `src/hal/waveshare_wifi_port.h`, nach `const char *waveshare_wifi_port_get_ip(void);` einfügen:

```c
/* SSID des verbundenen Netzes ("" wenn nicht verbunden). */
const char *waveshare_wifi_port_get_ssid(void);

/* Registrierter mDNS-Hostname ("" wenn nicht gesetzt). */
const char *waveshare_wifi_port_get_hostname(void);

/*
 * Aktuelle Signalstärke des verbundenen APs in dBm.
 * @return RSSI (negativ) oder 0, wenn nicht verbunden/nicht verfügbar.
 */
int waveshare_wifi_port_get_rssi(void);
```

- [ ] **Step 2: WLAN-Getter implementieren**

In `src/hal/waveshare_wifi_port.c`:

Statischen Speicher bei den anderen Statics (nach `static char s_ip[16] = "0.0.0.0";`) ergänzen:

```c
static char s_ssid[33]     = "";
static char s_hostname[33] = "";
```

In `waveshare_wifi_port_start`, im Erfolgszweig (nach `s_status = WIFI_PORT_CONNECTED;`, vor `start_mdns(...)`), SSID und Hostname merken:

```c
            strncpy(s_ssid, creds->networks[i].ssid, sizeof(s_ssid) - 1);
            strncpy(s_hostname, creds->hostname, sizeof(s_hostname) - 1);
```

Am Dateiende, nach `const char *waveshare_wifi_port_get_ip(void) { return s_ip; }`, anfügen:

```c
const char *waveshare_wifi_port_get_ssid(void)     { return s_ssid; }
const char *waveshare_wifi_port_get_hostname(void) { return s_hostname; }

int waveshare_wifi_port_get_rssi(void)
{
    if (s_status != WIFI_PORT_CONNECTED) return 0;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return 0;
    return ap.rssi;
}
```

- [ ] **Step 3: SD-Getter deklarieren**

In `src/hal/waveshare_sd_port.h`, vor dem schließenden `#ifdef __cplusplus`-Block einfügen:

```c
/* true, wenn die SD-Karte erfolgreich gemountet wurde. */
bool waveshare_sd_port_is_mounted(void);

/* true, wenn die Datei unter path existiert (z. B. "/sdcard/dashboard.json"). */
bool waveshare_sd_port_file_exists(const char *path);
```

Außerdem oben `#include <stdbool.h>` ergänzen (nach `#include <stddef.h>`).

- [ ] **Step 4: SD-Getter implementieren**

In `src/hal/waveshare_sd_port.c`:

`#include <stdbool.h>` und `#include <sys/stat.h>` (falls noch nicht vorhanden) oben ergänzen. Eine statische Mount-Flag einführen: dort wo `waveshare_sd_port_init()` bei Erfolg `ESP_OK` zurückgibt, vor dem `return ESP_OK;` `s_mounted = true;` setzen. Statics oben in der Datei deklarieren:

```c
static bool s_mounted = false;
```

Am Dateiende anfügen:

```c
bool waveshare_sd_port_is_mounted(void) { return s_mounted; }

bool waveshare_sd_port_file_exists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0;
}
```

> Hinweis für den Umsetzer: Prüfe in `waveshare_sd_port_init()`, an welcher Stelle der erfolgreiche Mount feststeht (nach `esp_vfs_fat_sdspi_mount`/Äquivalent), und setze dort `s_mounted = true;`. Falls die Funktion mehrere Erfolgs-Returns hat, setze die Flag unmittelbar vor jedem `return ESP_OK;`.

- [ ] **Step 5: Firmware bauen**

Run: `pio run`
Expected: SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add src/hal/waveshare_wifi_port.h src/hal/waveshare_wifi_port.c src/hal/waveshare_sd_port.h src/hal/waveshare_sd_port.c
git commit -m "feat(hal): Getter fuer WLAN (SSID/Hostname/RSSI) und SD (mounted/file_exists)"
```

---

## Task 4: `ui/status_icon` – Zahnrad-Icon mit WLAN-Statusfarbe

Ein Zahnrad-Symbol oben rechts auf dem Dashboard-Screen (als letztes Kind → über den Tiles). Es färbt sich nach WLAN-Status; ein Klick öffnet den Settings-Screen. Wird auf HW validiert (Task 7).

**Files:**
- Create: `src/ui/status_icon.h`
- Create: `src/ui/status_icon.c`

- [ ] **Step 1: Header anlegen**

Create `src/ui/status_icon.h`:

```c
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Erzeugt das Zahnrad-Icon oben rechts auf parent_scr (dem Dashboard-Screen).
 * Klick öffnet den Settings-Screen (Rückkehr zu parent_scr).
 * Muss innerhalb des LVGL-Mutex aufgerufen werden.
 */
void status_icon_create(lv_obj_t *parent_scr);

/*
 * Aktualisiert die Icon-Farbe anhand des aktuellen WLAN-Status.
 * Periodisch aus dem LVGL-Task aufrufen (innerhalb des Mutex).
 */
void status_icon_tick(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Implementierung schreiben**

Create `src/ui/status_icon.c`:

```c
#include "status_icon.h"
#include "settings_screen.h"
#include "sdkconfig.h"

#if CONFIG_DASHBOARD_WIFI_ENABLE
#include "hal/waveshare_wifi_port.h"
#endif

static lv_obj_t *s_icon;       /* das Label mit dem Zahnrad-Symbol */
static lv_obj_t *s_parent_scr; /* Dashboard-Screen (Rückkehrziel)  */

static void on_icon_clicked(lv_event_t *e)
{
    (void)e;
    settings_screen_open(s_parent_scr);
}

void status_icon_create(lv_obj_t *parent_scr)
{
    s_parent_scr = parent_scr;

    /* Klickfläche (rund) als Container, damit der Tap-Bereich großzügig ist. */
    lv_obj_t *btn = lv_obj_create(parent_scr);
    lv_obj_set_size(btn, 48, 48);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2B3A4D), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_80, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, on_icon_clicked, LV_EVENT_CLICKED, NULL);

    s_icon = lv_label_create(btn);
    lv_label_set_text(s_icon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(s_icon, &lv_font_montserrat_20, 0);
    lv_obj_center(s_icon);

    status_icon_tick();
}

void status_icon_tick(void)
{
    if (!s_icon) return;

    uint32_t color = 0x8895A5; /* grau = Default/deaktiviert */
#if CONFIG_DASHBOARD_WIFI_ENABLE
    switch (waveshare_wifi_port_get_status()) {
        case WIFI_PORT_CONNECTED:  color = 0x36D17F; break; /* grün  */
        case WIFI_PORT_CONNECTING: color = 0xE0B341; break; /* gelb  */
        case WIFI_PORT_FAILED:     color = 0xE0564B; break; /* rot   */
        case WIFI_PORT_IDLE:
        default:                   color = 0x8895A5; break; /* grau  */
    }
#endif
    lv_obj_set_style_text_color(s_icon, lv_color_hex(color), 0);
}
```

> Hinweis: `status_icon.c` referenziert `settings_screen_open()` aus Task 5. Da beide Module im selben Commit-Bereich liegen, wird Task 5 vor dem ersten gemeinsamen Build (Task 6) umgesetzt. Falls dieser Task isoliert gebaut wird, zuerst Task 5 implementieren.

- [ ] **Step 3: (Build erfolgt gemeinsam in Task 6)**

Dieser Task hängt von `settings_screen.h` (Task 5) ab. Kein isolierter Build; Commit erst nach Task 5.

- [ ] **Step 4: Commit (zusammen mit Task 5 möglich)**

```bash
git add src/ui/status_icon.h src/ui/status_icon.c
git commit -m "feat(ui): status_icon – Zahnrad mit WLAN-Statusfarbe"
```

---

## Task 5: `ui/settings_screen` – Settings-/Info-Screen

Eigener LVGL-Screen mit Kopfzeile (Zurück), linker Spalte (drei Info-Karten) und rechter Spalte (CAN-RX-Tabelle). Wird lazy beim ersten Öffnen erzeugt und beim Tick aktualisiert, solange aktiv. HW-validiert (Task 7).

**Files:**
- Create: `src/ui/settings_screen.h`
- Create: `src/ui/settings_screen.c`

- [ ] **Step 1: Header anlegen**

Create `src/ui/settings_screen.h`:

```c
#pragma once

#include "lvgl.h"
#include "app/config_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Übergibt der Settings-Seite einen Zeiger auf die geladene Konfiguration
 * (für die Geräte-Info-Karte). Einmalig beim Start aufrufen.
 */
void settings_screen_set_config(const dashboard_config_t *cfg);

/*
 * Öffnet den Settings-Screen (erzeugt ihn beim ersten Aufruf lazy) und lädt
 * ihn animiert. return_scr ist das Ziel des „Zurück"-Buttons.
 * Innerhalb des LVGL-Mutex aufrufen.
 */
void settings_screen_open(lv_obj_t *return_scr);

/*
 * Aktualisiert die Live-Werte (WLAN, Gerät, SD, CAN-Tabelle), solange der
 * Screen aktiv ist; no-op sonst. Periodisch aus dem LVGL-Task aufrufen.
 */
void settings_screen_tick(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Grundgerüst + Layout implementieren**

Create `src/ui/settings_screen.c`:

```c
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

/* Wertelabels, die im Tick aktualisiert werden */
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
        lv_screen_load_anim(s_return_scr, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 250, 0, false);
}

/* Erzeugt eine Info-Karte und gibt das (leere) Wertelabel zurück. */
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
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);

    lv_obj_t *v = lv_label_create(card);
    lv_label_set_long_mode(v, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(v, LV_PCT(100));
    lv_obj_set_style_text_color(v, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_14, 0);
    lv_label_set_text(v, "");
    return v;
}

static void build_screen(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Kopfzeile */
    lv_obj_t *hdr = lv_obj_create(s_screen);
    lv_obj_set_size(hdr, LV_HOR_RES, 56);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x18222E), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = lv_button_create(hdr);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x27384A), 0);
    lv_obj_add_event_cb(back, on_back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " Zurueck");
    lv_obj_center(bl);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "Einstellungen & Info");
    lv_obj_set_style_text_color(title, lv_color_hex(0xEAF2FB), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 160, 0);

    /* Körper: zwei Spalten */
    lv_obj_t *body = lv_obj_create(s_screen);
    lv_obj_set_size(body, LV_HOR_RES, LV_VER_RES - 56);
    lv_obj_align(body, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 8, 0);
    lv_obj_set_style_pad_gap(body, 8, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);

    /* Linke Spalte (~42 %) mit drei Karten */
    lv_obj_t *left = lv_obj_create(body);
    lv_obj_set_height(left, LV_PCT(100));
    lv_obj_set_width(left, LV_PCT(42));
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_set_style_pad_gap(left, 8, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);

    s_wifi_val = make_card(left, "WLAN / Netzwerk");
    s_dev_val  = make_card(left, "Geraet / Firmware");
    s_sd_val   = make_card(left, "SD-Karte");

    /* Rechte Spalte: CAN-Monitor, volle Höhe */
    lv_obj_t *can = lv_obj_create(body);
    lv_obj_set_height(can, LV_PCT(100));
    lv_obj_set_flex_grow(can, 1);
    lv_obj_set_style_bg_color(can, COLOR_CARD_CAN, 0);
    lv_obj_set_style_border_color(can, lv_color_hex(0x1F4030), 0);
    lv_obj_set_style_border_width(can, 1, 0);
    lv_obj_set_style_radius(can, 6, 0);
    lv_obj_set_style_pad_all(can, 8, 0);
    lv_obj_set_style_pad_gap(can, 2, 0);
    lv_obj_clear_flag(can, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(can, LV_FLEX_FLOW_COLUMN);

    s_can_header = lv_label_create(can);
    lv_obj_set_style_text_color(s_can_header, lv_color_hex(0x36D17F), 0);
    lv_obj_set_style_text_font(s_can_header, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_can_header, "CAN-RX");

    for (int i = 0; i < CAN_ROWS_MAX; i++) {
        s_can_rows[i] = lv_label_create(can);
        lv_obj_set_style_text_color(s_can_rows[i], COLOR_CAN_TXT, 0);
        lv_obj_set_style_text_font(s_can_rows[i], &lv_font_montserrat_14, 0);
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
    settings_screen_tick();   /* sofort befüllen, dann animiert laden */
    lv_screen_load_anim(s_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
}
```

- [ ] **Step 3: Tick (Live-Aktualisierung) implementieren**

In `src/ui/settings_screen.c` ans Dateiende anfügen:

```c
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

    /* Gerät / Firmware */
    uint32_t up_s   = (uint32_t)(esp_timer_get_time() / 1000000);
    uint32_t heap_k = (uint32_t)(esp_get_free_heap_size() / 1024);
    uint32_t psram_k = (uint32_t)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    snprintf(buf, sizeof(buf),
             "FW %d.%d  (%s)\nUptime: %lu s\nHeap frei: %lu k\nPSRAM frei: %lu k\nSeiten: %u",
             s_cfg ? s_cfg->version_major : 0,
             s_cfg ? s_cfg->version_minor : 0,
             __DATE__,
             (unsigned long)up_s, (unsigned long)heap_k, (unsigned long)psram_k,
             s_cfg ? s_cfg->page_count : 0);
    lv_label_set_text(s_dev_val, buf);

    /* SD-Karte */
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
            snprintf(row, sizeof(row), "0x%03lX  %-18s %lu",
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
```

- [ ] **Step 4: (Build erfolgt in Task 6)**

`settings_screen` und `status_icon` werden gemeinsam mit der `main.c`-Verdrahtung gebaut.

- [ ] **Step 5: Commit**

```bash
git add src/ui/settings_screen.h src/ui/settings_screen.c
git commit -m "feat(ui): settings_screen – Layout, Info-Karten, CAN-Tabelle, Tick"
```

---

## Task 6: `main.c` – Icon erstellen und Ticks registrieren

Status-Icon nach dem Dashboard erzeugen, der Settings-Seite die Config geben und beide Ticks per LVGL-Timer takten.

**Files:**
- Modify: `src/main.c`

- [ ] **Step 1: Includes ergänzen**

In `src/main.c`, nach `#include "ui/dashboard.h"` einfügen:

```c
#include "ui/status_icon.h"
#include "ui/settings_screen.h"
```

- [ ] **Step 2: Icon + Settings-Config + Ticks im LVGL-Block registrieren**

In `app_main`, den bestehenden Block

```c
    lvgl_port_lock(-1);
    dashboard_create(&s_cfg, event_queue);
    lv_timer_create((lv_timer_cb_t)dashboard_tick, 50, NULL);
    lvgl_port_unlock();
```

ersetzen durch:

```c
    lvgl_port_lock(-1);
    dashboard_create(&s_cfg, event_queue);
    lv_obj_t *dashboard_scr = lv_scr_act();
    settings_screen_set_config(&s_cfg);
    status_icon_create(dashboard_scr);
    lv_timer_create((lv_timer_cb_t)dashboard_tick, 50, NULL);
    lv_timer_create((lv_timer_cb_t)status_icon_tick, 1000, NULL);   /* WLAN-Farbe ~1 Hz */
    lv_timer_create((lv_timer_cb_t)settings_screen_tick, 250, NULL); /* CAN-Tabelle ~4 Hz */
    lvgl_port_unlock();
```

- [ ] **Step 3: Firmware bauen (gesamtes Feature)**

Run: `pio run`
Expected: SUCCESS – status_icon, settings_screen, can_monitor und die HAL-Getter linken sauber.

- [ ] **Step 4: Native-Suite erneut prüfen (keine Regression)**

Run: `pio test -e native`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/main.c
git commit -m "feat(ui): Status-Icon + Settings-Ticks in main verdrahten"
```

---

## Task 7: Hardware-Validierung

LVGL-UI ist nicht nativ testbar – Verhalten am echten Board prüfen. Falls `status_icon`/`settings_screen` noch nicht committet sind (Tasks 4/5), jetzt nachholen.

**Files:** keine (Validierung)

- [ ] **Step 1: Flashen**

Run: `pio run -t upload && pio device monitor`
Expected: Board bootet, Dashboard erscheint, kein Crash im Log.

- [ ] **Step 2: Icon sichtbar & farbig**

Prüfen: Zahnrad oben rechts sichtbar, über allen Dashboard-Seiten (durchswipen). Farbe entspricht WLAN-Status (grün bei verbundenem WLAN; bei deaktiviertem WLAN grau).

- [ ] **Step 3: Navigation**

Prüfen: Tap aufs Zahnrad öffnet den Settings-Screen (Slide nach links). „‹ Zurueck" kehrt zum Dashboard zurück (Slide nach rechts). Wiederholtes Öffnen/Schließen funktioniert ohne Crash/Leak (Heap im Log stabil).

- [ ] **Step 4: Info-Karten korrekt**

Prüfen: WLAN-Karte zeigt SSID, Status, IP, Hostname, RSSI. Geräte-Karte zeigt FW-Version, Build-Datum, plausible Uptime/Heap/PSRAM, Seitenzahl. SD-Karte zeigt „erkannt" + ja/ja für dashboard.json/wifi.json.

- [ ] **Step 5: CAN-RX-Monitor**

Prüfen (mit aktivem CAN-Simulator oder echtem Bus): Tabelle listet je CAN-ID eine Zeile, sortiert nach ID, mit letzten Datenbytes (hex) und plausibler fps. Kopfzeile zeigt ID-Anzahl + Gesamt-fps. Werte aktualisieren sich ~4×/s. Bei keinem Verkehr bleibt die Tabelle leer, kein Crash.

- [ ] **Step 6: Abschluss-Commit (falls offene UI-Commits) & Notiz**

```bash
git status   # sicherstellen, dass alle Tasks committet sind
```

Ergebnis der HW-Validierung kurz festhalten (für die Projekt-Memory: Feature 005 Stand).

---

## Self-Review-Ergebnis

**Spec-Abdeckung:**
- Status-farbiges Zahnrad-Overlay → Task 4 + Task 6 (Erstellung) + Task 7/Step 2.
- Eigener Screen + „Zurück"-Navigation → Task 5 + Task 7/Step 3.
- Zwei-Spalten-Layout → Task 5/Step 2.
- WLAN-Karte (SSID/Status/IP/Hostname/RSSI) → Task 3 + Task 5/Step 3.
- Geräte-Karte (FW/Build/Uptime/Heap/PSRAM/Seiten) → Task 5/Step 3.
- SD-Karte (mounted + Dateien) → Task 3 + Task 5/Step 3.
- CAN-RX-Monitor gruppiert pro ID (ID/Daten/fps, sortiert, Gesamt-fps) → Task 1 + Task 2 + Task 5/Step 3.
- Datenpfad ohne Einfluss aufs Dashboard (eigene Tabelle, Spinlock, Poll nur bei offenem Screen) → Task 2 + Task 5 (`s_active`-Guard).
- YAGNI (read-only, kein Log/TX/WLAN-Config) → eingehalten.

**Hinweise aus der Spec berücksichtigt:** Kleiner, fester RAM (CAN_MONITOR_MAX_IDS=32, statische Puffer); kein `%f` in LVGL (nur Integer/`snprintf`/Hex). 

**Abweichung von der Spec (bewusst):** Das Icon liegt auf dem Dashboard-Screen (nicht `lv_layer_top()`), damit es beim Settings-Screen nicht mit überlagert wird. Es überdeckt trotzdem alle Tiles (gleicher Screen, als letztes Kind gerendert). Funktional identisch zur Spec-Absicht.

**Offene Detailpunkte für den Umsetzer:** exakte Stelle für `s_mounted = true;` in `waveshare_sd_port_init()` (Task 3/Step 4); RSSI/PSRAM-Werte als „-/0" wenn nicht verfügbar.
