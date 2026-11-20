// Shared optionsFrom generator (depends on Alpine stores at runtime)
(function () {
  const APP = (window.APP = window.APP || {});
  APP.options = APP.options || {};

  function silkLabel(id, name) {
    const silk = String(id || '').trim();
    if (!silk) return '—';
    const n = String(name || '').trim();
    return n ? `${silk} · ${n}` : silk;
  }

  APP.options.forSource = function forSource(source, Alpine) {
    try {
      const from = String(source || '');
      if (!from) return [];

      if (from === 'relays') {
        const st = Alpine.store('deviceStatus');
        const hw = st?.hwMap;
        if (hw && Array.isArray(hw.relays) && hw.relays.length) {
          return hw.relays.map((r) => {
            const id = String(r?.id || '').trim();
            const name = String(r?.label || r?.name || '').trim();
            return { value: id, label: silkLabel(id, name) };
          }).filter((o) => o.value);
        }
        return [];
      }

      if (from === 'inputs') {
        const st = Alpine.store('deviceStatus');
        const hw = st?.hwMap;
        if (hw && Array.isArray(hw.inputs) && hw.inputs.length) {
          return hw.inputs.map((r, idx) => {
            const id = String(r?.id || '').trim();
            const name = String(r?.label || r?.name || '').trim()
              || Alpine.store('uiFormat')?.inputName?.(idx)
              || '';
            return { value: id, label: silkLabel(id, name) };
          }).filter((o) => o.value);
        }
        return [];
      }

      // Input ids from settings, optionally filtered by type (0 = digital, 1 = pulse).
      const inputIdsOfType = (type) => {
        const s = Alpine.store('settings');
        const list = Array.isArray(s?.inputs) ? s.inputs : [];
        const out = [{ value: '', label: '— нет —' }];
        for (let idx = 0; idx < list.length; idx++) {
          const item = list[idx] || {};
          const id = String(item.id || '').trim();
          if (!id) continue;
          if (type !== undefined && (Number(item.type) || 0) !== type) continue;
          const label = Alpine.store('uiFormat')?.inputLabelById?.(id)
            || Alpine.store('uiFormat')?.inputName?.(idx)
            || id;
          out.push({ value: id, label });
        }
        return out;
      };

      if (from === 'inputIds') return inputIdsOfType(undefined);
      if (from === 'digitalInputIds') return inputIdsOfType(0);
      if (from === 'pulseInputIds') return inputIdsOfType(1);

      // ENGINE_START input follows vehicle.start_method: rpm → pulse inputs, input → digital inputs.
      if (from === 'startInputIds') {
        const method = String(Alpine.store('settings')?.vehicle?.start_method || 'timed');
        if (method === 'timed') return [];
        return inputIdsOfType(method === 'rpm' ? 1 : 0);
      }

      // Relays for RELAY_* program steps: the starter relay is driven only by ENGINE_START.
      if (from === 'programRelays') {
        const st = Alpine.store('deviceStatus');
        const hw = st?.hwMap;
        const starterId = String(Alpine.store('settings')?.vehicle?.starter_relay_id || '').trim();
        if (hw && Array.isArray(hw.relays) && hw.relays.length) {
          return hw.relays
            .map((r) => {
              const id = String(r?.id || '').trim();
              const name = String(r?.label || r?.name || '').trim();
              return { value: id, label: silkLabel(id, name) };
            })
            .filter((o) => o.value && (!starterId || o.value !== starterId));
        }
        return [];
      }

      if (from === 'sensors') {
        const st = Alpine.store('deviceStatus');
        const n = (st?.hwCounts?.locked ? st.hwCounts.sensors : (st?.tempSensors?.length || 0)) || 0;
        const out = [];
        for (let idx = 0; idx < n; idx++) {
          out.push({ value: idx, label: Alpine.store('uiFormat')?.sensorName?.(idx) || String(idx) });
        }
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
        const out = [{ value: '', label: '— нет —' }];
        for (const item of list) {
          if (!item) continue;
          const id = String(item.id || '').trim();
          if (!id) continue;
          const name = String(item.name || '').trim();
          out.push({ value: id, label: silkLabel(id, name) });
        }
        return out;
      }

      if (from === 'programs') {
        const items = Alpine.store('programs')?.items || [];
        const out = [{ value: 0, label: '— нет —' }];
        for (const p of items) {
          const name = String(p?.name || '').trim();
          out.push({ value: p.id, label: name || String(p.id) });
        }
        return out;
      }

      if (from === 'inputTriggerIds') {
        const list = Alpine.store('settings')?.input_triggers || [];
        const out = [{ value: 0, label: '— нет —' }];
        for (let i = 0; i < list.length; i++) {
          const tr = list[i] || {};
          const id = Number(tr.id) || 0;
          if (!id) continue;
          const inId = String(tr.input_id || '').trim();
          const inName = Alpine.store('uiFormat')?.inputLabelById?.(inId) || inId || '—';
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
          const sId = String(tr.sensor_id || '').trim();
          const sName = Alpine.store('uiFormat')?.sensorLabelById?.(sId) || sId || '—';
          const cmp = tr.comparison === 'below' ? '<' : '>';
          out.push({ value: id, label: `${sName} ${cmp} ${(tr.threshold || 0)}°C` });
        }
        return out;
      }
    } catch (e) {}
    return [];
  };
})();
