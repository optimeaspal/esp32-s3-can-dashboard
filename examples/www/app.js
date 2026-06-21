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
      html += '<input type="text" data-prop="' + f.k + '" value="' + esc(w[f.k] || '') + '" placeholder="#RRGGBB">';
    else
      html += '<input type="' + (f.t === 'number' ? 'number' : 'text') + '" data-prop="' + f.k + '" value="' + (w[f.k] == null ? '' : esc(w[f.k])) + '">';
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
