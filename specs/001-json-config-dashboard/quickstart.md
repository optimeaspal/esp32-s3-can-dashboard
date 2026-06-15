# Quickstart & Validation Guide: JSON-basierte Dashboard-Konfiguration

**Feature**: 001-json-config-dashboard
**Date**: 2026-06-15

Dieses Dokument beschreibt drei Validierungsstufen: native Unit-Tests,
Hardware-Tests ohne CAN-Quelle und Hardware-Tests mit CAN-Quelle.

---

## Voraussetzungen (alle Stufen)

- PlatformIO CLI installiert (`pio --version`)
- Repository geklont und im Projektverzeichnis

```bash
cd C:\Users\pal\Documents\2_git\projects\ESP32-S3-Touch-LCD-7
```

---

## Stufe 1 – Native Unit-Tests (kein Board, kein CAN)

**Testet**: JSON-Parser-Logik, Signal-Binding-Auflösung, Fehlerbehandlung

### Voraussetzung

Test-Fixtures liegen in `test/fixtures/`:
- `valid_2widget_1page.json` – Basis-Validierung
- `valid_8widget_2page.json` – Multi-Page-Validierung
- `invalid_syntax.json` – absichtlicher JSON-Syntaxfehler
- `missing_required_field.json` – fehlendes Pflichtfeld `byte_length`

### Ausführen

```bash
pio test -e native
```

### Erwartetes Ergebnis

```
test/test_can_signal.c: PASS  (bestehende Tests)
test/test_config_loader.c:
  test_parse_valid_1page ........ PASS
  test_parse_valid_2pages ....... PASS
  test_parse_invalid_syntax ..... PASS  (ESP_ERR_INVALID_ARG erwartet)
  test_parse_missing_field ...... PASS  (ESP_ERR_INVALID_ARG erwartet)
  test_signal_binding_resolve ... PASS  (signal_idx ≥ 0)
  test_signal_binding_unknown ... PASS  (ESP_ERR_NOT_FOUND erwartet)
  test_color_parse_hex .......... PASS
test/test_config_types.c:
  test_min_less_than_max ........ PASS
  test_signal_name_unique ....... PASS

----- Passed 10 test(s) -----
```

---

## Stufe 2 – Hardware ohne CAN-Quelle (SD-Karte + Simulator)

**Testet**: SD-Karten-Zugriff, Widget-Rendering, Touch-Navigation,
Stale-Timeout mit Simulator

### Voraussetzung

1. **SD-Karte** (FAT32 formatiert) mit `dashboard.json` im Root-Verzeichnis

   Empfohlene Test-JSON (`dashboard.json`):
   ```json
   {
     "version": "1.0",
     "signals": [
       { "name":"RPM","can_id":"0x102","byte_offset":0,"byte_length":2,
         "endianness":"little","min":0,"max":6000,"unit":"1/min",
         "stale_ms":2000,"simulated":true },
       { "name":"Temp","can_id":"0x02A","byte_offset":0,"byte_length":4,
         "endianness":"little","float":true,"min":0,"max":120,"unit":"°C",
         "stale_ms":3000,"simulated":false }
     ],
     "pages": [
       { "title":"Seite1", "widgets": [
           { "type":"gauge","x":10,"y":10,"width":350,"height":350,
             "signal":"RPM","title":"Drehzahl",
             "normal_color":"#00AA00","warning_color":"#FF4400",
             "warning_threshold":5000 },
           { "type":"chart","x":380,"y":10,"width":400,"height":200,
             "signal":"Temp","title":"Temperatur","normal_color":"#0088FF" }
       ]},
       { "title":"Seite2", "widgets": [
           { "type":"bar","x":20,"y":50,"width":760,"height":80,
             "signal":"RPM","title":"Drehzahl Bar","warning_threshold":5000 }
       ]}
     ]
   }
   ```

2. **sdkconfig.defaults**: `CONFIG_CAN_SIMULATOR_ENABLE=y` gesetzt

3. Board per USB-C angeschlossen (COM3 oder ähnlich)

### Build & Flash

```bash
pio run -e esp32-s3-touch-lcd-7 -t upload
pio device monitor
```

### Validierungsschritte

