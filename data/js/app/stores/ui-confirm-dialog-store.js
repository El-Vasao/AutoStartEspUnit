// dialog store (confirm modal)
(function () {
  const APP = (window.APP = window.APP || {});
  APP.stores = APP.stores || {};

  APP.stores.registerUiConfirmDialogStore = function registerUiConfirmDialogStore(Alpine) {
    Alpine.store('uiDialog', {
      open: false,
      title: '',
      message: '',
      cancelLabel: 'Отмена',
      confirmLabel: 'Да',
      extraLabel: '',
      onConfirm: null,
      onCancel: null,
      onExtra: null,
      _lastFocus: null,

      show({ title, message, onConfirm, onCancel = null, onExtra = null, cancelLabel = 'Отмена', confirmLabel = 'Да', extraLabel = '' }) {
        try { this._lastFocus = document.activeElement; } catch (e) { this._lastFocus = null; }
        this.title = title;
        this.message = message;
        this.cancelLabel = String(cancelLabel || 'Отмена');
        this.confirmLabel = String(confirmLabel || 'Да');
        this.extraLabel = String(extraLabel || '');
        this.onConfirm = onConfirm;
        this.onCancel = onCancel;
        this.onExtra = onExtra;
        this.open = true;
        try {
          Alpine.store('overlay')?.open?.({
            id: 'dialog',
            type: 'dialog',
            // dismiss (Esc/backdrop) should NOT trigger actions
            close: () => { try { this.dismiss(); } catch (e) {} }
          });
        } catch (e) {}
        queueMicrotask(() => {
          try {
            const card = document.querySelector('.modal-card');
            const btn = card?.querySelector('button');
            if (btn) btn.focus({ preventScroll: true });
          } catch (e) {}
        });
      },

      _close() {
        try { Alpine.store('overlay')?.remove?.('dialog'); } catch (e) {}
        this.open = false;
        this.onConfirm = null;
        this.onCancel = null;
        this.onExtra = null;
        queueMicrotask(() => {
          try {
            const el = this._lastFocus;
            if (el && typeof el.focus === 'function') el.focus();
          } catch (e) {}
          this._lastFocus = null;
        });
      },

      dismiss() {
        this._close();
      },

      cancel() {
        try { this.onCancel?.(); } catch (e) {}
        this._close();
      },

      confirm() {
        try { this.onConfirm?.(); } catch (e) {}
        this._close();
      },

      extra() {
        try { this.onExtra?.(); } catch (e) {}
        this._close();
      }
    });

    // click-outside handled centrally by overlay manager
  };
})();

