import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createBrowserEnv } from './helpers/browser-env.mjs';

function loadFormat() {
  const env = createBrowserEnv({
    stores: {
      settings: {
        sensors: [
          { id: 'T1', name: 'Салон', rom: '28:AA' },
          { id: 'T2', name: '', rom: '28:BB' },
        ],
      },
      deviceStatus: {
        temperatureSensorRoms: ['28:BB', '28:CC'],
      },
    },
  });
  env.load('js/app/stores/ui-formatters-store.js');
  env.APP.stores.registerUiFormattersStore(env.Alpine);
  return env.Alpine.store('uiFormat');
}

test('live sensor tile uses silk id, not bus index', () => {
  const fmt = loadFormat();
  assert.equal(fmt.sensorLiveLabel({ id: 'T2' }, 0), 'T2');
  assert.equal(fmt.sensorLiveLabel({ id: 'T1' }, 0), 'T1 · Салон');
});

test('unbound live sensor shows the ROM at that bus index', () => {
  const fmt = loadFormat();
  assert.equal(fmt.sensorLiveLabel({ id: null }, 1), '28:CC');
  assert.equal(fmt.sensorLiveLabel({}, 5), '—');
});

test('ROM option label is silk and name', () => {
  const fmt = loadFormat();
  assert.equal(fmt.sensorLabelByRom('28:AA'), 'T1 · Салон');
  assert.equal(fmt.sensorLabelByRom('28:BB'), 'T2');
  assert.equal(fmt.sensorLabelByRom('28:ZZ'), '28:ZZ');
});