| Schritt | Aktion | Erwartetes Ergebnis |
|---------|--------|---------------------|
| SD-Booten | SD eingelegt, Board einschalten | Dashboard erscheint in < 3 s; Seite 1 mit Gauge + Chart sichtbar |
| Gauge läuft | Warten ~2 s | Gauge-Zeiger bewegt sich (Simulator, RPM animiert) |
| Stale-Test | Seite 1: Temp-Chart beobachten | Nach 3 s Stale-Timeout wird Chart ausgegraut (kein Simulator für Temp) |
| Navigation Swipe | Finger von rechts nach links wischen | Seite 2 erscheint mit Bar-Widget |
| Navigation Dots | Linken Punkt antippen | Seite 1 erscheint; aktiver Punkt wechselt |
| 1 Seite = kein Nav | JSON auf 1 Seite reduzieren, Reboot | Keine Navigationspunkte sichtbar |
| SD fehlt | SD-Karte entfernen, Reboot | Fehlermeldung "SD: dashboard.json nicht gefunden" auf Display |
| Fehler-JSON | `dashboard.json` mit `"signals":` löschen, Reboot | Fehlermeldung mit Feldname auf Display; kein Absturz |
| Farbtest | `"normal_color":"#FF0000"` setzen, Reboot | Gauge-Normalbereich erscheint rot |

---

## Stufe 3 – Hardware mit echter CAN-Quelle

**Testet**: echte CAN-Frame-Dekodierung, Endianness, Scale/Offset, Stale-Timeout

### Voraussetzung

1. **sdkconfig.defaults**: `CONFIG_CAN_SIMULATOR_ENABLE=n`
2. **USB-CAN-A-Adapter** an PC (Tool: `documents/USBCANV2.10.zip` installieren)
3. **Verkabelung**:
   - USB-CAN-A → CAN-H / CAN-L Kabel am Board (PH2.0 2-Pin-Stecker)
   - GND verbinden (Board-GND ↔ CAN-Adapter-GND)
   - Terminierung: 120 Ω am USB-CAN-A-Ende einstellen (falls Adapter-Feature)
4. **dashboard.json** mit `"simulated": false` für alle Signale

### CAN-Test-Frame-Tabelle

Die folgende Tabelle gilt für die Beispiel-JSON aus Stufe 2 (ohne `simulated: true`):

| Signal | CAN-ID | Beispiel-Frame (8 Bytes, Hex) | Erwarteter physik. Wert |
|--------|--------|-------------------------------|------------------------|
| RPM | 0x102 | `B8 0B 00 00 00 00 00 00` | 3000 RPM (0x0BB8 = 3000, LE) |
| RPM Warn | 0x102 | `70 13 00 00 00 00 00 00` | 4976 RPM (Warnbereich bei 5000) |
| Temp (Float) | 0x02A | `00 00 48 42 00 00 00 00` | 50.0 °C (IEEE-754 LE: 0x42480000) |
| RPM Stale | (kein Frame) | – | Gauge ausgegraut nach 2000 ms |

### Build & Flash

```bash
# CONFIG_CAN_SIMULATOR_ENABLE=n sicherstellen
pio run -e esp32-s3-touch-lcd-7 -t upload
pio device monitor
```

### Validierungsschritte

| Schritt | Aktion | Erwartetes Ergebnis |
|---------|--------|---------------------|
| Grundfunktion | RPM-Frame `B8 0B 00 00...` senden (1 Hz) | Gauge zeigt 3000 RPM |
| Warnfarbe | RPM-Frame `70 13 00 00...` senden | Gauge wechselt in Warnfarbe (RPM > 5000? Nein → prüfe threshold) |
| Float-Signal | Temp-Frame `00 00 48 42...` senden | Chart zeigt 50.0 °C |
| Scale-Test | Signal mit `scale=0.1,offset=-40` konfigurieren; Frame 500 dezimal senden | Widget zeigt 10.0 |
| Endianness BE | Signal mit `"endianness":"big"`, `byte_length=2`; Frame `0B B8 00...` senden | Widget zeigt 3000 (0x0BB8 = 3000 BE) |
| Stale-Timeout | Frames stoppen (USB-CAN pausieren) | Widget graut nach `stale_ms` ms aus |
| Stale-Recovery | Frames wieder senden | Widget wird wieder aktiv (Farbe kehrt zurück) |
| Mehrere Signale | RPM + Temp gleichzeitig senden | Beide Widgets aktualisieren unabhängig |

---

## Logging

Während aller Hardware-Tests läuft `pio device monitor` und zeigt ESP-IDF-Logs.
Relevante Log-Tags:

| Tag | Bedeutung |
|-----|-----------|
| `config_loader` | JSON-Parsing Schritte; Fehlerdetails |
| `waveshare_sd` | SD-Mount-Status, Dateizugriff |
| `dashboard` | Widget-Erstellung, Seiten-Setup |
| `can_dispatcher` | Empfangene Frames, Signal-Matching |
| `can_simulator` | Simulierte Werte (nur bei SIMULATOR_ENABLE=y) |
