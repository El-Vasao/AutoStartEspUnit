import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createBrowserEnv, plain } from './helpers/browser-env.mjs';

function load({ startMethod = 'timed', starterRelayId = 2002 } = {}) {
  const env = createBrowserEnv({
    stores: {
      settings: {
        vehicle: { start_method: startMethod, starter_relay_id: starterRelayId },
        inputs: [
          { id: 1001, type: 0 },
          { id: 1002, type: 1 },
          { id: 1003, type: 0 },
        ],
        sensors: [{ id: 5, name: 'Салон' }, { id: 0, name: 'unmapped' }],
      },
      deviceStatus: {
        hwMap: {
          relays: [
            { id: 2001, label: 'K1' },
            { id: 2002, label: 'K2' },
            { id: 2003, label: 'K3' },
          ],
        },
      },
      programs: { items: [{ id: 1, name: 'Прогрев' }] },
      uiFormat: { inputLabelById: (id) => 'IN' + id },
    },
  });
  env.load('js/app/options.js');
  return (src) => plain(env.APP.options.forSource(src, env.Alpine));
}

const values = (opts) => opts.map((o) => o.value);

test('input lists are filtered by type and start with "none"', () => {
  const opt = load();
  assert.deepEqual(values(opt('inputIds')), [0, 1001, 1002, 1003]);
  assert.deepEqual(values(opt('digitalInputIds')), [0, 1001, 1003]);
  assert.deepEqual(values(opt('pulseInputIds')), [0, 1002]);
});

test('start input list follows vehicle.start_method', () => {
  assert.deepEqual(values(load({ startMethod: 'timed' })('startInputIds')), []);
  assert.deepEqual(values(load({ startMethod: 'rpm' })('startInputIds')), [0, 1002]);
  assert.deepEqual(values(load({ startMethod: 'input' })('startInputIds')), [0, 1001, 1003]);
});

test('program relays exclude the starter relay', () => {
  assert.deepEqual(values(load()('programRelays')), [2001, 2003]);
  assert.deepEqual(values(load({ starterRelayId: 0 })('programRelays')), [2001, 2002, 2003]);
  assert.deepEqual(values(load()('relays')), [2001, 2002, 2003]);
});

test('sensor ids skip unmapped sensors', () => {
  const opts = load()('sensorIds');
  assert.deepEqual(values(opts), [0, 5]);
  assert.equal(opts[1].label, 'Салон');
});

test('programs and unknown sources', () => {
  const opt = load();
  assert.deepEqual(values(opt('programs')), [0, 1]);
  assert.deepEqual(opt('nope'), []);
  assert.deepEqual(opt(''), []);
});
