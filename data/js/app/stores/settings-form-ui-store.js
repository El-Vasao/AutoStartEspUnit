// settings form store (client/server validation + fieldRow VM)
(function () {
  const APP = (window.APP = window.APP || {});
  APP.stores = APP.stores || {};

  // Client-side validation (Ajv) is the primary UX; server still validates on save.
  const FRONTEND_VALIDATION_ENABLED = true;

  APP.stores.registerSettingsFormStore = function registerSettingsFormStore(Alpine) {
    Alpine.store('settingsForm', {
      _clientErrors: {},
      _setClientError(path, message) {
        const p = String(path || '');
        if (!p) return;
        if (message) this._clientErrors[p] = String(message);
        else delete this._clientErrors[p];
      },
      clearClientErrors() {
        this._clientErrors = {};
      },
      isClientInvalid(path) {
        const p = String(path || '');
        return !!this._clientErrors[p];
      },
      getClientError(path) {
        const p = String(path || '');
        return this._clientErrors[p] || '';
      },
      getServerError(path) {
        const p = String(path || '');
        if (!p) return '';
        const errs = Alpine.store('settings')?.validationErrors;
        if (!errs || !Array.isArray(errs) || errs.length === 0) return '';
        const hit = errs.find(e => String(e?.field || '') === p && String(e?.source || 'server') === 'server');
        return hit ? String(hit?.message || 'Некорректное значение') : '';
      },
      getError(path) {
        // Priority: client regex errors first, then server validation errors.
        const c = this.getClientError(path);
        if (c) return c;
        const s = this.getServerError(path);
        if (s) return s;
        return '';
      },
      hasError(path) {
        return !!this.getError(path);
      },
      validateField(field, value) {
        if (!FRONTEND_VALIDATION_ENABLED) {
          if (field) this._setClientError(String(field.path || ''), '');
          return { ok: true };
        }
        if (!field) return { ok: true };
        const path = String(field.path || '');
        const st = Alpine.store('settings');
        const v = Alpine.store('settingsValidator');
        if (!path || !st || !v || !v.loaded) {
          // Fail closed: if validator is unavailable, do not pretend value is valid.
          const reason = v ? String(v._lastInitError || '') : '';
          if (path) this._setClientError(path, reason ? `Валидация недоступна: ${reason}` : 'Валидация недоступна (схема не загружена)');
          return { ok: false };
        }
        const data = st.getConfigForSave?.() || {};
        const res = v.validatePath(data, path);
        this._setClientError(path, res.ok ? '' : (res.message || 'Некорректное значение'));
        return { ok: !!res.ok };
      },
      fieldWithPath(field, path) {
        try {
          return Object.assign({}, field, { path: String(path || '') });
        } catch (e) {
          return field;
        }
      },
      optionsForField(field) {
        try {
          if (!field) return [];
          if (Array.isArray(field.options)) return field.options;
          const from = String(field.optionsFrom || '');
          if (!from) return [];
          const shared = APP.options?.forSource?.(from, Alpine);
          if (Array.isArray(shared)) return shared;
        } catch (e) {}
        return [];
      },
      getByPath(obj, path) {
        try {
          if (!obj) return undefined;
          const parts = String(path || '').split('.').filter(Boolean);
          let cur = obj;
          for (const p of parts) {
            if (cur == null) return undefined;
            cur = cur[p];
          }
          return cur;
        } catch (e) {
          return undefined;
        }
      },
      setByPath(obj, path, value) {
        try {
          if (!obj) return false;
          const parts = String(path || '').split('.').filter(Boolean);
          if (!parts.length) return false;
          const isIndex = (s) => /^\d+$/.test(String(s));
          let cur = obj;
          for (let i = 0; i < parts.length - 1; i++) {
            const p = parts[i];
            const next = parts[i + 1];
            if (isIndex(p)) {
              const idx = Number(p);
              if (!Array.isArray(cur)) return false;
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
            if (!Array.isArray(cur)) return false;
            cur[idx] = value;
          } else {
            cur[last] = value;
          }
          return true;
        } catch (e) {
          return false;
        }
      },
      coerceValue(field, raw) {
        const valueType = String(field?.valueType || '');
        const coerceByValueType = (vt, v, fallback) => (APP.coerce?.byValueType ? APP.coerce.byValueType(vt, v, fallback) : v);

        const t = String(field?.type || '');
        if (t === 'number') {
          const s = String(raw ?? '');
          if (s === '') return '';
          const n = Number(s);
          return coerceByValueType(valueType, (Number.isFinite(n) ? n : raw), field?.default);
        }
        if (t === 'select') {
          const s = String(raw ?? '');
          const opts = this.optionsForField(field) || [];
          for (const opt of opts) {
            if (String(opt?.value) === s) return coerceByValueType(valueType, opt.value, field?.default);
          }
          return coerceByValueType(valueType, raw, field?.default);
        }
        return coerceByValueType(valueType, raw, field?.default);
      },
      toggleStateText(v) {
        return v ? 'Включен' : 'Выключен';
      },
      toggleLabel(field, v) {
        if (field && (field.labelOn || field.labelOff)) {
          return v ? (field.labelOn ?? 'Включен') : (field.labelOff ?? 'Выключен');
        }
        return this.toggleStateText(v);
      },
      fieldRow(field, path) {
        const uiForm = this;
        const resolvedPath = String(path ?? field?.path ?? '');
        if (!resolvedPath) {
          return {
            field,
            path: '',
            openHelp: false,
            value: undefined,
            hasError() { return false; },
            errorText() { return ''; },
            onTextInput() {},
            onNumberInput() {},
            onSelectChange() {},
            onToggleClick() {},
            inputClass() { return ''; },
            switchClass() { return ''; },
            toggleText() { return uiForm.toggleLabel(field, false); },
            options() { return uiForm.optionsForField(field); },
            closeHelp() { this.openHelp = false; },
            onHelpKeydown(e) { if (e && e.key === 'Escape') this.openHelp = false; }
          };
        }

        return {
          field,
          path: resolvedPath,
          openHelp: false,
          _touched: false,
          get value() {
            return uiForm.getByPath(Alpine.store('settings'), this.path);
          },
          closeHelp() { this.openHelp = false; },
          onHelpKeydown(e) { if (e && e.key === 'Escape') this.openHelp = false; },
          setValue(v) {
            uiForm.setByPath(Alpine.store('settings'), this.path, v);
            Alpine.store('settings')?.recomputeDirty?.();
          },
          validate() {
            // Live validation is debounced to keep UI responsive.
            const self = this;
            uiForm.validateFieldDebounced(
              uiForm.fieldWithPath(this.field, this.path),
              function ({ ok, becameInvalid }) {
                try {
                  if (!self._touched) return;
                  if (becameInvalid) self.openHelp = true;
                  // If it became valid again, keep popover state as user chose.
                } catch (e) {}
              }
            );
          },
          hasError() { return uiForm.hasError(this.path); },
          errorText() { return uiForm.getError(this.path); },
          _closeHelpIfNotError() {
            try {
              if (!this.openHelp) return;
              if (uiForm.hasError(this.path)) return; // keep error popover open while user fixes input
              this.closeHelp();
            } catch (e) {}
          },
          onTextInput(e) { this._touched = true; this._closeHelpIfNotError(); this.setValue(e?.target?.value ?? ''); this.validate(); },
          onNumberInput(e) { this._touched = true; this._closeHelpIfNotError(); this.setValue(uiForm.coerceValue(this.field, e?.target?.value)); this.validate(); },
          onSelectChange(e) {
            this._touched = true;
            this._closeHelpIfNotError();
            const v = uiForm.coerceValue(this.field, e?.target?.value);
            this.setValue(v);

            // When selecting a sensor by ROM, also persist the current sensor name (hidden field).
            try {
              const from = String(this.field?.optionsFrom || '');
              if (from === 'sensorIds') {
                const p = String(this.path || '');
                if (p.endsWith('.sensor_id')) {
                  const namePath = p.slice(0, -('.sensor_id'.length)) + '.sensor_name';
                  // Resolve current name from settings.sensors by id.
                  const list = Alpine.store('settings')?.sensors;
                  let name = '';
                  if (Array.isArray(list)) {
                    const hit = list.find(x => x && String(x.id) === String(v));
                    name = String(hit?.name || '').trim();
                  }
                  uiForm.setByPath(Alpine.store('settings'), namePath, String(name || ''));
                }
              }
            } catch (e2) {}

            this.validate();
          },
          onToggleClick() { this._touched = true; this._closeHelpIfNotError(); this.setValue(!uiForm.getByPath(Alpine.store('settings'), this.path)); this.validate(); },
          inputClass() { return uiForm.fieldInputClass(this.path); },
          switchClass() { return uiForm.fieldSwitchClass(this.path, uiForm.getByPath(Alpine.store('settings'), this.path)); },
          toggleText() { return uiForm.toggleLabel(this.field, uiForm.getByPath(Alpine.store('settings'), this.path)); },
          options() { return uiForm.optionsForField(this.field); },
          helpBtnClass() {
            const base = (String(this.field?.type || '') === 'toggle') ? 'help-btn-toggle' : 'help-btn-control';
            return uiForm.hasError(this.path) ? `${base} has-error` : base;
          }
        };
      },
      _liveTimer: 0,
      validateFieldDebounced(fieldWithPath, onDone) {
        if (!FRONTEND_VALIDATION_ENABLED) return;
        try {
          try { Alpine.store('settingsValidator')?.load?.(); } catch (e0) {}
          const p = String(fieldWithPath?.path || '');
          if (this._liveTimer) clearTimeout(this._liveTimer);
          this._liveTimer = setTimeout(() => {
            try {
              const st = Alpine.store('settings');
              const value = this.getByPath(st, p);
              const wasInvalid = this.isClientInvalid(p) || !!this.getServerError(p);
              const res = this.validateField(fieldWithPath, value);
              const isInvalid = this.isClientInvalid(p) || !!this.getServerError(p);
              try {
                if (typeof onDone === 'function') onDone({ ok: !!res.ok, becameInvalid: !wasInvalid && isInvalid });
              } catch (e2) {}
            } catch (e) {}
          }, 220);
        } catch (e) {}
      },
      _collectVisiblePaths() {
        const out = new Set();
        try {
          const schema = Alpine.store('settingsUiSchema')?.settings || {};
          const st = Alpine.store('settings') || {};
          const programs = Alpine.store('programs');

          const addPath = (p) => {
            const s = String(p || '');
            if (s) out.add(s);
          };
          const visible = (field, i = undefined) => {
            try {
              if (!field || typeof field !== 'object') return false;
              if (field.hidden) return false;
              if (typeof field.showIf !== 'function') return true;
              return !!field.showIf({ settings: st, programs }, i);
            } catch (e) { return true; }
          };

          // Simple tabs: fields with explicit path
          for (const tabKey of Object.keys(schema)) {
            const sec = schema[tabKey] || {};
            const fields = sec.fields;
            if (!Array.isArray(fields)) continue;
            // sensors/inputs are repeats; handled separately
            if (tabKey === 'sensors' || tabKey === 'inputs') continue;
            for (const f of fields) {
              if (!visible(f)) continue;
              addPath(f.path);
            }
          }

          // Sensors repeat (based on rendered sensor count)
          try {
            const sec = schema.sensors || {};
            const fields = sec.fields || [];
            const n = Alpine.store('deviceStatus')?.tempSensors?.length || 0;
            for (let i = 0; i < n; i++) {
              for (const f of fields) {
                if (!visible(f, i)) continue;
                const path = String(f?.pathTmpl || '').replaceAll('{i}', String(i));
                addPath(path);
              }
            }
          } catch (e2) {}

          // Inputs repeat (based on rendered inputs count)
          try {
            const sec = schema.inputs || {};
            const fields = sec.fields || [];
            const n = Alpine.store('deviceStatus')?.inputs?.length || 0;
            for (let i = 0; i < n; i++) {
              for (const f of fields) {
                if (!visible(f, i)) continue;
                const path = String(f?.pathTmpl || '').replaceAll('{i}', String(i));
                addPath(path);
              }
            }
          } catch (e3) {}

          // Triggers: inputFields/tempFields use bind
          try {
            const sec = schema.triggers || {};
            const inFields = Array.isArray(sec.inputFields) ? sec.inputFields : [];
            const tFields = Array.isArray(sec.tempFields) ? sec.tempFields : [];
            const sFields = Array.isArray(sec.scheduleFields) ? sec.scheduleFields : [];

            const inLen = st.input_triggers?.length || 0;
            for (let idx = 0; idx < inLen; idx++) {
              for (const f of inFields) {
                if (f && f.hidden) continue;
                const bind = String(f?.bind || '');
                if (!bind) continue;
                addPath(`input_triggers.${idx}.${bind}`);
              }
            }
            const tLen = st.temperature_triggers?.length || 0;
            for (let idx = 0; idx < tLen; idx++) {
              for (const f of tFields) {
                if (f && f.hidden) continue;
                const bind = String(f?.bind || '');
                if (!bind) continue;
                addPath(`temperature_triggers.${idx}.${bind}`);
              }
            }
            const sLen = st.schedule_triggers?.length || 0;
            for (let idx = 0; idx < sLen; idx++) {
              for (const f of sFields) {
                if (f && f.hidden) continue;
                const bind = String(f?.bind || '');
                if (!bind) continue;
                addPath(`schedule_triggers.${idx}.${bind}`);
              }
            }
          } catch (e4) {}
        } catch (e) {}
        return Array.from(out);
      },
      subtabHasErrors(subtabKey) {
        const k = String(subtabKey || '');
        if (!k) return false;
        const prefixMap = {
          wifi: ['wifi.'],
          gsm: ['gsm.'],
          mqtt: ['mqtt.'],
          vehicle: ['vehicle.'],
          thermostat: ['thermostat.'],
          battery: ['battery_saver.'],
          time: ['time.'],
          sensors: ['sensors.'],
          inputs: ['inputs.'],
          triggers: ['input_triggers.', 'temperature_triggers.', 'schedule_triggers.']
        };
        const prefixes = prefixMap[k] || [];
        if (!prefixes.length) return false;
        const client = this._clientErrors || {};
        for (const p of Object.keys(client)) {
          for (const pref of prefixes) if (p.startsWith(pref)) return true;
        }
        const serverErrs = Alpine.store('settings')?.validationErrors || [];
        for (const e of serverErrs) {
          const p = String(e?.field || '');
          if (!p) continue;
          for (const pref of prefixes) if (p.startsWith(pref)) return true;
        }
        return false;
      },
      validateAllSettings() {
        if (!FRONTEND_VALIDATION_ENABLED) return [];
        const out = [];
        try {
          const v = Alpine.store('settingsValidator');
          const st = Alpine.store('settings') || {};
          if (!v || !v.loaded) {
            const reason = v ? String(v._lastInitError || '') : '';
            return [{ field: '__schema__', message: reason ? `Валидация недоступна: ${reason}` : 'Валидация недоступна (схема не загружена)' }];
          }

          const data = st.getConfigForSave?.() || {};
          const errorsByPath = v.validateData(data).errorsByPath || {};
          const visiblePaths = this._collectVisiblePaths();

          // Clear out-of-UI errors, keep only what UI actually renders.
          const keep = new Set(visiblePaths);
          for (const p of Object.keys(this._clientErrors || {})) {
            if (!keep.has(p)) this._setClientError(p, '');
          }

          for (const p of visiblePaths) {
            const msg = errorsByPath[p]?.[0];
            this._setClientError(p, msg ? String(msg) : '');
            if (msg) out.push({ field: p, message: String(msg) });
          }
        } catch (e) {}
        return out;
      },
      fieldInputClass(path) {
        const p = String(path || '');
        return {
          'field-changed': Alpine.store('settings')?.isPathChanged?.(p),
          'field-invalid': Alpine.store('settings')?.isPathInvalid?.(p),
        };
      },
      fieldSwitchClass(path, on) {
        const p = String(path || '');
        return {
          on: !!on,
          'field-changed': Alpine.store('settings')?.isPathChanged?.(p),
          'field-invalid': Alpine.store('settings')?.isPathInvalid?.(p),
        };
      }
    });
  };
})();

