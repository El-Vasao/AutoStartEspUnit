// Frontend↔Device contract (API endpoints + basic response shapes)
// Keep this file dependency-free: it must be safe to load early.
(function () {
  const APP = (window.APP = window.APP || {});

  APP.contract = APP.contract || {};

  APP.contract.api = {
    endpoints: {
      status: '/status',
      events: '/events',
      /** Inventory + live snapshot in one response. */
      bootstrap: '/bootstrap',
      uiSession: '/ui/session',

      configGet: '/config/get',
      configSave: '/config/save',
      configReset: '/config/reset',

      programsList: '/programs',
      /** POST: application/x-www-form-urlencoded — run=(id программы) или reset=1 */
      programsPost: '/programs',
      program: '/program',

      reboot: '/reboot',
      upload: '/upload',
      otaStart: '/ota/start',

      /** POST body: op=thermostat|batterysaver|input|trigger_input_id|trigger_temp_id (+ id/enabled) */
      runtime: '/runtime',
    },

    timeoutsMs: {
      fast: 4000,
      default: 8000,
      flashCommit: 12000,
      ota: 30000,
      /** Optional gap after device HTTP (ESP32-C3 SoftAP: usually 0). */
      deviceRequestGapMs: 0,
      /** Pause between init phases (bootstrap → schemas → data → SSE). */
      initPhaseGapMs: 0,
      /** Pause before EventSource after checklist HTTP. */
      sseStartDelayMs: 0,
      /** Max concurrent SoftAP HTTP (was 1 on ESP8266). */
      deviceFetchMaxConcurrent: 2,
      /** Max wait for mode+hardware while UI stays locked; then degraded unlock. */
      sseInitDeadlineMs: 12000,
      /** Alias for startEvents wait (same budget). */
      ssePanelReadyTimeoutMs: 12000,
    }
  };

  // Minimal expected shape for /status. This is NOT a validator, only documentation + defaults.
  APP.contract.statusDefaults = {
    mode: '—',
    version: '—',
    uptime: 0,
    freeHeap: null,
    lastError: '—',
    relays: [],
    inputs: [],
    inputFrequencies: [],
    inputsEnabled: [],
    tempSensors: [],
    voltage: null,
    lastProgramName: '—',
    runtime: {},
    flashCommit: { pending: false, lastOp: 'none', lastOk: true, lastMillis: 0 },
  };
})();
