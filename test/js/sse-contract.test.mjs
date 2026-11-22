import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readProjectFile } from './helpers/browser-env.mjs';

const kinds = ['clocks', 'hardware', 'runtime', 'program', 'flash', 'mode', 'gsm', 'error', 'resync', 'log'];

test('SSE event names in C++ are handled by sse.js', () => {
  const cpp = [
    readProjectFile('src/web/WebServer.SseIncremental.cpp'),
    readProjectFile('src/web/WebServer.Runtime.cpp'),
  ].join('\n');
  const js = readProjectFile('data/js/app/sse.js');
  for (const name of kinds) {
    assert.match(cpp, new RegExp(`"${name}"`), name);
    assert.match(js, new RegExp(`['"]${name}['"]`), name);
  }
});
