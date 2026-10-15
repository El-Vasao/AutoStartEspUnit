import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createBrowserEnv } from './helpers/browser-env.mjs';

function loadCoerce() {
  const env = createBrowserEnv();
  env.load('js/app/coerce.js');
  return env.APP.coerce;
}

test('byValueType without type returns raw value', () => {
  const c = loadCoerce();
  assert.equal(c.byValueType('', '12'), '12');
});

test('string coercion', () => {
  const c = loadCoerce();
  assert.equal(c.byValueType('string', 12), '12');
  assert.equal(c.byValueType('string', null), '');
  assert.equal(c.byValueType('string', undefined), '');
});

test('bool coercion accepts common tokens', () => {
  const c = loadCoerce();
  for (const v of [true, 'true', '1', 'on', 'yes', 'TRUE']) assert.equal(c.byValueType('bool', v), true, String(v));
  for (const v of [false, 'false', '0', 'off', 'no', '']) assert.equal(c.byValueType('bool', v), false, String(v));
});

test('int coercion truncates and keeps empty input empty', () => {
  const c = loadCoerce();
  assert.equal(c.byValueType('int', 3.9), 3);
  assert.equal(c.byValueType('int', '-7.5'), -7);
  assert.equal(c.byValueType('int', ''), '');
  assert.equal(c.byValueType('int', 'abc', 5), 5);
  assert.equal(c.byValueType('int', 'abc'), 'abc');
});

test('float coercion', () => {
  const c = loadCoerce();
  assert.equal(c.byValueType('float', '12.5'), 12.5);
  assert.equal(c.byValueType('float', ''), '');
  assert.equal(c.byValueType('float', 'x', 1.5), 1.5);
});
