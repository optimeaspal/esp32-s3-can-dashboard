# Data Model: JSON-basierte Dashboard-Konfiguration

**Feature**: 001-json-config-dashboard
**Date**: 2026-06-15

---

## Übersicht der Entitäten und ihrer Beziehungen

```
DashboardConfig
├── signals[]          ← Array von can_signal_t (erweitert)
└── pages[]            ← Array von page_config_t
        └── widgets[]  ← Array von widget_config_t
                └── signal_idx  ← aufgelöster Index in signals[]
```

---

## config_types.h – Struct-Definitionen

Alle Structs sind in `src/app/config_types.h` definiert (kein ESP-IDF-Include,
nativ kompilierbar).

### Limits (compile-time, konfigurierbar)

```c
#define CFG_MAX_SIGNALS         32
#define CFG_MAX_PAGES           8
#define CFG_MAX_WIDGETS_PER_PAGE 16
#define CFG_SIGNAL_NAME_LEN     32   // identisch mit CAN_SIGNAL_NAME_LEN
#define CFG_WIDGET_TITLE_LEN    32
#define CFG_WIDGET_TYPE_LEN     16   // "gauge", "chart", "bar", "led", "label", "arc"
```

---

### widget_style_t – Erscheinungseigenschaften

```c
typedef struct {
    uint32_t normal_color;      // RGB: 0xRRGGBB
    uint32_t warning_color;     // RGB: 0xRRGGBB  (0 = nicht gesetzt → Standard)
    uint32_t background_color;  // RGB: 0xRRGGBB  (0 = transparent/Standard)
    float    warning_threshold; // Wert ab dem warning_color greift (0 = kein Warnbereich)
    char     title[CFG_WIDGET_TITLE_LEN];
} widget_style_t;
```

**Validierungsregeln**:
- `warning_threshold` MUSS im Bereich [min, max] des zugeordneten Signals liegen,
  wenn `warning_color != 0`
- Fehlende Farben → Standardwerte des Widget-Typs (hardkodiert in `widget_*.c`)

---

### widget_config_t – Widget-Definition

```c
typedef struct {
    char     type[CFG_WIDGET_TYPE_LEN];  // "gauge"|"chart"|"bar"|"led"|"label"|"arc"
    int16_t  x;                          // Pixel-Position (linke obere Ecke der Seite)
    int16_t  y;
    uint16_t width;
    uint16_t height;

    char     signal_name[CFG_SIGNAL_NAME_LEN]; // Referenz auf signal.name
    int16_t  signal_idx;                        // Aufgelöst von config_loader (−1 = unresolved)

    widget_style_t style;
} widget_config_t;
```

**Pflichtfelder**: `type`, `x`, `y`, `width`, `height`, `signal_name`
**Optionale Felder**: alle `style`-Felder (Defaults werden in Widget-Modulen gesetzt)

---

### page_config_t – Seiten-Definition

```c
typedef struct {
    char              title[CFG_WIDGET_TITLE_LEN];
    widget_config_t   widgets[CFG_MAX_WIDGETS_PER_PAGE];
    uint8_t           widget_count;
} page_config_t;
```

---

### dashboard_config_t – Wurzel-Objekt

```c
typedef struct {
    uint8_t       version_major;
    uint8_t       version_minor;

    can_signal_t  signals[CFG_MAX_SIGNALS];
    uint8_t       signal_count;

    page_config_t pages[CFG_MAX_PAGES];
    uint8_t       page_count;

    /* tx_commands reserviert für v2-CAN-TX-Feature (FR-013):
       wird geparst und validiert auf Existenz, aber ignoriert in v1 */
    bool          has_tx_commands;  // true wenn "tx_commands"-Key in JSON vorhanden
} dashboard_config_t;
```

---

## can_signal_t – Erweiterungen gegenüber POC

Das bestehende `can_signal_t` aus `src/app/can_signal.h` wird um ein Feld ergänzt:

```c
// Bestehend (unverändert):
//   can_id, extended_id, byte_offset, byte_length, little_endian,
//   is_signed, is_float, scale, offset, min_value, max_value,
//   timeout_ms, name[32], unit[8]

// NEU (in can_signal_t ergänzen):
bool is_simulated;   // ← bereits vorhanden! (war schon im POC)
```

→ Kein Breaking Change. `is_simulated` wird aus JSON-Feld `"simulated": true/false`
gesetzt; fehlt das Feld, ist der Default `false`.

---

## config_loader.h – API

```c
/**
 * Parsed eine JSON-Zeichenkette in eine dashboard_config_t.
 * Löst Widget-Signal-Bindungen auf (setzt widget_config.signal_idx).
 *
 * @param json_str   NULL-terminierter JSON-String (z.B. aus Datei gelesen)
 * @param cfg        Ziel-Struct (vom Aufrufer allokiert)
 * @param err_buf    Puffer für Fehlermeldung (NULL = kein Output)
 * @param err_len    Größe des err_buf
 *
 * @return ESP_OK bei Erfolg,
 *         ESP_ERR_INVALID_ARG bei JSON-Fehler (err_buf enthält Details),
 *         ESP_ERR_NOT_FOUND wenn Signal-Referenz nicht auflösbar
 *
 * Nativ testbar: kein ESP-IDF-Include in dieser Funktion.
 * Für native Tests: Rückgabewerte als int typedef oder eigene Fehler-Enum definieren.
 */
esp_err_t config_loader_parse(const char *json_str,
                               dashboard_config_t *cfg,
                               char *err_buf, size_t err_len);
```

**Hinweis für native Testbarkeit**: `esp_err_t` ist in ESP-IDF definiert. Für
`pio test -e native` muss entweder:
- ein `test/stubs/esp_err.h` mit `typedef int esp_err_t;` und `#define ESP_OK 0` bereitgestellt werden
- ODER `config_loader.h` eine build-guard-gesteuerte Typdefinition verwenden

---

## Zustandsübergänge: Signal-Lifecycle

```
JSON geladen
    │
    ▼
can_signal_t.is_simulated=true/false
    │
    ├─ true  → can_simulator_task() generiert Werte
    │
    └─ false → can_dispatcher_task() wartet auf TWAI-Frame
                    │
                    ├─ Frame empfangen → can_value_event_t in Queue
                    │
                    └─ Timeout überschritten → Stale-Event in Queue
                                                     │
                                                     ▼
                                             dashboard_tick():
                                             widget_stale_fn(obj, true)
                                             → Widget ausgegraut
```

---

## Beziehungen und Invarianten

| Beziehung | Regel |
|-----------|-------|
| Widget → Signal | widget_config.signal_name MUSS in signals[].name existieren; aufgelöst als signal_idx ≥ 0 |
| Signal CAN-ID | Innerhalb eines `dashboard_config_t` MUSS jede (can_id, byte_offset)-Kombination eindeutig sein |
| Page Widget-Count | widget_count ≤ CFG_MAX_WIDGETS_PER_PAGE |
| Version | version_major=1 für diese Spec; Parser lehnt version_major > 1 mit Warnung ab |
| tx_commands | In v1 geparst und has_tx_commands gesetzt, aber keine weitere Verarbeitung |
