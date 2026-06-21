// Editor-App. Hängt an editor-core.js (gemeinsamer globaler Scope).
'use strict';

let config = emptyConfig();   // Single Source of Truth
let online = false;           // wird true, wenn GET /api/config gelingt
let currentPage = 0;          // aktive Seite im Layout-Tab
let selectedWidget = null;    // Referenz auf das ausgewählte Widget-Objekt

const $ = sel => document.querySelector(sel);
const $$ = sel => Array.from(document.querySelectorAll(sel));

// HTML-Sonderzeichen escapen – nötig, weil Tabelle/Canvas per innerHTML aufgebaut
// werden und Nutzereingaben (z. B. ein " in can_id) sonst die Zeile zerschießen.
function esc(s) {
  return String(s).replace(/[&<>"]/g, c =>
    ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
}

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
    col.opts.map(o => '<option' + (o === v ? ' selected' : '') + '>' + esc(o) + '</option>').join('') + '</select>';
  const type = col.t === 'number' ? 'number' : 'text';
  return '<input type="' + type + '" ' + attr + ' value="' + (v == null ? '' : esc(v)) + '">';
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
    // Container (Hintergrund/Rand/Radius) zeichnet das SVG selbst – gerätegetreu.
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

// Polarkoordinaten → [x,y]. Winkelkonvention wie LVGL: 0° = Osten (3 Uhr),
// positive Winkel im Uhrzeigersinn (passt zur nach unten wachsenden SVG-Y-Achse).
function polar(cx, cy, r, deg) {
  const a = deg * Math.PI / 180;
  return [cx + r * Math.cos(a), cy + r * Math.sin(a)];
}

// SVG-Pfad für einen Kreisbogen von startDeg bis endDeg (im Uhrzeigersinn).
function arcPath(cx, cy, r, startDeg, endDeg) {
  const [x1, y1] = polar(cx, cy, r, startDeg);
  const [x2, y2] = polar(cx, cy, r, endDeg);
  const large = (endDeg - startDeg) % 360 > 180 ? 1 : 0;
  return 'M ' + x1.toFixed(2) + ' ' + y1.toFixed(2) +
         ' A ' + r + ' ' + r + ' 0 ' + large + ' 1 ' + x2.toFixed(2) + ' ' + y2.toFixed(2);
}

// ── Geräte-Konstanten (exakt aus src/ui/widgets/*.c gespiegelt) ─────────────
const GAUGE_START = 135, GAUGE_SPAN = 270;   // lv_meter/lv_arc: 270°, Lücke unten
const TITLE_H = 24;                          // WC_TITLE_H
const FONT = "'Montserrat','Segoe UI',system-ui,sans-serif";
// def_normal je Widget-Typ in wc_base_init(): gilt, wenn keine Farbe gesetzt ist.
const TYPE_DEFAULT_COLOR = {
  gauge: '#00B894', chart: '#0088FF', bar: '#00B894',
  led: '#E74C3C', label: '#ECF0F1', arc: '#FFCC00',
};
const WARN_DEFAULT = '#FF4400';              // WC_DEFAULT_WARN

// Gerätegetreue Vorschau: ein SVG pro Widget mit viewBox in echten 800×480-Pixeln,
// damit Schrift-/Tick-/Strichgrößen exakt den LVGL-Werten entsprechen und nur der
// Gesamt-Canvas einheitlich skaliert. Beispielwert ≈ 70 % des Bereichs.
function widgetPreview(w) {
  const sig = config.signals.find(s => s.name === w.signal) || { min: 0, max: 100, unit: '' };
  const min = Number(sig.min) || 0;
  const max = isNaN(Number(sig.max)) ? 100 : Number(sig.max);
  const val = sampleValue(sig);
  const pct = Math.max(0, Math.min(1, (val - min) / ((max - min) || 1)));
  const color = isWarning(w, val)
    ? (normalizeColor(w.warning_color) || WARN_DEFAULT)
    : (normalizeColor(w.normal_color) || TYPE_DEFAULT_COLOR[w.type] || '#00AA00');
  const unit = sig.unit || '';
  const W = Math.max(1, Number(w.width) || 1);
  const H = Math.max(1, Number(w.height) || 1);
  const bg = normalizeColor(w.background_color) || '#16213E';

  // Container: abgerundetes Rechteck + Rand, optionaler Titel oben (nur wenn gesetzt).
  let s = '<svg viewBox="0 0 ' + W + ' ' + H + '" width="100%" height="100%" style="display:block">';
  s += '<rect x="0.5" y="0.5" width="' + (W - 1) + '" height="' + (H - 1) +
       '" rx="8" fill="' + bg + '" stroke="#0F3460" stroke-width="1"/>';
  if (w.title)
    s += txt(W / 2, 16, esc(w.title), '#ECF0F1', 14);

  switch (w.type) {
    case 'arc':   s += arcGfx(W, H, color, pct, val, unit); break;
    case 'chart': s += chartGfx(W, H, color, pct); break;
    case 'bar':   s += barGfx(W, H, color, pct, val, unit); break;
    case 'led':   s += ledGfx(W, H, color, val, w); break;
    case 'label': s += labelGfx(W, H, color, val, unit); break;
    case 'gauge':
    default:      s += gaugeGfx(W, H, color, pct, min, max, w); break;
  }
  return s + '</svg>';
}

// Kleiner SVG-Text-Helfer (zentriert).
function txt(x, y, content, fill, size, weight) {
  return '<text x="' + x + '" y="' + y.toFixed(1) + '" text-anchor="middle" fill="' + fill +
    '" font-size="' + size + '" font-family="' + FONT + '"' +
    (weight ? ' font-weight="' + weight + '"' : '') + '>' + content + '</text>';
}

// gauge → lv_meter: 270°-Skala, 41 Ticks (jeder 8. groß + Zahl), Zeiger, Warn-Band.
function gaugeGfx(W, H, color, pct, min, max, w) {
  const side = Math.max(20, Math.min(W, H) - 28);
  const cx = W / 2, cy = H / 2 + TITLE_H / 2, r = side / 2;
  let s = '';
  const wt = Number(w.warning_threshold) || 0;
  if (wt > min && wt < max) {                       // Warnbereich-Bogen (Breite 5)
    const a0 = GAUGE_START + (wt - min) / (max - min) * GAUGE_SPAN;
    s += '<path d="' + arcPath(cx, cy, r, a0, GAUGE_START + GAUGE_SPAN) +
         '" fill="none" stroke="' + (normalizeColor(w.warning_color) || WARN_DEFAULT) + '" stroke-width="5"/>';
  }
  for (let i = 0; i <= 40; i++) {
    const major = i % 8 === 0;
    const d = GAUGE_START + i / 40 * GAUGE_SPAN;
    const len = major ? 12 : 8;
    const [x1, y1] = polar(cx, cy, r, d);
    const [x2, y2] = polar(cx, cy, r - len, d);
    s += '<line x1="' + x1.toFixed(1) + '" y1="' + y1.toFixed(1) + '" x2="' + x2.toFixed(1) + '" y2="' +
         y2.toFixed(1) + '" stroke="' + (major ? '#ECF0F1' : '#444466') + '" stroke-width="' + (major ? 4 : 2) + '"/>';
    if (major) {
      const [lx, ly] = polar(cx, cy, r - len - 12, d);
      s += txt(lx, ly + 4, String(Math.round(min + i / 40 * (max - min))), '#ECF0F1', 13);
    }
  }
  const [nx, ny] = polar(cx, cy, r - 10, GAUGE_START + pct * GAUGE_SPAN);
  s += '<line x1="' + cx + '" y1="' + cy.toFixed(1) + '" x2="' + nx.toFixed(1) + '" y2="' + ny.toFixed(1) +
       '" stroke="' + color + '" stroke-width="4" stroke-linecap="round"/>';
  s += '<circle cx="' + cx + '" cy="' + cy.toFixed(1) + '" r="5" fill="' + color + '"/>';
  return s;
}

// arc → lv_arc: 270°-Bogen Breite 10, Track #0F3460, Zahl mittig.
function arcGfx(W, H, color, pct, val, unit) {
  const side = Math.max(20, Math.min(W, H) - 28);
  const cx = W / 2, cy = H / 2 + TITLE_H / 2, r = side / 2;
  const valDeg = GAUGE_START + Math.max(0.001, pct) * GAUGE_SPAN;
  return '<path d="' + arcPath(cx, cy, r, GAUGE_START, GAUGE_START + GAUGE_SPAN) +
    '" fill="none" stroke="#0F3460" stroke-width="10" stroke-linecap="round"/>' +
    '<path d="' + arcPath(cx, cy, r, GAUGE_START, valDeg) +
    '" fill="none" stroke="' + color + '" stroke-width="10" stroke-linecap="round"/>' +
    txt(cx, cy + 5, esc(Math.round(val) + (unit ? ' ' + unit : '')), '#ECF0F1', 16);
}

// chart → lv_chart (LINE): BG #1A1A2E, 5×6 Rasterlinien, Linie endet beim Wert.
function chartGfx(W, H, color, pct) {
  const cw = Math.max(10, W - 24), ch = Math.max(10, H - 24 - TITLE_H);
  const x0 = (W - cw) / 2, y1 = H - 4, y0 = y1 - ch;
  let s = '<rect x="' + x0.toFixed(1) + '" y="' + y0.toFixed(1) + '" width="' + cw + '" height="' +
    ch.toFixed(1) + '" fill="#1A1A2E" stroke="#0F3460" stroke-width="1"/>';
  for (let i = 1; i <= 5; i++) {                    // 5 horizontale Teilungslinien
    const gy = y0 + i / 6 * ch;
    s += '<line x1="' + x0.toFixed(1) + '" y1="' + gy.toFixed(1) + '" x2="' + (x0 + cw).toFixed(1) +
         '" y2="' + gy.toFixed(1) + '" stroke="#334466" stroke-width="0.5"/>';
  }
  for (let i = 1; i <= 6; i++) {                    // 6 vertikale Teilungslinien
    const gx = x0 + i / 7 * cw;
    s += '<line x1="' + gx.toFixed(1) + '" y1="' + y0.toFixed(1) + '" x2="' + gx.toFixed(1) +
         '" y2="' + y1 + '" stroke="#334466" stroke-width="0.5"/>';
  }
  const samples = [0.42, 0.6, 0.5, 0.68, 0.55, 0.72, pct];
  const n = samples.length;
  const pts = samples.map((p, i) =>
    (x0 + i / (n - 1) * cw).toFixed(1) + ',' + (y1 - p * ch).toFixed(1)).join(' ');
  return s + '<polyline points="' + pts + '" fill="none" stroke="' + color +
    '" stroke-width="2" stroke-linejoin="round" stroke-linecap="round"/>';
}

// bar → lv_bar: Breite W-28, Höhe 26, Radius 4, Wert darunter.
function barGfx(W, H, color, pct, val, unit) {
  const bw = Math.max(10, W - 28), bh = 26, x0 = (W - bw) / 2, y0 = H / 2 - bh / 2;
  return '<rect x="' + x0.toFixed(1) + '" y="' + y0.toFixed(1) + '" width="' + bw + '" height="' + bh +
    '" rx="4" fill="#0F3460"/>' +
    '<rect x="' + x0.toFixed(1) + '" y="' + y0.toFixed(1) + '" width="' + (bw * pct).toFixed(1) + '" height="' + bh +
    '" rx="4" fill="' + color + '"/>' +
    txt(W / 2, y0 + bh + 18, esc(Math.round(val) + (unit ? ' ' + unit : '')), '#ECF0F1', 14);
}

// led → lv_led: Kreis min(W,H)-40, Glow wenn an (Wert ≥ Schwelle), Status darunter.
function ledGfx(W, H, color, val, w) {
  const d = Math.max(20, Math.min(W, H) - 40);
  const cx = W / 2, cy = H / 2 + TITLE_H / 2, r = d / 2;
  const thr = (Number(w.warning_threshold) || 0) > 0 ? Number(w.warning_threshold) : 0.5;
  const on = val >= thr;
  let s = '';
  if (on) s += '<circle cx="' + cx + '" cy="' + cy.toFixed(1) + '" r="' + (r + 5).toFixed(1) +
    '" fill="' + color + '" opacity="0.35"/>';
  s += '<circle cx="' + cx + '" cy="' + cy.toFixed(1) + '" r="' + r.toFixed(1) + '" fill="' + color +
    '" opacity="' + (on ? '1' : '0.25') + '"/>';
  return s + txt(cx, cy + r + 18, on ? 'AN' : 'AUS', '#ECF0F1', 14);
}

// label → lv_label: große Zahl in Montserrat-24, Wertfarbe.
function labelGfx(W, H, color, val, unit) {
  const cy = H / 2 + TITLE_H / 2;
  return txt(W / 2, cy + 8, esc(val.toFixed(1) + (unit ? ' ' + unit : '')), color, 24, 500);
}

// ── Layout: Eigenschaften-Panel ──────────────────────────────────────────
// Gängige Dashboard-Farben für die Schnellauswahl (Name als Tooltip).
const COLOR_PALETTE = [
  ['#00AA00', 'Grün'], ['#7ED321', 'Hellgrün'], ['#FFCC00', 'Gelb'],
  ['#FF8800', 'Orange'], ['#FF4400', 'Rotorange'], ['#FF0000', 'Rot'],
  ['#0088FF', 'Blau'], ['#00CCCC', 'Cyan'], ['#CC00CC', 'Magenta'],
  ['#FFFFFF', 'Weiß'], ['#888888', 'Grau'], ['#1A1A1A', 'Dunkel'],
];

const PROP_FIELDS = [
  { k: 'type', t: 'select', opts: WIDGET_TYPES },
  { k: 'signal', t: 'signal' },
  { k: 'title', t: 'text' },
  { k: 'x', t: 'number' }, { k: 'y', t: 'number' },
  { k: 'width', t: 'number' }, { k: 'height', t: 'number' },
  { k: 'normal_color', t: 'color' }, { k: 'warning_color', t: 'color' },
  { k: 'warning_threshold', t: 'number' }, { k: 'background_color', t: 'color' },
];

// Farbfeld: klickbares Farbquadrat (nativer Picker) + Hex-Textfeld + Palette.
// Alle drei spiegeln denselben Wert und werden in renderPropPanel synchron gehalten.
function colorFieldHTML(key, value) {
  const hex = normalizeColor(value) || '#000000';
  let pal = '<div class="swatches" data-swatch-for="' + key + '">';
  COLOR_PALETTE.forEach(([c, name]) => {
    pal += '<button type="button" class="swatch" title="' + esc(name) + ' (' + c + ')"' +
           ' data-set-color="' + c + '" style="background:' + c + '"></button>';
  });
  pal += '</div>';
  return '<div class="color-field">' +
    '<input type="color" data-colorpick="' + key + '" value="' + hex.toLowerCase() + '">' +
    '<input type="text" data-prop="' + key + '" value="' + esc(value || '') + '" placeholder="#RRGGBB">' +
    '</div>' + pal;
}

// Farbquadrat dem aktuellen (Text-)Wert nachführen; nur gültige Hex-Werte
// kann der native Picker übernehmen, ungültige Eingaben lässt er unverändert.
function syncColorControls(key, value) {
  const pick = document.querySelector('[data-colorpick="' + key + '"]');
  const hex = normalizeColor(value);
  if (pick && hex) pick.value = hex.toLowerCase();
}

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
      html += colorFieldHTML(f.k, w[f.k]);
    else
      html += '<input type="' + (f.t === 'number' ? 'number' : 'text') + '" data-prop="' + f.k + '" value="' + (w[f.k] == null ? '' : esc(w[f.k])) + '">';
  });
  html += '<button class="danger" id="del-widget" style="margin-top:0.8em">🗑 Widget löschen</button>';
  panel.innerHTML = html;

  panel.querySelectorAll('[data-prop]').forEach(el => el.addEventListener('change', () => {
    const k = el.dataset.prop;
    const isNum = ['x', 'y', 'width', 'height', 'warning_threshold'].includes(k);
    w[k] = isNum ? Number(el.value) : el.value;
    syncColorControls(k, w[k]);
    renderCanvas();
  }));

  // Eine Farbe an allen drei Bedienelementen + im Modell setzen.
  function setColor(key, hex) {
    w[key] = hex;
    const txt = panel.querySelector('[data-prop="' + key + '"]');
    if (txt) txt.value = hex;
    syncColorControls(key, hex);
    renderCanvas();
  }
  // Natives Farbquadrat → Textfeld/Modell.
  panel.querySelectorAll('[data-colorpick]').forEach(el =>
    el.addEventListener('input', () => setColor(el.dataset.colorpick, el.value.toUpperCase())));
  // Palette-Swatch → alles setzen.
  panel.querySelectorAll('[data-swatch-for]').forEach(box =>
    box.addEventListener('click', ev => {
      const btn = ev.target.closest('[data-set-color]');
      if (btn) setColor(box.dataset.swatchFor, btn.dataset.setColor);
    }));

  $('#del-widget').addEventListener('click', () => {
    const page = config.pages[currentPage];
    page.widgets.splice(page.widgets.indexOf(w), 1);
    selectedWidget = null; renderCanvas(); renderPropPanel();
  });
}

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

document.addEventListener('DOMContentLoaded', init);
