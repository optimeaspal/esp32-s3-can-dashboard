---
description: "Task list for 001-json-config-dashboard"
---

# Tasks: JSON-basierte Dashboard-Konfiguration

**Input**: Design documents from `specs/001-json-config-dashboard/`

**Prerequisites**: plan.md ✅ | spec.md ✅ | research.md ✅ | data-model.md ✅ | contracts/ ✅ | constitution.md ✅

**Tests**: Inklusive — Constitution Prinzip III mandatiert Test-First für alle `src/app/`-Logik.

**Organization**: Aufgaben nach User Story gruppiert für unabhängige Implementierung und Validierung.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Parallel ausführbar (verschiedene Dateien, keine offenen Abhängigkeiten)
- **[Story]**: Zugehörige User Story (US1–US4)
- Alle Pfade relativ zum Repository-Root

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Konfigurationsinfrastruktur, Test-Stubs und Fixture-Dateien bereitstellen.

- [X] T001 Kconfig.projbuild erweitern: SD_SPI_MOSI=11, SD_SPI_MISO=13, SD_SPI_CLK=12 und SD_CS_CH422G_PIN als Kconfig-Symbole in `src/Kconfig.projbuild`
- [X] T002 [P] `test/stubs/esp_err.h` erstellen: `typedef int esp_err_t;`, `#define ESP_OK 0`, `#define ESP_ERR_INVALID_ARG 0x102`, `#define ESP_ERR_NOT_FOUND 0x105` für native Testbarkeit ohne ESP-IDF
- [X] T003 [P] Test-Fixtures anlegen: `test/fixtures/valid_2widget_1page.json`, `test/fixtures/valid_8widget_2page.json`, `test/fixtures/invalid_syntax.json`, `test/fixtures/missing_required_field.json` gemäß quickstart.md-Spezifikation

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Datenmodell, HAL-API, Parser-API und Test-Rümpfe — alles, was ALLE User Stories blockiert.

**⚠️ CRITICAL**: Kein User-Story-Code beginnt, bevor diese Phase abgeschlossen ist. `pio test -e native` muss nach T010 grün sein.

- [X] T004 `src/app/config_types.h` erstellen: Limits (`CFG_MAX_SIGNALS=32`, `CFG_MAX_PAGES=8`, `CFG_MAX_WIDGETS_PER_PAGE=16`), `widget_style_t`, `widget_config_t`, `page_config_t`, `dashboard_config_t` gemäß data-model.md
- [X] T005 [P] `src/hal/waveshare_sd_port.h` erstellen: `waveshare_sd_port_init()`, `waveshare_sd_read_file(path, buf, max_len)` — kein ESP-IDF-Include in Header
- [X] T006 [P] `src/app/config_loader.h` erstellen: `config_loader_parse(json_str, cfg, err_buf, err_len)` API-Deklaration mit build-guard `#ifdef ESP_IDF_VERSION` für `esp_err_t` vs. `test/stubs/esp_err.h`
- [X] T007 [P] `test/test_config_types.c` schreiben (TEST-FIRST — muss rot sein): `test_min_less_than_max`, `test_signal_name_unique`, `test_widget_count_limit` — nach T004
- [X] T008 [P] `test/test_config_loader.c` schreiben (TEST-FIRST — muss rot sein): `test_parse_valid_1page`, `test_parse_valid_2pages`, `test_parse_invalid_syntax`, `test_parse_missing_field`, `test_signal_binding_resolve`, `test_signal_binding_unknown`, `test_color_parse_hex` — nach T004 + T006
- [X] T009 `src/hal/waveshare_sd_port.c` implementieren: SPI2-Init (MOSI/MISO/CLK via Kconfig), CH422G-CS-Callbacks (`pre_cb`/`post_cb`), FATFS-Mount via `f_mount()`, Dateilesen via `f_open`/`f_read`/`f_close` — nach T005; MUSS nach `waveshare_rgb_lcd_init()` aufgerufen werden
- [X] T010 `src/app/config_loader.c` implementieren: cJSON-Parse-Tree → `dashboard_config_t`, Signal-Array, Seiten + Widgets, `signal_name`-Auflösung zu `signal_idx`, Pflichtfeld-Validierung, `cJSON_Delete` direkt nach Parse, `has_tx_commands` setzen — nach T004 + T006 + T007 rot + T008 rot; `pio test -e native` muss danach grün sein

