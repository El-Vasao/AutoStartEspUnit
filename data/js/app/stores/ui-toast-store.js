// Single visible notification surface: toast + hardware error banner.
(function () {
  const APP = (window.APP = window.APP || {});
  APP.stores = APP.stores || {};

  function createNotificationApi(Alpine) {
    return {
      open: false,
      text: '',
      variant: 'info',
      hardwareError: '',
      _timer: null,
      show(text, variant = 'info', duration = 3500) {
        const msg = String(text || '');
        if (!msg) return;
        this.text = msg;
        this.variant = String(variant || 'info');
        this.open = true;
        clearTimeout(this._timer);
        const d = Number(duration) || 0;
        if (d > 0) {
          this._timer = setTimeout(() => { this.open = false; }, d);
        }
      },
      hide() {
        this.open = false;
        clearTimeout(this._timer);
        this._timer = null;
      },
      showMessage(text, variant = 'info', duration = 3500) {
        this.show(text, variant, duration);
      },
      success(text, duration = 3500) { this.show(text, 'success', duration); },
      error(text, duration = 4500) { this.show(text, 'error', duration); },
      warning(text, duration = 4000) { this.show(text, 'warning', duration); },
      info(text, duration = 3500) { this.show(text, 'info', duration); },
      flash(text, variant = 'info', duration = 3500) { this.show(text, variant, duration); },
      clearHardwareError() { this.hardwareError = ''; },
      setHardwareError(msg) { this.hardwareError = msg ? String(msg) : ''; },
      _syncVisible() {},
      goToValidationField(path) {
        try {
          const p = String(path || '');
          if (!p) return;
          const el = document.querySelector(`[data-path="${CSS.escape(p)}"]`)
            || document.querySelector(`[name="${CSS.escape(p)}"]`);
          if (el && typeof el.scrollIntoView === 'function') {
            el.scrollIntoView({ block: 'center', behavior: 'smooth' });
            if (typeof el.focus === 'function') el.focus();
          }
        } catch (e) {}
      }
    };
  }

  APP.stores.registerUiToastStore = function registerUiToastStore(Alpine) {
    const api = createNotificationApi(Alpine);
    Alpine.store('uiToast', api);
    Alpine.store('uiNotification', api);
    Alpine.store('uiStatusBar', api);
  };
})();
