# Contract: dashboard.json – Konfigurationsformat

**Feature**: 001-json-config-dashboard
**Version**: 1.0 (v1 – rx-only, kein CAN-TX)
**Dateiname**: `dashboard.json` (fix, im SD-Karten-Root)
**Encoding**: UTF-8

---

## Vollständiges Beispiel

```json
{
  "version": "1.0",
  "signals": [
    {
      "name":        "RPM",
      "can_id":      "0x102",
      "byte_offset": 0,
      "byte_length": 2,
      "endianness":  "little",
      "signed":      false,
      "float":       false,
      "scale":       1.0,
      "offset":      0.0,
      "unit":        "1/min",
      "min":         0.0,
      "max":         6000.0,
      "stale_ms":    2000,
      "simulated":   true
    },
    {
      "name":        "Temperatur",
      "can_id":      "0x02A",
      "byte_offset": 0,
      "byte_length": 4,
      "endianness":  "little",
      "float":       true,
      "unit":        "°C",
      "min":         0.0,
      "max":         120.0,
      "stale_ms":    3000,
      "simulated":   false
    },
    {
      "name":        "Kraftstoff",
      "can_id":      "0x103",
      "byte_offset": 0,
      "byte_length": 1,
      "endianness":  "little",
      "scale":       0.392,
      "offset":      0.0,
      "unit":        "%",
      "min":         0.0,
      "max":         100.0,
      "stale_ms":    5000,
      "simulated":   true
    },
    {
      "name":        "Status",
      "can_id":      "0x02B",
      "byte_offset": 0,
      "byte_length": 1,
      "endianness":  "little",
      "scale":       1.0,
      "unit":        "",
      "min":         0.0,
      "max":         1.0,
      "stale_ms":    2000,
      "simulated":   false
    }
  ],
  "pages": [
    {
      "title": "Motor",
      "widgets": [
        {
          "type":    "gauge",
          "x": 10,  "y": 10,
          "width":  350, "height": 350,
          "signal": "RPM",
          "title":  "Drehzahl",
          "normal_color":    "#00AA00",
          "warning_color":   "#FF4400",
          "warning_threshold": 5000.0,
          "background_color": "#1A1A1A"
        },
        {
          "type":    "chart",
          "x": 380, "y": 10,
          "width":  400, "height": 200,
          "signal": "Temperatur",
          "title":  "Temperatur",
          "normal_color":    "#0088FF",
          "warning_color":   "#FF4400",
          "warning_threshold": 90.0,
          "background_color": "#1A1A1A"
        },
        {
          "type":    "arc",
          "x": 380, "y": 230,
          "width":  200, "height": 200,
          "signal": "Kraftstoff",
          "title":  "Kraftstoff",
          "normal_color":  "#FFCC00",
          "warning_color": "#FF4400",
          "warning_threshold": 20.0
        },
        {
          "type":    "led",
          "x": 600, "y": 250,
          "width":  150, "height": 150,
          "signal": "Status",
          "title":  "Warnung"
        }
      ]
    },
    {
      "title": "Details",
      "widgets": [
        {
          "type":    "bar",
          "x": 20,  "y": 50,
          "width":  760, "height": 80,
          "signal": "Kraftstoff",
          "title":  "Kraftstoff-Füllstand",
          "normal_color":  "#00CC00",
          "warning_color": "#FF4400",
          "warning_threshold": 20.0
        },
        {
          "type":    "label",
          "x": 20,  "y": 200,
          "width":  300, "height": 80,
          "signal": "Temperatur",
          "title":  "Temp. aktuell"
        }
      ]
    }
  ],
  "tx_commands": []
}
```

---

## Feld-Referenz

### Root-Objekt

| Feld          | Typ     | Pflicht | Beschreibung |
|---------------|---------|---------|--------------|
| `version`     | string  | ✅      | Muss `"1.0"` sein; zukünftige Versionen können größer sein |
| `signals`     | array   | ✅      | Liste von Signal-Objekten (1–32 Einträge) |
| `pages`       | array   | ✅      | Liste von Seiten-Objekten (1–8 Einträge) |
| `tx_commands` | array   | ❌      | Reserviert (v2); in v1 geparst aber ignoriert |

---

### Signal-Objekt