**Checkpoint**: `pio test -e native` grün — User-Story-Implementierung kann jetzt beginnen.

---

## Phase 3: User Story 1 – Widget-Layout per JSON konfigurieren (Priority: P1) 🎯 MVP

**Goal**: SD-Karte mit valider `dashboard.json` einlegen → Board bootet → Widgets erscheinen exakt wie konfiguriert (Position, Größe, Typ, Signal-Zuordnung).

**Independent Test**: SD-Karte mit `valid_2widget_1page.json` (Gauge + Chart) → Board booten → Dashboard zeigt 2 Widgets an konfigurierten Positionen. JSON ändern (anderer Widget-Typ) → Reboot → neue Konfiguration sichtbar.

### Implementation für User Story 1

- [X] T011 [P] [US1] `src/ui/widget_registry.h` erstellen: `widget_descriptor_t` (type_name, create, update, set_stale), `widget_create_fn_t`, `widget_update_fn_t`, `widget_stale_fn_t` typedefs, `widget_registry_find_by_type()` Deklaration
- [X] T012 [P] [US1] `src/ui/widgets/widget_gauge.c/h` implementieren: `gauge_create(parent, cfg)`, `gauge_update(obj, value)`, `gauge_stale(obj, stale)` mit `lv_meter` LVGL v8 Widget
- [X] T013 [P] [US1] `src/ui/widgets/widget_chart.c/h` implementieren: `chart_create`, `chart_update`, `chart_stale` mit `lv_chart` (Zeitreihen-Liniengraph)
- [X] T014 [P] [US1] `src/ui/widgets/widget_bar.c/h` implementieren: `bar_create`, `bar_update`, `bar_stale` mit `lv_bar`
- [X] T015 [P] [US1] `src/ui/widgets/widget_led.c/h` implementieren: `led_create`, `led_update` (on/off bei threshold), `led_stale` mit `lv_led`
- [X] T016 [P] [US1] `src/ui/widgets/widget_label.c/h` implementieren: `label_create`, `label_update` (float → String), `label_stale` mit `lv_label`
- [X] T017 [P] [US1] `src/ui/widgets/widget_arc.c/h` implementieren: `arc_create`, `arc_update`, `arc_stale` mit `lv_arc` (Halbkreis-Gauge)
- [X] T018 [US1] `src/ui/widget_registry.c` implementieren: statische `s_registry[]`-Tabelle mit 6 Einträgen (gauge/chart/bar/led/label/arc), `widget_registry_find_by_type()` via linearer Suche — nach T011–T017
- [X] T019 [US1] `src/ui/dashboard.c/h` refaktorieren: `dashboard_create(cfg)` akzeptiert `const dashboard_config_t *`, erstellt Widgets via `widget_registry_find_by_type()`, bindet `signal_idx` an Widget-Handle-Array; `dashboard_tick()` verarbeitet `can_value_event_t` aus Queue und ruft `widget_update_fn` auf — nach T018; alle LVGL-Operationen unter `lvgl_port_lock`
- [X] T020 [US1] `src/main.c` refaktorieren: SD-Init → `waveshare_sd_read_file("/dashboard.json")` → `config_loader_parse()` → `dashboard_create(cfg)` Pipeline; Fehlerfall-Anzeige auf Display bei SD-Fehler oder JSON-Fehler — nach T009, T010, T019

**Checkpoint**: US1 vollständig funktional — SD-Karte mit 2-Widget-JSON → Dashboard sichtbar.

---

## Phase 4: User Story 2 – CAN-Signale im JSON definieren (Priority: P2)

**Goal**: Beliebige CAN-Signale (ID, Offset, Länge, Endianness, Scale, Offset, Einheit, Stale-Timeout) vollständig per JSON konfigurieren — ohne Quellcode-Änderung.

