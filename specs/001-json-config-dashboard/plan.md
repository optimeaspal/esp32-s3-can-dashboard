# Implementation Plan: JSON-basierte Dashboard-Konfiguration

**Branch**: `001-json-config-dashboard` | **Date**: 2026-06-15 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/001-json-config-dashboard/spec.md`

## Summary

Das Dashboard soll vollständig über eine `dashboard.json` auf der SD-Karte konfiguriert
werden: Signaldefinitionen (CAN-ID, Dekodierung, Skalierung), Widget-Layout (Typ,
Position, Farben, 6 Widget-Typen) und Seitenstruktur (Multi-Page + Touch-Navigation).
Technischer Ansatz: neues `config_loader`-Modul (nativ testbar) parst cJSON →
DashboardConfig-Structs; neues `widget_registry`-Modul dispatcht Widget-Create/Update
über Funktionstabelle; HAL-seitiger `waveshare_sd_port` liest die Datei via SPI+CH422G.

## Technical Context

**Language/Version**: C (C99), ESP-IDF ≥ 5.1.0

**Primary Dependencies**:
- LVGL 8.3.x (bestehend, via idf_component.yml)
- cJSON (im ESP-IDF enthalten, kein separates Download nötig)
- ESP-IDF FATFS + SPI master (für SD-Karte)
- FreeRTOS (bestehend, LVGL-Mutex, CAN-Queue)

**Storage**: SD-Karte (FAT32) – `dashboard.json` im Root-Verzeichnis

**Testing**:
- Native: Unity via `pio test -e native` (config_loader, can_signal)
- Hardware ohne CAN: `CONFIG_CAN_SIMULATOR_ENABLE=y` + SD-Karte
- Hardware mit CAN: USB-CAN-A-Adapter (Waveshare-Tool) oder M12 DeviceNet

**Target Platform**: ESP32-S3-Touch-LCD-7 (800×480, ESP-IDF 5.x, PlatformIO)

**Project Type**: Embedded firmware (standalone device)

**Performance Goals**:
- Dashboard sichtbar in < 3 s nach Power-on (exkl. SD-Init-Zeit)
- Stabiler 24-h-Betrieb ohne Absturz/Speicherleck bei ≤ 8 Widgets auf 2 Seiten

**Constraints**:
- Heap-Nutzung für cJSON-Parse-Tree: max. ~16 kB (cJSON_Delete direkt nach Parse)
- SD-CS wird nicht über direkten GPIO gesteuert, sondern über CH422G-IO-Expander
  (I2C) → erfordert custom HAL-Wrapper (siehe research.md)
- LVGL v8 nicht thread-safe → alle Widget-Operationen unter `lvgl_port_lock`
- Alle neuen Dateien in C (C99), keine C++-Erweiterungen

**Scale/Scope**: ≤ 32 Signale, ≤ 8 Seiten, ≤ 16 Widgets pro Seite (v1-Limits,
defensiv gewählt; konfigurierbar via `#define` in `config_types.h`)

## Constitution Check

*GATE: Muss vor Phase-0-Research bestanden sein. Re-Check nach Phase-1-Design.*

| Prinzip | Anforderung | Bewertung |
|---------|-------------|-----------|
| **I. HAL/App-Trennung** | SD-Dateilesen → `src/hal/waveshare_sd_port.c`; JSON-Parsing → `src/app/config_loader.c` (keine ESP-IDF-Abhängigkeit); Widget-Rendering → `src/ui/` | ✅ Konform |
| **II. C-First** | Alle neuen Module in `.c`/`.h`; cJSON ist reines C; keine C++-Features | ✅ Konform |
| **III. Test-First** | `config_loader.c` ist App-Logik → Unity-Tests in `test/test_config_loader.c` MÜSSEN vor Implementierung geschrieben und rot sein | ✅ Konform – Tests sind Phase-3-Gate |
| **IV. Konfiguration via Kconfig** | SD-SPI-Pins als Kconfig-Symbole; v1-Limits (`MAX_SIGNALS`, `MAX_PAGES`) als `#define` in `config_types.h` (build-time, nicht runtime) | ✅ Konform |
| **V. LVGL-Thread-Safety** | `dashboard_create()` und `dashboard_tick()` bleiben unter `lvgl_port_lock`; Widget-Registry-Aufrufe nur aus LVGL-Task | ✅ Konform |

**Constitution Check: BESTANDEN** – keine Violations, kein Complexity-Tracking erforderlich.

## Project Structure

### Documentation (dieses Feature)

```text
specs/001-json-config-dashboard/
├── plan.md              ← diese Datei
├── research.md          ← Phase-0-Output
├── data-model.md        ← Phase-1-Output
├── quickstart.md        ← Phase-1-Output (Test-Guide)
├── contracts/
│   └── dashboard-json-schema.md   ← JSON-Vertragsformat
└── tasks.md             ← Phase-2-Output (/speckit-tasks)
```

### Source Code (Repository-Root)

```text
src/
├── Kconfig.projbuild       (ERWEITERN – SD-SPI-Pins, Limits)
├── main.c                  (REFACTOR – config laden, dynamisch an dispatcher/dashboard übergeben)
├── hal/
│   ├── lvgl_port.c/h       (unverändert)
│   ├── waveshare_rgb_lcd_port.c/h  (unverändert)
│   ├── waveshare_twai_port.c/h     (unverändert)
│   └── waveshare_sd_port.c/h       (NEU – SPI+CH422G → FATFS mount + Dateilesen)
├── app/
│   ├── can_signal.c/h      (unverändert – struct bereits passend)
│   ├── can_dispatcher.c/h  (minimale Anpassung – Signaltabelle kommt jetzt aus Config)
│   ├── can_simulator.c/h   (minimale Anpassung – is_simulated Flag aus Config)
│   ├── config_types.h      (NEU – DashboardConfig, PageConfig, WidgetConfig Structs)
│   └── config_loader.c/h   (NEU – cJSON → DashboardConfig, nativ testbar)
└── ui/
    ├── dashboard.c/h       (REFACTOR – akzeptiert DashboardConfig statt statischer Tabelle)
    ├── widget_registry.c/h (NEU – Funktionstabelle: type_name → create_fn + update_fn)
    ├── nav_indicator.c/h   (NEU – Navigationspunkte unten, nur bei ≥ 2 Seiten)
    └── widgets/
        ├── widget_gauge.c/h   (NEU)
        ├── widget_chart.c/h   (NEU)
        ├── widget_bar.c/h     (NEU)
        ├── widget_led.c/h     (NEU)
        ├── widget_label.c/h   (NEU)
        └── widget_arc.c/h     (NEU)

test/
├── fixtures/               (NEU – JSON-Testdateien für native Tests)
│   ├── valid_2widget_1page.json
│   ├── valid_8widget_2page.json
│   ├── invalid_syntax.json
│   └── missing_required_field.json
├── test_can_signal.c       (unverändert)
├── test_config_loader.c    (NEU – Unit-Tests für JSON-Parsing)
└── test_config_types.c     (NEU – Entity-Validierungstests)
```

**Structure Decision**: Single-Project-Layout (`src/` + `test/` im Repository-Root),
konsistent mit bestehendem POC. HAL/App/UI-Trennung aus Constitution Prinzip I wird
strikt eingehalten.
