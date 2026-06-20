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

// TEMPORÄR (wird in Task 6/7 ersetzt):
function renderPageTabs() {}
function renderCanvas() {}
function renderPropPanel() {}
function wireLayoutToolbar() {}

document.addEventListener('DOMContentLoaded', init);
