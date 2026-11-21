import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createBrowserEnv, plain } from './helpers/browser-env.mjs';

function load({
  startMethod = 'timed',
  starterRelayId = 'K2',
  relayNames = {},
  relayLabels = {},
  inputNames = {},
} = {}) {
  const env = createBrowserEnv({
    stores: {
      settings: {
        vehicle: { start_method: startMethod, starter_relay_id: starterRelayId },
        inputs: [
          { id: 'IN1', type: 0, name: inputNames.IN1 || '' },
          { id: 'IN2', type: 1, name: inputNames.IN2 || '' },
          { id: 'IN3', type: 0, name: inputNames.IN3 || '' },
        ],
        relays: [
          { id: 'K1', name: relayNames.K1 || '' },
          { id: 'K2', name: relayNames.K2 || '' },
          { id: 'K3', name: relayNames.K3 || '' },
        ],
        sensors: [{ id: 'T1', name: 'Салон' }, { id: '', name: 'unmapped' }],
      },
      deviceStatus: {
        hwMap: {
          relays: [
            { id: 'K1', label: relayLabels.K1 || '' },
            { id: 'K2', label: relayLabels.K2 || '' },
            { id: 'K3', label: relayLabels.K3 || '' },
          ],
          inputs: [
            { id: 'IN1', label: '' },
            { id: 'IN2', label: '' },
            { id: 'IN3', label: '' },
          ],
        },
      },
      programs: { items: [{ id: 1, name: 'Прогрев' }] },
      uiFormat: { inputLabelById: (id) => String(id) },
    },
  });
  env.load('js/app/options.js');
  return (src) => plain(env.APP.options.forSource(src, env.Alpine));
}

const values = (opts) => opts.map((o) => o.value);

test('input lists are filtered by type and start with "none"', () => {
  const opt = load();
  assert.deepEqual(values(opt('inputIds')), ['', 'IN1', 'IN2', 'IN3']);
  assert.deepEqual(values(opt('digitalInputIds')), ['', 'IN1', 'IN3']);
  assert.deepEqual(values(opt('pulseInputIds')), ['', 'IN2']);
});

test('empty input name stays silk-only without double prefix', () => {
  const opts = load()('inputIds');
  assert.equal(opts[1].label, 'IN1');
});

test('start input list follows vehicle.start_method', () => {
  assert.deepEqual(values(load({ startMethod: 'timed' })('startInputIds')), []);
  assert.deepEqual(values(load({ startMethod: 'rpm' })('startInputIds')), ['', 'IN2']);
  assert.deepEqual(values(load({ startMethod: 'input' })('startInputIds')), ['', 'IN1', 'IN3']);
});

test('program relays exclude the starter relay', () => {
  assert.deepEqual(values(load()('programRelays')), ['K1', 'K3']);
  assert.deepEqual(values(load({ starterRelayId: '' })('programRelays')), ['K1', 'K2', 'K3']);
  const relays = load()('relays');
  assert.deepEqual(values(relays), ['K1', 'K2', 'K3']);
  assert.equal(relays[0].label, 'K1');
});

test('relay label uses settings name with silk', () => {
  const relays = load({ relayNames: { K2: 'Зажигание' } })('relays');
  assert.equal(relays[1].label, 'K2 · Зажигание');
});

test('empty relay name does not fall back to bootstrap label', () => {
  const relays = load({ relayNames: { K2: '' }, relayLabels: { K2: 'Зажигание' } })('relays');
  assert.equal(relays[1].label, 'K2');
});

test('missing relay settings row uses bootstrap label', () => {
  const env = createBrowserEnv({
    stores: {
      settings: { relays: [{ id: 'K1', name: '' }] },
      deviceStatus: {
        hwMap: {
          relays: [
            { id: 'K1', label: 'Стартер' },
            { id: 'K2', label: 'Зажигание' },
          ],
        },
      },
    },
  });
  env.load('js/app/options.js');
  const relays = plain(env.APP.options.forSource('relays', env.Alpine));
  assert.equal(relays[0].label, 'K1');
  assert.equal(relays[1].label, 'K2 · Зажигание');
});

test('sensor ids skip unmapped sensors', () => {
  const opts = load()('sensorIds');
  assert.deepEqual(values(opts), ['', 'T1']);
  assert.equal(opts[1].label, 'T1 · Салон');
});

test('programs and unknown sources', () => {
  const opt = load();
  assert.deepEqual(values(opt('programs')), [0, 1]);
  assert.deepEqual(opt('nope'), []);
  assert.deepEqual(opt(''), []);
});
