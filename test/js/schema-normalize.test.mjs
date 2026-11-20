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

test('silkIdPattern builds allow-empty and required forms', () => {
  const n = load();
  assert.equal(n.silkIdPattern('IN', 4, true), '^(?:|IN1|IN2|IN3|IN4)$');
  assert.equal(n.silkIdPattern('K', 2, false), '^(?:K1|K2)$');
});

test('applyHardwareLimits rewrites silk patterns from hwCounts', () => {
  const n = load();
  const schema = {
    properties: {
      steps: {
        items: {
          properties: {
            relay_id: { type: 'string', pattern: '^(?:|K[1-5])$' },
            input_id: { type: 'string', pattern: '^(?:|IN[1-3])$' },
            sensor_id: { type: 'string', pattern: '^T[1-3]$' }
          }
        }
      }
    }
  };
  const out = plain(n.applyHardwareLimits(schema, { relays: 6, inputs: 4, sensors: 2 }));
  const props = out.properties.steps.items.properties;
  assert.equal(props.relay_id.pattern, '^(?:|K1|K2|K3|K4|K5|K6)$');
  assert.equal(props.input_id.pattern, '^(?:|IN1|IN2|IN3|IN4)$');
  assert.equal(props.sensor_id.pattern, '^(?:T1|T2)$');
  assert.equal(schema.properties.steps.items.properties.input_id.pattern, '^(?:|IN[1-3])$', 'input schema must not be mutated');
});
