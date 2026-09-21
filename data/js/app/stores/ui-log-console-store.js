// logs store (always keep text in sync; visible only gates autoscroll)
(function () {
  const APP = (window.APP = window.APP || {});
  APP.stores = APP.stores || {};

  APP.stores.registerUiLogConsoleStore = function registerUiLogConsoleStore(Alpine) {
    Alpine.store('uiLogs', {
      entries: [],
      text: '',
      visible: false,
      _el: null,
      _getEl() {
        if (this._el && document.body.contains(this._el)) return this._el;
        this._el = document.getElementById('logsDisplay');
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
        if (this.entries.length > 200) {
          this.entries.shift();
          this.text = this.entries.join('\n');
        } else {
          this.text = this.text ? (this.text + '\n' + line) : String(line);
        }

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
      }
    });
  };
})();
