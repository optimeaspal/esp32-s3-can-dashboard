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
