# Research: JSON-basierte Dashboard-Konfiguration

**Feature**: 001-json-config-dashboard
**Date**: 2026-06-15

---

## 1. SD-Karten-Zugriff mit CH422G-kontrolliertem CS

### Problem

Die SD-Karte auf dem Waveshare ESP32-S3-Touch-LCD-7 wird über folgende GPIOs
angebunden:

| Signal  | GPIO |
|---------|------|
| MOSI    | 11   |
| CLK     | 12   |
| MISO    | 13   |
| CS (SD) | –    |

CS wird **nicht** direkt über einen GPIO gesteuert, sondern über **CH422G-Pin 4**
(IO-Expander, I²C-Adresse 0x24/0x38, SDA=GPIO8, SCL=GPIO9). Quelle: Arduino-Demo
`documents/_demo_extract/.../03_SD_Test/waveshare_sd_card.h`.

### Lösung

**Decision**: Custom `waveshare_sd_port.c` HAL mit manuellem CS-Management über
CH422G-Callbacks.

**Implementierungsansatz**:

1. SPI2-Bus initialisieren (MOSI=11, MISO=13, CLK=12) via `spi_bus_initialize()`
2. SD-SPI-Gerät hinzufügen mit `gpio_cs = GPIO_NUM_NC` (kein direkter GPIO-CS)
3. `spi_device_interface_config_t.pre_cb` → ruft `ch422g_set_pin(SD_CS_PIN, 0)` auf
   (CS low = SD aktiv)
4. `spi_device_interface_config_t.post_cb` → ruft `ch422g_set_pin(SD_CS_PIN, 1)` auf
   (CS high = SD inaktiv)
5. Eigene FatFs `diskio`-Schicht (`sd_diskio.c`) nutzt dieses SPI-Device-Handle
6. FatFs-Volume mit `f_mount()` einbinden → Standardaufrufe `f_open`, `f_read`,
   `f_close` nutzen

**Wichtig**: Der CH422G wird bereits in `waveshare_rgb_lcd_port.c` initialisiert.
`waveshare_sd_port_init()` MUSS nach `waveshare_rgb_lcd_init()` aufgerufen werden,
damit die CH422G-Initialisierung vorher abgeschlossen ist.

**Alternativen abgelehnt**:
- `esp_vfs_fat_sdspi_mount()` direkt: keine CS-Callback-Option; würde direkten GPIO
  voraussetzen – nicht möglich mit CH422G
- Hardware-Modifikation (extra GPIO für CS): Eingriff in Board-Hardware, abgelehnt
- Software-SPI (Bit-Banging): zu langsam für zuverlässiges JSON-Lesen (< 1 MB/s)

---

## 2. cJSON in ESP-IDF

**Decision**: cJSON, direkt aus ESP-IDF, kein externer Download.

ESP-IDF ≥ 4.x enthält cJSON als Built-in-Komponente (`components/json/cJSON/`).
Include: `#include "cJSON.h"`.

**Standardmuster für `config_loader.c`**:

```c
cJSON *root = cJSON_Parse(json_str);
if (!root) {
    const char *err = cJSON_GetErrorPtr();
    // Fehlermeldung auf Display ausgeben
    return ESP_ERR_INVALID_ARG;
}

cJSON *signals = cJSON_GetObjectItemCaseSensitive(root, "signals");
cJSON *signal;
cJSON_ArrayForEach(signal, signals) {
    cJSON *name = cJSON_GetObjectItemCaseSensitive(signal, "name");
    cJSON *can_id = cJSON_GetObjectItemCaseSensitive(signal, "can_id");
    // ...
}

cJSON_Delete(root); // MUSS aufgerufen werden (gibt Heap frei)
```

**Heap-Budget**: Parse-Tree für ein `dashboard.json` mit 8 Signalen + 2 Seiten +
8 Widgets belegt ca. 8–14 kB Heap. `cJSON_Delete(root)` sofort nach Abschluss des
Parsens aufrufen, bevor der resultierende `DashboardConfig`-Struct in PSRAM/Heap
bleibt.

