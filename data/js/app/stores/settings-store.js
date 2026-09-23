// settings store (load/save/dirty/validation)
(function () {
  const APP = (window.APP = window.APP || {});
  APP.stores = APP.stores || {};

  function getSchemaSettings() {
    // Source of truth: `settingsUiSchema` store (bundled + normalized).
    try { return window.Alpine?.store?.('settingsUiSchema')?.settings || null; } catch (e) { return null; }
  }

  function emptySettingsModelFromSchema() {
    // Главный режим: НИКАКИХ дефолтов значений. Создаём только структуру, чтобы UI не падал на доступе к объектам.
    const out = {
      wifi: {},
      gsm: {},
      mqtt: {},
      vehicle: {},
      sensors: [],
      inputs: [],
      thermostat: {},
      battery_saver: {},
      time: {},
      input_triggers: [],
      temperature_triggers: [],
      schedule_triggers: [],
      setup_required: false,
    };

    const schema = getSchemaSettings();
    if (!schema) return out;

    // “Плоские” секции: поля с явным path.
    for (const key of Object.keys(schema)) {
      const sec = schema[key];
      const fields = sec?.fields;
      if (!Array.isArray(fields)) continue;
      for (const f of fields) {
        const path = String(f?.path || '');
        if (!path) continue;
        const parts = path.split('.').filter(Boolean);
        if (parts.length < 2) continue;
        const top = parts[0];
        const sub = parts[1];
        if (!out[top] || typeof out[top] !== 'object') out[top] = {};
        if (!(sub in out[top])) out[top][sub] = null;
      }
    }

    return out;
  }

  function collectTopLevelConfigKeysFromSchema() {
    const schema = getSchemaSettings();
    if (!schema) return [];
    const out = new Set();

    function takePath(p) {
      const path = String(p || '');
      if (!path) return;
      const parts = path.split('.').filter(Boolean);
      if (!parts.length) return;
      out.add(parts[0]);
    }

    for (const key of Object.keys(schema)) {
      const sec = schema[key] || {};
      const fields = sec?.fields;
      if (Array.isArray(fields)) {
        for (const f of fields) {
          takePath(f?.path);
          takePath(f?.pathTmpl && String(f.pathTmpl).replaceAll('{i}', '0'));
        }
      }
      // triggers section uses inputFields/tempFields (no paths); handled explicitly below.
    }

    // Static contract pieces not represented as path-based fields in schema:
    out.add('sensors');
    out.add('inputs');
    out.add('input_triggers');
    out.add('temperature_triggers');
    out.add('schedule_triggers');

    return Array.from(out);
  }

  function collectSubtabKeysFromSchema(Alpine) {
    try {
      // Source of truth: settingsUiSchema store (bundled + normalized)
      const s = Alpine?.store?.('settingsUiSchema')?.settings || {};
      const out = {};
      const add = (tab, topKey) => {
        if (!tab || !topKey) return;
        if (!out[tab]) out[tab] = [];
        if (!out[tab].includes(topKey)) out[tab].push(topKey);
      };
      const takePath = (tab, p) => {
        const path = String(p || '');
        if (!path) return;
        const top = path.split('.').filter(Boolean)[0];
        if (top) add(tab, top);
      };

      for (const tab of Object.keys(s)) {
        const sec = s[tab] || {};
        const fields = sec.fields;
        if (Array.isArray(fields)) {
          for (const f of fields) {
            takePath(tab, f.path);
            if (f.pathTmpl) takePath(tab, String(f.pathTmpl).replace('{i}', '0'));
          }
        }
      }

      if (!out.triggers) out.triggers = ['input_triggers', 'temperature_triggers', 'schedule_triggers'];
      if (!out.battery) out.battery = ['battery_saver'];
      if (!out.time) out.time = ['time'];
      if (!out.sensors) out.sensors = ['sensors'];
      if (!out.inputs) out.inputs = ['inputs'];

      return out;
    } catch (e) {
      return {};
    }
  }

  function configGetUrl() {
    return APP.api?.endpoints?.configGet || APP.contract?.api?.endpoints?.configGet || '/config/get';
  }
  function configSaveUrl() {
    return APP.api?.endpoints?.configSave || APP.contract?.api?.endpoints?.configSave || '/config/save';
  }

  APP.stores.registerSettingsStore = function registerSettingsStore(Alpine) {
    Alpine.store('settings', {
      ...emptySettingsModelFromSchema(),

      rebootRequired: false,
      loading: false,
      validationErrors: [],
      loaded: false,
      dirty: false,
      _baseline: '',
      _baselineObj: null,
      changedPaths: {},
      _subtabKeys: null,

      init() {
        this.rebootRequired = false;
      },

      resetModel() {
        const m = emptySettingsModelFromSchema();
        // Не пересоздаём store целиком, чтобы Alpine реактивность не “потеряла” ссылки.
        for (const k of Object.keys(m)) this[k] = m[k];
      },

      async load(force = false) {
        if (this.loaded && !force) return;
        if (this.loading && !force) return;
        const manageLoading = !force;
        if (manageLoading) this.loading = true;
        try {
          if (force || !this.loaded) this.resetModel();
          // /config/get may return 409 while flash write is pending.
          // We retry with backoff to avoid showing partial/old settings.
          const res = await APP.api.apiJsonWithBusyRetry(configGetUrl(), { timeoutMs: 8000 }, { timeoutMsTotal: 12000 });
          if (!res.ok) throw new Error(`Failed to load config (HTTP ${res.status || 'ERR'})`);
          this.updateFromJSON(res.data);
          this.loaded = true;
          const cfg = this.getConfigForSave();
          this._baseline = APP.utils.stableStringify(cfg);
          this._baselineObj = APP.utils.deepClone(cfg);
          this.dirty = false;
        } catch (e) {
          Alpine.store('uiNotification').error('Ошибка загрузки конфигурации');
        } finally {
          if (manageLoading) this.loading = false;
        }
      },

      updateFromJSON(data) {
        if (!Array.isArray(this.input_triggers)) this.input_triggers = [];
        if (!Array.isArray(this.temperature_triggers)) this.temperature_triggers = [];
        if (!Array.isArray(this.schedule_triggers)) this.schedule_triggers = [];
        if (!Array.isArray(this.sensors)) this.sensors = [];
        if (!Array.isArray(this.inputs)) this.inputs = [];
        if (!this.time || typeof this.time !== 'object') this.time = {};

        for (let key in data) {
          if (!this.hasOwnProperty(key)) continue;
          const v = data[key];
          if (key === 'sensors') {
            this.sensors = Array.isArray(v) ? v : [];
            continue;
          }
          if (key === 'inputs') {
            this.inputs = Array.isArray(v) ? v : [];
            continue;
          }
          // Shallow assign objects/arrays/primitives (WYSIWYG, no default fabrication).
          this[key] = v;
        }

        // Normalize loose-typed fields from device/config.json to match schema expectations.
        try {
          const veh = this.vehicle;
          if (veh && typeof veh === 'object') {
            const raw = veh.adc_voltage_coeff;
            // settings.schema.json expects number; older configs may store it as string.
            if (typeof raw === 'string' && raw !== '') {
              const n = Number(raw);
              if (Number.isFinite(n)) veh.adc_voltage_coeff = n;
            }
          }
        } catch (e) {}

        // Intentionally do NOT create defaults for sensors/inputs here.
        // UI should reflect exactly what device returned; missing fields will be created only on user input.
      },

      async save() {
        if (this.loading) return;
        this.loading = true;
        this.validationErrors = [];
        try {
          Alpine.store('settingsForm')?.clearClientErrors?.();
          try { await Alpine.store('settingsValidator')?.load?.(); } catch (e) {}
          const errs = Alpine.store('settingsForm')?.validateAllSettings?.() || [];
          if (errs.length) {
            this.validationErrors = errs.map(e => ({ ...e, source: 'client' }));
            this.loading = false;
            return;
          }
        } catch (e) {}

        Alpine.store('uiStatusBar').clearHardwareError();
        try {
          const self = this;
          const result = await APP.api.runFlashWrite({
            Alpine,
            busyTitle: 'Сохранение',
            busyMessage: 'Сохранение настроек…',
            expectedLastOp: 'save_config',
            timeoutMsCommit: 12000,
            request: async () => await APP.api.apiJson(configSaveUrl(), {
              method: 'POST',
              headers: { 'Content-Type': 'application/json' },
              body: JSON.stringify(self.getConfigForSave())
            }),
            onHttpOk: async ({ data }) => {
              // Server-side validation errors: do not proceed with commit wait if save rejected.
              if (data?.errors && Array.isArray(data.errors)) {
                self.validationErrors = data.errors.map(e => ({ ...e, source: 'server' }));
              }
            },
            onCommitOk: async () => {
              // Reload from LittleFS source-of-truth after commit
              await self.load(true);
              Alpine.store('uiBusy').showOk({
                title: 'Сохранено',
                message: 'Настройки записаны во flash. Требуется перезагрузка устройства.',
                okLabel: 'OK'
              });
            }
          });

          if (result.busy409) return;

          if (result.timeout) {
            Alpine.store('uiStatusBar').setHardwareError('Нет подтверждения записи во flash. Проверьте соединение и при необходимости перезагрузите устройство.');
            return;
          }
          if (!result.ok) {
            // If validation errors already set above, do not overwrite them with generic message.
            if (!this.validationErrors?.length) {
              Alpine.store('uiStatusBar').setHardwareError('Не удалось сохранить настройки. Перезагрузите устройство и повторите попытку.');
            }
          }
        } catch (e) {
          Alpine.store('uiStatusBar').setHardwareError('Ошибка сети при сохранении. Проверьте соединение; при необходимости перезагрузите устройство.');
        } finally {
          this.loading = false;
        }
      },

      getConfigForSave() {
        // WYSIWYG: send only fields that UI actually shows (schema + showIf).
        const schema = Alpine.store('settingsUiSchema')?.settings || null;
        if (!schema) {
          // Fallback to previous behavior if schema didn't load.
          const config = {};
          const keys = collectTopLevelConfigKeysFromSchema();
          for (const k of keys) {
            if (this[k] !== undefined) config[k] = this[k];
          }
          if (config.sensors === undefined) config.sensors = this.sensors || {};
          if (config.inputs === undefined) config.inputs = this.inputs || {};
          if (config.input_triggers === undefined) config.input_triggers = Array.isArray(this.input_triggers) ? this.input_triggers : [];
          if (config.temperature_triggers === undefined) config.temperature_triggers = Array.isArray(this.temperature_triggers) ? this.temperature_triggers : [];
          if (config.schedule_triggers === undefined) config.schedule_triggers = Array.isArray(this.schedule_triggers) ? this.schedule_triggers : [];
          return config;
        }

        const getByPath = (obj, path) => {
          try {
            if (!obj) return undefined;
            const parts = String(path || '').split('.').filter(Boolean);
            let cur = obj;
            for (const p of parts) {
              if (cur == null) return undefined;
              cur = cur[p];
            }
            return cur;
          } catch (e) { return undefined; }
        };
        const setByPath = (obj, path, value) => {
          try {
            const parts = String(path || '').split('.').filter(Boolean);
            if (!parts.length) return;
            const isIndex = (s) => /^\d+$/.test(String(s));
            let cur = obj;
            for (let i = 0; i < parts.length - 1; i++) {
              const p = parts[i];
              const next = parts[i + 1];
              if (isIndex(p)) {
                const idx = Number(p);
                if (!Array.isArray(cur)) return;
                if (cur[idx] == null || typeof cur[idx] !== 'object') cur[idx] = (isIndex(next) ? [] : {});
                cur = cur[idx];
              } else {
                if (cur[p] == null || typeof cur[p] !== 'object') cur[p] = (isIndex(next) ? [] : {});
                cur = cur[p];
              }
            }
            const last = parts[parts.length - 1];
            if (isIndex(last)) {
              const idx = Number(last);
              if (!Array.isArray(cur)) return;
              cur[idx] = value;
            } else {
              cur[last] = value;
            }
          } catch (e) {}
        };

        const isVisible = (field, i = undefined) => {
          try {
            if (!field || typeof field !== 'object') return false;
            if (typeof field.showIf !== 'function') return true;
            // compiled showIf expects (store,i) where store.settings is used
            return !!field.showIf({ settings: this, programs: Alpine.store('programs') }, i);
          } catch (e) {
            return true;
          }
        };

        const out = {};

        // Simple tabs: explicit paths in schema.fields
        for (const tabKey of Object.keys(schema)) {
          const sec = schema[tabKey] || {};
          const fields = sec.fields;
          if (!Array.isArray(fields)) continue;

          // sensors/inputs are rendered separately using pathTmpl and repeats; handle below.
          if (tabKey === 'sensors' || tabKey === 'inputs') continue;

          for (const f of fields) {
            const path = String(f?.path || '');
            if (!path) continue;
            if (!isVisible(f)) continue;
            const v = getByPath(this, path);
            // Do not fabricate missing values into the outgoing payload (prevents false diffs/highlights).
            if (v === undefined) continue;
            setByPath(out, path, v);
          }
        }

        // Sensors repeat: prefer locked hwCounts; avoid live discovery drift.
        try {
          const sec = schema.sensors || {};
          const fields = sec.fields || [];
          const st = Alpine.store('deviceStatus') || {};
          const locked = !!st?.hwCounts?.locked;
          const n = locked
            ? (Number(st?.hwCounts?.sensors) || 0)
            : Math.max(
              Array.isArray(this.sensors) ? this.sensors.length : 0,
              Number(st?.hwCounts?.sensors) || 0
            );
          for (let i = 0; i < n; i++) {
            for (const f of fields) {
              const tmpl = String(f?.pathTmpl || '');
              if (!tmpl) continue;
              if (!isVisible(f, i)) continue;
              const path = tmpl.replaceAll('{i}', String(i));
              const v = getByPath(this, path);
              if (v === undefined) continue;
              setByPath(out, path, v);
            }
          }
        } catch (e) {}

        // Inputs repeat: prefer locked hwCounts; avoid runtime arrays drift.
        try {
          const sec = schema.inputs || {};
          const fields = sec.fields || [];
          const st = Alpine.store('deviceStatus') || {};
          const locked = !!st?.hwCounts?.locked;
          const n = locked
            ? (Number(st?.hwCounts?.inputs) || 0)
            : Math.max(
              Array.isArray(this.inputs) ? this.inputs.length : 0,
              Number(st?.hwCounts?.inputs) || 0
            );
          for (let i = 0; i < n; i++) {
            for (const f of fields) {
              const tmpl = String(f?.pathTmpl || '');
              if (!tmpl) continue;
              if (!isVisible(f, i)) continue;
              const path = tmpl.replaceAll('{i}', String(i));
              const v = getByPath(this, path);
              if (v === undefined) continue;
              setByPath(out, path, v);
            }
          }
        } catch (e) {}

        // Triggers: these are rendered as inputFields/tempFields with bind → path.
        try {
          const trig = schema.triggers || {};
          const inFields = Array.isArray(trig.inputFields) ? trig.inputFields : [];
          const tFields = Array.isArray(trig.tempFields) ? trig.tempFields : [];
          const sFields = Array.isArray(trig.scheduleFields) ? trig.scheduleFields : [];

          const inOut = [];
          for (let idx = 0; idx < (this.input_triggers?.length || 0); idx++) {
            const row = {};
            for (const f of inFields) {
              const bind = String(f?.bind || '');
              if (!bind) continue;
              const path = `input_triggers.${idx}.${bind}`;
              const v = getByPath(this, path);
              if (v !== undefined) row[bind] = v;
            }
            inOut.push(row);
          }
          out.input_triggers = inOut;

          const tOut = [];
          for (let idx = 0; idx < (this.temperature_triggers?.length || 0); idx++) {
            const row = {};
            for (const f of tFields) {
              const bind = String(f?.bind || '');
              if (!bind) continue;
              const path = `temperature_triggers.${idx}.${bind}`;
              const v = getByPath(this, path);
              if (v !== undefined) row[bind] = v;
            }
            tOut.push(row);
          }
          out.temperature_triggers = tOut;

          const sOut = [];
          for (let idx = 0; idx < (this.schedule_triggers?.length || 0); idx++) {
            const row = {};
            for (const f of sFields) {
              const bind = String(f?.bind || '');
              if (!bind) continue;
              const path = `schedule_triggers.${idx}.${bind}`;
              const v = getByPath(this, path);
              if (v !== undefined) row[bind] = v;
            }
            sOut.push(row);
          }
          out.schedule_triggers = sOut;
        } catch (e) {
          // best-effort; do not block save
        }

        return out;
      },

      isPathChanged(path) {
        if (!path || !this.dirty) return false;
        return !!this.changedPaths[String(path)];
      },

      isPathInvalid(path) {
        const p = String(path || '');
        if (!p) return false;
        // Include live client-side errors (Ajv) from uiForm.
        try {
          if (window.Alpine?.store?.('uiForm')?.isClientInvalid?.(p)) return true;
        } catch (e) {}
        const errs = this.validationErrors;
        if (!errs || !Array.isArray(errs) || errs.length === 0) return false;
        return errs.some(e => String(e?.field || '') === p);
      },

      isSchemaInvalid() {
        try {
          const errs = this.validationErrors;
          if (!errs || !Array.isArray(errs) || errs.length === 0) return false;
          return errs.some(e => String(e?.field || '') === '__schema__');
        } catch (e) {
          return false;
        }
      },

      triggerRowIsNew(kind, idx) {
        if (!this._baseline) return false;
        try {
          const base = JSON.parse(this._baseline);
          const listKey = kind === 'in' ? 'input_triggers' : 'temperature_triggers';
          const oldList = Array.isArray(base[listKey]) ? base[listKey] : [];
          return Number(idx) >= oldList.length;
        } catch (e) {
          return false;
        }
      },

      triggerFieldTone(kind, idx, key) {
        if (!this._baseline) return '';
        try {
          const base = JSON.parse(this._baseline);
          const listKey = kind === 'in' ? 'input_triggers' : 'temperature_triggers';
          const oldList = Array.isArray(base[listKey]) ? base[listKey] : [];
          const i = Number(idx);
          if (i >= oldList.length) return '';
          const cur = this[listKey]?.[i];
          const old = oldList[i];
          if (APP.utils.stableStringify(cur?.[key]) !== APP.utils.stableStringify(old?.[key])) return 'field-changed';
        } catch (e) {}
        return '';
      },

      subtabHasChanges(tab) {
        if (!this._subtabKeys) this._subtabKeys = collectSubtabKeysFromSchema(Alpine);
        const keys = this._subtabKeys?.[tab];
        if (!keys || !this._baseline) return false;
        try {
          const cur = JSON.parse(APP.utils.stableStringify(this.getConfigForSave()));
          const base = JSON.parse(this._baseline);
          for (const key of keys) {
            if (APP.utils.stableStringify(cur[key]) !== APP.utils.stableStringify(base[key])) return true;
          }
        } catch (e) {}
        return false;
      },

      revertSubtab(tab) {
        if (!this._subtabKeys) this._subtabKeys = collectSubtabKeysFromSchema(Alpine);
        const keys = this._subtabKeys?.[tab];
        if (!keys || !this._baseline) return;
        try {
          const base = JSON.parse(this._baseline);
          for (const key of keys) {
            if (base[key] !== undefined) this[key] = APP.utils.deepClone(base[key]);
          }
          this.validationErrors = [];
          try { Alpine.store('settingsForm')?.clearClientErrors?.(); } catch (e2) {}
          this.recomputeDirty();
        } catch (e) {}
      },

      revertAll() {
        if (!this._baseline) return;
        try {
          const base = JSON.parse(this._baseline);
          const curKeys = Object.keys(this.getConfigForSave());
          for (const key of curKeys) {
            if (base[key] !== undefined) this[key] = APP.utils.deepClone(base[key]);
          }
          this.validationErrors = [];
          try { Alpine.store('settingsForm')?.clearClientErrors?.(); } catch (e2) {}
          this.recomputeDirty();
        } catch (e) {}
      },

      recomputeDirty() {
        const curObj = this.getConfigForSave();
        const nowStr = APP.utils.stableStringify(curObj);
        this.dirty = !!(this._baseline && nowStr !== this._baseline);
        this.changedPaths = {};
        if (!this._baseline || !this.dirty) return;
        try {
          const base = this._baselineObj || null;
          if (base) APP.utils.collectLeafDiffPaths(curObj, base, [], this.changedPaths);
        } catch (e) {}
      },

      addInputTrigger() {
        const list = Array.isArray(this.input_triggers) ? this.input_triggers : [];
        let maxId = 0;
        for (const tr of list) {
          const id = Number(tr?.id) || 0;
          if (id > maxId) maxId = id;
        }
        const firstInputId = Number(this.inputs?.[0]?.id) || 0;
        this.input_triggers.push({ id: maxId + 1, enabled: false, input_id: firstInputId, trigger_level: 1, program_id: 0 });
        this.recomputeDirty();
      },
      removeInputTrigger(index) { this.input_triggers.splice(index, 1); this.recomputeDirty(); },
      addTempTrigger() {
        const list = Array.isArray(this.temperature_triggers) ? this.temperature_triggers : [];
        let maxId = 0;
        for (const tr of list) {
          const id = Number(tr?.id) || 0;
          if (id > maxId) maxId = id;
        }
        this.temperature_triggers.push({ id: maxId + 1, enabled: false, sensor_id: 0, sensor_name: '', comparison: 'above', threshold: 0, program_id: 0 });
        this.recomputeDirty();
      },
      removeTempTrigger(index) { this.temperature_triggers.splice(index, 1); this.recomputeDirty(); },
      addScheduleTrigger() {
        const list = Array.isArray(this.schedule_triggers) ? this.schedule_triggers : [];
        let maxId = 0;
        for (const tr of list) {
          const id = Number(tr?.id) || 0;
          if (id > maxId) maxId = id;
        }
        this.schedule_triggers.push({
          id: maxId + 1,
          enabled: false,
          hour: 7,
          minute: 0,
          days_mask: 127,
          program_id: 0
        });
        this.recomputeDirty();
      },
      removeScheduleTrigger(index) { this.schedule_triggers.splice(index, 1); this.recomputeDirty(); },
      updateInputType(index) {}
    });
  };
})();

