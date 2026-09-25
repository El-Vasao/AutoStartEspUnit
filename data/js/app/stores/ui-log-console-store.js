// System + modem log consoles (always keep text in sync; visible only gates autoscroll)
(function () {
  const APP = (window.APP = window.APP || {});
  APP.stores = APP.stores || {};

  function createLogConsole(elementId) {
    return {
      entries: [],
      text: '',
      visible: false,
      _el: null,
      _elementId: elementId,
      _getEl() {
        if (this._el && document.body.contains(this._el)) return this._el;
        this._el = document.getElementById(this._elementId);
        return this._el;
      },
      flushToView() {
        this.text = this.entries.join('\n');
        if (!this.visible) return;
        const el = this._getEl();
        if (el) {
          requestAnimationFrame(() => {
            el.scrollTop = el.scrollHeight;
          });
        }
      },
      setVisible(v) {
        const next = !!v;
        if (next === this.visible) {
          if (next) this.flushToView();
          return;
        }
        this.visible = next;
        if (this.visible) this.flushToView();
      },
      add(line) {
        const el = this._getEl();
        const wasNearBottom = !!el && (el.scrollHeight - (el.scrollTop + el.clientHeight) < 40);

        this.entries.push(line);
        this.text = this.text ? (this.text + '\n' + line) : String(line);

        if (this.visible && el && wasNearBottom) {
          requestAnimationFrame(() => {
            el.scrollTop = el.scrollHeight;
          });
        }
      },
      clear() {
        this.entries = [];
        this.text = '';
        const el = this._getEl();
        if (el) el.scrollTop = 0;
      },
      async copy() {
        const toast = Alpine.store('uiToast');
        const text = String(this.text || '');
        if (!text) {
          toast?.show?.('Лог пуст', 'info', 2500);
          return;
        }

        const fallbackCopy = () => {
          const ta = document.createElement('textarea');
          ta.value = text;
          ta.setAttribute('readonly', '');
          ta.style.position = 'fixed';
          ta.style.left = '-9999px';
          ta.style.top = '0';
          document.body.appendChild(ta);
          ta.focus();
          ta.select();
          ta.setSelectionRange(0, ta.value.length);
          let ok = false;
          try {
            ok = document.execCommand('copy');
          } catch (_) {
            ok = false;
          }
          document.body.removeChild(ta);
          return ok;
        };

        try {
          if (navigator.clipboard && typeof navigator.clipboard.writeText === 'function') {
            await navigator.clipboard.writeText(text);
            toast?.show?.('Лог скопирован', 'success', 2500);
            return;
          }
        } catch (_) {
          // fall through to execCommand
        }

        if (fallbackCopy()) {
          toast?.show?.('Лог скопирован', 'success', 2500);
        } else {
          toast?.show?.('Не удалось скопировать', 'error', 3500);
        }
      }
    };
  }

  APP.stores.registerUiLogConsoleStore = function registerUiLogConsoleStore(Alpine) {
    Alpine.store('uiLogs', createLogConsole('logsDisplay'));
    Alpine.store('uiAtLogs', createLogConsole('atLogsDisplay'));
  };
})();
