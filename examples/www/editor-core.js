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
