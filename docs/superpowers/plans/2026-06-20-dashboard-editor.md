# Dashboard-Editor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ein web-basierter Editor in `examples/www/`, mit dem sich Signale, Seiten und Widgets der `dashboard.json` visuell bearbeiten lassen – am Gerät (laden/speichern via HTTP) und offline (Datei-Import/-Export).

**Architecture:** Vanilla-HTML/CSS/JS Single-Page-App mit drei Tabs (Layout/Signale/Backup), die sich ein im Browser gehaltenes `config`-Objekt teilen. Die reine Logik (Skelett, Defaults, Validierung, Serialisierung, Beispielwert) liegt DOM-frei in `editor-core.js` und ist mit Node-Bordmitteln testbar. Geräteseitig kommt ein `GET /api/config`-Handler dazu; `POST /api/config` bleibt unverändert (Validierung + atomares Rename + Neustart). Schlägt `GET /api/config` fehl, läuft der Editor im Offline-Modus.

**Tech Stack:** C (ESP-IDF, `esp_http_server`), Vanilla JS (ES2020, keine Frameworks/CDNs), Node ≥ 22 nur für JS-Unit-Tests (eingebauter `node:test`-Runner, keine npm-Abhängigkeiten).

**Spec:** `docs/superpowers/specs/2026-06-20-dashboard-editor-design.md`

---

## Dateistruktur

| Datei | Art | Verantwortung |
|-------|-----|---------------|
| `examples/www/editor-core.js` | NEU | Reine Logik: `emptyConfig`, `defaultSignal`, `defaultWidget`, `sampleValue`, `isWarning`, `normalizeColor`, `validateConfig`, `serializeConfig`. Keine DOM-Zugriffe. Im Browser global, in Node via `module.exports`. |
| `examples/www/app.js` | ERSETZEN | DOM-Verdrahtung: Laden (GET + Offline-Fallback), Tab-Umschaltung, Signale-Tabelle, Canvas-Render, Drag/Resize, Eigenschaften-Panel, Backup (Import/Export/Speichern). |
| `examples/www/index.html` | ERSETZEN | Editor-Grundgerüst: Tab-Leiste, drei Tab-Container, Statusbanner. |
| `examples/www/style.css` | ERSETZEN | Editor-Layout/Theme (Tabs, Canvas, Panel, Tabelle). |
| `test/www/editor-core.test.cjs` | NEU | Node-Unit-Tests für `editor-core.js`. |
| `src/hal/web_server.c` | ÄNDERN | `GET /api/config`-Handler + Skelett-Fallback; Asset-Puffer vergrößern. |

Reihenfolge: zuerst die testbare Kernlogik (Task 1–2), dann der Geräte-Endpoint (Task 3), dann die UI von außen nach innen (Task 4–8).

---

## Task 1: editor-core.js – Skelett, Defaults, Hilfsfunktionen (TDD)

**Files:**
- Create: `examples/www/editor-core.js`
- Test: `test/www/editor-core.test.cjs`

- [ ] **Step 1: Failing-Test schreiben**

Create `test/www/editor-core.test.cjs`:

```js
const test = require('node:test');
const assert = require('node:assert/strict');
const core = require('../../examples/www/editor-core.js');

test('emptyConfig liefert valides Skelett mit einer Seite', () => {
  const c = core.emptyConfig();
  assert.equal(c.version, '1.0');
  assert.deepEqual(c.signals, []);
  assert.equal(c.pages.length, 1);
  assert.equal(c.pages[0].title, 'Seite 1');
  assert.deepEqual(c.pages[0].widgets, []);
  assert.deepEqual(c.tx_commands, []);
});

test('defaultSignal setzt Pflicht-Defaults', () => {
  const s = core.defaultSignal('RPM');
  assert.equal(s.name, 'RPM');
  assert.equal(s.byte_length, 2);
  assert.equal(s.endianness, 'little');
  assert.ok(s.min < s.max);
});

test('defaultWidget liegt im Display und referenziert Signal', () => {
  const w = core.defaultWidget('gauge', 'RPM');
  assert.equal(w.type, 'gauge');
  assert.equal(w.signal, 'RPM');
  assert.ok(w.x >= 0 && w.x + w.width <= core.DISPLAY.w);
  assert.ok(w.y >= 0 && w.y + w.height <= core.DISPLAY.h);
});

test('sampleValue = min + 0.7*(max-min)', () => {
  assert.equal(core.sampleValue({ min: 0, max: 100 }), 70);
  assert.equal(core.sampleValue({ min: 1000, max: 2000 }), 1700);
});

test('isWarning nur wenn Schwelle > 0 und Wert >= Schwelle', () => {
  assert.equal(core.isWarning({ warning_threshold: 50 }, 70), true);
  assert.equal(core.isWarning({ warning_threshold: 90 }, 70), false);
  assert.equal(core.isWarning({ warning_threshold: 0 }, 70), false);
});

test('normalizeColor akzeptiert #RRGGBB und RRGGBB, lehnt Müll ab', () => {
  assert.equal(core.normalizeColor('#1A2B3C'), '#1A2B3C');
  assert.equal(core.normalizeColor('1a2b3c'), '#1A2B3C');
  assert.equal(core.normalizeColor('#xyz'), null);
  assert.equal(core.normalizeColor(''), null);
});
```

- [ ] **Step 2: Test laufen lassen, Fehlschlag bestätigen**

Run: `node --test test/www/editor-core.test.cjs`
Expected: FAIL – „Cannot find module '../../examples/www/editor-core.js'".

- [ ] **Step 3: editor-core.js mit diesen Funktionen anlegen**

Create `examples/www/editor-core.js`:

```js
// Reine Editor-Logik – keine DOM-Zugriffe. Wird im Browser als klassisches
// <script> geladen (Funktionen/Konstanten landen im gemeinsamen globalen Scope)
// UND in Node-Tests via require() (module.exports am Dateiende).

const WIDGET_TYPES = ['gauge', 'chart', 'bar', 'led', 'label', 'arc'];
const DISPLAY = { w: 800, h: 480 };
const LIMITS = {
  signals: 32, pages: 8, widgetsPerPage: 16,
  nameLen: 31, titleLen: 31, unitLen: 7,
};

// Leeres, valides Skelett (vgl. GET-Fallback im web_server.c).
function emptyConfig() {
  return {
    version: '1.0',
    signals: [],
    pages: [{ title: 'Seite 1', widgets: [] }],
    tx_commands: [],
  };
}

function defaultSignal(name) {
  return {
    name: name || '', can_id: '0x100',
    byte_offset: 0, byte_length: 2, endianness: 'little',
    signed: false, float: false,
    scale: 1.0, offset: 0.0, unit: '',
    min: 0.0, max: 100.0, stale_ms: 2000, simulated: false,
  };
}

function defaultWidget(type, signalName) {
  return {
    type: type, x: 20, y: 20, width: 300, height: 200,
    signal: signalName || '', title: '',
    normal_color: '#00AA00', warning_color: '#FF4400',
    warning_threshold: 0.0, background_color: '#1A1A1A',
  };
}

// Beispielwert = 70 % des Anzeigebereichs.
function sampleValue(signal) {
  const min = Number(signal.min) || 0;
  const max = Number(signal.max);
  const hi = isNaN(max) ? 100 : max;
  return min + 0.7 * (hi - min);
}

// Warnfarbe greift, wenn Schwelle > 0 und Wert >= Schwelle.
function isWarning(widget, value) {
  const t = Number(widget.warning_threshold) || 0;
  return t > 0 && value >= t;
}

// "#RRGGBB" oder "RRGGBB" → "#RRGGBB" (Großschreibung); sonst null.
function normalizeColor(str) {
  if (typeof str !== 'string') return null;
  const m = str.replace(/^#/, '');
  if (!/^[0-9a-fA-F]{6}$/.test(m)) return null;
  return '#' + m.toUpperCase();
}

if (typeof module !== 'undefined' && module.exports) {
  module.exports = {
    WIDGET_TYPES, DISPLAY, LIMITS,
    emptyConfig, defaultSignal, defaultWidget,
    sampleValue, isWarning, normalizeColor,
  };
}
```

