import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createBrowserEnv, readProjectFile } from './helpers/browser-env.mjs';

test('toast hides itself after the duration', async () => {
  const env = createBrowserEnv();
  env.load('js/app/stores/ui-toast-store.js');
  env.APP.stores.registerUiToastStore(env.Alpine);
  const toast = env.Alpine.store('uiToast');
  toast.show('saved', 'success', 20);
  assert.equal(toast.open, true);
  assert.equal(toast.variant, 'success');
  await new Promise((r) => setTimeout(r, 40));
  assert.equal(toast.open, false);
});

test('showIf eq and in follow the settings value', async () => {
  const env = createBrowserEnv();
  env.load('js/app/utils.js');
  env.load('js/app/stores/settings-ui-schema-store.js');
  env.APP.preloaded = {
    settingsUiSchema: {
      settings: {
        vehicle: {
          fields: [
            { path: 'vehicle.starter_crank_time_sec', showIf: { eq: { path: 'vehicle.start_method', value: 'timed' } } },
            { path: 'vehicle.start_input_id', showIf: { in: { path: 'vehicle.start_method', values: ['rpm', 'input'] } } },
          ],
        },
      },
    },
  };
  env.APP.stores.registerSettingsUiSchemaStore(env.Alpine);
  await env.Alpine.store('settingsUiSchema').load();
  const fields = env.Alpine.store('settingsUiSchema').settings.vehicle.fields;
  const timed = { settings: { vehicle: { start_method: 'timed' } } };
  const rpm = { settings: { vehicle: { start_method: 'rpm' } } };
  assert.equal(fields[0].showIf(timed), true);
  assert.equal(fields[0].showIf(rpm), false);
  assert.equal(fields[1].showIf(rpm), true);
  assert.equal(fields[1].showIf(timed), false);
});

test('save payload keeps a value whose showIf would hide the control', () => {
  const env = createBrowserEnv();
  env.load('js/app/utils.js');
  env.load('js/app/stores/settings-ui-schema-store.js');
  env.APP.preloaded = {
    settingsUiSchema: {
      settings: {
        vehicle: {
          fields: [
            {
              path: 'vehicle.starter_crank_time_sec',
              showIf: { eq: { path: 'vehicle.start_method', value: 'timed' } },
            },
          ],
        },
      },
    },
  };
  env.APP.stores.registerSettingsUiSchemaStore(env.Alpine);
  env.Alpine.store('settingsUiSchema').load();
  env.load('js/app/stores/settings-store.js');
  env.APP.stores.registerSettingsStore(env.Alpine);
  const settings = env.Alpine.store('settings');
  settings.vehicle.starter_crank_time_sec = 2.5;
  settings.vehicle.start_method = 'rpm';
  const saved = settings.getConfigForSave();
  assert.equal(saved.vehicle.starter_crank_time_sec, 2.5);
});

test('uptime going backwards clears the OTA flag and opens the panel', () => {
  const env = createBrowserEnv();
  env.load('js/app/utils.js');
  env.load('js/app/ui-maps.js');
  env.load('js/app/stores/device-status-store.js');
  env.APP.stores.registerDeviceStatusStore(env.Alpine);
  const ota = { cleared: false, clearAfterReboot() { this.cleared = true; } };
  const ui = { tab: '', setActiveTab(tab) { this.tab = tab; } };
  const toast = { flashed: null, flash(...args) { this.flashed = args; } };
  env.Alpine.store('deviceOta', ota);
  env.Alpine.store('uiState', ui);
  env.Alpine.store('uiToast', toast);
  env.APP.utils.LS.set('lastUptime', '100');
  env.APP.utils.LS.set('otaInProgress', '1');
  env.APP.utils.LS.set('otaPostRebootGoPanel', '1');
  const status = env.Alpine.store('deviceStatus');
  status.patchFromSse('clocks', { uptime: 200 });
  assert.equal(env.APP.utils.LS.get('otaInProgress'), '1');
  status.patchFromSse('clocks', { uptime: 3 });
  assert.equal(env.APP.utils.LS.get('otaInProgress'), null);
  assert.equal(ota.cleared, true);
  assert.equal(ui.tab, 'panel');
  assert.equal(toast.flashed[0], 'Обновление завершено');
  status._stopClockExtrapolation();
});

test('apiJson sends the UI header and parses JSON', async () => {
  const env = createBrowserEnv();
  env.load('js/app/utils.js');
  env.load('js/app/api.js');
  let seen = null;
  env.window.fetch = async (url, opts) => {
    seen = { url, opts };
    return {
      ok: true,
      status: 200,
      headers: { get: (name) => (name === 'content-type' ? 'application/json' : '') },
      json: async () => ({ value: 7 }),
      text: async () => '',
    };
  };
  const res = await env.APP.api.apiJson('/config/get');
  assert.equal(res.ok, true);
  assert.equal(res.data.value, 7);
  assert.equal(seen.opts.headers['X-Requested-With'], 'ElLineUI');
  assert.equal(env.APP.api.parseErrorMessage({ status: 409, data: {} }), 'Устройство занято. Повторите позже.');
});

test('ui maps name kernel and gsm states', () => {
  const env = createBrowserEnv();
  env.load('js/app/ui-maps.js');
  assert.equal(env.APP.uiMaps.badges.statusMode('normal').text, 'Норма');
  assert.equal(env.APP.uiMaps.badges.statusMode('normal').class, 'tone-success');
  assert.equal(env.APP.uiMaps.badges.gsmState('READY').text, 'GSM: готов');
  assert.equal(env.APP.uiMaps.badges.gsmState('').text, 'GSM: —');
});

test('dom ids used by the UI exist in index.html', () => {
  const env = createBrowserEnv();
  env.load('js/app/dom-ids.js');
  const html = readProjectFile('data/index.html');
  const ids = env.APP.domIds;
  for (const id of [ids.panelTab, ids.settings.thermostatCard, ids.settings.batteryCard]) {
    assert.ok(html.includes(id), id);
  }
  for (const prefix of ['settings-sensor-', 'settings-input-', 'settings-in-trigger-', 'settings-temp-trigger-']) {
    assert.ok(html.includes(prefix), prefix);
  }
  assert.equal(ids.settings.sensorItem(0), 'settings-sensor-0');
  assert.equal(ids.settings.inputItem(1), 'settings-input-1');
  assert.equal(ids.settings.inputTriggerItem(2), 'settings-in-trigger-2');
  assert.equal(ids.settings.tempTriggerItem(3), 'settings-temp-trigger-3');
});
