// UI formatting helpers store (template helpers)
(function () {
  const APP = (window.APP = window.APP || {});
  APP.stores = APP.stores || {};

  APP.stores.registerUiFormattersStore = function registerUiFormattersStore(Alpine) {
    Alpine.store('uiFormat', {
      _silkWithName(id, name) {
        const silk = String(id || '').trim();
        if (!silk) return '';
        const n = String(name || '').trim();
        return n ? `${silk} · ${n}` : silk;
      },
      inputName(idx) {
        try {
          const item = Alpine.store('settings')?.inputs?.[idx];
          const id = String(item?.id || '').trim() || `IN${(Number(idx) || 0) + 1}`;
          return this._silkWithName(id, item?.name);
        } catch (e) {}
        return '';
      },
      inputLabelById(id) {
        try {
          const iid = String(id || '').trim();
          if (!iid) return '—';
          const s = Alpine.store('settings');
          const list = Array.isArray(s?.inputs) ? s.inputs : [];
          const hit = list.find(x => x && String(x.id || '') === iid);
          return this._silkWithName(iid, hit?.name) || '—';
        } catch (e) {}
        return '—';
      },
      relayName(idx) {
        try {
          const item = Alpine.store('settings')?.relays?.[idx];
          let id = String(item?.id || '').trim();
          if (!id) {
            const hw = Alpine.store('deviceStatus')?.hwMap?.relays?.[idx];
            id = String(hw?.id || '').trim() || `K${(Number(idx) || 0) + 1}`;
          }
          return this._silkWithName(id, item?.name);
        } catch (e) {}
        return '';
      },
      relayLabelById(id) {
        try {
          const rid = String(id || '').trim();
          if (!rid) return '—';
          const s = Alpine.store('settings');
          const list = Array.isArray(s?.relays) ? s.relays : [];
          const hit = list.find(x => x && String(x.id || '') === rid);
          return this._silkWithName(rid, hit?.name) || '—';
        } catch (e) {}
        return '—';
      },
      sensorName(idx) {
        try {
          const item = Alpine.store('settings')?.sensors?.[idx];
          const id = String(item?.id || '').trim() || `T${(Number(idx) || 0) + 1}`;
          return this._silkWithName(id, item?.name);
        } catch (e) {}
        return '';
      },
      sensorLabelByRom(rom) {
        try {
          const r = String(rom || '');
          if (!r) return '—';
          const s = Alpine.store('settings');
          const list = Array.isArray(s?.sensors) ? s.sensors : [];
          const hit = list.find(x => x && String(x.rom || '') === r);
          const name = hit?.name;
          if (typeof name === 'string' && name.trim()) return name.trim();
          return r;
        } catch (e) {}
        return String(rom || '');
      },
      programLabel(id) {
        try {
          const pid = Number(id) || 0;
          if (!pid) return '—';
          const items = Alpine.store('programs')?.items || [];
          const hit = items.find(p => p && Number(p.id) === pid);
          const name = String(hit?.name || '').trim();
          return name || String(pid);
        } catch (e) {}
        return '—';
      },
      sensorLabelById(id) {
        try {
          const sid = String(id || '').trim();
          if (!sid) return '—';
          const s = Alpine.store('settings');
          const list = Array.isArray(s?.sensors) ? s.sensors : [];
          const hit = list.find(x => x && String(x.id || '') === sid);
          return this._silkWithName(sid, hit?.name) || '—';
        } catch (e) {}
        return '—';
      },
      tempClass(t) { return t < 5 ? 'temp-cold' : t > 30 ? 'temp-hot' : 'temp-normal'; },
      inputClass(idx, state) {
        const s = Alpine.store('deviceStatus');
        if (!s.inputsEnabled?.[idx]) return 'input-disabled';
        if (s.inputFrequencies?.[idx] > 0) return 'input-counter';
        return state ? 'input-active' : 'input-inactive';
      },
      isCounter(idx) { return (Alpine.store('deviceStatus').inputFrequencies?.[idx] || 0) > 0; },
      getFrequency(idx) {
        const f = Alpine.store('deviceStatus').inputFrequencies?.[idx];
        return f ? Math.round(f) : 0;
      },
      formatVoltage(v) { return v ? v.toFixed(2) + ' В' : '—'; },
      formatUptime(s) {
        if (!s && s !== 0) return '—';
        const days = Math.floor(s / 86400);
        const hours = Math.floor((s % 86400) / 3600);
        const mins = Math.floor((s % 3600) / 60);
        const secs = s % 60;
        return (days ? days + 'д ' : '') + hours + 'ч ' + mins + 'м ' + secs + 'с';
      },
      formatTimer(sec) {
        if (!sec && sec !== 0) return '—';
        if (sec < 60) return `${sec} с`;
        const mins = Math.floor(sec / 60);
        const secs = sec % 60;
        return `${mins} мин ${secs} с`;
      },
      formatWallClock(st) {
        try {
          if (!st || !st.timeSynced || !st.epoch) return st?.timeSynced === false ? 'нет времени' : '—';
          return this.formatWallClockPreview(st.tzOffsetHours, st);
        } catch (e) {
          return '—';
        }
      },
      /** Preview using UI-selected TZ hours (may differ from saved/device TZ). */
      formatWallClockPreview(tzHours, st) {
        try {
          if (!st || !st.timeSynced || !st.epoch) return st?.timeSynced === false ? 'нет времени' : '—';
          const offH = Number(tzHours);
          const hours = Number.isFinite(offH) ? offH : (Number(st.tzOffsetHours) || 0);
          const d = new Date((Number(st.epoch) + hours * 3600) * 1000);
          const pad = (n) => String(n).padStart(2, '0');
          const stamp = `${pad(d.getUTCDate())}.${pad(d.getUTCMonth() + 1)}.${d.getUTCFullYear()} ${pad(d.getUTCHours())}:${pad(d.getUTCMinutes())}:${pad(d.getUTCSeconds())}`;
          const sign = hours >= 0 ? '+' : '';
          const label = `UTC${sign}${hours}`;
          return st.timeStale ? `${stamp} (${label}, устарело)` : `${stamp} (${label})`;
        } catch (e) {
          return '—';
        }
      },
      spinnerSvg: `
<svg class="animate-spin h-4 w-4 inline ml-1" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
    <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
    <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
</svg>
`
    });
  };
})();

