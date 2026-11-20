// JSON backup of saved settings and programs (System tab).
(function () {
  const APP = (window.APP = window.APP || {});
  APP.stores = APP.stores || {};

  function endpoint(key, fallback) {
    return APP.api?.endpoints?.[key] || fallback;
  }

  function downloadText(filename, text) {
    const blob = new Blob([String(text)], { type: 'application/json;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    try {
      const a = document.createElement('a');
      a.href = url;
      a.download = filename;
      a.rel = 'noopener';
      document.body.appendChild(a);
      a.click();
      a.remove();
    } finally {
      URL.revokeObjectURL(url);
    }
  }

  function takeFile(event) {
    const input = event?.target;
    const file = input?.files?.[0] || null;
    if (input) {
      try { input.blur(); } catch (e) {}
      // Очистка в том же тике change снова вызывает change на части браузеров.
      setTimeout(() => { try { input.value = ''; } catch (e) {} }, 0);
    }
    return file;
  }

  function afterDialogClosed() {
    // uiDialog.confirm() ещё не снял x-trap, пока onConfirm выполняется синхронно.
    // Следующий тик: диалог закрыт и вернул overflow, затем можно открывать «Ждите…».
    return new Promise((resolve) => setTimeout(resolve, 0));
  }

  function releaseStuckScroll() {
    const busyOpen = !!Alpine.store('uiBusy')?.open;
    const dialogOpen = !!Alpine.store('uiDialog')?.open;
    if (busyOpen || dialogOpen) return;
    const root = document.documentElement;
    if (root.style.overflow === 'hidden') root.style.overflow = '';
    if (root.style.paddingRight) root.style.paddingRight = '';
  }

  function firstSchemaError(errorsByPath) {
    const map = errorsByPath || {};
    if (map.__schema__?.[0]) return String(map.__schema__[0]);
    for (const key of Object.keys(map)) {
      if (map[key]?.[0]) return String(map[key][0]);
    }
    return 'Неверные данные';
  }

  APP.stores.registerBackupStore = function registerBackupStore(Alpine) {
    function toast(kind, message) {
      Alpine.store('uiToast')?.[kind]?.(message);
    }

    async function readFileText(file) {
      try {
        return { ok: true, text: await file.text() };
      } catch (e) {
        return { ok: false };
      }
    }

    async function postJson(url, body, { busyMessage, expectedLastOp, timeoutMsCommit, onCommitOk }) {
      return APP.api.runFlashWrite({
        Alpine,
        busyTitle: 'Импорт',
        busyMessage,
        expectedLastOp,
        timeoutMsCommit,
        request: async () => APP.api.apiJson(url, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body,
        }),
        onCommitOk,
      });
    }

    Alpine.store('backup', {
      busy: false,

      locked() {
        return !!this.busy || !!Alpine.store('deviceOta')?.started;
      },

      async exportSettings() {
        if (this.locked()) return;
        this.busy = true;
        try {
          const res = await APP.api.apiJsonWithBusyRetry(
            endpoint('configGet', '/config/get'),
            { timeoutMs: 8000 },
            { timeoutMsTotal: 12000 }
          );
          if (!res.ok || !res.data || typeof res.data !== 'object' || Array.isArray(res.data)) {
            toast('error', 'Не удалось прочитать настройки');
            return;
          }
          downloadText('autostart-settings.json', JSON.stringify(res.data, null, 2));
          toast('success', 'Настройки скачаны');
        } catch (e) {
          toast('error', 'Ошибка сети');
        } finally {
          this.busy = false;
        }
      },

      async importSettings(event) {
        const file = takeFile(event);
        if (!file || this.locked()) return;
        const read = await readFileText(file);
        if (!read.ok) {
          toast('error', 'Не удалось прочитать файл');
          return;
        }
        const parsed = APP.backupJson?.parseSettingsBackup?.(read.text);
        if (!parsed?.ok) {
          toast('error', parsed?.error || 'Модуль копии недоступен');
          return;
        }
        try { await Alpine.store('settingsValidator')?.load?.(); } catch (e) {}
        const verdict = Alpine.store('settingsValidator')?.validateDataHard?.(parsed.data)
          || { ok: false, errorsByPath: { __schema__: ['Проверка недоступна'] } };
        if (!verdict.ok) {
          toast('error', firstSchemaError(verdict.errorsByPath));
          return;
        }

        try { document.activeElement?.blur?.(); } catch (e) {}
        const self = this;
        Alpine.store('uiDialog').show({
          title: 'Импорт настроек',
          message: 'Заменить настройки на устройстве содержимым файла? Вернуть их нельзя.',
          onConfirm: async () => {
            self.busy = true;
            try {
              await afterDialogClosed();
              const result = await postJson(endpoint('configSave', '/config/save'), parsed.body, {
                busyMessage: 'Запись настроек…',
                expectedLastOp: 'save_config',
                timeoutMsCommit: 12000,
              });
              if (result.busy409) return;
              if (result.timeout) {
                Alpine.store('uiToast').setHardwareError('Нет подтверждения записи. Проверьте связь.');
                return;
              }
              if (!result.ok) {
                toast('error', 'Не удалось импортировать настройки');
                return;
              }
              try { await Alpine.store('settings')?.load?.(true); } catch (e) {}
              releaseStuckScroll();
              Alpine.store('uiBusy').showOk({
                title: 'Импорт выполнен',
                message: 'Настройки записаны. Нужна перезагрузка.',
                okLabel: 'OK'
              });
            } catch (e) {
              toast('error', 'Ошибка сети');
            } finally {
              self.busy = false;
              releaseStuckScroll();
            }
          },
        });
      },

      async exportPrograms() {
        if (this.locked()) return;
        this.busy = true;
        try {
          const programs = Alpine.store('programs');
          await programs.loadList(true);
          if (!programs.loadOk) return;
          const bodies = [];
          for (const row of programs.items) {
            const id = Number(row?.id);
            if (!Number.isInteger(id) || id < 1) continue;
            const res = await APP.api.apiJsonWithBusyRetry(
              `${endpoint('program', '/program')}?id=${id}`,
              {},
              { timeoutMsTotal: 12000 }
            );
            if (!res.ok || !res.data) {
              toast('error', `Не удалось прочитать программу ${id}`);
              return;
            }
            bodies.push(res.data);
          }
          downloadText('autostart-programs.json', JSON.stringify(bodies, null, 2));
          toast('success', bodies.length ? 'Программы скачаны' : 'Список программ пуст');
        } catch (e) {
          toast('error', 'Ошибка сети');
        } finally {
          this.busy = false;
        }
      },

      async importPrograms(event) {
        const file = takeFile(event);
        if (!file || this.locked()) return;
        const read = await readFileText(file);
        if (!read.ok) {
          toast('error', 'Не удалось прочитать файл');
          return;
        }
        const parsed = APP.backupJson?.parseProgramsBackup?.(read.text);
        if (!parsed?.ok) {
          toast('error', parsed?.error || 'Модуль копии недоступен');
          return;
        }
        try { await Alpine.store('programValidator')?.load?.(); } catch (e) {}
        for (const item of parsed.programs) {
          const verdict = Alpine.store('programValidator')?.validateProgramDataHard?.(item.data)
            || { ok: false, errorsByPath: { __schema__: ['Проверка недоступна'] } };
          if (!verdict.ok) {
            toast('error', `Программа ${item.id}: ${firstSchemaError(verdict.errorsByPath)}`);
            return;
          }
        }

        const programs = Alpine.store('programs');
        await programs.loadList(true);
        if (!programs.loadOk) return;
        const limit = APP.backupJson.checkProgramsMergeLimit(
          programs.items.map((row) => row?.id),
          parsed.programs
        );
        if (!limit.ok) {
          toast('error', limit.error);
          return;
        }

        try { document.activeElement?.blur?.(); } catch (e) {}
        const self = this;
        Alpine.store('uiDialog').show({
          title: 'Импорт программ',
          message: `Записать программы (${parsed.programs.length})? Совпадающие номера будут перезаписаны.`,
          onConfirm: async () => {
            self.busy = true;
            let wrote = 0;
            let failedId = 0;
            try {
              await afterDialogClosed();
              for (const item of parsed.programs) {
                const result = await postJson(endpoint('program', '/program'), item.body, {
                  busyMessage: `Запись программы ${item.id}…`,
                  expectedLastOp: 'save_program',
                  timeoutMsCommit: 10000,
                });
                if (result.busy409 || !result.ok) {
                  failedId = item.id;
                  break;
                }
                wrote += 1;
              }
              try { await programs.loadList(true); } catch (e) {}
              if (!failedId) {
                toast('success', `Импортировано программ: ${wrote}`);
                return;
              }
              toast('error', wrote
                ? `Остановлено на программе ${failedId} (записано ${wrote})`
                : `Не удалось записать программу ${failedId}`);
            } catch (e) {
              toast('error', 'Ошибка сети');
            } finally {
              self.busy = false;
              releaseStuckScroll();
            }
          },
        });
      },
    });
  };
})();
