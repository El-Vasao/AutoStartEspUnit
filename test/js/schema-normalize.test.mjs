import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createBrowserEnv, plain } from './helpers/browser-env.mjs';

function load() {
  const env = createBrowserEnv();
  env.load('js/app/schema-normalize.js');
  return env.APP.schemaNormalize;
}

test('inferValueTypeFromField by field type', () => {
  const n = load();
  assert.equal(n.inferValueTypeFromField({ type: 'toggle' }), 'bool');
  assert.equal(n.inferValueTypeFromField({ type: 'number' }), 'float');
  assert.equal(n.inferValueTypeFromField({ type: 'text' }), 'string');
  assert.equal(n.inferValueTypeFromField({ type: 'number', valueType: 'int' }), 'int');
  assert.equal(n.inferValueTypeFromField(null), 'string');
});

test('inferValueTypeFromField for selects', () => {
  const n = load();
  assert.equal(n.inferValueTypeFromField({ type: 'select', options: [{ value: 1 }, { value: 2 }] }), 'int');
  assert.equal(n.inferValueTypeFromField({ type: 'select', options: [{ value: 'a' }] }), 'string');
  assert.equal(n.inferValueTypeFromField({ type: 'select', options: [{ value: true }] }), 'bool');
  for (const src of ['relays', 'programRelays', 'inputs', 'inputIds', 'digitalInputIds', 'pulseInputIds', 'startInputIds', 'sensors', 'sensorIds', 'programs']) {
    assert.equal(n.inferValueTypeFromField({ type: 'select', optionsFrom: src }), 'int', src);
  }
  assert.equal(n.inferValueTypeFromField({ type: 'select', optionsFrom: 'sensorRoms' }), 'string');
});

test('applyHardwareLimits patches array sizes from hwCounts', () => {
  const n = load();
  const schema = { properties: { sensors: { type: 'array', maxItems: 3 }, inputs: { type: 'array', maxItems: 3 } } };
  const out = plain(n.applyHardwareLimits(schema, { relays: 5, inputs: 2, sensors: 1 }));
  assert.equal(out.properties.inputs.maxItems, 2);
  assert.equal(out.properties.sensors.maxItems, 1);
  assert.equal(schema.properties.inputs.maxItems, 3, 'input schema must not be mutated');
});

test('applyHardwareLimits keeps schema maxItems when hwCounts missing', () => {
  const n = load();
  const schema = { properties: { sensors: { type: 'array', maxItems: 3 }, inputs: { type: 'array', maxItems: 3 } } };
  const out = plain(n.applyHardwareLimits(schema, {}));
  assert.equal(out.properties.inputs.maxItems, 3);
  assert.equal(out.properties.sensors.maxItems, 3);
});