**Independent Test**: `dashboard.json` mit neuem Signal (CAN-ID 0x200, Big-Endian, 2 Bytes, scale=0.1, unit="km/h") anlegen; USB-CAN-Adapter sendet Frame → Widget zeigt korrekten skalierten Wert. Signal ohne Daten → Widget graut nach `stale_ms` aus.

### Implementation für User Story 2

- [X] T021 [US2] `src/app/can_dispatcher.c` anpassen: Signaltabelle aus `dashboard_config_t.signals[]` statt statischer Tabelle; `can_dispatcher_init(cfg)` empfängt Config-Pointer; TWAI-Frame-Matching gegen dynamische Signal-Liste; `signal_idx` aus Config für `can_value_event_t` — nach T019
- [X] T022 [US2] `src/app/can_simulator.c` anpassen: Nur Signale mit `is_simulated=true` simulieren; Simulator iteriert über `dashboard_config_t.signals[]` und prüft Flag vor Wertgenerierung — nach T021
- [X] T023 [US2] Stale-Timeout in `src/app/can_dispatcher.c` implementieren: Zeitstempel pro Signal bei Frame-Empfang aktualisieren; separater Stale-Check-Task oder Tick im Dispatcher prüft `timeout_ms` und sendet Stale-Event in Queue wenn überschritten — nach T021; `dashboard_tick()` ruft daraufhin `widget_stale_fn(obj, true)` auf

**Checkpoint**: US2 vollständig — CAN-Signale aus JSON dekodiert, Stale-Grauschaltung aktiv.

---

## Phase 5: User Story 4 – Widget-Erscheinungsbild vollständig konfigurieren (Priority: P2)

**Goal**: Jedes Widget zeigt Farben, Warnbereich, Titel und typ-spezifische Eigenschaften exakt wie in der JSON konfiguriert — kein Codeumbau nötig.

**Independent Test**: JSON mit Gauge (normal_color="#00AA00", warning_color="#FF0000", warning_threshold=80, title="Druck bar") → Board booten → Gauge zeigt Titel, grünen Normalbereich und roten Warnbereich ab 80.

### Implementation für User Story 4

- [X] T024 [US4] `src/app/config_loader.c` erweitern: `widget_style_t`-Felder parsen — `normal_color`, `warning_color`, `warning_threshold`, `background_color` aus JSON-Hex-Strings (`#RRGGBB` → `uint32_t`); fehlende Farben auf Typ-Defaults setzen (0 = nicht gesetzt) — nach T010
- [X] T025 [P] [US4] `src/ui/widgets/widget_gauge.c` style-Anwendung: `normal_color` auf Meter-Hauptbereich, `warning_color`/`warning_threshold` auf Warnbereich, `title` als Label — nach T012, T024
- [X] T026 [P] [US4] `src/ui/widgets/widget_chart.c` style-Anwendung: `normal_color` als Linienfarbe (`lv_chart_set_series_color`), Y-Achse aus Signal-min/max konfigurieren — nach T013, T024
- [X] T027 [P] [US4] `src/ui/widgets/widget_bar.c` style-Anwendung: `normal_color` als Füllfarbe, `warning_color` ab `warning_threshold` — nach T014, T024
- [X] T028 [P] [US4] `src/ui/widgets/widget_led.c` style-Anwendung: `normal_color` als LED-Farbe, `warning_threshold` als binären Umschaltwert — nach T015, T024
- [X] T029 [P] [US4] `src/ui/widgets/widget_label.c` style-Anwendung: `normal_color` als Textfarbe, `background_color` als Container-Hintergrund, `title` als Label-Präfix — nach T016, T024
- [X] T030 [P] [US4] `src/ui/widgets/widget_arc.c` style-Anwendung: `normal_color` auf Arc-Indikatorfarbe, `warning_color`/`warning_threshold` via `lv_arc` Style-Segmente — nach T017, T024

**Checkpoint**: US4 vollständig — alle Farben, Titel und Warnbereiche aus JSON aktiv.

---

## Phase 6: User Story 3 – Mehrere Dashboard-Seiten (Priority: P3)