**Nativ testbar**: cJSON ist platform-unabhängiges ANSI-C. Tests in
`test/test_config_loader.c` können `json_str` aus lokalen Dateien laden (POSIX stdio)
und auf dem Host-PC mit `pio test -e native` ausgeführt werden.

---

## 3. LVGL v8 – Mehrseitige Navigation (lv_tileview + Navigationspunkte)

**Decision**: `lv_tileview` für Swipe-Gesten + custom `nav_indicator` für
Navigationspunkte am unteren Rand.

### lv_tileview

`lv_tileview` ist das passende LVGL-v8-Widget für horizontale Swipe-Navigation:
- Unterstützt Swipe-Gesten nativ (kein manuelles Gesture-Handling nötig)
- Jede Seite ist ein `lv_tileview_add_tile(tv, col, row, dir)` mit `dir = LV_DIR_HOR`
- Navigation per Event-Callback: `LV_EVENT_VALUE_CHANGED` → `lv_tileview_get_tile_act()`

```c
lv_obj_t *tv = lv_tileview_create(lv_scr_act());
lv_obj_set_size(tv, LV_HOR_RES, LV_VER_RES - NAV_HEIGHT); // Platz für dots
lv_obj_add_event_cb(tv, on_page_changed, LV_EVENT_VALUE_CHANGED, NULL);

// Für jede Seite:
lv_obj_t *tile = lv_tileview_add_tile(tv, page_idx, 0, LV_DIR_HOR);
// Widgets auf tile erstellen
```

### nav_indicator (custom)

Navigationspunkte (dots) unter dem Tileview:
- Container `lv_obj_t *nav_bar` mit `height = NAV_HEIGHT` (z. B. 32 px), unten
- Pro Seite: kleines `lv_obj_t` mit runder Form, grau = inaktiv, weiß = aktiv
- `NAV_HEIGHT = 0` wenn nur 1 Seite → Tileview nimmt volle Höhe (800×480)

---

## 4. Dynamische Widget-Erstellung (Widget-Registry-Pattern)

**Decision**: Registrierungs-Tabelle aus Funktionszeigern (Typen-String → Create/Update/Stale-Fn).

Dieses Muster erfüllt FR-014 (erweiterbar ohne Umbau, zukünftige TX-Input-Widgets
können einfach hinzugefügt werden).

```c
// widget_registry.h
typedef lv_obj_t* (*widget_create_fn_t)(lv_obj_t *parent, const widget_config_t *cfg);
typedef void      (*widget_update_fn_t)(lv_obj_t *obj, float value);
typedef void      (*widget_stale_fn_t) (lv_obj_t *obj, bool stale);

typedef struct {
    const char        *type_name;   // z.B. "gauge", "chart"
    widget_create_fn_t create;
    widget_update_fn_t update;
    widget_stale_fn_t  set_stale;
} widget_descriptor_t;

// widget_registry.c – statische Tabelle
static const widget_descriptor_t s_registry[] = {
    { "gauge",  gauge_create,  gauge_update,  gauge_stale  },
    { "chart",  chart_create,  chart_update,  chart_stale  },
    { "bar",    bar_create,    bar_update,    bar_stale    },
    { "led",    led_create,    led_update,    led_stale    },
    { "label",  label_create,  label_update,  label_stale  },
    { "arc",    arc_create,    arc_update,    arc_stale    },
    // Future CAN-TX: { "slider", slider_create, slider_update, slider_stale },
};
```

**Lookup**: Lineare Suche über `s_registry` nach `type_name` – bei 6–8 Einträgen
schneller als Hashtable und wartungsfreundlicher.

---

## 5. Signal-Index-Mapping (Dispatcher ↔ Dashboard)

