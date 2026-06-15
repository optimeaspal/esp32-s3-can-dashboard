<!--
SYNC IMPACT REPORT
==================
Version change: (template) → 1.0.0
Modified principles: N/A (initial ratification)
Added sections:
  - Core Principles (5 principles)
  - Hardware Constraints & Platform Rules
  - Development Workflow
  - Governance
Removed sections: N/A
Templates requiring updates:
  ✅ .specify/templates/plan-template.md — Constitution Check gates align with principles below
  ✅ .specify/templates/spec-template.md — no mandatory section changes required
  ✅ .specify/templates/tasks-template.md — task categories (HAL vs App, Test-First) align
Deferred TODOs: none
-->

# ESP32-S3 CAN Dashboard Constitution

## Core Principles

### I. HAL/App-Trennung

Hardware-Abstraktions-Code (Display-Init, TWAI-Init, IO-Expander) MUSS in `src/hal/`
liegen und darf keine Anwendungslogik enthalten. Anwendungslogik (`can_signal`,
`can_dispatcher`) MUSS in `src/app/` liegen und DARF KEINE direkten ESP-IDF- oder
Hardware-Aufrufe enthalten – nur abstrakte Schnittstellen.

Rationale: `src/app/` muss nativ kompilierbar und ohne Board testbar sein
(`pio test -e native`). Wer HAL-Abhängigkeiten in App-Code einbringt, bricht den
nativen Test-Umgebung.

### II. C-First (kein C++)

Der gesamte Projektcode MUSS in C (C99 oder C11) geschrieben werden. C++ ist
ausschließlich dort erlaubt, wo ESP-IDF- oder LVGL-Internals es technisch erzwingen.
Neue Dateien MÜSSEN `.c`/`.h`-Endungen tragen.

Rationale: Der Projekteigentümer hat C-Hintergrund; der Waveshare-Demo-Code ist
reines C. C++ würde die Einstiegshürde unnötig erhöhen und die native Testbarkeit
erschweren.

### III. Test-First für Datenmodell-Logik

Für alle Logik in `src/app/` (Signal-Dekodierung, Dispatcher, Skalierung,
Endianness-Handling) MÜSSEN Unity-Tests in `test/` GESCHRIEBEN werden, BEVOR die
Implementierung beginnt. Tests MÜSSEN zunächst fehlschlagen (Red), bevor die
Implementierung sie grün macht (Green).

Ausnahme: HAL-Code (`src/hal/`) und UI-Code (`src/ui/`) sind von dieser Pflicht
ausgenommen – diese werden manuell auf Hardware verifiziert.

Rationale: Signal-Dekodierungsfehler sind schwer auf echter Hardware zu debuggen.
Frühe Regressionserkennung über `pio test -e native` spart Debugging-Zeit erheblich.

### IV. Konfiguration via Kconfig – keine Magic Numbers

Alle konfigurierbaren Werte (CAN-Bitrate, TX/RX-GPIO, Stale-Timeout,
RX-Queue-Länge) MÜSSEN als Kconfig-Symbole in `src/Kconfig.projbuild` definiert
und über `sdkconfig.defaults` gesetzt werden. Hardkodierte Konstanten für
Board-Konfigurationsparameter sind VERBOTEN.

Rationale: Das Projekt soll auf weiterer Hardware portierbar bleiben. Kconfig
ermöglicht Konfigurationsänderungen ohne Code-Modifikation und `menuconfig`-Support.

### V. LVGL-Thread-Safety

Alle LVGL-Widget-Operationen MÜSSEN innerhalb des LVGL-Tasks oder unter einem
LVGL-Mutex (`lvgl_port_lock`) ausgeführt werden. Direkte Widget-Updates aus
fremden FreeRTOS-Tasks oder ISRs sind VERBOTEN.

Der einzige erlaubte Kommunikationsweg zwischen CAN-Dispatcher-Task und
LVGL-Task ist eine FreeRTOS-Queue (`can_event_queue`).

Rationale: LVGL v8 ist nicht thread-safe. Direkte Widget-Zugriffe aus dem
CAN-Task führen zu nicht-deterministischen Abstürzen.

## Hardware Constraints & Platform Rules

- **Target**: ESP32-S3, Dual-Core 240 MHz, ESP-IDF ≥ 5.1.0
- **Build-System**: PlatformIO mit `framework = espidf` (kein Arduino-Framework)
- **LVGL**: v8.3.x via ESP-IDF Component Manager (`idf_component.yml`); kein
  manuelles Einbinden
- **CAN-Transceiver**: TJA1051 onboard; GPIO 19 (RX) / GPIO 20 (TX); 120 Ω-
  Terminierung am Bus-Ende MUSS sichergestellt sein
- **IO-Expander CH422G**: Steuert LCD_BL, TP_RST, LCD_RST, SD_CS und USB_SEL –
  CAN-Init MUSS CH422G initialisieren, bevor TWAI gestartet wird
- **PSRAM**: Zwei Framebuffer in PSRAM für Anti-Tearing; PSRAM MUSS in
  `sdkconfig.defaults` aktiviert sein

## Development Workflow

1. **Spec zuerst**: Neue Features beginnen mit `/speckit-specify` bevor Code
   geschrieben wird.
2. **Plan vor Implementierung**: `/speckit-plan` erstellt den Implementierungsplan
   mit Constitution Check als Gate.
3. **Test-First für App-Logik**: Gemäß Prinzip III – Tests schreiben, scheitern
   lassen, dann implementieren.
4. **Commit-Granularität**: Ein logischer Schritt pro Commit; Commit-Message im
   Format `type: kurze Beschreibung` (feat, fix, refactor, test, docs, chore).
5. **Native Tests vor Flash**: `pio test -e native` MUSS grün sein bevor auf
   Hardware geflasht wird.
6. **CAN-Simulator für Entwicklung**: `CONFIG_CAN_SIMULATOR_ENABLE=y` nutzen
   solange kein echtes CAN-Gerät angeschlossen ist.

## Governance

Diese Constitution hat Vorrang vor allen anderen Konventionen und Praktiken
im Projekt. Amendments erfordern:

1. Dokumentation der Änderung (welches Prinzip, warum)
2. Versionsbump nach Semantic-Versioning-Regeln:
   - **MAJOR**: Prinzip entfernt oder inkompatibel neu definiert
   - **MINOR**: Neues Prinzip oder neuer Abschnitt hinzugefügt
   - **PATCH**: Klarstellung, Formulierung, Tippfehler
3. Aktualisierung von `LAST_AMENDED_DATE`
4. Alle PRs/Reviews MÜSSEN die Einhaltung der Constitution-Prinzipien prüfen.

Laufzeit-Entwicklungshinweise: `README.md` im Repository-Root.

**Version**: 1.0.0 | **Ratified**: 2026-06-15 | **Last Amended**: 2026-06-15
