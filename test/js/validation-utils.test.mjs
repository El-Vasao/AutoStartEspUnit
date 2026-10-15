import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createBrowserEnv } from './helpers/browser-env.mjs';

function load() {
  const env = createBrowserEnv();
  env.load('js/app/validation-utils.js');
  return env.APP.validation;
}

test('pointerToDotted converts JSON pointers', () => {
  const v = load();
  assert.equal(v.pointerToDotted('/vehicle/starter_max_time_sec'), 'vehicle.starter_max_time_sec');
  assert.equal(v.pointerToDotted('/inputs/2/pulse/threshold_rpm'), 'inputs.2.pulse.threshold_rpm');
  assert.equal(v.pointerToDotted('/a~1b/c~0d'), 'a/b.c~d');
  assert.equal(v.pointerToDotted(''), '');
  assert.equal(v.pointerToDotted(undefined), '');
});

test('errorToPath points at the offending property', () => {
  const v = load();
  assert.equal(v.errorToPath({ instancePath: '/vehicle', keyword: 'additionalProperties', params: { additionalProperty: 'x' } }), 'vehicle.x');
  assert.equal(v.errorToPath({ instancePath: '', keyword: 'required', params: { missingProperty: 'wifi' } }), 'wifi');
  assert.equal(v.errorToPath({ instancePath: '/mqtt/port', keyword: 'maximum', params: { limit: 65535 } }), 'mqtt.port');
  assert.equal(v.errorToPath(null), '');
});

test('errorToMessage gives Russian texts for common keywords', () => {
  const v = load();
  assert.equal(v.errorToMessage({ keyword: 'minimum', params: { limit: 1 } }), 'Не меньше 1');
  assert.equal(v.errorToMessage({ keyword: 'maximum', params: { limit: 30 } }), 'Не больше 30');
  assert.equal(v.errorToMessage({ keyword: 'maxLength', params: { limit: 31 } }), 'Не длиннее 31');
  assert.equal(v.errorToMessage({ keyword: 'type' }), 'Неверное значение');
  assert.equal(v.errorToMessage({ keyword: 'required' }), 'Обязательно');
  assert.equal(v.errorToMessage({ keyword: 'pattern', instancePath: '/steps/0/message' }), 'Только латиница (ASCII)');
  assert.equal(v.errorToMessage({ keyword: 'pattern', instancePath: '/gsm/owner_phone' }), 'Неверный формат');
  assert.equal(v.errorToMessage({ keyword: 'whatever', message: 'raw' }), 'raw');
});
