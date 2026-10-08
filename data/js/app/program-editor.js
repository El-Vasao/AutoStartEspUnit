// program editor component (Alpine.data)
(function () {
  const FRONTEND_VALIDATION_ENABLED = true;

  document.addEventListener('alpine:init', () => {
    try {
      window.Alpine.data('programEditor', () => ({
        form: { id: 0, name: '', steps: [] },
        errors: {},
        selectedActionValue: '',
        saving: false,
        _baseline: '',
        _baselineObj: null,
        _dirtyTimer: null,
        _validateTimer: null,

        initEditor() {
          const prog = this.$store.programs.current;
          if (prog) {
            this.form = {
              id: prog.id,
              name: prog.name,
              steps: prog.steps.map(s => this.stepFromServer(s))
            };
            this.coerceFormInPlace();
            this._baseline = window.APP.utils.stableStringify(this.form);
            try { this._baselineObj = JSON.parse(this._baseline); } catch (e) { this._baselineObj = null; }
            window.Alpine.store('deviceRuntime').programEditorDirty = false;

            // Allow app-level navigation guard to trigger save from a dialog.
            // Best-effort: only one editor instance is expected.
            try {
              window.__programEditorSave = () => { try { this.save(); } catch (e) {} };
              window.__programEditorRevert = () => { try { this.revertForm(); } catch (e) {} };
            } catch (e) {}
          }
        },

        stripStepForCompare(step) {
          if (!step) return {};
          const { actionDef, category, ...rest } = step;
          return rest;
        },

        /** Новый шаг — заливка целиком на `.step-card` (см. stepCardClass), без подсветки отдельных полей. */
        stepFieldTone(index, key) {
          if (this.$store.programs.editingMode === 'new') return '';
          const bSteps = this._baselineObj?.steps;
          const n = bSteps?.length ?? 0;
          if (index >= n) return '';
          const cur = this.stripStepForCompare(this.form.steps[index] || {});
          const old = this.stripStepForCompare(bSteps[index] || {});
          if (!window.APP.utils.fastEqual(cur?.[key], old?.[key])) return 'field-changed';
          return '';
        },

        stepCardClass(index) {
          if (this.$store.programs.editingMode === 'new') return '';
          const n = this._baselineObj?.steps?.length ?? 0;
          return index >= n ? 'step-card-new' : '';
        },

        revertForm() {
          if (!this._baseline) return;
          try {
            const snap = JSON.parse(this._baseline);
            this.form = snap;
            this.form.steps.forEach(step => {
            const cat = this.$store.programStepsUiSchema.actionCategories.find(c =>
                c.actions.some(a => a.value === step.action)
              );
              if (cat) {
                step.category = cat.name;
                step.actionDef = cat.actions.find(a => a.value === step.action);
              }
            });
            try { this._baselineObj = JSON.parse(this._baseline); } catch (e) {}
            window.Alpine.store('deviceRuntime').programEditorDirty = false;
          } catch (e) {}
        },

        stepFromServer(s) {
          const cat = this.$store.programStepsUiSchema.actionCategories.find(c =>
            c.actions.some(a => a.value === s.action)
          );
          const actionDef = cat ? cat.actions.find(a => a.value === s.action) : null;
          const base = { step: s.step, action: s.action };
          if (cat) {
            base.category = cat.name;
            base.actionDef = actionDef;
          }

          const params = actionDef?.params || [];
          for (const p of params) {
            if (p === 'relay_id') base.relay_id = (s.relay_id !== undefined) ? (Number(s.relay_id) || 0) : 0;
            else if (p === 'ms') base.ms = s.ms ? (s.ms / 1000) : 0;
            else if (p === 'timeout_ms') base.timeout = s.timeout_ms ? (s.timeout_ms / 1000) : 0;
            else if (p === 'retries') base.retries = (s.retries || 1);
            else if (p === 'input_id') base.input_id = (s.input_id !== undefined) ? (Number(s.input_id) || 0) : 0;
            else if (p === 'sensor_id') base.sensor_id = (s.sensor_id !== undefined) ? (Number(s.sensor_id) || 0) : 0;
            else if (p === 'sensor_name') base.sensor_name = (s.sensor_name !== undefined) ? String(s.sensor_name || '') : '';
            else if (p === 'input_trigger_id') base.input_trigger_id = (s.input_trigger_id !== undefined) ? (Number(s.input_trigger_id) || 0) : 0;
            else if (p === 'temp_trigger_id') base.temp_trigger_id = (s.temp_trigger_id !== undefined) ? (Number(s.temp_trigger_id) || 0) : 0;
            else if (p === 'program_id') base.program_id = (s.program_id || 0);
            else if (p === 'expected_state') base.expected_state = (s.expected_state !== undefined) ? s.expected_state : 1;
            else if (p === 'comparison') base.comparison = (s.comparison || 'above');
            else if (p === 'threshold') base.threshold = (s.threshold || 0);
            else if (p === 'engine_state') base.engine_state = (s.engine_state !== undefined) ? s.engine_state : 1;
            else if (p === 'timeout_action') base.timeout_action = (s.timeout_action || 0);
            else if (p === 'skip_count') {
              // WYSIWYG: skip_count is shown only when timeout_action==1
              const ta = (s.timeout_action || 0);
              if (ta === 1) base.skip_count = (s.skip_count || 0);
            }
            else if (p === 'message') base.message = (s.message !== undefined) ? String(s.message || '') : '';
          }

          return base;
        },

        stepToServer(step) {
          const result = { step: step.step, action: step.action };

          // Source of truth: what UI shows for this action.
          let params = step?.actionDef?.params;
          if (!Array.isArray(params)) {
            try {
              const cat = this.$store.programStepsUiSchema.actionCategories.find(c => c.actions.some(a => a.value === step.action));
              const def = cat ? cat.actions.find(a => a.value === step.action) : null;
              params = def?.params || [];
            } catch (e) { params = []; }
          }

          const add = (k, v) => {
            if (v === undefined) return;
            result[k] = v;
          };

          const toInt = (v, fallback = 0) => {
            const n = Number(v);
            if (!Number.isFinite(n)) return fallback;
            return Math.trunc(n);
          };

          for (const p of params) {
            if (p === 'relay_id') add('relay_id', toInt(step.relay_id, 0));
            else if (p === 'ms') add('ms', step.ms ? Math.round(step.ms * 1000) : 0);
            else if (p === 'timeout_ms') add('timeout_ms', step.timeout ? Math.round(step.timeout * 1000) : 0);
            else if (p === 'retries') add('retries', toInt(step.retries, 1) || 1);
            else if (p === 'input_id') add('input_id', toInt(step.input_id, 0));
            else if (p === 'sensor_id') add('sensor_id', (step.sensor_id !== undefined) ? (Number(step.sensor_id) || 0) : 0);
            else if (p === 'sensor_name') add('sensor_name', (step.sensor_name !== undefined) ? String(step.sensor_name || '') : '');
            else if (p === 'input_trigger_id') add('input_trigger_id', (step.input_trigger_id !== undefined) ? (Number(step.input_trigger_id) || 0) : 0);
            else if (p === 'temp_trigger_id') add('temp_trigger_id', (step.temp_trigger_id !== undefined) ? (Number(step.temp_trigger_id) || 0) : 0);
            else if (p === 'program_id') add('program_id', toInt(step.program_id, 0));
            else if (p === 'expected_state') add('expected_state', toInt(step.expected_state, 1));
            else if (p === 'comparison') add('comparison', step.comparison);
            else if (p === 'threshold') add('threshold', Number(step.threshold) || 0);
            else if (p === 'engine_state') add('engine_state', toInt(step.engine_state, 1));
            else if (p === 'timeout_action') add('timeout_action', toInt(step.timeout_action, 0));
            else if (p === 'skip_count') {
              // WYSIWYG: skip_count is shown only when timeout_action==1
              if (toInt(step.timeout_action, 0) === 1) add('skip_count', toInt(step.skip_count, 0));
            }
            else if (p === 'message') add('message', (step.message !== undefined) ? String(step.message || '') : '');
          }

          return result;
        },

        addStepFromSelect() {
          if (!this.selectedActionValue) return;
          let foundAction = null;
          let foundCategory = null;
          for (let cat of this.$store.programStepsUiSchema.actionCategories) {
            const act = cat.actions.find(a => a.value === this.selectedActionValue);
            if (act) {
              foundAction = act;
              foundCategory = cat;
              break;
            }
          }
          if (!foundAction) return;

          const p = this.$store.programStepsUiSchema?.params || {};
          const d = (k, fallback) => (p?.[k] && p[k].default !== undefined) ? p[k].default : fallback;

          const newStep = {
            step: this.form.steps.length + 1,
            action: foundAction.value,
            category: foundCategory.name,
            actionDef: foundAction,
          };

          const params = foundAction.params || [];
          for (const key of params) {
            if (key === 'timeout_ms') newStep.timeout = d('timeout_ms', 0);
            else newStep[key] = d(key, undefined);
          }

          this.form.steps.push(newStep);
          this.selectedActionValue = '';
          this.markDirty();
        },

        removeStep(index) {
          this.form.steps.splice(index, 1);
          this.form.steps.forEach((s, i) => s.step = i + 1);
          this.markDirty();
        },

        moveStep(index, direction) {
          const newIdx = index + direction;
          if (newIdx < 0 || newIdx >= this.form.steps.length) return;
          [this.form.steps[index], this.form.steps[newIdx]] = [this.form.steps[newIdx], this.form.steps[index]];
          this.form.steps.forEach((s, i) => s.step = i + 1);
          this.markDirty();
        },

        validate() {
          // Keep the same object reference so field rows don't capture stale `errors`.
          try {
            if (!this.errors || typeof this.errors !== 'object') this.errors = {};
            for (const k of Object.keys(this.errors)) delete this.errors[k];
          } catch (e) {
            this.errors = {};
          }
          if (!FRONTEND_VALIDATION_ENABLED) return true;
          // Validate the exact payload we are going to send to the device.
          this.coerceFormInPlace();
          const program = {
            id: this.form.id,
            name: this.form.name.trim(),
            steps: this.form.steps.map((s, idx) => {
              const step = this.stepToServer(s);
              step.step = idx + 1;
              return step;
            })
          };

          const pv = this.$store.programValidator;
          if (!pv) return true;

          // Lazy-load schema/ajv (best-effort). If it fails, we do not hard-block saving.
          pv.load?.();
          const res = pv.validateProgramData?.(program) || { ok: true, errorsByPath: {} };
          if (res.ok) return true;

          const errs = res.errorsByPath || {};
          const first = (arr) => Array.isArray(arr) && arr.length ? String(arr[0]) : 'Неверное значение';

          // Map Ajv dotted paths to existing UI error keys.
          for (const p of Object.keys(errs)) {
            if (p === 'id') this.errors.id = first(errs[p]);
            else if (p === 'name') this.errors.name = first(errs[p]);
            else if (p === 'steps') this.errors.steps = first(errs[p]);
            else if (p.startsWith('steps.')) {
              const m = /^steps\.(\d+)\.(.+)$/.exec(p);
              if (!m) continue;
              const i = Number(m[1]);
              const k = String(m[2] || '');
              if (!Number.isFinite(i) || i < 0) continue;
              this.errors[`step_${i}_${k}`] = first(errs[p]);
            }
          }

          // Fallback: if nothing mapped, show a generic message.
          if (!Object.keys(this.errors).length) {
            this.errors.steps = 'Неверные данные программы';
          }
          return false;
        },

        validateDebounced() {
          if (!FRONTEND_VALIDATION_ENABLED) return;
          try {
            if (this._validateTimer) clearTimeout(this._validateTimer);
            this._validateTimer = setTimeout(() => {
              try { this.validate(); } catch (e) {}
            }, 120);
          } catch (e) {}
        },

        async save() {
          if (!this.validate()) return;
          this.saving = true;
          this.coerceFormInPlace();
          const program = {
            id: this.form.id,
            name: this.form.name.trim(),
            steps: this.form.steps.map((s, idx) => {
              const step = this.stepToServer(s);
              step.step = idx + 1;
              return step;
            })
          };
          await this.$store.programs.save(program);
          this.saving = false;
          if (this.$store.programs.current == null) return;
          this._baseline = window.APP.utils.stableStringify(this.form);
          try { this._baselineObj = JSON.parse(this._baseline); } catch (e) {}
          window.Alpine.store('deviceRuntime').programEditorDirty = false;
        },

        markDirty() {
          clearTimeout(this._dirtyTimer);
          this._dirtyTimer = setTimeout(() => {
            try {
              this.coerceFormInPlace();
              const now = window.APP.utils.stableStringify(this.form);
              window.Alpine.store('deviceRuntime').programEditorDirty = (this._baseline && now !== this._baseline);
            } catch (e) {}
          }, 120);
        },

        deleteProgram() {
          this.$store.uiDialog.show({
            title: 'Удаление',
            message: `Удалить «${this.form.name}»?`,
            onConfirm: () => this.$store.programs.delete(this.form.id)
          });
        },

        // ----- schema-driven UI helpers -----
        paramDef(key) {
          try { return this.$store.programStepsUiSchema?.params?.[String(key || '')] || null; } catch (e) { return null; }
        },
        paramUiField(paramKey) {
          try {
            const k = String(paramKey || '');
            // Most fields come straight from schema.
            const d = this.paramDef(k) || null;
            if (d) return d;
            // Fallbacks (should be rare if schema stays in sync).
            if (k === 'timeout_ms') return { type: 'number', valueType: 'float', label: 'Таймаут (с)' };
            return { type: 'text', valueType: 'string', label: k };
          } catch (e) {
            return { type: 'text', valueType: 'string', label: String(paramKey || '') };
          }
        },
        // Maps schema param key -> actual model path in step object
        paramUiPath(paramKey) {
          const k = String(paramKey || '');
          if (k === 'timeout_ms') return 'timeout'; // UI stores seconds in `step.timeout`
          return k;
        },
        // Which key to use for change highlighting
        paramUiToneKey(paramKey) {
          const k = String(paramKey || '');
          if (k === 'timeout_ms') return 'timeout';
          return k;
        },
        // Options for programForm.fieldRow (errors + special behaviors)
        paramUiOpts(step, index, paramKey) {
          const k = String(paramKey || '');
          const opts = {
            errors: this.errors,
            errorKey: `step_${index}_${k}`,
            onChange: () => { this.markDirty(); this.validateDebounced(); }
          };
          if (k === 'timeout_ms') opts.errorKey = `step_${index}_timeout_ms`;
          if (k === 'sensor_id') {
            opts.onSelect = (v) => {
              try {
                const list = window.Alpine?.store?.('settings')?.sensors;
                let name = '';
                if (Array.isArray(list)) {
                  const hit = list.find(x => x && String(x.id) === String(v));
                  name = String(hit?.name || '').trim();
                }
                step.sensor_name = String(name || '');
              } catch (e) {}
            };
          }
          return opts;
        },

        coerceStepInPlace(step) {
          try {
            if (!step) return;
            const params = step?.actionDef?.params || [];
            for (const p of params) {
              if (p === 'timeout_ms') {
                const def = this.paramDef('timeout_ms');
                step.timeout = (APP.coerce?.byValueType ? APP.coerce.byValueType(def?.valueType, step.timeout, def?.default ?? 0) : step.timeout);
                continue;
              }
              const def = this.paramDef(p);
              if (!def) continue;
              if (step[p] === undefined) continue;
              step[p] = (APP.coerce?.byValueType ? APP.coerce.byValueType(def.valueType, step[p], def.default) : step[p]);
            }
          } catch (e) {}
        },

        coerceFormInPlace() {
          try {
            const steps = this.form?.steps || [];
            for (const s of steps) this.coerceStepInPlace(s);
          } catch (e) {}
        },
      }));
    } catch (e) {}
  });
})();

