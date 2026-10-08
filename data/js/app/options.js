// Shared optionsFrom generator (depends on Alpine stores at runtime)
(function () {
  const APP = (window.APP = window.APP || {});
  APP.options = APP.options || {};

  APP.options.forSource = function forSource(source, Alpine) {
    try {
      const from = String(source || '');
      if (!from) return [];

      if (from === 'relays') {
        const st = Alpine.store('deviceStatus');
        const hw = st?.hwMap;
        if (hw && Array.isArray(hw.relays) && hw.relays.length) {
          return hw.relays.map((r, idx) => ({ value: Number(r?.id) || 0, label: String(r?.label || ('K' + (idx + 1))) }));
        }
        return [];
      }

      if (from === 'inputs') {
        const st = Alpine.store('deviceStatus');
        const hw = st?.hwMap;
        if (hw && Array.isArray(hw.inputs) && hw.inputs.length) {
          return hw.inputs.map((r, idx) => ({ value: Number(r?.id) || 0, label: String(r?.label || (Alpine.store('uiFormat')?.inputName?.(idx) || ('IN' + (idx + 1)))) }));
        }
        return [];
      }

      if (from === 'inputIds') {
        const s = Alpine.store('settings');
        const list = Array.isArray(s?.inputs) ? s.inputs : [];
        const out = [{ value: 0, label: '— нет —' }];
        for (let idx = 0; idx < list.length; idx++) {
          const item = list[idx] || {};
          const id = Number(item.id) || 0;
          if (!id) continue;
          const label = Alpine.store('uiFormat')?.inputLabelById?.(id) || (Alpine.store('uiFormat')?.inputName?.(idx) || ('IN' + (idx + 1)));
          out.push({ value: id, label });
        }
        return out;
      }

      if (from === 'sensors') {
        const st = Alpine.store('deviceStatus');
        const n = (st?.hwCounts?.locked ? st.hwCounts.sensors : (st?.tempSensors?.length || 0)) || 0;
        const out = [];
        for (let idx = 0; idx < n; idx++) out.push({ value: idx, label: Alpine.store('uiFormat')?.sensorName?.(idx) || ('Датчик ' + (idx + 1)) });
        return out;
      }

      if (from === 'sensorRoms') {
        const st = Alpine.store('deviceStatus');
        const list = Array.isArray(st?.temperatureSensorRoms) ? st.temperatureSensorRoms : [];
        const out = [{ value: '', label: '— нет —' }];
        for (const rom of list) {
          if (!rom) continue;
          const label = Alpine.store('uiFormat')?.sensorLabelByRom?.(rom) || String(rom);
          out.push({ value: String(rom), label });
        }
        return out;
      }

      if (from === 'sensorIds') {
        const s = Alpine.store('settings');
        const list = Array.isArray(s?.sensors) ? s.sensors : [];
        const out = [{ value: 0, label: '— нет —' }];
        for (const item of list) {
          if (!item) continue;
          const id = Number(item.id) || 0;
          if (!id) continue;
          const name = String(item.name || '').trim();
          out.push({ value: id, label: name || ('Датчик ' + id) });
        }
        return out;
      }

      if (from === 'programs') {
        const items = Alpine.store('programs')?.items || [];
        const out = [{ value: 0, label: '— нет —' }];
        for (const p of items) out.push({ value: p.id, label: p.name });
        return out;
      }

      if (from === 'inputTriggerIds') {
        const list = Alpine.store('settings')?.input_triggers || [];
        const out = [{ value: 0, label: '— нет —' }];
        for (let i = 0; i < list.length; i++) {
          const tr = list[i] || {};
          const id = Number(tr.id) || 0;
          if (!id) continue;
          const inName = Alpine.store('uiFormat')?.inputLabelById?.(tr.input_id) || ('IN' + (Number(tr.input_id) || 0));
          const lvl = tr.trigger_level ? 'Высокий' : 'Низкий';
          out.push({ value: id, label: `${inName} ${lvl}` });
        }
        return out;
      }

      if (from === 'tempTriggerIds') {
        const list = Alpine.store('settings')?.temperature_triggers || [];
        const out = [{ value: 0, label: '— нет —' }];
        for (let i = 0; i < list.length; i++) {
          const tr = list[i] || {};
          const id = Number(tr.id) || 0;
          if (!id) continue;
          const sName = Alpine.store('uiFormat')?.sensorLabelById?.(tr.sensor_id) || ('Датчик ' + (tr.sensor_id || 0));
          const cmp = tr.comparison === 'below' ? '<' : '>';
          out.push({ value: id, label: `${sName} ${cmp} ${(tr.threshold || 0)}°C` });
        }
        return out;
      }
    } catch (e) {}
    return [];
  };
})();

