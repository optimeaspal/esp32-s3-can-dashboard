# Design: Web-basierter Dashboard-Editor

**Datum**: 2026-06-20
**Status**: Entwurf (zur Review)
**Kontext**: Folgefeature zu 001 (JSON-Dashboard) und 002 (WLAN-Upload)

## Problem / Ziel

Heute lässt sich die `dashboard.json` nur extern (Texteditor) erstellen und per
Upload-Formular auf das Gerät laden. Der nächste Schritt: die Dashboard-Elemente
**direkt im Browser editieren** – Signale, Seiten und Widgets visuell konfigurieren,
ohne JSON von Hand zu schreiben. Der Editor wird Teil der bestehenden Web-Oberfläche
im `www`-Ordner auf der SD-Karte.

## Nicht-Ziele (v1)

- Live-CAN-Werte in der Vorschau (nur Beispielwert-Render)
- TX-UI (CAN senden) – `tx_commands` wird nur durchgereicht, nicht editiert
- Undo/Redo, Versionshistorie im Editor
- Mehrbenutzer-/gleichzeitiges Editieren, Sperren
- On-Device-Editieren über den Touchscreen (rein Web-basiert)

## Entscheidungen (aus dem Brainstorming)

| Frage | Entscheidung |
|-------|--------------|
| Editor-Paradigma | **Hybrid**: WYSIWYG-Canvas + Eigenschaften-Panel |
| Umfang | Signale **und** Seiten **und** Widgets; Signale auf eigenem Tab |
| Datenfluss | **Gerät + Datei**: laden/speichern via Gerät, zusätzlich Datei-Import/-Export |
| Offline-Nutzung | Dieselben Dateien auch ohne Gerät (lokal via `file://`) nutzbar; Offline-Modus mit Skelett/Import + Export |
| Vorschau-Treue | **Beispielwert-Render** (≈70 % des Bereichs, Warnfarbe ab Schwelle) |
| TX | Mitgedacht (durchgereicht), nicht implementiert |
| Tech-Stack | Vanilla HTML/CSS/JS, selbst-enthalten, keine CDNs/Build-Kette |

## Architektur

### Frontend: Single-Page-App in `examples/www/`

Eine SPA mit **drei Tabs**, die sich **eine im Browser gehaltene Config** teilen
(kein Datenverlust beim Tab-Wechsel):

1. **Layout** – Canvas + Eigenschaften-Panel + Seiten-Tabs
2. **Signale** – Tabellen-Editor der CAN-Signal-Definitionen
3. **Backup** – Datei-Import/-Export, „Speichern & Übernehmen", Skelett/Neu

Dateien (in `examples/www/`, werden auf SD nach `/sdcard/www` kopiert):

- `index.html` – Grundgerüst, Tab-Navigation
- `app.js` – Zustand, Render, Validierung, Fetch
- `style.css` – Layout/Theme

Da das Display 800×480 ist, passt die Editor-Fläche komfortabel auf einen
Desktop-Browser; der Canvas wird maßstäblich (skaliert) dargestellt.

### Zustandsmodell (Browser)

Ein einziges JavaScript-Objekt `config` als Single Source of Truth, strukturgleich
zur `dashboard.json` (siehe `specs/001-.../contracts/dashboard-json-schema.md`):

```
config = { version, signals[], pages[], tx_commands[] }
```

Alle Tabs lesen/schreiben dasselbe Objekt. `tx_commands` wird beim Laden bewahrt
und beim Speichern unverändert serialisiert.

### Datenfluss

```
Start  → GET /api/config            → config in Browser laden
         (404/leer → leeres Skelett)
         (fetch schlägt fehl = kein Gerät → Offline-Modus, s.u.)
Edit   → rein im Browser (config-Objekt mutieren, Re-Render)
Save   → POST /api/config (Raw-Body) → Gerät validiert → temp → rename → Neustart
Backup → Export: config als .json herunterladen (clientseitig)
         Import: .json-Datei lesen → config ersetzen (mit Validierung)
```

## Komponenten

### 1. Layout-Tab

- **Seiten-Tabs**: Seite wählen, hinzufügen, umbenennen, löschen (≥1 Seite, ≤8).
- **Canvas**: maßstäbliche 800×480-Fläche.
  - Widget anklicken = auswählen (Markierungsrahmen + Anfasser).
  - Ziehen = `x/y` ändern; Eck-Anfasser = `width/height` ändern.
  - Werte beim Ziehen ins Eigenschaften-Panel rückgespiegelt (und umgekehrt).
  - Render je Widget über typ-spezifische Funktion mit Beispielwert.
- **Eigenschaften-Panel** (für ausgewähltes Widget): `type`, `signal` (Dropdown
  aus `config.signals`), `title`, `x/y/width/height`, `normal_color`,
  `warning_color`, `warning_threshold`, `background_color`, Löschen.
- **„➕ Widget"**: legt Widget mit Default-Position/-Größe auf aktueller Seite an.

### 2. Signale-Tab

- Tabelle aller Signale; Zeile = ein Signal mit allen Schemafeldern
  (`name`, `can_id`, `byte_offset`, `byte_length`, `endianness`, `signed`,
  `float`, `scale`, `offset`, `unit`, `min`, `max`, `stale_ms`, `simulated`).