**Goal**: Mehrere Seiten in der JSON konfigurieren; per Wischgeste oder Navigationspunkt zwischen Seiten navigieren; bei nur 1 Seite keine Navigationsleiste.

**Independent Test**: `valid_8widget_2page.json` (2 Seiten à 2 Widgets) → Board booten → Seite 1 sichtbar, 2 Navigationspunkte unten; Swipe links → Seite 2; Punkttippen → Seite 1; JSON auf 1 Seite reduzieren → keine Punkte.

### Implementation für User Story 3

- [X] T031 [US3] `src/ui/dashboard.c` Multi-Page umbauen: `lv_tileview_create()` als Root-Container; pro Seite ein `lv_tileview_add_tile(tv, page_idx, 0, LV_DIR_HOR)`; `LV_EVENT_VALUE_CHANGED`-Callback `on_page_changed()`; Tileview-Höhe = `LV_VER_RES - NAV_HEIGHT` (NAV_HEIGHT=0 bei 1 Seite) — nach T019
- [X] T032 [P] [US3] `src/ui/nav_indicator.c/h` implementieren: `nav_indicator_create(parent, page_count)` erstellt Container mit runden Punkt-Objekten (grau = inaktiv, weiß = aktiv); `nav_indicator_set_active(idx)` aktualisiert aktive Seite; kein Aufruf bei `page_count <= 1` — nach Phase 2
- [X] T033 [US3] `src/ui/dashboard.c` nav_indicator integrieren: `nav_indicator_create()` nur wenn `cfg->page_count >= 2`; `on_page_changed()` ruft `nav_indicator_set_active()` auf; Tipp auf Navigationspunkt via `lv_obj_add_event_cb` springt zu Seite — nach T031, T032

**Checkpoint**: US3 vollständig — Multi-Page-Navigation per Swipe und Navigationspunkte.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Edge Cases, FR-Compliance-Checks, Fehlermeldungsanzeige und Abschlussvalidierung.

- [X] T034 [P] Fehlermeldungsanzeige auf Display in `src/main.c`: Hilfsfunktion `show_error_screen(msg)` die vor LVGL-Task-Start einen `lv_label` mit Fehlermeldung anzeigt; aufgerufen bei SD-Mount-Fehler ("SD: dashboard.json nicht gefunden") und bei JSON-Parse-Fehler (err_buf aus config_loader_parse) — nach T020
- [X] T035 [P] FR-013 tx_commands-Feld in `src/app/config_loader.c`: `"tx_commands"`-Key via `cJSON_HasObjectItem()` prüfen; `dashboard_config_t.has_tx_commands = true` setzen; kein Fehler, kein Parsing des Inhalts — nach T010
- [X] T036 [P] Unbekannte Widget-Typen in `src/ui/widget_registry.c`: Wenn `widget_registry_find_by_type()` keinen Treffer findet → `ESP_LOGW("widget_registry", "Unbekannter Typ: %s – übersprungen", type)` und NULL zurückgeben; `dashboard.c` behandelt NULL-Return (Widget wird nicht erstellt, andere bleiben) — nach T018
- [X] T037 Widget-Wert-Clamping in allen `widget_*_update()`: Wert auf `[signal.min, signal.max]` klemmen bevor LVGL-Wert gesetzt wird; kein Absturz bei Wert außerhalb Bereich — nach T025–T030
- [X] T038 Abschlussvalidierung: Quickstart.md Stufe 2 vollständig durchführen (alle 9 Validierungsschritte aus Tabelle); `pio test -e native` grün; Ergebnis in Commit-Message dokumentieren

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: Keine Abhängigkeiten — sofort starten
- **Foundational (Phase 2)**: Depends on Phase 1 — BLOCKIERT alle User Stories
- **US1 (Phase 3)**: Depends on Phase 2 complete + `pio test -e native` grün
- **US2 (Phase 4)**: Depends on Phase 2 + US1 (T019, T020)
- **US4 (Phase 5)**: Depends on Phase 2 (T010) + US1-Widget-Implementierungen (T012–T017)
- **US3 (Phase 6)**: Depends on US1 (T019) + Phase 2
- **Polish (Phase 7)**: Depends on alle gewünschten User Stories abgeschlossen

