// device maintenance actions store (misc UI actions)
(function () {
  const APP = (window.APP = window.APP || {});
  APP.stores = APP.stores || {};

  APP.stores.registerDeviceMaintenanceActionsStore = function registerDeviceMaintenanceActionsStore(Alpine) {
    Alpine.store('deviceMaintenance', {
      performReboot() {
        Alpine.store('uiDialog').show({
          title: 'Перезагрузка',
          message: 'Перезагрузить устройство?',
          onConfirm: async () => {
            try {
              // Soft UI guard: reboot/reset/OTA are assumed safe only in trusted network (device AP).
              // If user enabled debug mode, we still show confirmation above.
              await APP.api.apiJson(APP.api.endpoints?.reboot || '/reboot', { method: 'POST' });
              Alpine.store('uiNotification').info('Перезагрузка…');
            } catch (e) {
              Alpine.store('uiNotification').error('Ошибка сети');
            }
          }
        });
      },
      performModemReboot() {
        Alpine.store('uiDialog').show({
          title: 'Перезагрузка модема',
          message: 'Перезагрузить модем?',
          onConfirm: async () => {
            try {
              await APP.api.apiJson('/modem/reboot', { method: 'POST' });
              Alpine.store('uiNotification').info('Перезагрузка модема…');
            } catch (e) {
              Alpine.store('uiNotification').error('Ошибка сети');
            }
          }
        });
      },
      resetBaseConfig() {
        Alpine.store('uiDialog').show({
          title: 'Сброс настроек',
          message: 'Сбросить настройки к заводским? Вернуть их нельзя.',
          onConfirm: async () => {
            try {
              const res = await APP.api.runFlashWrite({
                Alpine,
                busyTitle: 'Сброс',
                busyMessage: 'Сброс настроек…',
                expectedLastOp: 'reset_config',
                timeoutMsCommit: 8000,
                request: async () => await APP.api.apiJson(APP.api.endpoints?.configReset || '/config/reset', { method: 'POST' }),
                onCommitOk: async () => {
                  Alpine.store('uiNotification').success('Настройки сброшены. Нужна перезагрузка.');
                  Alpine.store('uiBusy').showOk({
                    title: 'Сброс выполнен',
                    message: 'Настройки сброшены. Нужна перезагрузка.',
                    okLabel: 'OK'
                  });
                }
              });
              if (res.busy409) return;
              if (!res.ok && !res.timeout) Alpine.store('uiNotification').error('Ошибка сброса');
            } catch (e) {}
          }
        });
      },
      resetAllPrograms() {
        Alpine.store('programs').resetAll();
      },
      otaFileSelected(e) {
        Alpine.store('deviceOta').setFile(e.target.files[0]);
      },
      startOTA() {
        Alpine.store('deviceOta').start();
      }
    });
  };
})();
