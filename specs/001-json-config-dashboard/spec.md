# Feature Specification: JSON-basierte Dashboard-Konfiguration

**Feature Branch**: `001-json-config-dashboard`

**Created**: 2026-06-15

**Status**: Bereit für Planung

**Input**: Dashboard-Konfiguration (Signale, Widgets, Seiten) vollständig über eine
JSON-Datei auf der SD-Karte steuern – ohne Codeänderung oder Neukompilierung.

---

## User Scenarios & Testing *(mandatory)*

### User Story 1 – Widget-Layout per JSON konfigurieren (Priority: P1)

Ein Techniker oder Inbetriebnehmer möchte das Dashboard ohne Programmier-
kenntnisse anpassen. Er legt eine `dashboard.json` auf der SD-Karte ab, die
beschreibt, welche Widgets auf welcher Position erscheinen sollen, welches CAN-
Signal sie anzeigen und wie sie aussehen (Skalierung, Farbe, Beschriftung,
Wertebereiche). Das Board bootet und zeigt exakt das konfigurierte Dashboard.

**Why this priority**: Das ist der Kern-Mehrwert des Features. Ohne diese Story
sind alle anderen Stories wertlos.

**Independent Test**: SD-Karte mit einer validen `dashboard.json` (2 Widgets,
1 Seite) einlegen, Board booten → Dashboard entspricht 1:1 der Konfiguration.
Anschließend JSON ändern (andere Farbe, anderer Widget-Typ) → nach Reboot
zeigt Board die geänderte Konfiguration.

**Acceptance Scenarios**:

1. **Given** eine valide `dashboard.json` auf der SD-Karte,
   **When** das Board bootet,
   **Then** werden alle konfigurierten Widgets mit den angegebenen Positionen,
   Größen, Farben und CAN-Signalzuordnungen angezeigt.

2. **Given** eine `dashboard.json` mit zwei unterschiedlichen Widget-Typen
   (z. B. Gauge und Bar),
   **When** das Board bootet,
   **Then** zeigt jedes Widget seinen konfigurierten Typ und seine Erscheinung.

3. **Given** eine `dashboard.json`, die einen CAN-Signal-Dezimierungsfaktor und
   eine Einheit definiert,
   **When** CAN-Daten empfangen werden,
   **Then** zeigt das Widget den skalierten Wert mit der konfigurierten Einheit an.

---

### User Story 2 – CAN-Signale im JSON definieren (Priority: P2)

Ein Fahrzeug-Applikationsingenieur möchte neue CAN-Signale aufnehmen, ohne den
Quellcode zu editieren. Er beschreibt in der JSON-Datei für jedes Signal:
CAN-ID, Byte-Offset, Byte-Länge, Endianness, Skalierungsfaktor, Offset-Wert,
Einheit, Minimalwert, Maximalwert und Stale-Timeout.

**Why this priority**: Ohne flexible Signaldefinition ist das Widget-Layout nur
für die vier POC-Signale nutzbar. Diese Story ermöglicht die Adaptierung auf
beliebige Fahrzeugsysteme.

**Independent Test**: `dashboard.json` mit einem neuen CAN-Signal (z. B. ID 0x200,
Big-Endian, 2 Bytes, Scale 0.1, Einheit "km/h") anlegen; mit dem USB-CAN-Adapter
einen passenden Frame einspeisen → Widget zeigt den korrekt skalierten Wert.

**Acceptance Scenarios**:

1. **Given** ein CAN-Signal in der JSON mit `scale=0.1` und `offset=-40`,
   **When** der Rohwert `0x01F4` (500 dezimal) empfangen wird,
   **Then** zeigt das Widget `10.0` (500 × 0.1 − 40).

2. **Given** ein Signal mit `endianness="big"` und `byte_length=2`,
   **When** die Bytes `0x0B B8` empfangen werden,
   **Then** dekodiert das System den Wert `3000` korrekt.