| Feld          | Typ            | Pflicht | Default | Beschreibung |
|---------------|----------------|---------|---------|--------------|
| `name`        | string         | ✅      | –       | Eindeutiger Bezeichner (max. 31 Zeichen) |
| `can_id`      | string/number  | ✅      | –       | CAN-ID als Hex-String (`"0x102"`) oder Dezimalzahl |
| `byte_offset` | number (uint)  | ✅      | –       | Startbyte im CAN-Nutzdatenfeld (0–7) |
| `byte_length` | number (uint)  | ✅      | –       | Länge in Bytes: 1, 2 oder 4 |
| `endianness`  | string         | ✅      | –       | `"little"` (Intel) oder `"big"` (Motorola) |
| `signed`      | bool           | ❌      | false   | true = vorzeichenbehafteter Integer-Rohwert |
| `float`       | bool           | ❌      | false   | true = IEEE-754 Float (scale/offset werden ignoriert) |
| `scale`       | number (float) | ❌      | 1.0     | Multiplikator: physik. Wert = raw × scale + offset |
| `offset`      | number (float) | ❌      | 0.0     | Additions-Offset nach Skalierung |
| `unit`        | string         | ❌      | `""`    | Einheitenstring (max. 7 Zeichen), z.B. `"km/h"` |
| `min`         | number (float) | ❌      | 0.0     | Anzeigebereich-Untergrenze (Clamp) |
| `max`         | number (float) | ❌      | 100.0   | Anzeigebereich-Obergrenze (Clamp) |
| `stale_ms`    | number (uint)  | ❌      | 2000    | Stale-Timeout in ms; 0 = kein Timeout |
| `simulated`   | bool           | ❌      | false   | true = Simulator erzeugt Werte (bei `CONFIG_CAN_SIMULATOR_ENABLE=y`) |

**Constraints**:
- `name` MUSS pro `signals`-Array eindeutig sein
- `min` MUSS < `max` sein
- `byte_offset + byte_length` MUSS ≤ 8 sein

---

### Widget-Objekt

| Feld                  | Typ            | Pflicht | Default        | Beschreibung |
|-----------------------|----------------|---------|----------------|--------------|
| `type`                | string         | ✅      | –              | `"gauge"`, `"chart"`, `"bar"`, `"led"`, `"label"`, `"arc"` |
| `x`                   | number (int)   | ✅      | –              | Pixel-X relativ zur Seiten-linken-oberen Ecke |
| `y`                   | number (int)   | ✅      | –              | Pixel-Y relativ zur Seiten-linken-oberen Ecke |
| `width`               | number (uint)  | ✅      | –              | Breite in Pixeln (> 0) |
| `height`              | number (uint)  | ✅      | –              | Höhe in Pixeln (> 0) |
| `signal`              | string         | ✅      | –              | Muss einem `name` in `signals[]` entsprechen |
| `title`               | string         | ❌      | `""`           | Beschriftung über/unter dem Widget (max. 31 Zeichen) |
| `normal_color`        | string         | ❌      | typ-spezifisch | Normalbereich-Farbe als `"#RRGGBB"` |
| `warning_color`       | string         | ❌      | `"#FF4400"`    | Warnbereich-Farbe als `"#RRGGBB"` |
| `warning_threshold`   | number (float) | ❌      | 0.0            | Wert ab dem warning_color greift; 0 = kein Warnbereich |
| `background_color`    | string         | ❌      | `"#000000"`    | Hintergrundfarbe als `"#RRGGBB"` |

**Farbformat**: `"#RRGGBB"` (6 Hex-Ziffern, führendes `#`, kein Alpha-Kanal).
Parser konvertiert zu `uint32_t 0xRRGGBB`.

---

### Seiten-Objekt (page)

| Feld      | Typ    | Pflicht | Beschreibung |
|-----------|--------|---------|--------------|
| `title`   | string | ❌      | Seitentitel (max. 31 Zeichen); aktuell nicht auf Display angezeigt |
| `widgets` | array  | ✅      | Liste von Widget-Objekten (0–16 Einträge) |

---

## Fehlerbehandlung

| Fehlerfall | Systemverhalten |
|------------|-----------------|
| Datei `dashboard.json` nicht gefunden | Fehlermeldung auf Display: "SD: dashboard.json nicht gefunden"; Warteschleife |
| JSON-Syntaxfehler | Fehlermeldung auf Display mit cJSON-Fehlerkontext; Warteschleife |
| Pflichtfeld fehlt | Fehlermeldung: "Fehlendes Feld '<name>' in Widget/Signal '<id>'"; Warteschleife |
| Unbekannter Widget-Typ | Warnung per `ESP_LOGW`; Widget wird übersprungen; andere Widgets bleiben |
| Signal-Referenz nicht auflösbar | Fehlermeldung: "Signal '<name>' nicht definiert (Widget auf Seite N)"; Warteschleife |
| `version` != "1.x" | Warnung; Parsing wird fortgesetzt (forward-compatible) |
| `tx_commands`-Feld vorhanden | Kein Fehler; `has_tx_commands=true` gesetzt; Inhalt ignoriert |

---

## Minimales valides Beispiel (1 Signal, 1 Widget, 1 Seite)

```json
{
  "version": "1.0",
  "signals": [
    {
      "name": "Speed",
      "can_id": "0x100",
      "byte_offset": 0,
      "byte_length": 2,
      "endianness": "little",
      "min": 0.0,
      "max": 200.0
    }
  ],
  "pages": [
    {
      "widgets": [
        {
          "type": "gauge",
          "x": 200, "y": 90,
          "width": 400, "height": 300,
          "signal": "Speed"
        }
      ]
    }
  ]
}
```
