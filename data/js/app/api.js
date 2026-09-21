// API helpers (no Alpine dependency at load time)
(function () {
  const APP = (window.APP = window.APP || {});
  const api = (APP.api = APP.api || {});
  const { sleep, LS } = APP.utils || {};
  const endpoints = APP.contract?.api?.endpoints || {};
  const timeoutsMs = APP.contract?.api?.timeoutsMs || {};

  // SoftAP ESP32-C3: allow a small number of concurrent HTTP (ESP8266 used 1).
  const maxConcurrent = Math.max(1, Number(timeoutsMs.deviceFetchMaxConcurrent) || 2);
  let deviceFetchInFlight = 0;
  const deviceFetchWaiters = [];

  function acquireDeviceFetchSlot() {
    return new Promise((resolve) => {
      const tryAcquire = () => {
        if (deviceFetchInFlight < maxConcurrent) {
          deviceFetchInFlight += 1;
          resolve();
          return;
        }
        deviceFetchWaiters.push(tryAcquire);
      };
      tryAcquire();
    });
  }

  function releaseDeviceFetchSlot() {
    deviceFetchInFlight = Math.max(0, deviceFetchInFlight - 1);
    const next = deviceFetchWaiters.shift();
    if (next) next();
  }

  /**
   * SoftAP HTTP with bounded concurrency. Optional gap after each request
   * (usually 0 on ESP32-C3).
   */
  api.deviceFetch = function deviceFetch(url, options = {}) {
    const gapMs = options.deviceGapMs ?? timeoutsMs.deviceRequestGapMs ?? 0;
    const { deviceGapMs: _gapIgnored, ...fetchOpts } = options;
    return (async () => {
      await acquireDeviceFetchSlot();
      try {
        const headers = Object.assign(
          {
            'X-Requested-With': 'ElLineUI',
            Connection: 'close'
          },
          fetchOpts.headers || {}
        );
        return await fetch(url, { cache: 'no-store', ...fetchOpts, headers });
      } finally {
        releaseDeviceFetchSlot();
        if (gapMs > 0 && typeof sleep === 'function') {
          try { await sleep(gapMs); } catch (e) {}
        }
      }
    })();
  };

  api.deviceFetchInFlight = function () { return deviceFetchInFlight; };

  function safeGetHeader(res, name) {
    try { return res?.headers?.get?.(name) || ''; } catch (e) { return ''; }
  }

  async function readResponseBody(res) {
    const ct = String(safeGetHeader(res, 'content-type') || '').toLowerCase();
    if (!res) return { data: {}, text: '', contentType: ct };
    if (ct.includes('application/json') || ct.includes('+json')) {
      try { return { data: await res.json(), text: '', contentType: ct }; } catch (e) { return { data: {}, text: '', contentType: ct }; }
    }
    try {
      const text = await res.text();
      return { data: {}, text: String(text || ''), contentType: ct };
    } catch (e) {
      return { data: {}, text: '', contentType: ct };
    }
  }

  api.apiJson = async function apiJson(url, options = {}) {
    const timeoutMs = options.timeoutMs ?? timeoutsMs.default ?? 8000;
    const controller = new AbortController();
    const t = setTimeout(() => controller.abort(), timeoutMs);
    const { timeoutMs: _ignored, deviceGapMs, ...rest } = options;
    let res;
    try {
      res = await api.deviceFetch(url, { signal: controller.signal, deviceGapMs, ...rest });
    } finally {
      clearTimeout(t);
    }
    const body = await readResponseBody(res);
    return { ok: res.ok, status: res.status, data: body.data, text: body.text, contentType: body.contentType };
  };

  api.parseErrorMessage = function parseErrorMessage(res) {
    try {
      const status = res?.status || 0;
      const data = res?.data;
      const text = String(res?.text || '');

      if (text) return text;

      if (data && typeof data === 'object') {
        if (typeof data.message === 'string' && data.message) return data.message;
        if (typeof data.error === 'string' && data.error) return data.error;
        if (Array.isArray(data.errors) && data.errors.length) {
          const first = data.errors[0];
          const msg = (first && typeof first === 'object') ? (first.message || first.error || '') : '';
          if (msg) return String(msg);
        }
      }

      if (status === 409) return 'Устройство занято (flash операция). Повторите позже.';
      if (status === 413) return 'Слишком большой запрос.';
      if (status >= 500) return 'Ошибка устройства.';
      if (status) return `Ошибка HTTP ${status}`;
      return 'Ошибка сети';
    } catch (e) {
      return 'Ошибка';
    }
  };

  api.isLowMemoryResponse = function isLowMemoryResponse(/* res */) {
    // ESP32-C3: firmware no longer returns LOW_MEMORY 503 gates; keep stub for callers.
    return false;
  };

  api.apiJsonWithBusyRetry = async function apiJsonWithBusyRetry(url, options = {}, retry = {}) {
    const timeoutMsTotal = retry.timeoutMsTotal ?? 12000;
    let delay = retry.initialDelayMs ?? 200;
    const maxDelayMs = retry.maxDelayMs ?? 2000;
    const start = Date.now();
    let last = null;
    while (Date.now() - start < timeoutMsTotal) {
      last = await api.apiJson(url, options);
      if (last.ok) return last;
      if (api.handleBusy409?.(last.status)) {
        await sleep(delay);
        delay = Math.min(delay * 2, maxDelayMs);
        continue;
      }
      return last;
    }
    return last || { ok: false, status: 0, data: {} };
  };

  api.waitForFlashCommit = async function waitForFlashCommit({ startLastMillis, expectedLastOp, timeoutMs = (timeoutsMs.flashCommit ?? 12000), Alpine }) {
    const start = Date.now();
    while (Date.now() - start < timeoutMs) {
      const fc = Alpine?.store?.('deviceStatus')?.flashCommit || {};
      const advanced = (fc.lastMillis || 0) > (startLastMillis || 0);
      if (advanced && !fc.pending) {
        if (expectedLastOp && fc.lastOp && fc.lastOp !== expectedLastOp) {
          // keep waiting
        } else {
          return { ok: !!fc.lastOk, lastOp: fc.lastOp };
        }
      }
      await sleep(200);
    }
    return { ok: false, timeout: true };
  };

  api.runFlashWrite = async function runFlashWrite({
    Alpine,
    busyTitle = 'Сохранение',
    busyMessage = 'Идёт запись…',
    request,
    expectedLastOp,
    timeoutMsCommit,
    onHttpOk,
    onCommitOk
  } = {}) {
    if (!Alpine) throw new Error('runFlashWrite requires Alpine');
    if (!request || typeof request !== 'function') throw new Error('runFlashWrite requires request()');

    Alpine.store('uiBusy')?.show?.({ title: busyTitle, message: busyMessage, progress: 5 });
    Alpine.store('uiBusy')?.startSoftProgress?.({
      durationMs: Math.max(4000, Number(timeoutMsCommit) || 8000),
      cap: 92
    });
    try { Alpine.store('uiState')?.setFlashLock?.(true); } catch (e) {}
    const startCommitMs = Alpine.store('deviceStatus')?.flashCommit?.lastMillis || 0;

    try {
      const { ok, status, data } = await request();
      if (api.handleBusy409?.(status)) return { ok: false, busy409: true, status, data };

      try { Alpine.store('uiBusy')?.setProgress?.(35); } catch (e) {}

      if (ok) {
        try { await onHttpOk?.({ ok, status, data }); } catch (e) {}
      }

      const successFlag = (data && typeof data === 'object' && Object.prototype.hasOwnProperty.call(data, 'success')) ? !!data.success : ok;
      if (!ok || !successFlag) return { ok: false, status, data };

      try { Alpine.store('uiBusy')?.setProgress?.(55); } catch (e) {}

      const res = await api.waitForFlashCommit({
        startLastMillis: startCommitMs,
        expectedLastOp,
        timeoutMs: timeoutMsCommit,
        Alpine
      });

      if (res.timeout) {
        try { Alpine.store('uiNotification')?.warning?.('Нет подтверждения записи во flash. Проверьте соединение.'); } catch (e) {}
        return { ok: false, timeout: true, status, data };
      }
      if (!res.ok) {
        try { Alpine.store('uiNotification')?.error?.('Не удалось записать во flash.'); } catch (e) {}
        return { ok: false, commitOk: false, status, data };
      }

      try { Alpine.store('uiBusy')?.setProgress?.(100); } catch (e) {}
      try { await onCommitOk?.({ status, data, commit: res }); } catch (e) {}
      return { ok: true, status, data, commit: res };
    } catch (e) {
      try { Alpine.store('uiNotification')?.error?.('Ошибка сети'); } catch (e2) {}
      return { ok: false, error: e };
    } finally {
      try { Alpine.store('uiState')?.setFlashLock?.(false); } catch (e) {}
      if (!Alpine.store('uiBusy')?.okMode) Alpine.store('uiBusy')?.hide?.();
    }
  };

  api.handleBusy409 = function handleBusy409(status) {
    return status === 409;
  };

  api.isOtaInProgress = function isOtaInProgress() {
    return LS?.get?.('otaInProgress') === '1';
  };

  api.endpoints = endpoints;
})();
