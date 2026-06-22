# Design: UX-Verbesserungen Dashboard-Editor

**Datum**: 2026-06-21
**Status**: Entwurf (zur Review)
**Kontext**: Folgearbeit zum Dashboard-Editor (Feature 003, Branch
`003-dashboard-editor`). Setzt auf dem dort implementierten Stand auf
(`examples/www/app.js`, `editor-core.js`, `index.html`, `style.css`).

## Problem / Ziel

Der Editor ist funktional vollständig (Signale/Seiten/Widgets editieren, laden,
speichern, Offline-Modus, gerätegetreue Vorschau). Beim Bedienen fallen drei
Reibungspunkte auf, die mit wenig Aufwand viel angenehmer werden:

1. Die Vorschau zeigt nur **einen festen Beispielwert** (≈70 %). Ob Farben,
   Zeiger­ausschlag und vor allem die **Warnschwelle** richtig sitzen, lässt
   sich nicht über den Wertebereich prüfen.
2. Seiten lassen sich **nur anlegen und wechseln**, nicht **umbenennen oder
   löschen** – obwohl die Ursprungs-Spec das vorsah (Komponente „Seiten-Tabs:
   wählen, hinzufügen, umbenennen, löschen").
3. Widgets lassen sich **nur mit der Maus** platzieren; pixelgenaues Ausrichten
   und Löschen ist umständlich.

Ziel: drei kleine, klar abgegrenzte UX-Verbesserungen, die den Editor runder
machen, ohne die Architektur oder das `dashboard.json`-Schema zu ändern.

## Nicht-Ziele (dieser Iteration)

- Undo/Redo, Versionshistorie (bleibt Nicht-Ziel wie in Feature 003).
- Snap-to-Grid / Ausrichtungshilfen (separat, spätere Iteration).
- Inline-Validierung im Canvas / Tabelle (separat, spätere Iteration).
- Live-CAN-Werte (weiterhin nur synthetischer Beispielwert).
- Änderungen an Geräteseite (`web_server.c`) oder am JSON-Schema – rein clientseitig.

## Entscheidungen

| Frage | Entscheidung |
|-------|--------------|
| Wert-Vorschau | **Globaler Schieberegler** über dem Canvas, 0–100 % des Bereichs; steuert den Beispielwert **aller** Widgets der Seite gleichzeitig |
| Default-Wert | Slider startet bei **70 %** (heutiges Verhalten bleibt der Ausgangspunkt) |
| Reichweite Slider | Nur **Anzeige** (Vorschau); verändert die `config` **nicht** |
| Seite umbenennen | **Doppelklick** auf den Seiten-Tab → Inline-Eingabefeld; leerer Name unzulässig |
| Seite löschen | Button am aktiven Seiten-Tab; **mind. 1 Seite** muss bleiben; Bestätigung, wenn Widgets vorhanden |
| Tastatur | Pfeile = 1 px, **Shift+Pfeil = 10 px**; **Entf/Backspace** = Widget löschen; **Esc** = Abwahl |
| Tastatur-Scope | Nur aktiv, wenn ein Widget ausgewählt ist **und** der Fokus nicht in einem Eingabefeld liegt |

## Architektur

Alle Änderungen sind **clientseitig** in `examples/www/` und bauen auf den
vorhandenen Funktionen auf. Kein neuer globaler Zustand außer dem Slider-Wert.

Bestehende, hier relevante Bausteine:
- `config` (Single Source of Truth), `currentPage`, `selectedWidget`.
- `sampleValue(signal)` in `editor-core.js` – aktuell fest `min + 0.7·(max−min)`.
- `renderCanvas()`, `widgetPreview(w)` – zeichnen Widgets mit dem Beispielwert.
- `renderPageTabs()` – baut die Seiten-Tabs.
- `startDrag/startResize`, `clamp()` – Maus-Interaktion + Begrenzung 0…800/0…480.

## Komponenten

### 1. Live-Wert-Schieberegler

**UI:** Ein `<input type="range">` (0–100) mit Prozent-Anzeige in der Layout-
Toolbar (neben „➕ Widget"). Optional ein kleiner „Reset"-Hinweis bei 70 %.

**Logik:**
- Neue Modul-Variable `previewPct = 0.7` (nur Anzeige, nicht in `config`).
- `sampleValue` bleibt unverändert (Kernlogik/Tests), bekommt aber eine
  parametrisierte Schwester: `sampleValueAt(signal, pct)` =
  `min + pct·(max−min)`. `sampleValue(sig)` ruft `sampleValueAt(sig, 0.7)` auf
  → bestehende 16 Tests bleiben grün; ein neuer Test deckt `sampleValueAt` ab.
- `widgetPreview(w)` nutzt statt fixem 0.7 den aktuellen `previewPct`.
- Slider-`input`-Event setzt `previewPct = wert/100` und ruft `renderCanvas()`.

**Wirkung:** Beim Schieben wandern Zeiger/Balken/Linie/LED, und ab der
`warning_threshold` schlägt – über `isWarning` – die Warnfarbe um. Direkt
sichtbar, ob Schwelle/Farben passen.

### 2. Seiten umbenennen & löschen

**Umbenennen:** `dblclick` auf einen Seiten-Tab ersetzt den Button durch ein
`<input>` mit dem aktuellen Titel; `Enter`/`blur` übernimmt, `Esc` verwirft.
Leerer/Whitespace-Titel wird abgelehnt (Fallback: alter Titel). Max. Länge wie
Schema (`LIMITS.titleLen` = 31).

**Löschen:** kleiner „🗑"-Button am **aktiven** Seiten-Tab.
- Blockiert, wenn es die **letzte** Seite ist (Hinweis „Mindestens eine Seite").
- Hat die Seite Widgets: `confirm()` mit Anzahl.
- Nach dem Löschen: `currentPage` auf gültigen Index klemmen, `selectedWidget`
  zurücksetzen, `renderPageTabs/renderCanvas/renderPropPanel`.

Beides ändert nur `config.pages` und nutzt die bestehenden Render-Funktionen.

### 3. Tastatur-Bedienung

Ein globaler `keydown`-Listener (in `init()` registriert):
- **Vorbedingung:** `selectedWidget` gesetzt **und** `document.activeElement`
  ist kein `INPUT`/`SELECT`/`TEXTAREA` (sonst normales Tippen nicht stören).
- **Pfeiltasten:** `x/y` um 1 px (mit `Shift` 10 px) ändern, über `clamp()`
  in 0…(800−width)/0…(480−height). `preventDefault` (kein Scrollen).
- **Entf/Backspace:** ausgewähltes Widget aus `config.pages[currentPage].widgets`
  entfernen (wie der vorhandene „Widget löschen"-Button).
- **Esc:** `selectedWidget = null`, neu zeichnen.
- Nach jeder Aktion `renderCanvas()` (+ ggf. `renderPropPanel()`), damit Panel
  und Canvas konsistent bleiben.

## Betroffene/neue Dateien

| Datei | Art | Zweck |
|-------|-----|-------|
| `examples/www/editor-core.js` | erweitern | `sampleValueAt(signal, pct)`; `sampleValue` darüber definieren |
| `examples/www/app.js` | erweitern | `previewPct` + Slider-Handler; Seiten umbenennen/löschen in `renderPageTabs`; globaler `keydown`-Handler |
| `examples/www/index.html` | erweitern | Slider-Element in der Layout-Toolbar |
| `examples/www/style.css` | erweitern | Stil für Slider, Inline-Umbenennen, Lösch-Button |
| `test/www/editor-core.test.cjs` | erweitern | Tests für `sampleValueAt` (0/0.5/1.0 und Round-Trip 0.7 = altes Verhalten) |

## Validierung & Randfälle

- **Slider** verändert `config` nicht → kein Validierungsbezug; rein visuell.
- **Umbenennen** respektiert `LIMITS.titleLen`; leerer Titel unzulässig.
- **Letzte Seite** kann nicht gelöscht werden (`validateConfig` verlangt ≥1 Seite).
- **Tastatur-Nudge** nutzt dasselbe `clamp`, hält Widgets also im 800×480-Bereich
  (Off-Canvas bleibt – wie bisher – nur über manuelle Zahleneingabe als *Warnung*
  möglich, kein harter Fehler).
- Tastatur darf Texteingaben in Tabelle/Panel **nicht** stören (Fokus-Check).

## Offene Punkte / Risiken

- **Signale-Tab vs. Slider:** Slider lebt im Layout-Tab; beim Tab-Wechsel keine
  Auswirkung (Vorschau nur im Layout). Kein Konflikt erwartet.
- **`sampleValue`-Refactor:** muss rückwärtskompatibel bleiben (Default 0.7),
  damit die bestehenden Node-Tests unverändert grün sind – durch Tests abgesichert.
- **Touch:** Editor ist als Desktop-Werkzeug gedacht; Tastatur-Features sind
  Desktop-only (kein Nachteil, additive Verbesserung).

## Aufwandsschätzung

| Komponente | grob |
|------------|------|
| Wert-Slider | ~2–3 h |
| Seiten umbenennen/löschen | ~2 h |
| Tastatur-Bedienung | ~1–2 h |

Reihenfolge für die Umsetzung: **erst nach** Geräte-Smoke-Test (Task 8) von
Feature 003 und dem Screenshot/Foto-Vergleich, damit nicht auf ungetestetem
Stand aufgebaut wird.