- [ ] **Step 4: Test laufen lassen, Erfolg bestätigen**

Run: `node --test test/www/editor-core.test.cjs`
Expected: PASS – 6 Tests grün.

- [ ] **Step 5: Commit**

```bash
git add examples/www/editor-core.js test/www/editor-core.test.cjs
git commit -m "feat(editor): editor-core Skelett/Defaults/Helfer + Node-Tests"
```

---

## Task 2: editor-core.js – Validierung & Serialisierung (TDD)

Spiegelt die Regeln aus `src/app/config_loader.c` für sofortiges Client-Feedback. Das Gerät bleibt maßgeblich.

**Files:**
- Modify: `examples/www/editor-core.js`
- Modify: `test/www/editor-core.test.cjs`

- [ ] **Step 1: Failing-Tests ergänzen**

Append to `test/www/editor-core.test.cjs`:

```js
function validCfg() {
  const c = core.emptyConfig();
  c.signals.push(core.defaultSignal('RPM'));
  c.pages[0].widgets.push(core.defaultWidget('gauge', 'RPM'));
  return c;
}

test('validateConfig: gültige Config hat keine Fehler', () => {
  const r = core.validateConfig(validCfg());
  assert.equal(r.ok, true);
  assert.deepEqual(r.errors, []);
});

test('validateConfig: doppelter Signalname', () => {
  const c = validCfg();
  c.signals.push(core.defaultSignal('RPM'));
  const r = core.validateConfig(c);
  assert.equal(r.ok, false);
  assert.ok(r.errors.some(e => /eindeutig/.test(e)));
});

test('validateConfig: min >= max', () => {
  const c = validCfg();
  c.signals[0].min = 100; c.signals[0].max = 100;
  const r = core.validateConfig(c);
  assert.ok(r.errors.some(e => /min/.test(e) && /max/.test(e)));
});

test('validateConfig: byte_offset + byte_length > 8', () => {
  const c = validCfg();
  c.signals[0].byte_offset = 6; c.signals[0].byte_length = 4;
  const r = core.validateConfig(c);
  assert.ok(r.errors.some(e => /byte_offset/.test(e)));
});

test('validateConfig: byte_length nicht in {1,2,4}', () => {
  const c = validCfg();
  c.signals[0].byte_length = 3;
  const r = core.validateConfig(c);
  assert.ok(r.errors.some(e => /byte_length/.test(e)));
});

test('validateConfig: Widget referenziert unbekanntes Signal', () => {
  const c = validCfg();
  c.pages[0].widgets[0].signal = 'Fehlt';
  const r = core.validateConfig(c);
  assert.ok(r.errors.some(e => /Fehlt/.test(e)));
});

test('validateConfig: unbekannter Widget-Typ', () => {
  const c = validCfg();
  c.pages[0].widgets[0].type = 'banana';
  const r = core.validateConfig(c);
  assert.ok(r.errors.some(e => /banana/.test(e)));
});

test('validateConfig: zu viele Seiten', () => {
  const c = validCfg();
  for (let i = 0; i < core.LIMITS.pages; i++) c.pages.push({ title: 'P', widgets: [] });
  const r = core.validateConfig(c);
  assert.ok(r.errors.some(e => /Seiten/.test(e)));
});

test('validateConfig: Off-Canvas ist Warnung, kein Fehler', () => {
  const c = validCfg();
  c.pages[0].widgets[0].x = 700; c.pages[0].widgets[0].width = 300; // 1000 > 800
  const r = core.validateConfig(c);
  assert.equal(r.ok, true);
  assert.ok(r.warnings.some(e => /Display/.test(e)));
});

test('serializeConfig: Round-Trip erhält tx_commands und lässt sich neu parsen', () => {
  const c = validCfg();
  c.tx_commands = [{ name: 'reserviert' }];
  const json = core.serializeConfig(c);
  const back = JSON.parse(json);
  assert.deepEqual(back.tx_commands, [{ name: 'reserviert' }]);
  assert.equal(back.signals[0].name, 'RPM');
  assert.equal(back.pages[0].widgets[0].type, 'gauge');
});
```

- [ ] **Step 2: Test laufen lassen, Fehlschlag bestätigen**

Run: `node --test test/www/editor-core.test.cjs`
Expected: FAIL – „core.validateConfig is not a function".

- [ ] **Step 3: validateConfig + serializeConfig implementieren**

In `examples/www/editor-core.js`, vor dem `module.exports`-Block einfügen:

