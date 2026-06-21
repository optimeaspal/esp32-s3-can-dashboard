# Dashboard-Editor (Web)

Web-basierter Editor für die `dashboard.json` des CAN-Dashboards: Signale,
Seiten und Widgets visuell bearbeiten. Reine Vanilla-HTML/CSS/JS-Anwendung,
keine Frameworks, keine CDNs.

| Datei | Zweck |
|-------|-------|
| `index.html` | Grundgerüst mit Tabs (Layout / Signale / Backup) |
| `style.css` | Layout & Theme |
| `editor-core.js` | Reine Logik (Defaults, Validierung, Serialisierung) – DOM-frei, in Node testbar |
| `app.js` | DOM-Verdrahtung: Laden, Tabs, Canvas, Drag/Resize, Backup |

## Offline auf dem PC nutzen (ohne Gerät)

Der Editor erkennt automatisch, ob ein Gerät erreichbar ist. Ist keines da,
schaltet er in den **Offline-Modus** (gelbes Banner): Konfiguration neu erstellen
oder eine vorhandene `dashboard.json` importieren, bearbeiten und wieder
exportieren. „Speichern & Übernehmen" ist offline deaktiviert.

Zwei Wege:

1. **Launcher (empfohlen):** `start-editor-offline.bat` doppelklicken. Startet
   einen lokalen HTTP-Server und öffnet `http://localhost:8000/`. Benötigt
   Python (Standardbibliothek genügt).
2. **Direkt:** `index.html` per Doppelklick im Browser öffnen (`file://`). Der
   Offline-Modus funktioniert auch so.

## Am Gerät nutzen

Die vier Asset-Dateien (`index.html`, `style.css`, `editor-core.js`, `app.js`)
nach `/sdcard/www/` auf die SD-Karte kopieren. Der Editor ist dann unter
`http://<geräte-ip>/` erreichbar, lädt die aktuelle Konfiguration über
`GET /api/config` und schreibt sie via `POST /api/config` zurück (das Gerät
validiert und startet bei Erfolg neu).

## Tests

Die DOM-freie Kernlogik wird mit Node-Bordmitteln getestet:

```sh
node --test test/www/editor-core.test.cjs
```
