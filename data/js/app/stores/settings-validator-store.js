// settings validator store (Ajv JSON Schema)
(function () {
  const APP = (window.APP = window.APP || {});
  APP.stores = APP.stores || {};

  const v = () => (APP.validation || {});

  APP.stores.registerSettingsValidatorStore = function registerSettingsValidatorStore(Alpine) {
    Alpine.store('settingsValidator', {
      loaded: false,
      loading: false,
      _ajv: null,
      _validate: null,
      _lastInitError: '',

      async load() {
        if (this.loaded) return;
        if (this.loading) return;
        this.loading = true;
        try {
          const Ajv2020 = window.ajv2020; // from ajv-dist bundle
          if (!Ajv2020) throw new Error('Ajv2020 bundle missing (window.ajv2020)');
          let schema = APP.preloaded?.settingsValidationSchema;
          if (!schema || typeof schema !== 'object') throw new Error('validation schema missing (preloaded)');

          // Patch schema with hardware-dependent limits from /bootstrap.
          try {
            const hwCounts = Alpine?.store?.('deviceStatus')?.hwCounts || null;
            if (window.APP?.schemaNormalize?.applyHardwareLimits) {
              schema = window.APP.schemaNormalize.applyHardwareLimits(schema, hwCounts);
            }
          } catch (e3) {}

          // Strict validation: config is safety-critical; allowUnionTypes is required for some fields
          // that can be sent either as numeric index or as string id.
          const ajv = new Ajv2020({ allErrors: true, strict: true, allowUnionTypes: true });
          const validate = ajv.compile(schema);

          this._ajv = ajv;
          this._validate = validate;
          this.loaded = true;
          this._lastInitError = '';
        } catch (e) {
          this.loaded = false;
          this._ajv = null;
          this._validate = null;
          this._lastInitError = String(e?.message || e || 'validator init failed');
          try { console.warn('[validator] init failed:', this._lastInitError); } catch (e2) {}
        } finally {
          this.loading = false;
        }
      },

      validateData(data) {
        // Validates the exact config payload we plan to send to the device.
        const validate = this._validate;
        if (!validate) return { ok: true, errorsByPath: {} };

        const ok = !!validate(data);
        const errs = ok ? [] : (validate.errors || []);

        const out = {};
        for (const e of errs) {
          const path = v().errorToPath?.(e) || '';
          if (!path) continue;
          if (!out[path]) out[path] = [];
          out[path].push(v().errorToMessage?.(e) || 'Неверное значение');
        }
        return { ok, errorsByPath: out };
      },

      validateDataHard(data) {
        // Write-hard validation: must not allow saving without an initialized validator.
        const validate = this._validate;
        if (!validate) {
          return { ok: false, errorsByPath: { __schema__: ['Проверка недоступна'] } };
        }
        return this.validateData(data);
      },

      validatePath(data, dottedPath) {
        const p = String(dottedPath || '');
        if (!p) return { ok: true, message: '' };
        const res = this.validateData(data);
        if (res.ok) return { ok: true, message: '' };
        const msgs = res.errorsByPath[p];
        if (!msgs || !msgs.length) return { ok: true, message: '' };
        return { ok: false, message: String(msgs[0] || 'Неверное значение') };
      }
    });
  };
})();