```js
// Spiegelt config_loader.c. errors blockieren das Speichern, warnings nicht.
function validateConfig(config) {
  const errors = [];
  const warnings = [];

  const signals = Array.isArray(config.signals) ? config.signals : [];
  const pages = Array.isArray(config.pages) ? config.pages : [];

  if (signals.length > LIMITS.signals) errors.push('Zu viele Signale (max ' + LIMITS.signals + ')');
  if (pages.length < 1) errors.push('Mindestens eine Seite erforderlich');
  if (pages.length > LIMITS.pages) errors.push('Zu viele Seiten (max ' + LIMITS.pages + ')');

  const names = new Set();
  signals.forEach((s, i) => {
    const id = s.name || ('#' + (i + 1));
    if (!s.name || !String(s.name).trim()) errors.push('Signal ' + id + ': Name darf nicht leer sein');
    else if (String(s.name).length > LIMITS.nameLen) errors.push('Signal ' + id + ': Name länger als ' + LIMITS.nameLen + ' Zeichen');
    else if (names.has(s.name)) errors.push("Signalname '" + s.name + "' nicht eindeutig");
    names.add(s.name);

    if (s.can_id === undefined || s.can_id === null || s.can_id === '') errors.push('Signal ' + id + ": Pflichtfeld 'can_id' fehlt");
    if (Number(s.min) >= Number(s.max)) errors.push('Signal ' + id + ': min (' + s.min + ') >= max (' + s.max + ')');
    if (Number(s.byte_offset) + Number(s.byte_length) > 8) errors.push('Signal ' + id + ': byte_offset+byte_length > 8');
    if (![1, 2, 4].includes(Number(s.byte_length))) errors.push('Signal ' + id + ': byte_length muss 1, 2 oder 4 sein');
    if (!['little', 'big'].includes(s.endianness)) errors.push('Signal ' + id + ": endianness muss 'little' oder 'big' sein");
  });

  pages.forEach((p, pi) => {
    const widgets = Array.isArray(p.widgets) ? p.widgets : [];
    if (widgets.length > LIMITS.widgetsPerPage) errors.push('Seite ' + (pi + 1) + ': zu viele Widgets (max ' + LIMITS.widgetsPerPage + ')');
    widgets.forEach((w, wi) => {
      const wid = "Widget " + (wi + 1) + ' auf Seite ' + (pi + 1);
      if (!WIDGET_TYPES.includes(w.type)) errors.push(wid + ": unbekannter Typ '" + w.type + "'");
      ['x', 'y', 'width', 'height'].forEach(k => {
        if (typeof w[k] !== 'number' || isNaN(w[k])) errors.push(wid + ': ' + k + ' muss eine Zahl sein');
      });
      if (Number(w.width) <= 0 || Number(w.height) <= 0) errors.push(wid + ': width/height müssen > 0 sein');
      if (!w.signal || !names.has(w.signal)) errors.push(wid + ": Signal '" + (w.signal || '') + "' nicht definiert");
      ['normal_color', 'warning_color', 'background_color'].forEach(k => {
        if (w[k] && normalizeColor(w[k]) === null) errors.push(wid + ': ' + k + ' ist kein gültiges #RRGGBB');
      });
      if (Number(w.x) < 0 || Number(w.y) < 0 ||
          Number(w.x) + Number(w.width) > DISPLAY.w ||
          Number(w.y) + Number(w.height) > DISPLAY.h) {
        warnings.push(wid + ': ragt über das Display (800×480) hinaus');
      }
    });
  });

  return { ok: errors.length === 0, errors, warnings };
}

// Hübsch formatiertes JSON. tx_commands wird unverändert mitgeschrieben.
function serializeConfig(config) {
  const out = {
    version: config.version || '1.0',
    signals: config.signals || [],
    pages: (config.pages || []).map(p => ({
      title: p.title || '',
      widgets: p.widgets || [],
    })),
  };
  if (config.tx_commands !== undefined) out.tx_commands = config.tx_commands;
  return JSON.stringify(out, null, 2);
}
```

Und den `module.exports`-Block um die zwei neuen Funktionen ergänzen:

```js
if (typeof module !== 'undefined' && module.exports) {
  module.exports = {
    WIDGET_TYPES, DISPLAY, LIMITS,
    emptyConfig, defaultSignal, defaultWidget,
    sampleValue, isWarning, normalizeColor,
    validateConfig, serializeConfig,
  };
}
```

- [ ] **Step 4: Test laufen lassen, Erfolg bestätigen**

Run: `node --test test/www/editor-core.test.cjs`
Expected: PASS – alle Tests grün (16 gesamt).

- [ ] **Step 5: Commit**

```bash
git add examples/www/editor-core.js test/www/editor-core.test.cjs
git commit -m "feat(editor): Client-Validierung + Serialisierung mit Tests"
```

---

## Task 3: Geräteseitig – GET /api/config + größerer Asset-Puffer

`POST /api/config` bleibt unangetastet. Neuer GET-Handler liefert die aktuelle `dashboard.json` bzw. ein Skelett. Der Asset-Lesepuffer wird vergrößert, weil `app.js`/`editor-core.js` die bisherigen 8 KB überschreiten.

**Files:**
- Modify: `src/hal/web_server.c`
- Modify: `src/hal/web_server.h` (Kommentar)

- [ ] **Step 1: Asset-Puffer vergrößern und benennen**

In `src/hal/web_server.c`, die Defines oben (nach `#define DEST_PATH …`) ergänzen:

```c
/* Asset-Lesepuffer: editor-core.js + app.js überschreiten die alten 8 KB.
 * Einzelne www-Dateien dürfen damit bis WWW_FILE_MAX-1 Bytes groß sein. */
#define WWW_FILE_MAX   (48 * 1024)

/* Skelett, wenn noch keine dashboard.json existiert (Offline-Erststart). */
static const char SKELETON_JSON[] =
    "{\"version\":\"1.0\",\"signals\":[],"
    "\"pages\":[{\"title\":\"Seite 1\",\"widgets\":[]}],\"tx_commands\":[]}";
```

Im `get_handler` die Zeile

```c
    static char filebuf[8192];
```

ersetzen durch

```c
    static char filebuf[WWW_FILE_MAX];
```

- [ ] **Step 2: GET-/api/config-Handler ergänzen**

In `src/hal/web_server.c`, vor `post_config_handler` einfügen:

```c
/* GET /api/config: aktuelle dashboard.json liefern; fehlt sie → Skelett. */
static esp_err_t get_config_handler(httpd_req_t *req)
{
    static char cfgbuf[UPLOAD_MAX + 1];
    size_t len = 0;
    esp_err_t rc = waveshare_sd_read_file(DEST_PATH, cfgbuf, sizeof(cfgbuf), &len);

    httpd_resp_set_type(req, "application/json");
    if (rc == ESP_OK) {
        return httpd_resp_send(req, cfgbuf, len);
    }
    /* Nicht gefunden o. ä.: leeres Skelett, damit der Editor sauber startet. */
    ESP_LOGI(TAG, "GET /api/config: keine dashboard.json, sende Skelett");
    return httpd_resp_send(req, SKELETON_JSON, strlen(SKELETON_JSON));
}
```

- [ ] **Step 3: Handler registrieren**

In `web_server_start`, nach dem Registrieren von `post_cfg` (vor `get_any`) einfügen:

```c
    httpd_uri_t get_cfg = {
        .uri = "/api/config", .method = HTTP_GET,
        .handler = get_config_handler,
    };
    httpd_register_uri_handler(server, &get_cfg);
```

- [ ] **Step 4: Header-Kommentar aktualisieren**

In `src/hal/web_server.h`, den Kommentarblock erweitern:

```c
 *   GET  /              → /sdcard/www/index.html  (Fallback: eingebettetes HTML)
 *   GET  /<asset>       → /sdcard/www/<asset>      (app.js, style.css, …)
 *   GET  /api/config    → aktuelle dashboard.json  (Fallback: leeres Skelett)
 *   POST /api/config    → Raw-Body → temp → validieren → rename → Neustart
```

- [ ] **Step 5: Firmware bauen**

