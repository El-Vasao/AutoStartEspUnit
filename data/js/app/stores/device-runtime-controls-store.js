// device runtime controls store (panel gestures actions + programEditorDirty)
(function () {
  const APP = (window.APP = window.APP || {});
  APP.stores = APP.stores || {};

  APP.stores.registerDeviceRuntimeControlsStore = function registerDeviceRuntimeControlsStore(Alpine) {
    function runtimeEndpoint() {
      return APP.api?.endpoints?.runtime || APP.contract?.api?.endpoints?.runtime || '/runtime';
    }
    Alpine.store('deviceRuntime', {
      programEditorDirty: false,
      inputEnabled: (index) => Alpine.store('deviceStatus').inputsEnabled?.[index] || false,
      inputTriggerEnabled: (index) => {
        const rt = Alpine.store('deviceStatus')?.runtime || {};
        const settings = Alpine.store('settings') || {};
        const id = Number(settings.input_triggers?.[index]?.id) || 0;
        const map = rt.inputTriggersById;
        if (!id || !map || typeof map !== 'object') return false;
        return !!map[String(id)];
      },
      tempTriggerEnabled: (index) => {
        const rt = Alpine.store('deviceStatus')?.runtime || {};
        const settings = Alpine.store('settings') || {};
        const id = Number(settings.temperature_triggers?.[index]?.id) || 0;
        const map = rt.tempTriggersById;
        if (!id || !map || typeof map !== 'object') return false;
        return !!map[String(id)];
      },
      get thermostatEnabled() {
        const rt = Alpine.store('deviceStatus')?.runtime || {};
        return !!rt.thermostat;
      },
      get thermostatAllowed() {
        const rt = Alpine.store('deviceStatus')?.runtime || {};
        return !!rt.thermostatAllowed;
      },
      get batterySaverEnabled() {
        const rt = Alpine.store('deviceStatus')?.runtime || {};
        return !!rt.batterySaver;
      },
      get batterySaverAllowed() {
        const rt = Alpine.store('deviceStatus')?.runtime || {};
        return !!rt.batterySaverAllowed;
      },
      get batterySaverAttemptsToday() {
        const rt = Alpine.store('deviceStatus')?.runtime || {};
        const n = Number(rt.batterySaverAttemptsToday);
        return Number.isFinite(n) ? n : 0;
      },

      handleLongPress(type, index) {
        switch (type) {
          case 'input': return this.toggleInput(index);
          case 'inputTrigger': return this.toggleInputTrigger(index);
          case 'tempTrigger': return this.toggleTempTrigger(index);
          case 'thermostat': return this.toggleThermostat();
          case 'batterySaver': return this.toggleBatterySaver();
        }
      },

      async handleDoubleTap(type, index) {
        try {
          const ui = Alpine.store('uiState');
          const settings = Alpine.store('settings');
          ui.setActiveTab('settings');
          await settings.load();

          const ids = APP.domIds?.settings || {};
          const scrollTo = (id) => {
            try {
              const el = document.getElementById(String(id || ''));
              if (!el) {
                Alpine.store('uiToast')?.show?.('Пункт не найден', 'warning');
                return;
              }
              el.scrollIntoView({ block: 'start', behavior: 'smooth' });
            } catch (e) {}
          };

          if (type === 'sensor') {
            ui.activeSubTab = 'sensors';
            const st = Alpine.store('deviceStatus');
            const reading = st?.tempSensors?.[index];
            const sensors = Array.isArray(settings?.sensors) ? settings.sensors : [];
            const sid = String(reading?.id || '').trim();
            let slot = (sid && sid !== 'null')
              ? sensors.findIndex((s) => s && String(s.id || '').trim() === sid)
              : -1;
            if (slot < 0) {
              const rom = String(st?.temperatureSensorRoms?.[index] || '').trim();
              if (rom) slot = sensors.findIndex((s) => s && String(s.rom || '').trim() === rom);
            }
            if (slot >= 0) {
              queueMicrotask(() => scrollTo(ids.sensorItem ? ids.sensorItem(slot) : `settings-sensor-${slot}`));
            }
            return;
          }
          if (type === 'input') {
            ui.activeSubTab = 'inputs';
            queueMicrotask(() => scrollTo(ids.inputItem ? ids.inputItem(index) : `settings-input-${index}`));
            return;
          }
          if (type === 'inputTrigger') {
            ui.activeSubTab = 'triggers';
            queueMicrotask(() => scrollTo(ids.inputTriggerItem ? ids.inputTriggerItem(index) : `settings-in-trigger-${index}`));
            return;
          }
          if (type === 'tempTrigger') {
            ui.activeSubTab = 'triggers';
            queueMicrotask(() => scrollTo(ids.tempTriggerItem ? ids.tempTriggerItem(index) : `settings-temp-trigger-${index}`));
            return;
          }
          if (type === 'thermostat') {
            ui.activeSubTab = 'thermostat';
            queueMicrotask(() => scrollTo(ids.thermostatCard || 'settings-thermostat'));
            return;
          }
          if (type === 'batterySaver') {
            ui.activeSubTab = 'battery';
            queueMicrotask(() => scrollTo(ids.batteryCard || 'settings-battery'));
            return;
          }
        } catch (e) {}
      },

      async toggleInput(index) {
        const current = Alpine.store('deviceStatus').inputsEnabled?.[index] || false;
        const newState = !current;
        try {
          const settings = Alpine.store('settings') || {};
          // Runtime API ждёт номер шёлка 1…n (не строку IN1 и не старый 1001).
          const raw = String(settings.inputs?.[index]?.id || '').trim();
          const m = raw.match(/^(?:IN|K|T)(\d+)$/i);
          const id = m ? Number(m[1]) : (Number(raw) || (Number(index || 0) + 1));
          const { ok } = await APP.api.apiJson(runtimeEndpoint(), {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `op=input_id&id=${id}&enabled=${newState}`
          });
          if (ok) {
            if (!Alpine.store('deviceStatus').inputsEnabled) Alpine.store('deviceStatus').inputsEnabled = [];
            Alpine.store('deviceStatus').inputsEnabled[index] = newState;
            Alpine.store('uiToast').success(newState ? 'Вкл' : 'Выкл');
          } else Alpine.store('uiToast').error('Ошибка');
        } catch (e) {
          Alpine.store('uiToast').error('Ошибка сети');
        }
      },

      async toggleInputTrigger(index) {
        const current = this.inputTriggerEnabled(index);
        try {
          const settings = Alpine.store('settings') || {};
          const row = settings.input_triggers?.[index] || {};
          const id = Number(row.id) || 0;
          if (!id) { Alpine.store('uiToast').error('Триггер не задан'); return; }
          const { ok } = await APP.api.apiJson(runtimeEndpoint(), {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `op=trigger_input_id&id=${id}&enabled=${!current}`
          });
          if (ok) {
            const st = Alpine.store('deviceStatus');
            if (!st.runtime || typeof st.runtime !== 'object') st.runtime = {};
            if (!st.runtime.inputTriggersById || typeof st.runtime.inputTriggersById !== 'object') st.runtime.inputTriggersById = {};
            st.runtime.inputTriggersById[String(id)] = !current;
            Alpine.store('uiToast').success(!current ? 'Вкл' : 'Выкл');
          } else Alpine.store('uiToast').error('Ошибка');
        } catch (e) {
          Alpine.store('uiToast').error('Ошибка сети');
        }
      },

      async toggleTempTrigger(index) {
        const current = this.tempTriggerEnabled(index);
        try {
          const settings = Alpine.store('settings') || {};
          const row = settings.temperature_triggers?.[index] || {};
          const id = Number(row.id) || 0;
          if (!id) { Alpine.store('uiToast').error('Триггер не задан'); return; }
          const { ok } = await APP.api.apiJson(runtimeEndpoint(), {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `op=trigger_temp_id&id=${id}&enabled=${!current}`
          });
          if (ok) {
            const st = Alpine.store('deviceStatus');
            if (!st.runtime || typeof st.runtime !== 'object') st.runtime = {};
            if (!st.runtime.tempTriggersById || typeof st.runtime.tempTriggersById !== 'object') st.runtime.tempTriggersById = {};
            st.runtime.tempTriggersById[String(id)] = !current;
            Alpine.store('uiToast').success(!current ? 'Вкл' : 'Выкл');
          } else Alpine.store('uiToast').error('Ошибка');
        } catch (e) {
          Alpine.store('uiToast').error('Ошибка сети');
        }
      },

      async toggleThermostat() {
        const current = this.thermostatEnabled;
        const turningOn = !current;
        if (turningOn && !this.thermostatAllowed) {
          Alpine.store('uiToast').error('Сначала заполните настройки термостата');
          return;
        }
        try {
          const { ok, status } = await APP.api.apiJson(runtimeEndpoint(), {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `op=thermostat&enabled=${turningOn}`
          });
          if (ok) {
            const st = Alpine.store('deviceStatus');
            if (!st.runtime || typeof st.runtime !== 'object') st.runtime = {};
            st.runtime.thermostat = turningOn;
            Alpine.store('uiToast').success(turningOn ? 'Вкл' : 'Выкл');
          } else if (status === 422) {
            Alpine.store('uiToast').error('Сначала заполните настройки термостата');
          } else Alpine.store('uiToast').error('Ошибка');
        } catch (e) {
          Alpine.store('uiToast').error('Ошибка сети');
        }
      },

      async toggleBatterySaver() {
        const current = this.batterySaverEnabled;
        const turningOn = !current;
        if (turningOn && !this.batterySaverAllowed) {
          Alpine.store('uiToast').error('Сначала заполните настройки защиты АКБ');
          return;
        }
        try {
          const { ok, status } = await APP.api.apiJson(runtimeEndpoint(), {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `op=batterysaver&enabled=${turningOn}`
          });
          if (ok) {
            const st = Alpine.store('deviceStatus');
            if (!st.runtime || typeof st.runtime !== 'object') st.runtime = {};
            st.runtime.batterySaver = turningOn;
            Alpine.store('uiToast').success(turningOn ? 'Вкл' : 'Выкл');
          } else if (status === 422) {
            Alpine.store('uiToast').error('Сначала заполните настройки защиты АКБ');
          } else Alpine.store('uiToast').error('Ошибка');
        } catch (e) {
          Alpine.store('uiToast').error('Ошибка сети');
        }
      }
    });
  };
})();

