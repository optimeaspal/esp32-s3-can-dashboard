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

// Beispielwert bei einem bestimmten Anteil (0…1) des Anzeigebereichs.
function sampleValueAt(signal, pct) {
  const min = Number(signal.min) || 0;
  const max = Number(signal.max);
  const hi = isNaN(max) ? 100 : max;
  return min + pct * (hi - min);
}

// Beispielwert = 70 % des Anzeigebereichs (Default für die Vorschau).
function sampleValue(signal) {
  return sampleValueAt(signal, 0.7);
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

// Spiegelt config_loader.c. errors blockieren das Speichern, warnings nicht.
// Bewusst strenger als der C-Parser (gewollte Editor-Leitplanken, NICHT an C
// angleichen): byte_length nur {1,2,4}, endianness nur little/big, type nur aus
// WIDGET_TYPES. Der C-Parser akzeptiert hier mehr; das Gerät bleibt maßgeblich.
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

if (typeof module !== 'undefined' && module.exports) {
  module.exports = {
    WIDGET_TYPES, DISPLAY, LIMITS,
    emptyConfig, defaultSignal, defaultWidget,
    sampleValue, sampleValueAt, isWarning, normalizeColor,
    validateConfig, serializeConfig,
  };
}