Run: `pio run -e esp32-s3-touch-lcd-7`
Expected: „SUCCESS" – kompiliert ohne Fehler/Warnungen zu `web_server.c`.

- [ ] **Step 6: Commit**

```bash
git add src/hal/web_server.c src/hal/web_server.h
git commit -m "feat(web): GET /api/config + groesserer Asset-Puffer"
```

> **Geräte-Verifikation (nach Flash, Task 8):** `curl http://<geräte-ip>/api/config`
> liefert die aktuelle JSON bzw. das Skelett mit `Content-Type: application/json`.

---

## Task 4: index.html + style.css – Editor-Gerüst mit Tabs

Ersetzt das reine Upload-Formular durch das Editor-Grundgerüst. Funktionalität folgt in Task 5–7; hier nur Struktur + Styles + Skript-Einbindung.

**Files:**
- Modify: `examples/www/index.html`
- Modify: `examples/www/style.css`

- [ ] **Step 1: index.html ersetzen**

Replace `examples/www/index.html` komplett:

```html
<!doctype html>
<html lang="de">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>CAN Dashboard – Editor</title>
  <link rel="stylesheet" href="style.css">
</head>
<body>
  <header>
    <h1>CAN Dashboard · Editor</h1>
    <div id="banner" class="banner hidden"></div>
    <nav class="tabs">
      <button class="tab active" data-tab="layout">▦ Layout</button>
      <button class="tab" data-tab="signals">⊟ Signale</button>
      <button class="tab" data-tab="backup">⇅ Backup</button>
    </nav>
  </header>

  <main>
    <!-- Layout-Tab -->
    <section id="tab-layout" class="tabpanel">
      <div class="toolbar">
        <div id="page-tabs" class="page-tabs"></div>
        <button id="add-widget">＋ Widget</button>
      </div>
      <div class="layout-grid">
        <div id="canvas-wrap" class="canvas-wrap">
          <div id="canvas" class="canvas"></div>
        </div>
        <aside id="prop-panel" class="prop-panel">
          <p class="muted">Kein Widget ausgewählt.</p>
        </aside>
      </div>
    </section>

    <!-- Signale-Tab -->
    <section id="tab-signals" class="tabpanel hidden">
      <button id="add-signal">＋ Signal</button>
      <div class="table-wrap"><table id="signal-table"></table></div>
    </section>

    <!-- Backup-Tab -->
    <section id="tab-backup" class="tabpanel hidden">
      <div class="card">
        <h2>Auf das Gerät speichern</h2>
        <p>Speichert die Konfiguration auf das Gerät. Das Gerät prüft sie und
           <strong>startet bei Erfolg neu</strong>.</p>
        <button id="save-device">💾 Speichern &amp; Übernehmen</button>
      </div>
      <div class="card">
        <h2>Datei</h2>
        <p>Konfiguration als <code>dashboard.json</code> sichern oder eine
           vorhandene Datei laden (auch ohne Gerät nutzbar).</p>
        <button id="export-file">⬇ Exportieren</button>
        <input type="file" id="import-file" accept=".json,application/json">
        <button id="new-config">Neu (leeres Skelett)</button>
      </div>
      <pre id="status"></pre>
    </section>
  </main>

  <script src="editor-core.js"></script>
  <script src="app.js"></script>
</body>
</html>
```

- [ ] **Step 2: style.css ersetzen**

Replace `examples/www/style.css` komplett:

```css
* { box-sizing: border-box; }
body { font-family: system-ui, sans-serif; margin: 0; background: #0d1116; color: #ecf0f1; }
header { padding: 0.6em 1em 0; background: #0a0d11; border-bottom: 1px solid #222b35; }
h1 { font-weight: 600; font-size: 1.2em; margin: 0 0 0.4em; }
h2 { font-weight: 600; font-size: 1.05em; }
main { padding: 1em; }
.hidden { display: none !important; }
.muted { color: #7a8a99; }
code { color: #9cf; }

.banner { padding: 0.5em 0.8em; border-radius: 6px; margin-bottom: 0.4em; font-size: 0.9em; }
.banner.offline { background: #4a3a12; color: #ffd479; }
.banner.error { background: #4a1a1a; color: #ff9d9d; }

.tabs { display: flex; gap: 2px; }
.tab { background: #141a20; color: #8aa; border: 0; border-radius: 6px 6px 0 0;
       padding: 0.5em 1.1em; font-size: 0.95em; cursor: pointer; }
.tab.active { background: #1c2530; color: #fff; }

.toolbar { display: flex; align-items: center; gap: 0.6em; margin-bottom: 0.6em; }
.page-tabs { display: flex; gap: 4px; flex: 1; flex-wrap: wrap; }
.page-tab { background: #141a20; color: #8aa; border: 1px solid #2a3540;
            border-radius: 4px; padding: 0.3em 0.8em; cursor: pointer; font-size: 0.85em; }
.page-tab.active { background: #2b3744; color: #fff; }

button { background: #2563a8; color: #fff; border: 0; border-radius: 6px;
         padding: 0.5em 1em; font-size: 0.95em; cursor: pointer; }
button:hover { background: #1d4f87; }
button.ghost { background: #243; }
button.danger { background: #7a2a2a; }

.layout-grid { display: flex; gap: 0.8em; align-items: flex-start; }
.canvas-wrap { flex: 3; min-width: 0; }
/* 800×480 maßstäblich; Widgets werden in App-JS in Prozent skaliert. */
.canvas { position: relative; width: 100%; aspect-ratio: 800 / 480;
          background: #1a1a1a; border: 1px solid #3a4654; border-radius: 4px;
          overflow: hidden; }
.widget { position: absolute; overflow: hidden; cursor: move;
          border: 1px solid transparent; }
.widget.selected { outline: 2px solid #4af; outline-offset: 2px; z-index: 5; }
.handle { position: absolute; right: -5px; bottom: -5px; width: 12px; height: 12px;
          background: #4af; border-radius: 2px; cursor: se-resize; }

.prop-panel { flex: 1; min-width: 230px; background: #141a20;
              border: 1px solid #2a3540; border-radius: 6px; padding: 0.8em; }
.prop-panel label { display: block; font-size: 0.8em; color: #9cc; margin: 0.5em 0 0.15em; }
.prop-panel input, .prop-panel select {
  width: 100%; background: #0d1116; color: #ecf0f1;
  border: 1px solid #2a3540; border-radius: 4px; padding: 0.35em; font-size: 0.9em; }
.prop-row { display: flex; gap: 0.5em; }
.prop-row > div { flex: 1; }

.table-wrap { overflow-x: auto; }
table { border-collapse: collapse; width: 100%; font-size: 0.85em; margin-top: 0.6em; }
th, td { border: 1px solid #2a3540; padding: 0.3em 0.4em; text-align: left; }
th { background: #1c2530; }
td input, td select { width: 7em; background: #0d1116; color: #ecf0f1;
                      border: 1px solid #2a3540; border-radius: 3px; padding: 0.2em; }

.card { background: #161d26; border-radius: 10px; padding: 1em; margin-bottom: 1em; max-width: 40em; }
input[type=file] { display: block; margin: 0.8em 0; color: #ecf0f1; }
pre { white-space: pre-wrap; margin-top: 1em; min-height: 1.5em; }
```