- „➕ Signal" / Löschen. Ein **referenziertes** Signal kann nicht gelöscht werden
  (Hinweis, welche Widgets es nutzen).

### 3. Backup-Tab

- **Export**: aktuelle `config` als `dashboard.json` herunterladen.
- **Import**: lokale `.json` einlesen → nach Validierung `config` ersetzen.
- **Speichern & Übernehmen**: `POST /api/config`; Hinweis, dass das Gerät neu startet.
- **Neu/Skelett**: leere Config anlegen.

### 4. Widget-Renderer (`app.js`)

Je eine Funktion pro Typ (gauge, chart, bar, led, label, arc), die das Widget
auf dem Canvas annähernd wie auf dem Gerät zeichnet (DOM/SVG oder `<canvas>`).
Beispielwert ≈ `min + 0.7·(max−min)`; ab `warning_threshold` Warnfarbe.
Es ist eine **visuelle Annäherung**, kein pixelgenaues LVGL-Abbild.

### 5. Geräteseitig: neuer GET-Endpoint (`src/hal/web_server.c`)

- `GET /api/config`: liest `CONFIG_DASHBOARD_JSON_PATH` von SD, liefert
  `application/json`. Existiert keine Datei: leeres, valides Skelett
  (`{"version":"1.0","signals":[],"pages":[{"title":"Seite 1","widgets":[]}]}`).
- `POST /api/config`: **unverändert** (Validierung mit Boot-Parser, temp,
  atomares Rename, verzögerter Neustart).
- Statische Assets (`www`) werden wie bisher ausgeliefert.

### 6. Offline-Modus (ohne Gerät)

Dieselben `www`-Dateien sollen auch **lokal ohne Gerät** funktionieren – z.&nbsp;B.
um eine `dashboard.json` am PC vorzubereiten (Datei via `file://` im Browser öffnen
oder von einem beliebigen statischen Server).

- Beim Start versucht der Editor `GET /api/config`. **Schlägt der Fetch fehl**
  (kein Gerät erreichbar / `file://`-Kontext), wechselt er in den **Offline-Modus**:
  Start mit leerem Skelett, Datei-**Import** als primärer Ladeweg.
- Im Offline-Modus ist **„Speichern & Übernehmen"** deaktiviert (kein Gerät);
  **Export** als `dashboard.json` ist der Ausgabeweg. Ein Statusbanner zeigt
  „Offline – kein Gerät verbunden".
- Wird der Editor vom Gerät ausgeliefert und `GET /api/config` gelingt, ist der
  volle Online-Modus aktiv. **Kein Code-Zweig dupliziert** – der Unterschied ist
  nur Geräte-Erreichbarkeit + ein/zwei deaktivierte Aktionen.
- Voraussetzung: keine externen CDNs, alle Assets relativ verlinkt (gilt ohnehin).
  `app.js`/`style.css` werden relativ geladen und funktionieren unter `file://`;
  die einzige Netzwerkanfrage ist `/api/config`.

## Validierung & Fehlerbehandlung

Clientseitige Validierung für sofortiges Feedback, spiegelt die Parser-Regeln:

- Signal-`name` eindeutig; nicht leer (max. 31 Zeichen).
- `min < max`; `byte_offset + byte_length ≤ 8`; `byte_length ∈ {1,2,4}`.
- Farben im Format `#RRGGBB`.
- Limits: ≤ 32 Signale, ≤ 8 Seiten, ≤ 16 Widgets/Seite, ≥ 1 Seite.
- Widget-`signal` muss in `config.signals` existieren.
- Widget-Position/-Größe innerhalb 800×480 (Warnung, nicht hart blockierend).

Das **Gerät bleibt maßgebliche Validierungsinstanz**: beim `POST` läuft der
Boot-Parser erneut; dessen Fehlertext wird im Editor angezeigt. Schlägt die
serverseitige Validierung fehl, wird **nichts** überschrieben und **nicht**
neu gestartet (bestehendes Verhalten).

## Betroffene/neue Dateien

| Datei | Art | Zweck |
|-------|-----|-------|
| `examples/www/index.html` | erweitern | Tab-Navigation, Editor-Gerüst |
| `examples/www/app.js` | erweitern | Zustand, Render, Validierung, Fetch |
| `examples/www/style.css` | neu/erweitern | Editor-Layout/Theme |
| `src/hal/web_server.c` | erweitern | `GET /api/config`-Handler |

## Offene Punkte / Risiken

- **Asset-Größe**: `app.js` wächst deutlich; SD-Platz unkritisch, aber
  Auslieferung über `filebuf` (8 KB) in `get_handler` prüfen – ggf. größerer
  Puffer oder Chunked-Send nötig, wenn eine Asset-Datei > 8 KB wird.
- **Genauigkeit der Vorschau** vs. tatsächliches LVGL-Rendering – bewusst nur
  Annäherung; ggf. Hinweis im UI.
- **Neustart bei jedem Speichern** bleibt; iteratives Editieren passiert im
  Browser, Speichern ist der bewusste, seltene Schritt.