3. **Given** ein Signal mit `stale_timeout_ms=2000`,
   **When** seit dem letzten CAN-Frame mehr als 2 Sekunden vergangen sind,
   **Then** wird das zugeordnete Widget ausgegraut (Stale-Zustand).

---

### User Story 3 – Mehrere Dashboard-Seiten (Priority: P3)

Ein Bediener möchte thematisch gruppierte Anzeigen nutzen (z. B. Seite 1:
Motorparameter, Seite 2: Drücke und Temperaturen). Die Seiten sind in der JSON-
Datei definiert. Zur Laufzeit kann er per Wischgeste seitwärts oder über eine
Navigationsleiste am unteren Rand zwischen den Seiten wechseln.

**Why this priority**: Mehr als vier Signale sind in der Praxis die Regel. Ohne
Mehrseiten-Support werden Dashboards für echte Fahrzeugsysteme unlesbar.

**Independent Test**: `dashboard.json` mit zwei Seiten à 2 Widgets anlegen;
Board booten → Seite 1 erscheint; per Wischgeste zur Seite 2 navigieren →
Seite 2 erscheint; Navigationspunkte am unteren Rand zeigen aktive Seite an.

**Acceptance Scenarios**:

1. **Given** eine `dashboard.json` mit zwei definierten Seiten,
   **When** das Board bootet,
   **Then** wird die erste Seite (Index 0) angezeigt und die Navigationsleiste
   zeigt zwei Punkte, der erste ist aktiv markiert.

2. **Given** Seite 1 ist aktiv,
   **When** der Nutzer nach links wischt oder den zweiten Navigationspunkt tippt,
   **Then** wird Seite 2 angezeigt und der zweite Punkt ist aktiv.

3. **Given** die letzte Seite ist aktiv,
   **When** der Nutzer vorwärts navigiert,
   **Then** erscheint wieder die erste Seite (zyklische Navigation).

4. **Given** eine `dashboard.json` mit nur einer Seite,
   **When** das Board bootet,
   **Then** ist keine Navigationsleiste sichtbar (sie erscheint nur bei ≥ 2 Seiten).

---

### User Story 4 – Widget-Erscheinungsbild vollständig konfigurieren (Priority: P2)

Ein Benutzer möchte für jedes Widget individuell festlegen: Beschriftung/Titel,
Warnbereich-Farbe, Normalbereich-Farbe, Skalen-Minimum und -Maximum sowie
sonstige typ-spezifische Eigenschaften (z. B. Zeigerfarbe bei Gauge, Chart-
Linienstärke). Dies alles ohne Codeänderung.

**Why this priority**: Gleiche Priorität wie US2, da ein konfigurierbares Signal
ohne konfiguriertes Erscheinungsbild für professionelle Anwendungen unzureichend ist.

**Independent Test**: JSON mit einem Gauge definieren (Normalbereich grün 0–80,
Warnbereich rot 80–100, Titel "Druck bar"); Board booten → Gauge zeigt den Titel,
den grünen Normalbereich und den roten Warnbereich.

**Acceptance Scenarios**:

1. **Given** ein Gauge-Widget mit `warning_color="#FF0000"` und
   `warning_threshold=80`,
   **When** das Board bootet,
   **Then** zeigt der Gauge-Warnbereich ab 80 in Rot.

2. **Given** ein Widget mit `title="Drehzahl"` und Signaleinheit `"RPM"`,
   **When** das Widget gerendert wird,
   **Then** sind Titel und Einheit sichtbar dargestellt.

3. **Given** ein Chart-Widget mit `line_color="#00FF00"`, `y_min=0`, `y_max=100`,
   **When** Daten empfangen werden,
   **Then** wird die Kurve grün gezeichnet und die Y-Achse reicht von 0 bis 100.

---

### Edge Cases

- **SD-Karte fehlt beim Booten**: Das System zeigt eine Fehlermeldung auf dem
  Display ("SD-Karte nicht gefunden") und bleibt in einer Warteschleife. Es
  erfolgt kein Fallback auf ein hardkodiertes Dashboard. Der Benutzer muss die
  SD-Karte einlegen und das Board neu starten.
