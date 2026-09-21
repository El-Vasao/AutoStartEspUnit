// SSE client: one EventSource, session heartbeat for SoftAP idle timeout, panel-ready gate.
(function () {
  const APP = (window.APP = window.APP || {});
  APP.sse = APP.sse || {};

  APP.sse.init = function initSse(Alpine) {
    const prev = APP.sse._ctx;
    if (prev && prev.running) return;

    const endpoints = APP.api?.endpoints || APP.contract?.api?.endpoints || {};
    const timeouts = APP.api?.timeoutsMs || APP.contract?.api?.timeoutsMs || {};
    const log = APP.utils?.log;

    if (prev && !prev.running) {
      try {
        if (prev.reconnectTimer) clearTimeout(prev.reconnectTimer);
        if (prev.heartbeatTimer) clearInterval(prev.heartbeatTimer);
        if (prev.staleTimer) clearInterval(prev.staleTimer);
        try { prev.source?.close?.(); } catch (e) {}
      } catch (e) {}
    }

    const ctx = (APP.sse._ctx = {
      running: true,
      stopped: false,
      eventsStarted: false,
      source: null,
      reconnectTimer: null,
      heartbeatTimer: null,
      heartbeatInFlight: false,
      backoffMs: 2000,
      state: 'idle', // idle|connecting|connected|backoff|stale
      lastEventAt: 0,
      connectingSince: 0,
      heartbeatMs: 5000,
      staleTimer: null,
      uiSessionId: (function () {
        try {
          const raw = sessionStorage.getItem('uiSessionId');
          const n = raw ? (parseInt(raw, 10) >>> 0) : 0;
          if (n) return n;
        } catch (e) {}
        return ((((Date.now() & 0x7fffffff) ^ ((Math.random() * 0x7fffffff) | 0)) >>> 0) || 1);
      })(),
      panelReadyWaiters: [],
      gotMode: false,
      gotHardware: false,
    });
    APP.uiSessionId = ctx.uiSessionId;
    try { sessionStorage.setItem('uiSessionId', String(ctx.uiSessionId)); } catch (e) {}

    Alpine.store('net', Alpine.store('net') || {
      state: 'disconnected',
      backoffMs: 0,
      lastEventAt: 0,
      staleMs: 0
    });

    function setState(next) {
      ctx.state = next;
      try {
        const net = Alpine.store('net');
        net.state = ctx.state;
        net.backoffMs = ctx.backoffMs;
        net.lastEventAt = ctx.lastEventAt;
        net.staleMs = Math.max(0, Date.now() - (ctx.lastEventAt || 0));
      } catch (e) {}
    }

    function isPanelReady() {
      try {
        const ds = Alpine?.store?.('deviceStatus');
        if (!ds) return false;
        const modeOk = !!(ds.mode && ds.mode !== '—');
        const nRelays = Number(ds.hwCounts?.relays) || 0;
        const nInputs = Number(ds.hwCounts?.inputs) || 0;
        const relaysOk = Array.isArray(ds.relays) && (nRelays === 0 || ds.relays.length === nRelays);
        const inputsOk = Array.isArray(ds.inputs) && (nInputs === 0 || ds.inputs.length === nInputs);
        return modeOk && relaysOk && inputsOk && ctx.gotMode && ctx.gotHardware;
      } catch (e) {
        return false;
      }
    }

    function seedPanelFlagsFromStore() {
      try {
        const ds = Alpine?.store?.('deviceStatus');
        if (!ds) return;
        if (ds.mode && ds.mode !== '—') ctx.gotMode = true;
        const nRelays = Number(ds.hwCounts?.relays) || 0;
        const nInputs = Number(ds.hwCounts?.inputs) || 0;
        if (Array.isArray(ds.relays) && (nRelays === 0 || ds.relays.length === nRelays) &&
            Array.isArray(ds.inputs) && (nInputs === 0 || ds.inputs.length === nInputs)) {
          ctx.gotHardware = true;
        }
      } catch (e) {}
    }

    function notifyPanelReady() {
      if (!isPanelReady()) return;
      if (!ctx.heartbeatTimer) startHeartbeat();
      const waiters = ctx.panelReadyWaiters.splice(0);
      for (const w of waiters) {
        try { w(true); } catch (e) {}
      }
    }

    function resetPanelFlags() {
      ctx.gotMode = false;
      ctx.gotHardware = false;
    }

    function scheduleReconnect() {
      if (ctx.reconnectTimer) return;
      if (!ctx.eventsStarted || !ctx.running) return;
      if (ctx.source) return;
      setState('backoff');
      const waitMs = Math.max(ctx.backoffMs, 1000);
      ctx.reconnectTimer = setTimeout(() => {
        ctx.reconnectTimer = null;
        if (ctx.source) return;
        connectSSE();
      }, waitMs);
      const jitter = Math.floor(Math.random() * 400);
      ctx.backoffMs = Math.min(ctx.backoffMs * 2 + jitter, 12000);
    }

    function buildSessionBody(close) {
      const body = new URLSearchParams();
      body.set('id', String(ctx.uiSessionId));
      if (close) body.set('close', '1');
      return body;
    }

    function postUiSession(close) {
      const url = endpoints.uiSession || '/ui/session';
      const body = buildSessionBody(close);

      if (close && navigator.sendBeacon) {
        try {
          const blob = new Blob([body.toString()], { type: 'application/x-www-form-urlencoded;charset=UTF-8' });
          if (navigator.sendBeacon(url, blob)) return Promise.resolve();
        } catch (e) {}
      }

      if (!close && ctx.heartbeatInFlight) return Promise.resolve();
      if (!close) ctx.heartbeatInFlight = true;
      try {
        const deviceFetch = APP.api?.deviceFetch;
        const doFetch = deviceFetch
          ? deviceFetch(url, {
              method: 'POST',
              keepalive: !!close,
              headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
              body: body.toString()
            })
          : fetch(url, {
              method: 'POST',
              cache: 'no-store',
              keepalive: !!close,
              headers: {
                'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
                'X-Requested-With': 'ElLineUI'
              },
              body: body.toString()
            });
        return doFetch.catch(() => {}).finally(() => {
          if (!close) ctx.heartbeatInFlight = false;
        });
      } catch (e) {
        if (!close) ctx.heartbeatInFlight = false;
        return Promise.resolve();
      }
    }

    /** Best-effort lease for SoftAP idle timeout; SSE does not require it. */
    function touchUiSessionOnce() {
      return postUiSession(false);
    }

    function startHeartbeat() {
      if (ctx.heartbeatTimer) return;
      if (!isPanelReady()) return;
      postUiSession(false);
      ctx.heartbeatTimer = setInterval(() => postUiSession(false), ctx.heartbeatMs);
    }

    function stopHeartbeat() {
      if (ctx.heartbeatTimer) clearInterval(ctx.heartbeatTimer);
      ctx.heartbeatTimer = null;
    }

    APP.sse.configureHeartbeat = function configureHeartbeat(lease) {
      try {
        const next = lease && Number.isFinite(lease.heartbeatMs) ? lease.heartbeatMs : null;
        if (!next || next < 500 || next > 60000) return;
        if (next === ctx.heartbeatMs) return;
        ctx.heartbeatMs = next;
        if (ctx.heartbeatTimer) {
          stopHeartbeat();
          startHeartbeat();
        }
      } catch (e) {}
    };

    function markStatusKind(kind, payload) {
      if (kind === 'mode' || ((kind === 'snapshot' || kind === 'status') && payload?.mode)) {
        ctx.gotMode = true;
      }
      if (kind === 'hardware' || kind === 'snapshot' || kind === 'status') {
        if (kind === 'hardware' || payload?.relaysById || payload?.inputsById || Array.isArray(payload?.relays)) {
          ctx.gotHardware = true;
        }
      }
    }

    function connectSSE() {
      if (!window.EventSource) {
        setState('idle');
        return;
      }
      if (ctx.source) return;

      try {
        ctx.connectingSince = Date.now();
        setState('connecting');
        ctx.source = new EventSource(endpoints.events || '/events');
      } catch (e) {
        ctx.source = null;
        scheduleReconnect();
        return;
      }

      ctx.source.onopen = () => {
        ctx.backoffMs = 2000;
        ctx.connectingSince = 0;
        setState('connected');
      };

      function onStatusPayload(kind, raw) {
        ctx.lastEventAt = Date.now();
        const payload = JSON.parse(raw);
        markStatusKind(kind, payload);
        try {
          Alpine.store('deviceStatus').patchFromSse(kind, payload);
        } catch (e) {}
        setState('connected');
        notifyPanelReady();
      }

      ctx.source.addEventListener('resync', () => {
        ctx.lastEventAt = Date.now();
        resetPanelFlags();
        setState('connected');
      });

      const sseKinds = ['clocks', 'hardware', 'runtime', 'program', 'flash', 'mode', 'gsm', 'error', 'status'];
      for (const k of sseKinds) {
        ctx.source.addEventListener(k, e => {
          try { onStatusPayload(k, e.data); } catch (err) { log?.warn?.('SSE payload parse failed', k, err); }
        });
      }
      ctx.source.addEventListener('log', e => {
        ctx.lastEventAt = Date.now();
        try { Alpine.store('uiLogs').add(e.data); } catch (err) {}
        setState('connected');
      });

      ctx.source.onerror = () => {
        setState('disconnected');
        const es = ctx.source;
        if (!es) {
          scheduleReconnect();
          return;
        }
        // Let the browser finish its own reconnect; only replace after CLOSED or stuck CONNECTING.
        if (es.readyState === EventSource.CONNECTING) {
          if (!ctx.connectingSince) ctx.connectingSince = Date.now();
          if ((Date.now() - ctx.connectingSince) > 20000) {
            try { es.close(); } catch (e) {}
            ctx.source = null;
            ctx.connectingSince = 0;
            resetPanelFlags();
            scheduleReconnect();
          }
          return;
        }
        if (es.readyState === EventSource.CLOSED) {
          ctx.source = null;
          ctx.connectingSince = 0;
          resetPanelFlags();
          scheduleReconnect();
        }
      };
    }

    function waitForPanelReady(timeoutMs) {
      if (isPanelReady()) return Promise.resolve(true);
      return new Promise(resolve => {
        let done = false;
        const finish = (ok) => {
          if (done) return;
          done = true;
          resolve(!!ok);
        };
        const timer = setTimeout(() => finish(false), Math.max(1000, timeoutMs || 12000));
        ctx.panelReadyWaiters.push((ok) => {
          clearTimeout(timer);
          finish(ok);
        });
      });
    }

    function stopAll(ev) {
      if (ctx.stopped) return;
      ctx.stopped = true;
      stopHeartbeat();
      try { if (ctx.source) ctx.source.close(); } catch (e) {}
      ctx.source = null;
      if (ctx.reconnectTimer) clearTimeout(ctx.reconnectTimer);
      ctx.reconnectTimer = null;
      if (ctx.staleTimer) clearInterval(ctx.staleTimer);
      ctx.staleTimer = null;
      ctx.running = false;
      ctx.eventsStarted = false;
      const waiters = ctx.panelReadyWaiters.splice(0);
      for (const w of waiters) {
        try { w(false); } catch (e) {}
      }
      void ev;
    }

    function startStaleDetector() {
      if (ctx.staleTimer) return;
      const maxSilentMs = 20000;
      ctx.staleTimer = setInterval(() => {
        try {
          if (!ctx.eventsStarted) return;
          const silentMs = Date.now() - (ctx.lastEventAt || 0);
          const net = Alpine?.store?.('net');
          if (net) net.staleMs = Math.max(0, silentMs);
          if (ctx.state === 'connected' && ctx.lastEventAt && silentMs > maxSilentMs) {
            setState('stale');
            try { ctx.source?.close?.(); } catch (e) {}
            ctx.source = null;
            resetPanelFlags();
            scheduleReconnect();
          }
        } catch (e) {}
      }, 1000);
    }

    document.addEventListener('visibilitychange', () => {
      try {
        if (!document.hidden && ctx.eventsStarted && !ctx.source && !ctx.reconnectTimer) {
          scheduleReconnect();
        }
      } catch (e) {}
    }, { passive: true });

    window.addEventListener('pagehide', stopAll);

    startStaleDetector();

    APP.sse.isPanelReady = function isPanelReadyExport() {
      return isPanelReady();
    };

    APP.sse.isConnected = function isConnected() {
      return !!ctx.source && ctx.source.readyState === EventSource.OPEN;
    };

    /**
     * Best-effort session touch → EventSource → wait mode+hardware → heartbeat.
     * @param {{ timeoutMs?: number }} [opts]
     * @returns {Promise<boolean>}
     */
    APP.sse.startEvents = async function startEvents(opts) {
      if (!ctx.running) {
        if (document.visibilityState === 'hidden') return false;
        ctx.stopped = false;
        ctx.running = true;
        ctx.eventsStarted = false;
        ctx.source = null;
        stopHeartbeat();
        if (ctx.reconnectTimer) {
          clearTimeout(ctx.reconnectTimer);
          ctx.reconnectTimer = null;
        }
        try {
          const raw = sessionStorage.getItem('uiSessionId');
          const n = raw ? (parseInt(raw, 10) >>> 0) : 0;
          if (n) ctx.uiSessionId = n;
        } catch (e) {}
        APP.uiSessionId = ctx.uiSessionId;
        try { sessionStorage.setItem('uiSessionId', String(ctx.uiSessionId)); } catch (e) {}
        resetPanelFlags();
      }
      seedPanelFlagsFromStore();
      if (ctx.eventsStarted && isPanelReady()) {
        startHeartbeat();
        return true;
      }
      ctx.eventsStarted = true;
      void touchUiSessionOnce();
      connectSSE();
      notifyPanelReady();
      const panelTimeout = Number(opts?.timeoutMs)
        || Number(timeouts.sseInitDeadlineMs)
        || Number(timeouts.ssePanelReadyTimeoutMs)
        || 12000;
      const ready = await waitForPanelReady(panelTimeout);
      if (ready) startHeartbeat();
      return ready;
    };
  };
})();
