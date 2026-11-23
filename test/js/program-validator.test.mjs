import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readdirSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { createBrowserEnv, projectRoot, readDataJson, readProjectFile } from './helpers/browser-env.mjs';

function createProgramValidator(hwCounts = { relays: 5, inputs: 3, sensors: 3 }, starterRelayId = 'K5') {
  const env = createBrowserEnv();
  env.run(readProjectFile('data/js/ajv2020.min.js'), 'ajv2020.min.js');
  env.load('js/app/ns.js');
  env.load('js/app/validation-utils.js');
  env.load('js/app/schema-normalize.js');
  env.load('js/app/stores/program-validator-store.js');
  env.APP.preloaded.programValidationSchema = readDataJson('validation/program.schema.json');
  env.APP.preloaded.programIndexValidationSchema = readDataJson('validation/programs.index.schema.json');
  env.Alpine.store('deviceStatus', { hwCounts });
  env.Alpine.store('settings', { vehicle: { starter_relay_id: starterRelayId }, input_triggers: [], temperature_triggers: [] });
  env.APP.stores.registerProgramValidatorStore(env.Alpine);
  const pv = env.Alpine.store('programValidator');
  pv.load();
  return pv;
}

test('programValidator compiles and accepts WAIT and RELAY_ON silk ids', () => {
  const pv = createProgramValidator();
  assert.equal(pv.loaded, true, pv._lastInitError || 'validator not loaded');

  const wait = {
    id: 1,
    name: 'pause',
    steps: [{ step: 1, action: 'WAIT', ms: 1000 }]
  };
  const waitRes = pv.validateProgramDataHard(wait);
  assert.equal(waitRes.ok, true, JSON.stringify(waitRes.errorsByPath));

  const relay = {
    id: 2,
    name: 'relay',
    steps: [{ step: 1, action: 'RELAY_ON', relay_id: 'K2' }]
  };
  const relayRes = pv.validateProgramDataHard(relay);
  assert.equal(relayRes.ok, true, JSON.stringify(relayRes.errorsByPath));
});

test('shared program fixtures agree with the schema', () => {
  const pv = createProgramValidator({ relays: 5, inputs: 3, sensors: 3 }, 'K3');
  const dir = path.join(projectRoot, 'test', 'fixtures', 'programs');
  const names = readdirSync(dir).filter((n) => n.endsWith('.json') && n !== 'index.json');
  assert.ok(names.length >= 2);
  for (const name of names) {
    const prog = JSON.parse(readFileSync(path.join(dir, name), 'utf8'));
    const res = pv.validateProgramDataHard(prog);
    if (name === 'unknown-action.json') {
      assert.equal(res.ok, false, name);
    } else {
      assert.equal(res.ok, true, `${name} ${JSON.stringify(res.errorsByPath)}`);
    }
  }
});

test('program index accepts the factory set and rejects past MAX_PROGRAMS', () => {
  const pv = createProgramValidator();
  const factory = [];
  for (let i = 1; i <= 22; i++) factory.push({ id: i, name: `p${i}` });
  const factoryRes = pv.validateProgramIndex(factory);
  assert.equal(factoryRes.ok, true, JSON.stringify(factoryRes.errorsByPath));

  const full = [];
  for (let i = 1; i <= 100; i++) full.push({ id: i, name: 'n' });
  assert.equal(pv.validateProgramIndex(full).ok, true);

  const over = full.concat([{ id: 101, name: 'n' }]);
  assert.equal(pv.validateProgramIndex(over).ok, false);
});

test('programValidator allows IN4 when hwCounts.inputs is 4', () => {
  const pv = createProgramValidator({ relays: 5, inputs: 4, sensors: 3 });
  assert.equal(pv.loaded, true, pv._lastInitError || 'validator not loaded');
  const prog = {
    id: 3,
    name: 'in4',
    steps: [{ step: 1, action: 'INPUT_ENABLE', input_id: 'IN4' }]
  };
  const res = pv.validateProgramDataHard(prog);
  assert.equal(res.ok, true, JSON.stringify(res.errorsByPath));
});
