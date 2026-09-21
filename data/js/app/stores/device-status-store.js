// device status store
(function () {
  const APP = (window.APP = window.APP || {});
  APP.stores = APP.stores || {};

  function hwIdsInOrder(hwMap, key) {
    try {
      const list = hwMap?.[key];
      if (!Array.isArray(list)) return [];
      return list.map(x => String(Number(x?.id) || 0)).filter(x => x !== '0');
    } catch (e) {
      return [];
    }
  }

  function projectByIdMap(hwMap, key, valueMap, mapper) {
    const ids = hwIdsInOrder(hwMap, key);
    if (!ids.length || !valueMap || typeof valueMap !== 'object') return null;
    const mapFn = (typeof mapper === 'function') ? mapper : ((v) => v);
    return ids.map(id => mapFn(valueMap[id], id));
  }

  APP.stores.registerDeviceStatusStore = function registerDeviceStatusStore(Alpine) {
    Alpine.store('deviceStatus', {
      mode: '—',
      uptime: 0,
      voltage: null,
      tempSensors: [],
      relays: [],
      inputs: [],
      inputFrequencies: [],
      inputsEnabled: [],
      runtime: {},
      temperatureSensorRoms: [],
      hwMap: null,
      // Hardware inventory snapshot (prevents select/options "blinking" on frequent SSE updates).
      hwCounts: { locked: false, relays: 0, inputs: 0, sensors: 0 },
      lastProgramName: '—',
      currentProgramName: null,
      engineRunning: false,
      programRunning: false,
      timerRemaining: 0,
      version: '—',
      freeHeap: null,
      lastError: '—',
      gsmState: '—',
      flashCommit: { pending: false, lastOp: 'none', lastOk: true, lastMillis: 0 },
      loaded: false,

      get modeBadge() {
        try {
          return APP.uiMaps?.badges?.statusMode?.(this.mode) || { text: this.mode || '—', class: 'tone-neutral' };
        } catch (e) {
          return { text: this.mode || '—', class: 'tone-neutral' };
        }
      },

      get gsmBadge() {
        try {
          return APP.uiMaps?.badges?.gsmState?.(this.gsmState) || { text: 'GSM: —', class: 'tone-neutral' };
        } catch (e) {
          return { text: 'GSM: —', class: 'tone-neutral' };
        }
      },

      get hasValidTemps() {
        return this.tempSensors.some(s => !!s && s.valid);
      },

      /** Settings/panel inventory size: prefer locked hwCounts from lite /bootstrap. */
      get sensorSlots() {
        if (this.hwCounts?.locked) return Number(this.hwCounts.sensors) || 0;
        return Array.isArray(this.tempSensors) ? this.tempSensors.length : 0;
      },
      get inputSlots() {
        if (this.hwCounts?.locked) return Number(this.hwCounts.inputs) || 0;
        return Array.isArray(this.inputs) ? this.inputs.length : 0;
      },
      get relaySlots() {
        if (this.hwCounts?.locked) return Number(this.hwCounts.relays) || 0;
        return Array.isArray(this.relays) ? this.relays.length : 0;
      },

      _seedInventoryTiles() {
        const nRelays = this.hwCounts?.relays || (Array.isArray(this.hwMap?.relays) ? this.hwMap.relays.length : 0);
        const nInputs = this.hwCounts?.inputs || (Array.isArray(this.hwMap?.inputs) ? this.hwMap.inputs.length : 0);
        const nSensors = this.hwCounts?.sensors || 0;
        if (!Array.isArray(this.relays) || this.relays.length !== nRelays) {
          this.relays = Array.from({ length: nRelays }, () => false);
        }
        if (!Array.isArray(this.inputs) || this.inputs.length !== nInputs) {
          this.inputs = Array.from({ length: nInputs }, () => false);
          this.inputFrequencies = Array.from({ length: nInputs }, () => null);
          this.inputsEnabled = Array.from({ length: nInputs }, () => true);
        }
        if (!Array.isArray(this.tempSensors) || this.tempSensors.length !== nSensors) {
          this.tempSensors = Array.from({ length: nSensors }, () => ({ id: null, valid: false, t: null, lastMs: 0 }));
        }
      },

      _clockTimer: null,
      _clockBaseline: { atPerf: 0, uptime: 0, timerRemaining: 0, programRunning: false },

      _stopClockExtrapolation() {
        if (this._clockTimer) {
          clearInterval(this._clockTimer);
          this._clockTimer = null;
        }
      },

      _applyClockBaseline(uptime, timerRemaining, programRunning) {
        this._clockBaseline = {
          atPerf: (typeof performance !== 'undefined' && performance.now) ? performance.now() : Date.now(),
          uptime: Number(uptime) || 0,
          timerRemaining: Math.max(0, Number(timerRemaining) || 0),
          programRunning: !!programRunning
        };
        this.uptime = this._clockBaseline.uptime;
        this.timerRemaining = this._clockBaseline.timerRemaining;
        this.programRunning = this._clockBaseline.programRunning;
      },

      _tickDisplayedClocks() {
        const b = this._clockBaseline;
        const t0 = b.atPerf;
        const elapsedSec = Math.floor(
          (((typeof performance !== 'undefined' && performance.now) ? performance.now() : Date.now()) - t0) / 1000
        );
        this.uptime = b.uptime + Math.max(0, elapsedSec);
        if (b.programRunning && b.timerRemaining > 0) {
          const next = b.timerRemaining - elapsedSec;
          this.timerRemaining = next > 0 ? next : 0;
          if (next <= 0) this.programRunning = false;
        } else {
          this.timerRemaining = b.timerRemaining;
          this.programRunning = b.programRunning;
        }
      },

      startClockExtrapolation() {
        this._stopClockExtrapolation();
        this._clockTimer = setInterval(() => {
          try { this._tickDisplayedClocks(); } catch (e) {}
        }, 250);
      },

      /**
       * @param {'snapshot'|'clocks'|'hardware'|'runtime'|'program'|'flash'|'mode'|'gsm'|'error'|'status'} kind
       */
      patchFromSse(kind, data) {
        if (!data || typeof data !== 'object') return;
        switch (kind) {
          case 'snapshot':
          case 'status':
            this.updateFromSSE(data);
            return;
          case 'clocks':
            if (typeof data.uptime === 'number') {
              const prevStr = APP.utils.LS.get('lastUptime', '');
              const prev = parseInt(prevStr, 10);
              const rebooted = Number.isFinite(prev) && data.uptime < prev;
              APP.utils.LS.set('lastUptime', data.uptime);
              if (rebooted) {
                APP.utils.LS.del('otaInProgress');
                if (APP.utils.LS.get('otaPostRebootGoPanel') === '1') {
                  APP.utils.LS.del('otaPostRebootGoPanel');
                  Alpine.store('uiState').setActiveTab('panel');
                  Alpine.store('uiStatusBar').flash('Обновление завершено.', 'success', 5000);
                }
              }
            }
            if (data.freeHeap !== undefined) this.freeHeap = data.freeHeap;
            if (Object.prototype.hasOwnProperty.call(data, 'lastError')) this.lastError = data.lastError || '—';
            this._applyClockBaseline(data.uptime, data.timerRemaining, data.programRunning);
            this.startClockExtrapolation();
            break;
          case 'hardware': {
            if (data.engineRunning !== undefined) this.engineRunning = !!data.engineRunning;
            if (Object.prototype.hasOwnProperty.call(data, 'voltage')) this.voltage = data.voltage;
            const relayMap = (data.relaysById && typeof data.relaysById === 'object') ? data.relaysById : null;
            if (relayMap) this.relays = projectByIdMap(this.hwMap, 'relays', relayMap, (v) => !!v) || this.relays;
            const inputMap = (data.inputsById && typeof data.inputsById === 'object') ? data.inputsById : null;
            if (inputMap) this.inputs = projectByIdMap(this.hwMap, 'inputs', inputMap, (v) => !!v) || this.inputs;
            const freqMap = (data.inputFrequenciesById && typeof data.inputFrequenciesById === 'object') ? data.inputFrequenciesById : null;
            if (freqMap)
              this.inputFrequencies = projectByIdMap(this.hwMap, 'inputs', freqMap,
                (v) => (v === null || v === undefined) ? null : v) || this.inputFrequencies;
            const enMap = (data.inputsEnabledById && typeof data.inputsEnabledById === 'object') ? data.inputsEnabledById : null;
            if (enMap) this.inputsEnabled = projectByIdMap(this.hwMap, 'inputs', enMap, (v) => !!v) || this.inputsEnabled;
            if (Array.isArray(data.tempSensors)) this.tempSensors = data.tempSensors;
            break;
          }
          case 'runtime':
            if (data.runtime && typeof data.runtime === 'object') this.runtime = data.runtime;
            break;
          case 'program':
            if (Object.prototype.hasOwnProperty.call(data, 'lastProgramName')) this.lastProgramName = data.lastProgramName || '—';
            if (Object.prototype.hasOwnProperty.call(data, 'currentProgramName')) this.currentProgramName = data.currentProgramName;
            if (data.timerRemaining !== undefined) this.timerRemaining = data.timerRemaining;
            if (data.programRunning !== undefined) this.programRunning = !!data.programRunning;
            break;
          case 'flash':
            if (data.flashCommit) this.flashCommit = data.flashCommit;
            break;
          case 'mode':
            if (data.mode) this.mode = data.mode;
            break;
          case 'gsm':
            if (Object.prototype.hasOwnProperty.call(data, 'gsmState')) this.gsmState = data.gsmState || '—';
            break;
          case 'error':
            if (data.lastError) this.lastError = data.lastError;
            break;
          default:
            break;
        }
        this.loaded = true;
      },

      applyBootstrap(data) {
        if (!data || typeof data !== 'object') return;
        if (data.version) this.version = data.version;
        const hc = data.hwCounts;
        if (hc && typeof hc === 'object') {
          const relays = Number.isFinite(hc.relays) ? hc.relays : 0;
          const inputs = Number.isFinite(hc.inputs) ? hc.inputs : 0;
          const sensors = Number.isFinite(hc.sensors) ? hc.sensors : 0;
          this.hwCounts = { locked: true, relays, inputs, sensors };
        }
        if (data.hwMap && typeof data.hwMap === 'object') this.hwMap = data.hwMap;
        if (Array.isArray(data.temperatureSensorRoms)) this.temperatureSensorRoms = data.temperatureSensorRoms;
        // Seed tiles from hwCounts; apply live snapshot when present in the same /bootstrap.
        this._seedInventoryTiles();
        this.loaded = true;
        if (data.live && typeof data.live === 'object') this.patchFromSse('snapshot', data.live);
      },

      updateFromSSE(data) {
        // Reboot detection based on uptime going backwards.
        if (typeof data.uptime === 'number') {
          const prevStr = APP.utils.LS.get('lastUptime', '');
          const prev = parseInt(prevStr, 10);
          const rebooted = Number.isFinite(prev) && data.uptime < prev;
          APP.utils.LS.set('lastUptime', data.uptime);

          if (rebooted) {
            APP.utils.LS.del('otaInProgress');

            if (APP.utils.LS.get('otaPostRebootGoPanel') === '1') {
              APP.utils.LS.del('otaPostRebootGoPanel');
              Alpine.store('uiState').setActiveTab('panel');
              Alpine.store('uiStatusBar').flash('Обновление завершено.', 'success', 5000);
            }
          }
        }
        if (data.mode) this.mode = data.mode;
        if (typeof data.uptime === 'number') this.uptime = data.uptime;
        if (Object.prototype.hasOwnProperty.call(data, 'programRunning')) this.programRunning = !!data.programRunning;
        this._applyClockBaseline(data.uptime, data.timerRemaining, data.programRunning);
        this.startClockExtrapolation();
        if (Object.prototype.hasOwnProperty.call(data, 'voltage')) this.voltage = data.voltage;
        if (Array.isArray(data.tempSensors)) this.tempSensors = data.tempSensors;
        // New contract: *_ById maps. Derive internal arrays using hwMap (from /bootstrap).
        const relayMap = (data && data.relaysById && typeof data.relaysById === 'object') ? data.relaysById : null;
        const nextRelays = projectByIdMap(this.hwMap, 'relays', relayMap, (v) => !!v);
        if (nextRelays) this.relays = nextRelays;

        const inputMap = (data && data.inputsById && typeof data.inputsById === 'object') ? data.inputsById : null;
        const freqMap = (data && data.inputFrequenciesById && typeof data.inputFrequenciesById === 'object') ? data.inputFrequenciesById : null;
        const enMap = (data && data.inputsEnabledById && typeof data.inputsEnabledById === 'object') ? data.inputsEnabledById : null;
        const nextInputs = projectByIdMap(this.hwMap, 'inputs', inputMap, (v) => !!v);
        if (nextInputs) this.inputs = nextInputs;
        const nextFreq = projectByIdMap(this.hwMap, 'inputs', freqMap, (v) => (v === null || v === undefined) ? null : v);
        if (nextFreq) this.inputFrequencies = nextFreq;
        const nextEn = projectByIdMap(this.hwMap, 'inputs', enMap, (v) => !!v);
        if (nextEn) this.inputsEnabled = nextEn;
        if (data && typeof data.runtime === 'object' && !Array.isArray(data.runtime)) this.runtime = data.runtime;
        if (Object.prototype.hasOwnProperty.call(data, 'lastProgramName')) {
          this.lastProgramName = data.lastProgramName || '—';
        }
        // Allow null to clear the field after program finishes.
        if (data && Object.prototype.hasOwnProperty.call(data, 'currentProgramName')) {
          this.currentProgramName = data.currentProgramName;
        }
        if (data.engineRunning !== undefined) this.engineRunning = data.engineRunning;
        if (data.timerRemaining !== undefined) this.timerRemaining = data.timerRemaining;
        if (data.freeHeap !== undefined) this.freeHeap = data.freeHeap;
        if (data.lastError) this.lastError = data.lastError;
        if (data && Object.prototype.hasOwnProperty.call(data, 'gsmState')) this.gsmState = data.gsmState || '—';
        if (data.flashCommit) this.flashCommit = data.flashCommit;
        this.loaded = true;
      }
    });
  };
})();

