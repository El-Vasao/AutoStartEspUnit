// program validators store (Ajv JSON Schema)
(function () {
  const APP = (window.APP = window.APP || {});
  APP.stores = APP.stores || {};

  const v = () => (APP.validation || {});

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
          const indexSchema = APP.preloaded?.programIndexValidationSchema;
          if (!programSchema || typeof programSchema !== 'object') throw new Error('program validation schema missing (preloaded)');
          if (!indexSchema || typeof indexSchema !== 'object') throw new Error('program index validation schema missing (preloaded)');

          // Silk bounds (K1..Kn / IN1..INm) follow /bootstrap hwCounts, not hardcoded schema ranges.
          try {
            const hwCounts = Alpine?.store?.('deviceStatus')?.hwCounts || null;
            if (APP.schemaNormalize?.applyHardwareLimits) {
              programSchema = APP.schemaNormalize.applyHardwareLimits(programSchema, hwCounts);
            }
          } catch (e3) {}

          const ajv = APP.validation.createAjv();
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
        let ok = !!validate(data);
        const errs = ok ? [] : (validate.errors || []);
        const out = {};
        for (const e of errs) {
          const path = v().errorToPath?.(e) || '';
          if (!path) continue;
          if (!out[path]) out[path] = [];
          out[path].push(v().errorToMessage?.(e) || 'Неверное значение');
        }

        // Firmware rejects RELAY_* steps on the starter relay; only ENGINE_START drives it.
        try {
          const starterId = String(Alpine.store('settings')?.vehicle?.starter_relay_id || '').trim();
          const steps = Array.isArray(data?.steps) ? data.steps : [];
          const inTrig = Alpine.store('settings')?.input_triggers || [];
          const tempTrig = Alpine.store('settings')?.temperature_triggers || [];
          const inIds = new Set(inTrig.map((t) => Number(t?.id)).filter((n) => n > 0));
          const tempIds = new Set(tempTrig.map((t) => Number(t?.id)).filter((n) => n > 0));
          steps.forEach((s, i) => {
            const action = String(s?.action || '');
            if (starterId && action.startsWith('RELAY_') && String(s?.relay_id || '').trim() === starterId) {
              const path = `steps.${i}.relay_id`;
              if (!out[path]) out[path] = [];
              out[path].push('Реле стартера управляется только шагом «Запуск двигателя»');
              ok = false;
            }
            if (action === 'SMS_OWNER' && !String(s?.message || '').trim()) {
              const path = `steps.${i}.message`;
              if (!out[path]) out[path] = [];
              out[path].push('Укажите текст SMS');
              ok = false;
            }
            if (action === 'INPUT_TRIGGER_ENABLE' || action === 'INPUT_TRIGGER_DISABLE') {
              const id = Number(s?.input_trigger_id) || 0;
              if (!inIds.has(id)) {
                const path = `steps.${i}.input_trigger_id`;
                if (!out[path]) out[path] = [];
                out[path].push('Выберите триггер входа');
                ok = false;
              }
            }
            if (action === 'TEMP_TRIGGER_ENABLE' || action === 'TEMP_TRIGGER_DISABLE') {
              const id = Number(s?.temp_trigger_id) || 0;
              if (!tempIds.has(id)) {
                const path = `steps.${i}.temp_trigger_id`;
                if (!out[path]) out[path] = [];
                out[path].push('Выберите температурный триггер');
                ok = false;
              }
            }
          });
        } catch (e) {}
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

