import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createBrowserEnv } from './helpers/browser-env.mjs';

function loadSse() {
  const env = createBrowserEnv();
  env.window.fetch = async () => ({
    ok: true,
    status: 204,
    headers: { get: () => '' },
    text: async () => '',
    json: async () => ({}),
  });
  env.window.navigator = { sendBeacon: () => true };
  class EventSource {
    constructor(url) {
      this.url = url;
      this.readyState = 0;
    }
    addEventListener() {}
    close() { this.readyState = 2; }
  }
  EventSource.CONNECTING = 0;
  EventSource.OPEN = 1;
  EventSource.CLOSED = 2;
  env.window.EventSource = EventSource;
  env.Alpine.store('deviceStatus', {
    mode: '—',
    relays: [],
    inputs: [],
    hwCounts: {},
    patchFromSse() {},
  });
  env.load('js/app/sse.js');
  env.APP.sse.init(env.Alpine);
  return env;
}

test('sse backoff doubles and caps, and open resets it', async () => {
  const env = loadSse();
  const pending = env.APP.sse.startEvents({ timeoutMs: 1000 });
  const ctx = env.APP.sse._ctx;
  const es = ctx.source;
  assert.equal(ctx.eventsStarted, true);
  assert.equal(es.url, '/events');

  es.onopen();
  assert.equal(ctx.backoffMs, 2000);
  assert.equal(ctx.state, 'connected');

  es.readyState = env.window.EventSource.CLOSED;
  es.onerror();
  assert.ok(ctx.backoffMs >= 4000 && ctx.backoffMs <= 4399, String(ctx.backoffMs));
  assert.equal(ctx.state, 'backoff');
  assert.equal(ctx.source, null);

  ctx.backoffMs = 10000;
  ctx.reconnectTimer = null;
  ctx.source = es;
  es.onerror();
  assert.equal(ctx.backoffMs, 12000);

  for (const w of ctx.panelReadyWaiters.splice(0)) w(false);
  if (ctx.staleTimer) clearInterval(ctx.staleTimer);
  if (ctx.reconnectTimer) clearTimeout(ctx.reconnectTimer);
  if (ctx.heartbeatTimer) clearInterval(ctx.heartbeatTimer);
  ctx.staleTimer = null;
  ctx.reconnectTimer = null;
  ctx.heartbeatTimer = null;
  await pending;
});
