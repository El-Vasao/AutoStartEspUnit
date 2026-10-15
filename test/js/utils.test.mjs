import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createBrowserEnv, plain } from './helpers/browser-env.mjs';

function loadUtils() {
  const env = createBrowserEnv();
  env.load('js/app/ns.js');
  env.load('js/app/utils.js');
  return env.APP.utils;
}

test('numberToPlainString expands exponents', () => {
  const u = loadUtils();
  assert.equal(u.numberToPlainString(1e21), '1000000000000000000000');
  assert.equal(u.numberToPlainString(1e-7), '0.0000001');
  assert.equal(u.numberToPlainString(-2.5e-3), '-0.0025');
  assert.equal(u.numberToPlainString('12.50'), '12.50');
  assert.equal(u.numberToPlainString(''), '');
  assert.equal(u.numberToPlainString(null), '');
  assert.equal(u.numberToPlainString(NaN), '');
});

test('stableStringify sorts keys recursively', () => {
  const u = loadUtils();
  assert.equal(u.stableStringify({ b: 1, a: { d: 2, c: [3, { f: 4, e: 5 }] } }), '{"a":{"c":[3,{"e":5,"f":4}],"d":2},"b":1}');
});

test('fastEqual compares by value', () => {
  const u = loadUtils();
  assert.equal(u.fastEqual({ a: 1, b: [1, 2] }, { b: [1, 2], a: 1 }), true);
  assert.equal(u.fastEqual({ a: 1 }, { a: 2 }), false);
  assert.equal(u.fastEqual(1, 1), true);
  assert.equal(u.fastEqual(1, '1'), false);
});

test('collectLeafDiffPaths reports changed leaves only', () => {
  const u = loadUtils();
  const out = {};
  u.collectLeafDiffPaths(
    { vehicle: { a: 1, b: 2 }, inputs: [{ id: 1 }, { id: 2 }] },
    { vehicle: { a: 1, b: 3 }, inputs: [{ id: 1 }, { id: 5 }, { id: 6 }] },
    [],
    out,
  );
  assert.deepEqual(Object.keys(plain(out)).sort(), ['inputs.1.id', 'inputs.2', 'vehicle.b']);
});

test('deepClone produces an independent copy', () => {
  const u = loadUtils();
  const src = { a: [1, { b: 2 }] };
  const copy = u.deepClone(src);
  copy.a[1].b = 3;
  assert.equal(src.a[1].b, 2);
});