### User Story Dependencies

| Story | Kann beginnen nach | Abhängigkeit von anderen Stories |
|-------|--------------------|----------------------------------|
| US1 (P1) | Phase 2 complete | Keine |
| US2 (P2) | Phase 2 + US1 T019 | Signaltabelle aus dashboard_create |
| US4 (P2) | Phase 2 T010 + US1 T012–T017 | Widget-Implementierungen müssen existieren |
| US3 (P3) | Phase 2 + US1 T019 | dashboard.c muss existieren |

### Intra-Phase Parallelität

**Phase 2**: T005 + T006 + T007 parallel nach T004; T009 nach T005; T010 nach T006 + T007 rot + T008 rot  
**Phase 3**: T012 + T013 + T014 + T015 + T016 + T017 + T011 alle parallel; T018 nach T011–T017; T019 nach T018; T020 nach T009 + T010 + T019  
**Phase 5**: T025 + T026 + T027 + T028 + T029 + T030 alle parallel nach T024  

---

## Parallel Example: US1 Widget-Implementierungen

```
# 6 Widgets gleichzeitig starten (alle verschiedene Dateien):
Task T012: src/ui/widgets/widget_gauge.c/h
Task T013: src/ui/widgets/widget_chart.c/h
Task T014: src/ui/widgets/widget_bar.c/h
Task T015: src/ui/widgets/widget_led.c/h
Task T016: src/ui/widgets/widget_label.c/h
Task T017: src/ui/widgets/widget_arc.c/h

# Erst wenn alle 6 fertig: T018 widget_registry.c
```

## Parallel Example: US4 Style-Anwendung

```
# 6 Style-Tasks gleichzeitig nach T024:
Task T025: widget_gauge.c style
Task T026: widget_chart.c style
Task T027: widget_bar.c style
Task T028: widget_led.c style
Task T029: widget_label.c style
Task T030: widget_arc.c style
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Phase 1 abschließen: Setup (T001–T003)
2. Phase 2 abschließen: Foundational — `pio test -e native` muss grün sein (T004–T010)
3. Phase 3 abschließen: US1 — Widget-Layout per JSON (T011–T020)
4. **STOP & VALIDATE**: SD-Karte mit `valid_2widget_1page.json` → Dashboard sichtbar
5. Ggf. deployen/demonstrieren

### Incremental Delivery

1. Setup + Foundational → Testinfrastruktur bereit
2. US1 fertig → `pio test -e native` grün, Dashboard mit JSON-Layout sichtbar (MVP!)
3. US2 fertig → CAN-Signale dynamisch aus JSON, Stale-Timeout aktiv
4. US4 fertig → Farben, Warnbereiche, Titel aus JSON
5. US3 fertig → Multi-Page-Navigation
6. Polish → Edge Cases, Fehlermeldungen, Abschlussvalidierung

### Constitution-Compliance Checklist

| Prinzip | Abgedeckt durch |
|---------|-----------------|
| I. HAL/App-Trennung | T009 (HAL), T010 (App ohne HAL-Import) |
| II. C-First | Alle Tasks erzeugen `.c`/`.h`-Dateien |
| III. Test-First | T007 + T008 vor T010 (rot → grün) |
| IV. Kconfig | T001 (SD-Pins als Kconfig-Symbole) |
| V. LVGL-Thread-Safety | T019 (alle Widget-Ops unter `lvgl_port_lock`) |

---

## Notes

- `[P]` = verschiedene Dateien, keine offenen Abhängigkeiten auf unfertige Tasks
- `[Story]` mappt Task auf User Story für Traceability
- Jede User Story ist unabhängig implementierbar und testbar
- Tests (T007, T008) MÜSSEN rot sein bevor T010 implementiert wird
- Nach jeder Phase: `pio test -e native` ausführen
- Commit nach jedem abgeschlossenen Task oder logischer Gruppe
- Unbekannte Widget-Typen: überspringen (T036), kein Absturz — konform mit spec.md Edge Cases
