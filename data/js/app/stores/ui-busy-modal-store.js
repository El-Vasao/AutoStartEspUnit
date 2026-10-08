// busy store (blocking modal)
(function () {
  const APP = (window.APP = window.APP || {});
  APP.stores = APP.stores || {};

  APP.stores.registerUiBusyModalStore = function registerUiBusyModalStore(Alpine) {
    Alpine.store('uiBusy', {
      open: false,
      title: 'Сохранение',
      message: 'Запись…',
      okMode: false,
      okLabel: 'OK',
      /** 0..100 while working; null hides the bar (okMode / indeterminate skip). */
      progress: null,
      _progressTimer: null,

      show({ title = 'Сохранение', message = 'Запись…', progress = 0 } = {}) {
        this._stopProgressTimer();
        this.title = String(title || 'Сохранение');
        this.message = String(message || 'Запись…');
        this.okMode = false;
        this.progress = (progress == null) ? 0 : Math.max(0, Math.min(100, Number(progress) || 0));
        this.open = true;
      },
      showOk({ title = 'Готово', message = '', okLabel = 'OK' } = {}) {
        this._stopProgressTimer();
        this.title = String(title || 'Готово');
        this.message = String(message || '');
        this.okLabel = String(okLabel || 'OK');
        this.okMode = true;
        this.progress = 100;
        this.open = true;
      },
      setProgress(pct) {
        if (this.okMode) return;
        const n = Number(pct);
        if (!Number.isFinite(n)) return;
        this.progress = Math.max(0, Math.min(100, Math.round(n)));
      },
      /**
       * Soft percent while waiting for flash commit (no real byte progress).
       * Caps at 92 until hide/showOk.
       */
      startSoftProgress({ durationMs = 8000, cap = 92 } = {}) {
        this._stopProgressTimer();
        const start = Date.now();
        const dur = Math.max(1500, Number(durationMs) || 8000);
        const maxPct = Math.max(50, Math.min(99, Number(cap) || 92));
        this.progress = Math.max(this.progress || 0, 8);
        this._progressTimer = setInterval(() => {
          try {
            if (!this.open || this.okMode) {
              this._stopProgressTimer();
              return;
            }
            const t = Math.min(1, (Date.now() - start) / dur);
            // Ease-out so early movement is visible
            const eased = 1 - Math.pow(1 - t, 1.6);
            this.progress = Math.max(this.progress || 0, Math.round(8 + eased * (maxPct - 8)));
          } catch (e) {}
        }, 200);
      },
      _stopProgressTimer() {
        if (this._progressTimer) {
          clearInterval(this._progressTimer);
          this._progressTimer = null;
        }
      },
      ok() {
        this.hide();
      },
      hide() {
        this._stopProgressTimer();
        this.open = false;
        this.okMode = false;
        this.progress = null;
      }
    });
  };
})();
