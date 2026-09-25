// ui state store (tabs/subtabs + init/flash lock lifecycle)
(function () {
  const APP = (window.APP = (window.APP || {}));
  APP.stores = APP.stores || {};

  function syncLocked(store) {
    store.locked = !!(store.initLock || store.flashLock);
  }

  function setBootingClass(on) {
    try {
      document.documentElement.classList.toggle('ui-booting', !!on);
    } catch (e) {}
  }

  function forceClearUiLockedDom() {
    try {
      document.querySelectorAll('.ui-locked').forEach((el) => {
        try { el.classList.remove('ui-locked'); } catch (e) {}
      });
    } catch (e) {}
  }

  function dismissBootSplashDom() {
    try {
      const el = document.getElementById('boot-splash');
      if (el && el.parentNode) el.parentNode.removeChild(el);
    } catch (e) {}
  }

  function ensureBootSplashDom() {
    try {
      if (document.getElementById('boot-splash')) return;
      const wrap = document.createElement('div');
      wrap.id = 'boot-splash';
      wrap.setAttribute('role', 'status');
      wrap.setAttribute('aria-live', 'polite');
      wrap.setAttribute('aria-busy', 'true');
      wrap.innerHTML =
        '<div class="boot-card">' +
        '<div class="boot-title">Загрузка…</div>' +
        '<div class="boot-sub" data-boot-label>Инициализация…</div>' +
        '<div class="boot-pct" data-boot-pct>0%</div>' +
        '<div class="boot-bar"><div class="boot-fill" data-boot-fill style="width:0%"></div></div>' +
        '</div>';
      const body = document.body;
      if (body) body.insertBefore(wrap, body.firstChild);
    } catch (e) {}
  }

  function paintBootSplash(pct, label) {
    try {
      ensureBootSplashDom();
      const root = document.getElementById('boot-splash');
      if (!root) return;
      const bar = root.querySelector('[data-boot-fill]');
      const text = root.querySelector('[data-boot-pct]');
      const sub = root.querySelector('[data-boot-label]');
      const n = Number(pct);
      const p = Number.isFinite(n) ? Math.max(0, Math.min(100, Math.round(n))) : 0;
      if (bar) bar.style.width = p + '%';
      if (text) text.textContent = p + '%';
      if (sub && label != null && String(label)) sub.textContent = String(label);
    } catch (e) {}
  }

  APP.stores.registerUiStateStore = function registerUiStateStore(Alpine) {
    // Drop legacy persisted tab keys so refresh always lands on panel / default sub-tab.
    try {
      localStorage.removeItem('ui.activeTab');
      localStorage.removeItem('ui.activeSubTab');
    } catch (e) {}

    Alpine.store('uiState', {
      // initLock: SoftAP boot checklist; flashLock: flash commit / busy write
      initLock: true,
      flashLock: false,
      locked: true,
      initFailed: false,
      initError: '',
      // Bootstrap lifecycle (for reliability + diagnostics)
      // starting -> bootstrap -> schemas -> data -> ready | degraded
      initPhase: 'starting',
      initProgress: 0,
      initProgressLabel: 'Загрузка…',
      initReasons: [],
      lastBootstrapAt: 0,
      lastReadyAt: 0,
      unsavedHint: false,
      // Main tab + settings sub-tab are never persisted: every load opens «Панель» / wifi.
      activeTab: 'panel',
      activeSubTab: 'wifi',

      dismissBootSplash() {
        dismissBootSplashDom();
        setBootingClass(false);
      },

      beginInit() {
        this.initFailed = false;
        this.initError = '';
        this.initReasons = [];
        this.initLock = true;
        this.flashLock = false;
        syncLocked(this);
        setBootingClass(true);
        ensureBootSplashDom();
        this.setActiveTab('panel');
        this.activeSubTab = 'wifi';
        this.setInitPhase('starting');
        this.setInitProgress(2, 'Старт…');
      },

      /**
       * @param {{ degraded?: boolean }} [opts]
       */
      unlock(opts) {
        const degraded = !!(opts && opts.degraded);
        this.initLock = false;
        syncLocked(this);
        this.setInitProgress(100, degraded ? 'Открыто без полного статуса' : 'Готово');
        this.setInitPhase(degraded ? 'degraded' : 'ready');
        this.dismissBootSplash();
        forceClearUiLockedDom();
      },

      failInit(message, phase) {
        this.initFailed = true;
        this.initError = String(message || 'Ошибка инициализации UI.');
        this.initLock = false;
        syncLocked(this);
        this.setInitPhase(phase || 'degraded');
        this.dismissBootSplash();
        forceClearUiLockedDom();
      },

      setFlashLock(on) {
        this.flashLock = !!on;
        syncLocked(this);
      },

      /**
       * Single entry for main-tab changes (enter-hooks live here).
       * Callers must use this instead of assigning activeTab directly.
       */
      setActiveTab(tab) {
        const next = String(tab || 'panel');
        if (!next) return;
        const prev = this.activeTab;
        this.activeTab = next;
        try {
          const logs = Alpine.store('uiLogs');
          const atLogs = Alpine.store('uiAtLogs');
          if (next === 'system') {
            logs?.setVisible?.(true);
            atLogs?.setVisible?.(true);
          } else if (prev === 'system') {
            logs?.setVisible?.(false);
            atLogs?.setVisible?.(false);
          }
        } catch (e) {}
      },

      goToSettings(target) {
        if (target === 'programs') {
          this.setActiveTab('programs');
        } else {
          this.setActiveTab('settings');
          this.activeSubTab = target;
        }
      },

      setInitPhase(phase) {
        const p = String(phase || '');
        if (!p) return;
        this.initPhase = p;
        if (p === 'bootstrap') this.lastBootstrapAt = Date.now();
        if (p === 'ready') this.lastReadyAt = Date.now();
      },

      /**
       * @param {number} pct 0..100
       * @param {string} [label]
       */
      setInitProgress(pct, label) {
        const n = Number(pct);
        this.initProgress = Number.isFinite(n) ? Math.max(0, Math.min(100, Math.round(n))) : 0;
        if (label != null && String(label)) this.initProgressLabel = String(label);
        if (this.initLock) {
          setBootingClass(true);
          paintBootSplash(this.initProgress, this.initProgressLabel);
        }
      },

      pushInitReason(code, detail) {
        const c = String(code || '');
        if (!c) return;
        const d = (detail == null) ? '' : String(detail);
        const msg = d ? `${c}: ${d}` : c;
        if (!Array.isArray(this.initReasons)) this.initReasons = [];
        // Keep it short to avoid LS/memory bloat
        if (!this.initReasons.includes(msg)) this.initReasons.push(msg);
        if (this.initReasons.length > 12) this.initReasons = this.initReasons.slice(-12);
      }
    });
  };
})();
