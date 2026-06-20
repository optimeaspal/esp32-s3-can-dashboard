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