- **JSON syntaktisch fehlerhaft**: Das System zeigt eine Fehlermeldung mit
  Zeilennummer und Fehlerursache auf dem Display; kein Booten ins Dashboard.
- **JSON semantisch inkonsistent** (referenziertes Signal nicht definiert):
  Fehlermeldung nennt Widget-Name und fehlendes Signal; kein Booten ins Dashboard.
- **Widget-Wert außerhalb Min/Max**: Das Widget zeigt den Wert am Grenzwert
  (clamping); kein Absturz.
- **CAN-Bus-Ausfall nach erfolgreichem Booten**: Stale-Timeout-Mechanismus greift;
  betroffene Widgets werden ausgegraut.
- **Zu viele Widgets auf einer Seite** (passen nicht auf 800×480): System loggt
  eine Warnung; Widgets können sich überlappen oder abgeschnitten werden; kein Absturz.
- **JSON enthält einen unbekannten Widget-Typ**: Das System überspringt dieses
  Widget, gibt eine Warnung aus und rendert die restlichen Widgets.

---

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Das System MUSS beim Booten die Datei `dashboard.json` vom
  SD-Karten-Wurzelverzeichnis lesen und parsen.
- **FR-002**: Das System MUSS folgende sechs Widget-Typen aus der JSON
  unterstützen: Gauge (Rundinstrument mit Nadel), Chart (Zeitreihen-Liniengraph),
  Bar (Balkenanzeige), LED (binäre Statusanzeige), Label (Textwert-Anzeige),
  Arc (Halbkreis-Gauge). Unbekannte Typen werden übersprungen (Warnung).
- **FR-003**: Jedes Widget MUSS über die JSON in Position (x, y), Größe
  (width, height) und Widget-Typ konfigurierbar sein.
- **FR-004**: Jedes Widget MUSS einem benannten Signal aus der JSON-Signaltabelle
  zugeordnet werden können.
- **FR-005**: Die JSON MUSS eine Signaltabelle enthalten. Jedes Signal MUSS
  folgende Attribute definieren: Name (eindeutiger String-Schlüssel), can_id,
  byte_offset, byte_length, endianness (little/big), scale, value_offset,
  unit, min, max, stale_timeout_ms.
- **FR-006**: Jedes Widget MUSS folgende Erscheinungseigenschaften per JSON
  konfigurieren können: title, normal_color (RGB-Hex), warning_color (RGB-Hex),
  warning_threshold, background_color.
- **FR-007**: Die JSON MUSS eine geordnete Liste von Seiten (pages) unterstützen.
  Jede Seite hat einen Titel und eine Liste von Widget-Definitionen.
- **FR-008**: Der Benutzer MUSS per Wischgeste seitwärts (Swipe) UND per Tipp
  auf einen Navigationspunkt in der Leiste am unteren Rand zwischen Seiten
  navigieren können. Die Navigationsleiste erscheint nur bei ≥ 2 Seiten.
- **FR-009**: Das System MUSS bei einem JSON-Parse-Fehler eine Fehlermeldung auf
  dem Display ausgeben, die Fehlerursache und Kontext (Zeilennummer oder
  Feldname) beschreibt.
- **FR-010**: Das System MUSS bei einem fehlenden Pflichtfeld eine Fehlermeldung
  ausgeben, die das betroffene Feld und den Widget-/Signal-Namen nennt.
- **FR-011**: Die JSON-Konfiguration MUSS ohne Neukompilierung oder
  Firmware-Update änderbar sein.
- **FR-012**: Der CAN-Simulator-Modus (`CONFIG_CAN_SIMULATOR_ENABLE=y`) MUSS
  weiterhin funktionieren und dieselbe JSON-Konfiguration nutzen.