- [ ] **Step 3: Im Browser sichten (offline)**

Run: Datei `examples/www/index.html` im Browser öffnen (`file://`).
Expected: Drei Tabs sichtbar; Layout-Tab zeigt leeren Canvas + „Kein Widget ausgewählt"; noch keine Interaktion (app.js folgt). Keine Konsolenfehler bzgl. fehlender Dateien.

- [ ] **Step 4: Commit**

```bash
git add examples/www/index.html examples/www/style.css
git commit -m "feat(editor): HTML/CSS-Geruest mit Tab-Navigation"
```

---

## Task 5: app.js – Zustand laden, Tabs, Signale-Tabelle, Backup

DOM-Verdrahtung ohne Canvas-Render (der folgt in Task 6/7). Hier: Laden via GET mit Offline-Fallback, Tab-Umschaltung, Signale-Tabelle (CRUD), Backup (Export/Import/Speichern/Neu).

**Files:**
- Modify: `examples/www/app.js`

- [ ] **Step 1: app.js ersetzen**

Replace `examples/www/app.js` komplett:

```js
// Editor-App. Hängt an editor-core.js (gemeinsamer globaler Scope).
'use strict';

let config = emptyConfig();   // Single Source of Truth
let online = false;           // wird true, wenn GET /api/config gelingt
let currentPage = 0;          // aktive Seite im Layout-Tab
let selectedWidget = null;    // Referenz auf das ausgewählte Widget-Objekt

const $ = sel => document.querySelector(sel);
const $$ = sel => Array.from(document.querySelectorAll(sel));

// ── Initiales Laden ────────────────────────────────────────────────────────
async function init() {
  try {
    const res = await fetch('/api/config', { cache: 'no-store' });
    if (!res.ok) throw new Error('HTTP ' + res.status);
    config = normalizeLoaded(await res.json());
    online = true;
  } catch (e) {
    online = false;
    config = emptyConfig();
    showBanner('offline', 'Offline – kein Gerät verbunden. Datei importieren oder neu anlegen, dann exportieren.');
  }
  wireTabs();
  wireSignals();
  wireBackup();
  wireLayoutToolbar();
  renderAll();
}

// Geladene JSON auf erwartete Felder normalisieren (fehlende Arrays ergänzen).
function normalizeLoaded(obj) {
  const c = emptyConfig();
  c.version = obj.version || '1.0';
  c.signals = Array.isArray(obj.signals) ? obj.signals : [];
  c.pages = Array.isArray(obj.pages) && obj.pages.length ? obj.pages : [{ title: 'Seite 1', widgets: [] }];
  c.pages.forEach(p => { if (!Array.isArray(p.widgets)) p.widgets = []; });
  c.tx_commands = obj.tx_commands !== undefined ? obj.tx_commands : [];
  return c;
}

function showBanner(kind, text) {
  const b = $('#banner');
  b.className = 'banner ' + kind;
  b.textContent = text;
  b.classList.remove('hidden');
}

function renderAll() {
  renderSignalTable();
  renderPageTabs();
  renderCanvas();
  renderPropPanel();
  if (online) $('#save-device').disabled = false;
  else $('#save-device').disabled = true;
}

// ── Tabs ─────────────────────────────────────────────────────────────────
function wireTabs() {
  $$('.tab').forEach(btn => btn.addEventListener('click', () => {
    $$('.tab').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    const tab = btn.dataset.tab;
    ['layout', 'signals', 'backup'].forEach(t =>
      $('#tab-' + t).classList.toggle('hidden', t !== tab));
  }));
}

// ── Signale-Tabelle ────────────────────────────────────────────────────────
const SIGNAL_COLS = [
  { k: 'name', t: 'text' }, { k: 'can_id', t: 'text' },
  { k: 'byte_offset', t: 'number' }, { k: 'byte_length', t: 'number' },
  { k: 'endianness', t: 'select', opts: ['little', 'big'] },
  { k: 'signed', t: 'check' }, { k: 'float', t: 'check' },
  { k: 'scale', t: 'number' }, { k: 'offset', t: 'number' },
  { k: 'unit', t: 'text' }, { k: 'min', t: 'number' }, { k: 'max', t: 'number' },
  { k: 'stale_ms', t: 'number' }, { k: 'simulated', t: 'check' },
];

function wireSignals() {
  $('#add-signal').addEventListener('click', () => {
    config.signals.push(defaultSignal('Signal' + (config.signals.length + 1)));
    renderSignalTable();
  });
}

function renderSignalTable() {
  const tbl = $('#signal-table');
  const head = '<tr>' + SIGNAL_COLS.map(c => '<th>' + c.k + '</th>').join('') + '<th></th></tr>';
  const rows = config.signals.map((s, i) => {
    const cells = SIGNAL_COLS.map(c => '<td>' + cellInput(c, s, i) + '</td>').join('');
    return '<tr>' + cells + '<td><button class="danger" data-del-signal="' + i + '">🗑</button></td></tr>';
  }).join('');
  tbl.innerHTML = head + rows;

  tbl.querySelectorAll('[data-sig]').forEach(el => {
    el.addEventListener('change', () => {
      const i = +el.dataset.sig, key = el.dataset.key;
      const col = SIGNAL_COLS.find(c => c.k === key);
      config.signals[i][key] = col.t === 'check' ? el.checked
        : col.t === 'number' ? Number(el.value) : el.value;
      if (key === 'name') renderPropPanel(); // Dropdown-Quelle änderte sich
    });
  });
  tbl.querySelectorAll('[data-del-signal]').forEach(b => b.addEventListener('click', () => {
    const i = +b.dataset.delSignal;
    const used = widgetUsingSignal(config.signals[i].name);
    if (used) { alert("Signal '" + config.signals[i].name + "' wird verwendet von: " + used); return; }
    config.signals.splice(i, 1);
    renderSignalTable();
  }));
}

function cellInput(col, s, i) {
  const v = s[col.k];
  const attr = 'data-sig="' + i + '" data-key="' + col.k + '"';
  if (col.t === 'check') return '<input type="checkbox" ' + attr + (v ? ' checked' : '') + '>';
  if (col.t === 'select') return '<select ' + attr + '>' +
    col.opts.map(o => '<option' + (o === v ? ' selected' : '') + '>' + o + '</option>').join('') + '</select>';
  const type = col.t === 'number' ? 'number' : 'text';
  return '<input type="' + type + '" ' + attr + ' value="' + (v == null ? '' : v) + '">';
}

// Name eines Widgets, das das Signal nutzt (für Lösch-Schutz); sonst ''.
function widgetUsingSignal(name) {
  for (const p of config.pages)
    for (const w of p.widgets)
      if (w.signal === name) return (w.title || w.type);
  return '';
}

// ── Backup-Tab ─────────────────────────────────────────────────────────────
function wireBackup() {
  $('#export-file').addEventListener('click', exportFile);
  $('#import-file').addEventListener('change', importFile);
  $('#new-config').addEventListener('click', () => {
    if (!confirm('Aktuelle Konfiguration verwerfen und neu beginnen?')) return;
    config = emptyConfig(); currentPage = 0; selectedWidget = null; renderAll();
  });
  $('#save-device').addEventListener('click', saveToDevice);
}

function exportFile() {
  const v = validateConfig(config);
  if (!v.ok) { setStatus('Nicht exportiert – Fehler:\n' + v.errors.join('\n')); return; }
  const blob = new Blob([serializeConfig(config)], { type: 'application/json' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = 'dashboard.json';
  a.click();
  URL.revokeObjectURL(a.href);
  setStatus('Exportiert als dashboard.json' + (v.warnings.length ? '\nWarnungen:\n' + v.warnings.join('\n') : ''));
}

function importFile(ev) {
  const file = ev.target.files[0];
  if (!file) return;
  const reader = new FileReader();
  reader.onload = () => {
    try {
      config = normalizeLoaded(JSON.parse(reader.result));
      currentPage = 0; selectedWidget = null;
      renderAll();
      setStatus('Datei geladen: ' + file.name);
    } catch (e) { setStatus('Import fehlgeschlagen: ' + e.message); }
  };
  reader.readAsText(file);
  ev.target.value = '';
}

async function saveToDevice() {
  const v = validateConfig(config);
  if (!v.ok) { setStatus('Nicht gespeichert – Fehler:\n' + v.errors.join('\n')); return; }
  if (!confirm('Speichern und Gerät neu starten?')) return;
  setStatus('Speichere …');
  try {
    const res = await fetch('/api/config', { method: 'POST', body: serializeConfig(config) });
    const text = await res.text();
    setStatus((res.ok ? 'OK: ' : 'Fehler ' + res.status + ': ') + text);
  } catch (e) { setStatus('Netzwerkfehler: ' + e); }
}

function setStatus(msg) { $('#status').textContent = msg; }

document.addEventListener('DOMContentLoaded', init);
```