Mit JSON-basierter Konfiguration werden Signale dynamisch geladen. Das Mapping
Widget → Signal geschieht in zwei Schritten:

**Schritt 1 – Compile-time-artig (beim Config-Laden)**:
`config_loader_resolve_bindings()` iteriert alle Widget-Configs, sucht den
`signal_name` in der Signaltabelle und schreibt `widget_config.signal_idx` (Array-Index).

**Schritt 2 – Runtime (in `dashboard_tick()`)**:
`can_value_event_t.signal_idx` kommt aus dem Dispatcher → `dashboard_tick()` durchsucht
seine interne Widget-Liste nach Einträgen mit passendem `signal_idx` → ruft
`widget_registry_update()` auf.

Das `can_value_event_t`-Struct bleibt unverändert (kein Breaking Change).

---

## 6. Test-Strategie (3 Ebenen)

### Ebene 1 – Native Tests (kein Board erforderlich)

**Befehl**: `pio test -e native`

**Was getestet wird**:
- `test_config_loader.c`: JSON-Parsing korrekt (valide JSON → DashboardConfig),
  JSON-Fehlerbehandlung (Syntax-Fehler, fehlendes Pflichtfeld, unbekannter Widget-Typ),
  Signal-Name-Auflösung, Binding-Validierung
- `test_can_signal.c` (bestehend): Dekodierung weiterhin korrekt – Regression-Schutz
- `test_config_types.c`: Validierungsfunktionen für Entities (z.B. `min < max`)

**Voraussetzung**: `test/fixtures/*.json` – statische JSON-Testdaten, die von
POSIX `fopen()`/`fread()` im nativen Test-Umfeld gelesen werden (kein SD-Karten-HAL).

### Ebene 2 – Hardware ohne CAN-Quelle (SD-Karte + Simulator)

**Voraussetzung**: Board, SD-Karte (FAT32), `dashboard.json` auf SD-Root.

**sdkconfig-Einstellung**: `CONFIG_CAN_SIMULATOR_ENABLE=y`

**Was getestet wird**:
1. Valide JSON → Dashboard zeigt konfigurierende Widgets korrekt
2. Touch-Navigation → Swipe und Punkte wechseln Seiten
3. Widget-Farben und Titel aus JSON vorhanden
4. Stale-Grauschaltung → Signal erhält via Simulator keinen Update → nach Timeout ausgegraut
5. Fehlerszenario: SD-Karte nicht eingelegt → Fehlermeldung auf Display
6. Fehlerszenario: JSON mit absichtlichem Syntaxfehler → Fehlermeldung mit Kontext

**Simulator-Verhalten**: Wenn `CONFIG_CAN_SIMULATOR_ENABLE=y`, werden ALLE Signale
mit `is_simulated=true` (oder wenn Flag im JSON nicht gesetzt) mit animierten Werten
versorgt. Signale mit `"simulated": false` im JSON empfangen keine simulierten Daten
→ gehen nach `stale_timeout_ms` in Stale-Zustand (testbar!).

### Ebene 3 – Hardware mit echter CAN-Quelle

**Voraussetzung**: Board + SD-Karte + USB-CAN-A-Adapter
(Tool: `documents/USBCANV2.10.zip`) ODER M12-DeviceNet-Kabel.

**sdkconfig-Einstellung**: `CONFIG_CAN_SIMULATOR_ENABLE=n`

**Was getestet wird**:
1. CAN-Frame einspeisen (mit USB-CAN-A) → Widget zeigt skalierten Wert
2. Endianness-Test: Big-Endian-Frame → korrekte Dekodierung
3. Scale/Offset-Test: z.B. `scale=0.1, offset=-40` → physikalischer Wert stimmt
4. Stale-Timeout: kein Frame mehr senden → Widget graut aus nach konfigurierten ms
5. Mehrere Signale gleichzeitig → alle Widgets aktualisieren unabhängig

**Referenz CAN-Frame-Tabelle**: siehe `quickstart.md`
