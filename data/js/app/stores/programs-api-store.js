// programs store
(function () {
  const APP = (window.APP = window.APP || {});
  APP.stores = APP.stores || {};

  APP.stores.registerProgramsStore = function registerProgramsStore(Alpine) {
    function programsPostUrl() {
      return APP.api?.endpoints?.programsPost || APP.contract?.api?.endpoints?.programsPost || '/programs';
    }
    function programsListUrl() {
      return APP.api?.endpoints?.programsList || APP.contract?.api?.endpoints?.programsList || '/programs';
    }
    function programEndpoint() {
      return APP.api?.endpoints?.program || APP.contract?.api?.endpoints?.program || '/program';
    }
    Alpine.store('programs', {
      items: [],
      current: null,
      loading: false,
      loaded: false,
      loadOk: true,
      editingMode: null,

      get recent() {
        return this.items.slice(0, 5);
      },

      async loadList(force = false) {
        if (this.loaded && !force) return;
        if (this.loading && !force) return;
        this.loading = true;
        this.loadOk = true;
        try {
          const res = await APP.api.apiJsonWithBusyRetry(programsListUrl() + '?_=' + Date.now(), {}, { timeoutMsTotal: 12000 });
          if (!res.ok) throw new Error('Failed to load programs');
          const list = Array.isArray(res.data) ? res.data : [];
          try {
            const pv = Alpine.store('programValidator');
            pv?.load?.();
            const vr = pv?.validateProgramIndex?.(list) || { ok: true };
            if (!vr.ok) throw new Error('index schema mismatch');
            this.items = list;
          } catch (e2) {
            this.items = [];
            this.loadOk = false;
            Alpine.store('uiNotification').error('Некорректный формат списка программ');
          }
        } catch (e) {
          this.items = [];
          this.loadOk = false;
          Alpine.store('uiNotification').error('Ошибка загрузки программ');
        } finally {
          this.loading = false;
          this.loaded = true;
        }
      },

      async get(id) {
        if (this.loading) return null;
        this.loading = true;
        this.editingMode = 'edit';
        try {
          const res = await APP.api.apiJsonWithBusyRetry(`${programEndpoint()}?id=${id}`, {}, { timeoutMsTotal: 12000 });
          if (!res.ok) throw new Error(`HTTP ${res.status || 'ERR'}`);
          const pv = Alpine.store('programValidator');
          pv?.load?.();
          const vr = pv?.validateProgramData?.(res.data) || { ok: true };
          if (!vr.ok) throw new Error('program schema mismatch');
          this.current = res.data;
          return this.current;
        } catch (e) {
          this.editingMode = null;
          Alpine.store('uiNotification').error('Ошибка загрузки программы');
          return null;
        } finally {
          this.loading = false;
        }
      },

      async run(id) {
        try {
          const { ok, data } = await APP.api.apiJson(programsPostUrl(), {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `run=${id}`
          });
          if (ok && data.success) Alpine.store('uiNotification').success('Программа запущена');
          else Alpine.store('uiNotification').error('Не удалось запустить программу');
        } catch (e) {
          Alpine.store('uiNotification').error('Ошибка сети');
        }
      },

      async delete(id) {
        Alpine.store('uiDialog').show({
          title: 'Удаление программы',
          message: 'Вы уверены, что хотите удалить программу?',
          onConfirm: async () => {
            try {
              const self = this;
              const res = await APP.api.runFlashWrite({
                Alpine,
                busyTitle: 'Сохранение',
                busyMessage: 'Удаление программы…',
                expectedLastOp: 'delete_program',
                timeoutMsCommit: 8000,
                request: async () => await APP.api.apiJson(`${programEndpoint()}?id=${id}`, { method: 'DELETE' }),
                onCommitOk: async () => {
                  Alpine.store('uiNotification').success('Программа удалена');
                  await self.loadList(true);
                }
              });
              if (res.busy409) return;
              if (!res.ok && !res.timeout) Alpine.store('uiNotification').error('Ошибка при удалении');
            } catch (e) {}
          }
        });
      },

      async save(programData) {
        this.loading = true;
        try {
          // Write-hard validation: do not allow flash write without validator.
          try {
            const pv = Alpine.store('programValidator');
            pv?.load?.();
            const vr = pv?.validateProgramDataHard?.(programData) || { ok: false, errorsByPath: { __schema__: ['Валидация недоступна'] } };
            if (!vr.ok) {
              const reason = (vr.errorsByPath?.__schema__ && vr.errorsByPath.__schema__[0]) ? String(vr.errorsByPath.__schema__[0]) : 'Некорректные данные программы';
              Alpine.store('uiNotification').error(reason);
              return;
            }
          } catch (e0) {
            Alpine.store('uiNotification').error('Ошибка валидации программы');
            return;
          }

          const self = this;
          const res = await APP.api.runFlashWrite({
            Alpine,
            busyTitle: 'Сохранение',
            busyMessage: 'Сохранение программы…',
            expectedLastOp: 'save_program',
            timeoutMsCommit: 8000,
            request: async () => await APP.api.apiJson(programEndpoint(), {
              method: 'POST',
              headers: { 'Content-Type': 'application/json' },
              body: JSON.stringify(programData)
            }),
            onCommitOk: async () => {
              Alpine.store('uiNotification').success('Программа сохранена');
              self.current = null;
              self.editingMode = null;
              await self.loadList(true);
            }
          });
          if (res.busy409) return;
          if (!res.ok && !res.timeout) Alpine.store('uiNotification').error('Ошибка при сохранении');
        } finally {
          this.loading = false;
        }
      },

      newProgram() {
        let maxId = 0;
        for (const p of this.items) if (p.id > maxId) maxId = p.id;
        const nextId = maxId + 1;
        this.editingMode = 'new';
        this.current = { id: nextId, name: '', steps: [] };
      },

      async edit(id) {
        await this.get(id);
      },

      cancelEdit() {
        this.current = null;
        this.editingMode = null;
        try { Alpine.store('deviceRuntime').programEditorDirty = false; } catch (e) {}
        try { window.__programEditorSave = null; } catch (e) {}
        try { window.__programEditorRevert = null; } catch (e) {}
      },

      async resetAll() {
        Alpine.store('uiDialog').show({
          title: 'Сброс всех программ',
          message: 'Вы уверены, что хотите удалить ВСЕ программы? Это действие необратимо.',
          onConfirm: async () => {
            this.loading = true;
            try {
              const self = this;
              const res = await APP.api.runFlashWrite({
                Alpine,
                busyTitle: 'Сохранение',
                busyMessage: 'Сброс всех программ…',
                expectedLastOp: 'reset_programs',
                timeoutMsCommit: 10000,
                request: async () => await APP.api.apiJson(programsPostUrl(), {
                  method: 'POST',
                  headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                  body: 'reset=1'
                }),
                onCommitOk: async () => {
                  Alpine.store('uiNotification').success('Все программы удалены');
                  await self.loadList(true);
                }
              });
              if (res.busy409) return;
              if (!res.ok && !res.timeout) Alpine.store('uiNotification').error('Ошибка при сбросе программ');
            } finally {
              this.loading = false;
            }
          }
        });
      }
    });
  };
})();