> **Hinweis:** `renderPageTabs`, `renderCanvas`, `renderPropPanel`, `wireLayoutToolbar`
> sind hier referenziert, werden aber erst in Task 6/7 implementiert. Damit Task 5
> isoliert lädt, in Step 2 temporäre Stubs einsetzen, in Task 6 ersetzen.

- [ ] **Step 2: Temporäre Stubs ans Dateiende setzen (vor DOMContentLoaded-Zeile)**

```js
// TEMPORÄR (wird in Task 6/7 ersetzt):
function renderPageTabs() {}
function renderCanvas() {}
function renderPropPanel() {}
function wireLayoutToolbar() {}
```

- [ ] **Step 3: Im Browser prüfen (offline)**

Run: `examples/www/index.html` neu laden (`file://`).
Expected: Offline-Banner erscheint. Signale-Tab: „＋ Signal" fügt Zeile hinzu, Felder editierbar, 🗑 entfernt. Backup-Tab: „Speichern & Übernehmen" ist deaktiviert (offline); „Neu" leert; Export lädt eine `dashboard.json` herunter. Keine Konsolenfehler.

- [ ] **Step 4: Commit**

```bash
git add examples/www/app.js
git commit -m "feat(editor): Laden/Offline, Tabs, Signale-Tabelle, Backup"
```

---

## Task 6: app.js – Seiten-Tabs, Canvas-Render (Beispielwert), Eigenschaften-Panel

Ersetzt die Stubs aus Task 5. Rendert Widgets mit Beispielwert und zeigt/editiert Eigenschaften.

**Files:**
- Modify: `examples/www/app.js`

- [ ] **Step 1: Stubs aus Task 5 Step 2 entfernen**

Die vier `// TEMPORÄR`-Stub-Funktionen löschen.

- [ ] **Step 2: Seiten-Tabs + Toolbar implementieren**

Vor der `DOMContentLoaded`-Zeile einfügen:

```js
// ── Layout: Seiten-Tabs & Toolbar ───────────────────────────────────────────
function wireLayoutToolbar() {
  $('#add-widget').addEventListener('click', () => {
    if (!config.signals.length) { alert('Bitte zuerst ein Signal anlegen.'); return; }
    const page = config.pages[currentPage];
    if (page.widgets.length >= LIMITS.widgetsPerPage) { alert('Maximal ' + LIMITS.widgetsPerPage + ' Widgets pro Seite.'); return; }
    const w = defaultWidget('gauge', config.signals[0].name);
    page.widgets.push(w);
    selectedWidget = w;
    renderCanvas(); renderPropPanel();
  });
}

function renderPageTabs() {
  const host = $('#page-tabs');
  host.innerHTML = '';
  config.pages.forEach((p, i) => {
    const b = document.createElement('button');
    b.className = 'page-tab' + (i === currentPage ? ' active' : '');
    b.textContent = (p.title || 'Seite ' + (i + 1));
    b.addEventListener('click', () => { currentPage = i; selectedWidget = null; renderCanvas(); renderPropPanel(); renderPageTabs(); });
    host.appendChild(b);
  });
  const add = document.createElement('button');
  add.className = 'page-tab'; add.textContent = '＋';
  add.addEventListener('click', () => {
    if (config.pages.length >= LIMITS.pages) { alert('Maximal ' + LIMITS.pages + ' Seiten.'); return; }
    config.pages.push({ title: 'Seite ' + (config.pages.length + 1), widgets: [] });
    currentPage = config.pages.length - 1; selectedWidget = null;
    renderPageTabs(); renderCanvas(); renderPropPanel();
  });
  host.appendChild(add);
}
```

- [ ] **Step 3: Canvas-Render mit Beispielwert implementieren**

Einfügen:

```js
// ── Layout: Canvas ───────────────────────────────────────────────────────
function renderCanvas() {
  const canvas = $('#canvas');
  canvas.innerHTML = '';
  const page = config.pages[currentPage];
  if (!page) return;
  page.widgets.forEach((w, i) => {
    const el = document.createElement('div');
    el.className = 'widget' + (w === selectedWidget ? ' selected' : '');
    el.style.left = (w.x / DISPLAY.w * 100) + '%';
    el.style.top = (w.y / DISPLAY.h * 100) + '%';
    el.style.width = (w.width / DISPLAY.w * 100) + '%';
    el.style.height = (w.height / DISPLAY.h * 100) + '%';
    el.style.background = normalizeColor(w.background_color) || '#1a1a1a';
    el.innerHTML = widgetPreview(w);
    el.addEventListener('mousedown', ev => startDrag(ev, w, el)); // Task 7
    canvas.appendChild(el);
    if (w === selectedWidget) {
      const h = document.createElement('div');
      h.className = 'handle';
      h.addEventListener('mousedown', ev => startResize(ev, w, el)); // Task 7
      el.appendChild(h);
    }
  });
}

// Vereinfachte Vorschau je Widget-Typ mit Beispielwert (≈70 %).
function widgetPreview(w) {
  const sig = config.signals.find(s => s.name === w.signal) || { min: 0, max: 100, unit: '' };
  const val = sampleValue(sig);
  const pct = Math.max(0, Math.min(1, (val - sig.min) / ((sig.max - sig.min) || 1)));
  const color = isWarning(w, val) ? (normalizeColor(w.warning_color) || '#FF4400')
                                  : (normalizeColor(w.normal_color) || '#00AA00');
  const title = w.title || w.signal || w.type;
  const num = (Math.round(val * 10) / 10) + (sig.unit ? ' ' + sig.unit : '');
  const cap = '<div style="font-size:11px;color:#9aa;text-align:center">' + esc(title) + '</div>';

  switch (w.type) {
    case 'bar':
      return cap + '<div style="margin:6px;height:60%;background:#333;border-radius:3px">' +
        '<div style="height:100%;width:' + (pct * 100) + '%;background:' + color + ';border-radius:3px"></div></div>' +
        '<div style="text-align:center;font-size:12px;color:#fff">' + num + '</div>';
    case 'led':
      return cap + '<div style="margin:auto;margin-top:8px;width:40%;aspect-ratio:1;border-radius:50%;background:' + color + '"></div>';
    case 'label':
      return cap + '<div style="text-align:center;font-size:18px;font-weight:600;color:#fff;margin-top:8px">' + num + '</div>';
    case 'chart':
      return cap + '<div style="display:flex;align-items:flex-end;gap:2px;height:60%;margin:6px">' +
        [40, 60, 50, 75, pct * 100].map(h => '<div style="flex:1;height:' + h + '%;background:' + color + '"></div>').join('') + '</div>';
    case 'arc':
    case 'gauge':
    default:
      return '<div style="width:100%;height:100%;border-radius:50%;background:conic-gradient(' + color + ' 0 ' +
        (pct * 100) + '%,#2a2a2a ' + (pct * 100) + '% 100%);display:flex;align-items:center;justify-content:center">' +
        '<div style="width:62%;height:62%;border-radius:50%;background:#1a1a1a;display:flex;flex-direction:column;align-items:center;justify-content:center;color:#fff">' +
        '<span style="font-size:10px;color:#9aa">' + esc(title) + '</span><span style="font-size:13px">' + num + '</span></div></div>';
  }
}

```

> **Hinweis:** `esc()` wurde bereits in Task 5 (oben bei den `$`/`$$`-Helfern)
> definiert und escaped `& < > "`. Hier NICHT erneut definieren – einfach nutzen.

- [ ] **Step 4: Eigenschaften-Panel implementieren**

Einfügen:

```js
// ── Layout: Eigenschaften-Panel ──────────────────────────────────────────
const PROP_FIELDS = [
  { k: 'type', t: 'select', opts: WIDGET_TYPES },
  { k: 'signal', t: 'signal' },
  { k: 'title', t: 'text' },
  { k: 'x', t: 'number' }, { k: 'y', t: 'number' },
  { k: 'width', t: 'number' }, { k: 'height', t: 'number' },
  { k: 'normal_color', t: 'color' }, { k: 'warning_color', t: 'color' },
  { k: 'warning_threshold', t: 'number' }, { k: 'background_color', t: 'color' },
];

function renderPropPanel() {
  const panel = $('#prop-panel');
  const w = selectedWidget;
  if (!w) { panel.innerHTML = '<p class="muted">Kein Widget ausgewählt.</p>'; return; }
  let html = '<h2>' + esc(w.type) + ' · Eigenschaften</h2>';
  PROP_FIELDS.forEach(f => {
    html += '<label>' + f.k + '</label>';
    if (f.t === 'select')
      html += '<select data-prop="' + f.k + '">' + f.opts.map(o => '<option' + (o === w[f.k] ? ' selected' : '') + '>' + o + '</option>').join('') + '</select>';
    else if (f.t === 'signal')
      html += '<select data-prop="signal">' + config.signals.map(s => '<option' + (s.name === w.signal ? ' selected' : '') + '>' + esc(s.name) + '</option>').join('') + '</select>';
    else if (f.t === 'color')
      html += '<input type="text" data-prop="' + f.k + '" value="' + (w[f.k] || '') + '" placeholder="#RRGGBB">';
    else
      html += '<input type="' + (f.t === 'number' ? 'number' : 'text') + '" data-prop="' + f.k + '" value="' + (w[f.k] == null ? '' : w[f.k]) + '">';
  });
  html += '<button class="danger" id="del-widget" style="margin-top:0.8em">🗑 Widget löschen</button>';
  panel.innerHTML = html;

  panel.querySelectorAll('[data-prop]').forEach(el => el.addEventListener('change', () => {
    const k = el.dataset.prop;
    const isNum = ['x', 'y', 'width', 'height', 'warning_threshold'].includes(k);
    w[k] = isNum ? Number(el.value) : el.value;
    renderCanvas();
  }));
  $('#del-widget').addEventListener('click', () => {
    const page = config.pages[currentPage];
    page.widgets.splice(page.widgets.indexOf(w), 1);
    selectedWidget = null; renderCanvas(); renderPropPanel();
  });
}
```

- [ ] **Step 5: Im Browser prüfen (offline)**

Run: `examples/www/index.html` neu laden. Signal anlegen (Signale-Tab) → Layout-Tab → „＋ Widget".
Expected: Widget erscheint als Gauge mit Beispielwert auf dem Canvas. Eigenschaften-Panel zeigt Felder; Typ/Signal/Farbe ändern aktualisiert die Vorschau sofort. Zweite Seite per „＋" anlegbar, Umschalten funktioniert. „🗑 Widget löschen" entfernt es. Noch kein Ziehen (Task 7).

- [ ] **Step 6: Commit**

```bash
git add examples/www/app.js
git commit -m "feat(editor): Seiten-Tabs, Canvas-Render, Eigenschaften-Panel"
```

---

## Task 7: app.js – Drag & Resize auf dem Canvas

Verbindet Maus-Interaktion mit `x/y/width/height`. Die Funktionen `startDrag`/`startResize` werden in Task 6 bereits referenziert.

**Files:**
- Modify: `examples/www/app.js`

- [ ] **Step 1: startDrag / startResize implementieren**

Vor der `DOMContentLoaded`-Zeile einfügen:

```js
// ── Layout: Drag & Resize ───────────────────────────────────────────────────
// Pixelumrechnung: Canvas-Breite (CSS-Pixel) ↔ 800×480-Koordinaten.
function canvasScale() {
  const rect = $('#canvas').getBoundingClientRect();
  return { sx: DISPLAY.w / rect.width, sy: DISPLAY.h / rect.height };
}

function startDrag(ev, w, el) {
  if (ev.target.classList.contains('handle')) return; // Resize hat Vorrang
  ev.preventDefault();
  selectedWidget = w; renderCanvas(); renderPropPanel();
  const { sx, sy } = canvasScale();
  const startX = ev.clientX, startY = ev.clientY, ox = w.x, oy = w.y;
  function move(e) {
    w.x = clamp(Math.round(ox + (e.clientX - startX) * sx), 0, DISPLAY.w - w.width);
    w.y = clamp(Math.round(oy + (e.clientY - startY) * sy), 0, DISPLAY.h - w.height);
    applyWidgetBox(w);
    syncPropPanel(w);
  }
  endOnUp(move);
}

function startResize(ev, w) {
  ev.preventDefault(); ev.stopPropagation();
  const { sx, sy } = canvasScale();
  const startX = ev.clientX, startY = ev.clientY, ow = w.width, oh = w.height;
  function move(e) {
    w.width = clamp(Math.round(ow + (e.clientX - startX) * sx), 20, DISPLAY.w - w.x);
    w.height = clamp(Math.round(oh + (e.clientY - startY) * sy), 20, DISPLAY.h - w.y);
    applyWidgetBox(w);
    syncPropPanel(w);
  }
  endOnUp(move);
}

function endOnUp(moveHandler) {
  function up() {
    document.removeEventListener('mousemove', moveHandler);
    document.removeEventListener('mouseup', up);
    renderCanvas(); // finaler Redraw (inkl. neuem Handle-Sitz)
  }
  document.addEventListener('mousemove', moveHandler);
  document.addEventListener('mouseup', up);
}

function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }

// Box live aktualisieren, ohne den ganzen Canvas neu zu zeichnen.
function applyWidgetBox(w) {
  const el = $('#canvas').children[config.pages[currentPage].widgets.indexOf(w)];
  if (!el) return;
  el.style.left = (w.x / DISPLAY.w * 100) + '%';
  el.style.top = (w.y / DISPLAY.h * 100) + '%';
  el.style.width = (w.width / DISPLAY.w * 100) + '%';
  el.style.height = (w.height / DISPLAY.h * 100) + '%';
}

// X/Y/B/H-Felder im Panel mitführen (ohne Panel-Neuaufbau).
function syncPropPanel(w) {
  ['x', 'y', 'width', 'height'].forEach(k => {
    const el = document.querySelector('[data-prop="' + k + '"]');
    if (el) el.value = w[k];
  });
}
```

- [ ] **Step 2: Im Browser prüfen (offline)**

Run: `examples/www/index.html` neu laden, Widget anlegen.
Expected: Widget lässt sich mit der Maus verschieben; X/Y im Panel laufen mit. Eckpunkt (blaues Quadrat) ändert Breite/Höhe; B/H laufen mit. Werte bleiben im Bereich 0…800 / 0…480.

- [ ] **Step 3: Node-Tests erneut laufen lassen (Regressionscheck Kernlogik)**

Run: `node --test test/www/editor-core.test.cjs`
Expected: PASS – alle editor-core-Tests weiterhin grün.
(Hinweis: Verzeichnisform `node --test test/www/` wird von Node 22 als Modulpfad
gedeutet → MODULE_NOT_FOUND; daher immer die Datei explizit angeben.)

- [ ] **Step 4: Commit**

```bash
git add examples/www/app.js
git commit -m "feat(editor): Drag & Resize der Widgets auf dem Canvas"
```

---

## Task 8: Integration – Geräte-Smoke-Test & Abschluss

Verifiziert das Zusammenspiel mit echter Hardware (kein automatisierter Test möglich – HAL/HTTP).

**Files:** keine Code-Änderung (nur ggf. Doku/Commit).

- [ ] **Step 1: www-Assets auf die SD-Karte kopieren**

Die vier Dateien aus `examples/www/` (`index.html`, `style.css`, `editor-core.js`, `app.js`) nach `/sdcard/www/` auf die SD-Karte kopieren (wie bei Feature 002).

- [ ] **Step 2: Firmware flashen & starten**

Run: `pio run -e esp32-s3-touch-lcd-7 -t upload && pio device monitor`
Expected: Boot ohne Fehler; Log „Webserver gestartet auf Port …".

- [ ] **Step 3: GET /api/config prüfen**

Run: `curl -i http://<geräte-ip>/api/config`
Expected: `200`, `Content-Type: application/json`, Body = aktuelle `dashboard.json` (oder Skelett, falls keine existiert).

- [ ] **Step 4: Editor im Browser gegen das Gerät bedienen**

Run: `http://<geräte-ip>/` öffnen.
Expected: Kein Offline-Banner; Signale/Widgets der aktuellen Config werden geladen und angezeigt. Eine Änderung machen → Backup-Tab → „Speichern & Übernehmen" → Erfolgsmeldung, Gerät startet neu, neues Layout erscheint auf dem Display.

- [ ] **Step 5: Offline-Pfad gegenprüfen**

Run: `examples/www/index.html` lokal (`file://`) öffnen.
Expected: Offline-Banner; „Speichern & Übernehmen" deaktiviert; Import einer zuvor exportierten `dashboard.json` lädt sie korrekt; erneuter Export erzeugt gültige Datei.

- [ ] **Step 6: Abschluss-Commit**

```bash
git add -A
git commit -m "test(editor): Geraete- und Offline-Smoke-Test abgeschlossen"
```

---

## Self-Review (Autor)

- **Spec-Abdeckung:** Hybrid-Editor (Task 6/7), Signale-Tab (Task 5), Layout-Tab (Task 6/7), Backup-Tab (Task 5), Beispielwert-Render (Task 6), Datenfluss Gerät+Datei (Task 3/5), Offline-Modus (Task 5, Verifikation Task 8), GET-Endpoint + Puffer (Task 3), Validierung spiegelt `config_loader.c` (Task 2), TX wird durchgereicht (`serializeConfig`/`normalizeLoaded`, Task 2/5). ✅
- **Platzhalter:** keine offenen TODO/TBD; jede Code-Funktion ist vollständig ausformuliert. Die in Task 5 referenzierten, in Task 6/7 definierten Funktionen sind durch explizite Stubs (Task 5 Step 2) überbrückt.
- **Typ-/Namenskonsistenz:** `validateConfig`→`{ok,errors,warnings}`, `serializeConfig`, `normalizeColor`, `sampleValue`, `isWarning`, `emptyConfig`, `defaultSignal`, `defaultWidget`, `DISPLAY`, `LIMITS`, `WIDGET_TYPES` einheitlich zwischen `editor-core.js`, Tests und `app.js` verwendet. `startDrag`/`startResize` in Task 6 referenziert, in Task 7 definiert.
- **Bekannte Grenze:** Canvas-Vorschau ist bewusst eine Annäherung, kein LVGL-Pixelabbild (Spec-Nichtziel).
