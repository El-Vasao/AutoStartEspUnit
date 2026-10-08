// program validators store (Ajv JSON Schema)
(function () {
  const APP = (window.APP = window.APP || {});
  APP.stores = APP.stores || {};

  const v = () => (APP.validation || {});

  function deepClone(obj) {
    try { return JSON.parse(JSON.stringify(obj)); } catch (e) { return obj; }
  }

  function clampInt(n, lo, hi) {
    const x = Number(n);
    if (!Number.isFinite(x)) return lo;
    if (x < lo) return lo;
    if (x > hi) return hi;
    return Math.trunc(x);
  }

  function patchProgramSchemaWithHwLimits(schema, hwCounts) {
    const sc = deepClone(schema);
    try {
      // IDs are not tied to 0..N-1 indices anymore. Keep bounds wide.
      const relayNode = sc?.properties?.steps?.items?.properties?.relay_id;
      if (relayNode && typeof relayNode === 'object') {
        relayNode.minimum = 0;
        relayNode.maximum = 65535;
      }

      const inputNode = sc?.properties?.steps?.items?.properties?.input_id;
      if (inputNode && typeof inputNode === 'object') {
        inputNode.minimum = 0;
        inputNode.maximum = 65535;
      }
    } catch (e) {}
    return sc;
  }

  APP.stores.registerProgramValidatorStore = function registerProgramValidatorStore(Alpine) {
    Alpine.store('programValidator', {
      loaded: false,
      loading: false,
      _validateProgram: null,
      _validateIndex: null,
      _lastInitError: '',

      load() {
        if (this.loaded) return;
        if (this.loading) return;
        this.loading = true;
        try {
          const Ajv2020 = window.ajv2020;
          if (!Ajv2020) throw new Error('Ajv2020 bundle missing (window.ajv2020)');

          let programSchema = APP.preloaded?.programValidationSchema;
          let indexSchema = APP.preloaded?.programIndexValidationSchema;
          if (!programSchema || typeof programSchema !== 'object') throw new Error('program validation schema missing (preloaded)');
          if (!indexSchema || typeof indexSchema !== 'object') throw new Error('program index validation schema missing (preloaded)');

          // Patch program schema with runtime HW limits from /bootstrap.
          try {
            const hwCounts = Alpine?.store?.('deviceStatus')?.hwCounts || null;
            programSchema = patchProgramSchemaWithHwLimits(programSchema, hwCounts);
          } catch (e3) {}

          const ajv = new Ajv2020({ allErrors: true, strict: true, allowUnionTypes: true });
          const validateProgram = ajv.compile(programSchema);
          const validateIndex = ajv.compile(indexSchema);

          this._validateProgram = validateProgram;
          this._validateIndex = validateIndex;
          this.loaded = true;
          this._lastInitError = '';
        } catch (e) {
          this.loaded = false;
          this._validateProgram = null;
          this._validateIndex = null;
          this._lastInitError = String(e?.message || e || 'programValidator init failed');
          try { console.warn('[programValidator] init failed:', this._lastInitError); } catch (e2) {}
        } finally {
          this.loading = false;
        }
      },

      validateProgramData(data) {
        const validate = this._validateProgram;
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

      validateProgramDataHard(data) {
        const validate = this._validateProgram;
        if (!validate) {
          return { ok: false, errorsByPath: { __schema__: ['Проверка недоступна'] } };
        }
        return this.validateProgramData(data);
      },

      validateProgramIndex(data) {
        const validate = this._validateIndex;
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
      }
    });
  };
})();

