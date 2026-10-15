// settings UI schema store
(function () {
  const APP = (window.APP = window.APP || {});
  APP.stores = APP.stores || {};

  APP.stores.registerSettingsUiSchemaStore = function registerSettingsUiSchemaStore(Alpine) {
    function getByPath(obj, path) {
      return APP.path.get(obj, path);
    }

    function compileShowIf(showIf) {
      try {
        if (!showIf || typeof showIf !== 'object') return null;
        if (showIf.eq && typeof showIf.eq === 'object') {
          const cond = showIf.eq;
          const value = cond.value;
          const path = cond.path ? String(cond.path) : '';
          const pathTmpl = cond.pathTmpl ? String(cond.pathTmpl) : '';
          return function (store, i) {
            try {
              const p = pathTmpl ? pathTmpl.replaceAll('{i}', String(i ?? '0')) : path;
              const cur = getByPath(store?.settings, p);
              // eslint-disable-next-line eqeqeq
              return cur == value;
            } catch (e) {
              return false;
            }
          };
        }
        if (showIf.in && typeof showIf.in === 'object') {
          const cond = showIf.in;
          const values = Array.isArray(cond.values) ? cond.values : [];
          const path = cond.path ? String(cond.path) : '';
          const pathTmpl = cond.pathTmpl ? String(cond.pathTmpl) : '';
          return function (store, i) {
            try {
              const p = pathTmpl ? pathTmpl.replaceAll('{i}', String(i ?? '0')) : path;
              const cur = getByPath(store?.settings, p);
              // eslint-disable-next-line eqeqeq
              return values.some((v) => cur == v);
            } catch (e) {
              return false;
            }
          };
        }
      } catch (e) {}
      return null;
    }

    function normalizeFieldsArray(fields) {
      try {
        if (!Array.isArray(fields)) return;
        for (const f of fields) {
          if (!f || typeof f !== 'object') continue;
          if (!f.valueType) f.valueType = APP.schemaNormalize?.inferValueTypeFromField?.(f) || 'string';
          if (f.showIf && typeof f.showIf === 'object') {
            const fn = compileShowIf(f.showIf);
            if (fn) f.showIf = fn;
          }
        }
      } catch (e) {}
    }

    function normalizeSchema(raw) {
      const out = raw && typeof raw === 'object' ? raw : {};
      if (!out.settings || typeof out.settings !== 'object') out.settings = {};

      // Compile declarative showIf objects into functions for templates.
      try {
        for (const tabKey of Object.keys(out.settings)) {
          const sec = out.settings[tabKey] || {};
          normalizeFieldsArray(sec.fields);
          normalizeFieldsArray(sec.inputFields);
          normalizeFieldsArray(sec.tempFields);
        }
      } catch (e) {}

      return out;
    }

    Alpine.store('settingsUiSchema', {
      loaded: false,
      loading: false,
      settings: {},
      async load() {
        if (this.loaded) return;
        if (this.loading) return;
        this.loading = true;
        try {
          const json = APP.preloaded?.settingsUiSchema;
          if (!json || typeof json !== 'object') throw new Error('bundled schema unavailable');
          const norm = normalizeSchema(json);
          this.settings = norm.settings || {};
          this.loaded = true;
        } catch (e) {
          this.loaded = false;
        } finally {
          this.loading = false;
        }
      }
    });
  };
})();

