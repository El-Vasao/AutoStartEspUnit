// Stores registry entrypoint
(function () {
  const APP = (window.APP = window.APP || {});
  APP.stores = APP.stores || {};

  APP.stores.init = function initStores(Alpine) {
    // Register in deterministic order
    const log = APP.utils?.log;
    function safe(name, fn) {
      try { fn?.(Alpine); } catch (e) { log?.warn?.(`store init failed: ${name}`, e); }
    }
    safe('settingsUiSchema', APP.stores.registerSettingsUiSchemaStore);
    safe('settingsValidator', APP.stores.registerSettingsValidatorStore);
    safe('programValidator', APP.stores.registerProgramValidatorStore);
    safe('settingsForm', APP.stores.registerSettingsFormStore);
    safe('programForm', APP.stores.registerProgramFormStore);
    safe('uiToast', APP.stores.registerUiToastStore);
    safe('uiBusy', APP.stores.registerUiBusyModalStore);
    safe('uiFormat', APP.stores.registerUiFormattersStore);
    safe('uiState', APP.stores.registerUiStateStore);
    safe('uiDialog', APP.stores.registerUiConfirmDialogStore);
    safe('deviceStatus', APP.stores.registerDeviceStatusStore);
    safe('uiLogs', APP.stores.registerUiLogConsoleStore);
    safe('programStepsUiSchema', APP.stores.registerProgramStepsUiSchemaStore);
    safe('programs', APP.stores.registerProgramsStore);
    safe('settings', APP.stores.registerSettingsStore);
    safe('deviceOta', APP.stores.registerDeviceOtaUpdateStore);
    safe('deviceRuntime', APP.stores.registerDeviceRuntimeControlsStore);
    safe('deviceMaintenance', APP.stores.registerDeviceMaintenanceActionsStore);
  };
})();