- **FR-013**: Das JSON-Schema MUSS ein reserviertes `tx_commands`-Feld auf
  Root-Ebene unterstützen (wird in v1 geparst aber ignoriert), damit ein
  späteres CAN-TX-Feature ohne Breaking-Change ergänzt werden kann.
- **FR-014**: Der Widget-Typ-Dispatcher MUSS über eine registrierungsbasierte
  Architektur verfügen (keine langen if/switch-Ketten ohne Erweiterungspunkt),
  sodass neue Input-Widget-Typen (für CAN-TX) in einem späteren Feature ohne
  Umbau der Kern-Rendering-Schicht ergänzt werden können.

### Key Entities

- **Signal**: CAN-Datenpunkt. Attribute: name, can_id, byte_offset, byte_length,
  endianness, scale, value_offset, unit, min, max, stale_timeout_ms.
- **Widget**: Visuelles Element. Attribute: type, x, y, width, height,
  signal_name, title, normal_color, warning_color, warning_threshold,
  background_color; typ-spezifische Felder je nach Widget-Typ.
- **Page**: Anzeige-Seite. Attribute: title, widgets (Liste von Widget-Objekten).
- **DashboardConfig**: Wurzelobjekt. Attribute: version, signals (Liste),
  pages (Liste), tx_commands (reserviert, leer in v1).

---

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Ein Techniker ohne C-Programmierkenntnisse kann ein neues
  CAN-Signal und ein zugehöriges Widget in unter 5 Minuten durch reine
  JSON-Bearbeitung hinzufügen und auf dem Board sehen.
- **SC-002**: Ein Board mit valider JSON bootet und zeigt das vollständige
  Dashboard in unter 3 Sekunden nach dem Power-on (exkl. SD-Karten-Init-Zeit).
- **SC-003**: Bei einer JSON mit bis zu 8 Widgets auf 2 Seiten läuft das
  Dashboard stabil (kein Absturz, keine Speicherlecks) für mindestens 24 Stunden.
- **SC-004**: Alle bestehenden Unit-Tests (`pio test -e native`) bleiben nach
  der Implementierung grün.
- **SC-005**: Eine fehlerhafte JSON führt zu einer menschenlesbaren
  Fehlermeldung auf dem Display – kein stummer Absturz oder Endlosreboot.
- **SC-006**: Der CAN-Simulator liefert weiterhin sichtbare Daten, wenn
  `CONFIG_CAN_SIMULATOR_ENABLE=y` aktiv ist.
- **SC-007**: Ein späteres CAN-TX-Feature kann ohne Änderung an bestehenden
  Modulen (can_dispatcher, dashboard, JSON-Parser) als neues Modul ergänzt werden –
  verifizierbar daran, dass FR-013 und FR-014 im Code-Review bestätigt werden.

---

## Assumptions

- Die SD-Karte ist im FAT32-Format formatiert und über den CH422G-IO-Expander
  (SD_CS) angebunden. Die SD-HAL-Initialisierung ist Teil dieses Features, falls
  sie noch nicht existiert.
- Das JSON-Parsing erfolgt über cJSON (im ESP-IDF bereits enthalten); kein
  externes Netzwerkfetch.
- Positionsangaben in der JSON sind Pixel relativ zur linken oberen Ecke der
  jeweiligen Seite (800×480).
- Widget-Overlaps werden nicht als Fehler behandelt – das Layout liegt in der
  Verantwortung des Konfigurators.
- Die JSON-Datei heißt immer `dashboard.json` im SD-Karten-Root; ein
  konfigurierbarer Dateiname ist out-of-scope für v1.
- Seiten-Übergangsanimationen (Slide-Transition) sind out-of-scope für v1.
- CAN-TX / Input-Widgets (Slider, Switch, Button zum Senden von CAN-Commands)
  sind out-of-scope für v1, werden aber durch FR-013 und FR-014 architektonisch
  vorbereitet.
- Multi-Touch-Gesten (Pinch, Zoom) sind out-of-scope.
