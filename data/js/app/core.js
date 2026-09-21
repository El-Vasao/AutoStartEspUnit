// SoftAP UI init: HTTP checklist, then wait SSE panel-ready; hard safety unlock if hung.
(function () {
  document.addEventListener('alpine:init', () => {
    try {
      window.APP?.stores?.init?.(window.Alpine);
      window.APP?.gestures?.init?.(window.Alpine);

      (async () => {
        const Alpine = window.Alpine;
        const log = window.APP?.utils?.log;
        const timeouts = window.APP?.contract?.api?.timeoutsMs || {};
        const phaseGapMs = Number(timeouts.initPhaseGapMs) || 400;
        const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
        // Longer than checklist + sseInitDeadlineMs so normal SSE-wait UX is not cut short.
        const SAFETY_UNLOCK_MS = 20000;

        let finished = false;
        let safetyTimer = null;
        const ui = () => {
          try { return Alpine?.store?.('uiState'); } catch (e) { return null; }
        };
        const setPhase = (p) => {
          try { ui()?.setInitPhase?.(p); } catch (e) {}
        };
        const setProgress = (pct, label) => {
          try { ui()?.setInitProgress?.(pct, label); } catch (e) {}
        };
        const pushReason = (code, detail) => {
          try { ui()?.pushInitReason?.(code, detail); } catch (e) {}
        };
        const clearSafety = () => {
          if (safetyTimer) {
            clearTimeout(safetyTimer);
            safetyTimer = null;
          }
        };
        const unlock = (opts) => {
          if (finished) return;
          finished = true;
          clearSafety();
          try { ui()?.unlock?.(opts); } catch (e) {}
        };
        const failInit = (message, phase = 'degraded') => {
          if (finished) return;
          finished = true;
          clearSafety();
          try { ui()?.failInit?.(message, phase); } catch (e) {}
        };
        const hasValidBootstrap = () => {
          try {
            const st = Alpine.store('deviceStatus');
            return !!(st && st.hwMap && typeof st.hwMap === 'object' && st.hwCounts && st.hwCounts.locked);
          } catch (e) {
            return false;
          }
        };

        const withTimeout = async (p, ms = 6500) => {
          let t = null;
          try {
            return await Promise.race([
              Promise.resolve(p),
              new Promise((resolve) => { t = setTimeout(() => resolve({ __timeout: true }), ms); })
            ]);
          } catch (e) {
            return { __error: e };
          } finally {
            if (t) clearTimeout(t);
          }
        };
        const phaseGap = async () => { await sleep(phaseGapMs); };

        function startSseBackgroundOnly(bootstrapUiLease) {
          // Used when UI already unlocked/failed — still need EventSource for live updates.
          try { window.APP?.sse?.init?.(Alpine); } catch (e) { pushReason('sse_init_failed', e?.message || e); }
          try { if (bootstrapUiLease) window.APP?.sse?.configureHeartbeat?.(bootstrapUiLease); } catch (e) {}
          const sseDelayMs = Number(timeouts.sseStartDelayMs) || 400;
          const deadlineMs = Number(timeouts.sseInitDeadlineMs) || 12000;
          (async () => {
            try {
              await sleep(sseDelayMs);
              if (typeof window.APP?.sse?.startEvents === 'function') {
                const ok = await window.APP.sse.startEvents({ timeoutMs: deadlineMs });
                if (!ok) pushReason('sse_panel_timeout', 'mode/hardware not received (background)');
              }
            } catch (e) {
              pushReason('sse_start_failed', e?.message || e);
            }
          })().catch((e) => pushReason('sse_bg_exception', e?.message || e));
        }

        try { ui()?.beginInit?.(); } catch (e) {}

        // Hard safety if HTTP/SSE path hangs. Longer than normal checklist+SSE wait.
        safetyTimer = setTimeout(() => {
          if (finished) return;
          pushReason('init_safety_unlock', `forced after ${SAFETY_UNLOCK_MS}ms`);
          unlock({ degraded: true });
        }, SAFETY_UNLOCK_MS);

        let bootstrapUiLease = null;

        // --- bootstrap (inventory + live snapshot) ---
        setPhase('bootstrap');
        setProgress(8, 'Bootstrap…');
        try {
          const apiJson = window.APP?.api?.apiJson;
          const bootstrapUrl = window.APP?.api?.endpoints?.bootstrap || window.APP?.contract?.api?.endpoints?.bootstrap || '/bootstrap';
          if (!apiJson) throw new Error('apiJson unavailable');
          const bootstrapState = (window.APP.__bootstrapState = window.APP.__bootstrapState || { inFlight: null });
          const runBootstrapRequest = () => {
            if (bootstrapState.inFlight) return bootstrapState.inFlight;
            bootstrapState.inFlight = withTimeout(apiJson(bootstrapUrl, { timeoutMs: 4000 }), 6500)
              .finally(() => { bootstrapState.inFlight = null; });
            return bootstrapState.inFlight;
          };
          let ok = false;
          for (let i = 0; i < 6; i++) {
            if (finished) break;
            setProgress(10 + i * 2, `Bootstrap (попытка ${i + 1})…`);
            const res = await runBootstrapRequest();
            if (res?.__timeout) pushReason('bootstrap_timeout', `attempt=${i + 1}`);
            if (res?.__error) pushReason('bootstrap_error', res.__error?.message || res.__error);
            if (res?.ok) {
              ok = true;
              Alpine.store('deviceStatus')?.applyBootstrap?.(res.data);
              bootstrapUiLease = res.data?.uiLease || null;
              break;
            }
            await sleep(400 + i * 350);
          }
          if (finished) {
            startSseBackgroundOnly(bootstrapUiLease);
            return;
          }
          if (!ok || !hasValidBootstrap()) {
            pushReason('bootstrap_unavailable', 'GET /bootstrap failed');
            failInit('Не удалось загрузить bootstrap (/bootstrap). UI заблокирован.');
          } else {
            setProgress(28, 'Bootstrap готов');
          }
        } catch (e) {
          pushReason('bootstrap_exception', e?.message || e);
          failInit('Не удалось загрузить bootstrap. UI заблокирован.');
        }

        if (Alpine.store('uiState')?.initFailed || finished) {
          startSseBackgroundOnly(bootstrapUiLease);
          return;
        }

        await phaseGap();
        if (finished) { startSseBackgroundOnly(bootstrapUiLease); return; }
        setPhase('schemas');
        setProgress(35, 'Схема настроек…');
        try {
          for (let i = 0; i < 3; i++) {
            if (finished) break;
            await withTimeout(Alpine.store('settingsUiSchema')?.load?.(), 6500);
            if (Alpine.store('settingsUiSchema')?.loaded) break;
            await sleep(200 + i * 250);
          }
          if (!Alpine.store('settingsUiSchema')?.loaded) pushReason('settings_schema_missing', 'settingsUiSchema not loaded');
        } catch (e) { pushReason('settings_schema_exception', e?.message || e); }

        await phaseGap();
        if (finished) { startSseBackgroundOnly(bootstrapUiLease); return; }
        setProgress(42, 'Валидатор настроек…');
        try {
          for (let i = 0; i < 2; i++) {
            if (finished) break;
            await withTimeout(Alpine.store('settingsValidator')?.load?.(), 6500);
            if (Alpine.store('settingsValidator')?.loaded) break;
            await sleep(150 + i * 200);
          }
        } catch (e) { pushReason('settings_validator_exception', e?.message || e); }

        await phaseGap();
        if (finished) { startSseBackgroundOnly(bootstrapUiLease); return; }
        setPhase('data');
        setProgress(52, 'Конфигурация…');
        try {
          for (let i = 0; i < 4; i++) {
            if (finished) break;
            await withTimeout(Alpine.store('settings')?.load?.(true), 8000);
            if (Alpine.store('settings')?.loaded) break;
            await sleep(350 + i * 400);
          }
          if (!Alpine.store('settings')?.loaded) pushReason('settings_missing', 'settings not loaded');
        } catch (e) { pushReason('settings_exception', e?.message || e); }

        await phaseGap();
        if (finished) { startSseBackgroundOnly(bootstrapUiLease); return; }
        setProgress(62, 'Схема программ…');
        try {
          for (let i = 0; i < 3; i++) {
            if (finished) break;
            await withTimeout(Alpine.store('programStepsUiSchema')?.load?.(), 6500);
            if (Alpine.store('programStepsUiSchema')?.loaded) break;
            await sleep(200 + i * 250);
          }
          if (!Alpine.store('programStepsUiSchema')?.loaded) pushReason('program_steps_schema_missing', 'programStepsUiSchema not loaded');
        } catch (e) { pushReason('program_steps_schema_exception', e?.message || e); }

        await phaseGap();
        if (finished) { startSseBackgroundOnly(bootstrapUiLease); return; }
        setProgress(70, 'Список программ…');
        try {
          for (let i = 0; i < 3; i++) {
            if (finished) break;
            await withTimeout(Alpine.store('programs')?.loadList?.(true), 8000);
            if (Alpine.store('programs')?.loaded) break;
            await sleep(300 + i * 350);
          }
        } catch (e) { pushReason('programs_list_exception', e?.message || e); }

        if (finished) {
          startSseBackgroundOnly(bootstrapUiLease);
          return;
        }

        let initOk = false;
        {
          const schemaOk = !!Alpine.store('settingsUiSchema')?.loaded;
          const settingsOk = !!Alpine.store('settings')?.loaded;
          const progSchemaOk = !!Alpine.store('programStepsUiSchema')?.loaded;
          if (!schemaOk || !settingsOk || !progSchemaOk) {
            const msg = (!schemaOk && !settingsOk && !progSchemaOk)
              ? 'Не удалось загрузить схему, конфигурацию и схему программ.'
              : (!schemaOk ? 'Не удалось загрузить схему настроек.'
                    : (!settingsOk ? 'Не удалось загрузить конфигурацию.' : 'Не удалось загрузить схему редактора программ.'));
            failInit(msg);
          } else {
            setPhase('checklist');
            setProgress(78, 'Чеклист готов');
            initOk = true;
          }
        }

        try {
          const s = Alpine.store('uiState');
          if (s?.initReasons?.length) log?.warn?.('[init] reasons:', s.initReasons);
        } catch (e) {}

        if (!initOk || Alpine.store('uiState')?.initFailed || finished) {
          startSseBackgroundOnly(bootstrapUiLease);
          return;
        }

        // --- SSE: keep lock until panel-ready (or deadline / safety) ---
        await phaseGap();
        if (finished) { startSseBackgroundOnly(bootstrapUiLease); return; }
        setPhase('sse');
        setProgress(82, 'Подключение SSE…');
        try { window.APP?.sse?.init?.(Alpine); } catch (e) { pushReason('sse_init_failed', e?.message || e); }
        try { if (bootstrapUiLease) window.APP?.sse?.configureHeartbeat?.(bootstrapUiLease); } catch (e) {}

        const sseDelayMs = Number(timeouts.sseStartDelayMs) || 400;
        const deadlineMs = Number(timeouts.sseInitDeadlineMs) || 12000;

        await sleep(sseDelayMs);
        if (finished) return;

        setProgress(86, 'Ждём статус устройства…');
        let panelReady = false;
        try {
          if (typeof window.APP?.sse?.startEvents !== 'function') {
            pushReason('sse_start_failed', 'startEvents missing');
          } else {
            panelReady = !!(await window.APP.sse.startEvents({ timeoutMs: deadlineMs }));
            if (!panelReady) pushReason('sse_panel_timeout', 'mode/hardware not received within deadline');
          }
        } catch (e) {
          pushReason('sse_start_failed', e?.message || e);
        }

        if (finished) return;

        if (!panelReady) {
          pushReason('sse_panel_timeout_unlock', 'unlocking without panel-ready');
          unlock({ degraded: true });
        } else {
          setProgress(100, 'Готово');
          unlock();
        }
      })();
    } catch (e) {
      window.APP?.utils?.log?.error?.('bootstrap failed', e);
    }
  });
})();
