// Schema normalization helpers (no Alpine dependency)
(function () {
  const APP = (window.APP = window.APP || {});
  APP.schemaNormalize = APP.schemaNormalize || {};

  // optionsFrom sources whose option values are numeric ids.
  const INT_SOURCES = new Set([
    'relays', 'programRelays', 'inputs', 'inputIds', 'digitalInputIds', 'pulseInputIds', 'startInputIds',
    'sensors', 'sensorIds', 'programs'
  ]);

  APP.schemaNormalize.inferValueTypeFromField = function inferValueTypeFromField(field) {
    try {
      if (!field || typeof field !== 'object') return 'string';
      const explicit = String(field.valueType || '');
      if (explicit) return explicit;
      const t = String(field.type || '');
      if (t === 'toggle') return 'bool';
      if (t === 'number') return 'float';
      if (t === 'text' || t === 'password' || t === 'textarea') return 'string';
      if (t === 'select') {
        const opts = Array.isArray(field.options) ? field.options : null;
        if (opts && opts.length) {
          const kinds = new Set(opts.map(o => typeof (o ? o.value : undefined)).filter(k => k !== 'undefined'));
          if (kinds.size === 1) {
            const only = Array.from(kinds)[0];
            if (only === 'boolean') return 'bool';
            if (only === 'number') return 'int';
            if (only === 'string') return 'string';
          }
        }
        const from = String(field.optionsFrom || '');
        if (INT_SOURCES.has(from)) return 'int';
        if (typeof field.default === 'boolean') return 'bool';
        if (typeof field.default === 'number') return 'int';
        if (typeof field.default === 'string') return 'string';
        return 'string';
      }
      return 'string';
    } catch (e) {
      return 'string';
    }
  };

  APP.schemaNormalize.inferValueTypeFromParam = function inferValueTypeFromParam(param) {
    try {
      if (!param || typeof param !== 'object') return 'string';
      const explicit = String(param.valueType || '');
      if (explicit) return explicit;
      const t = String(param.type || '');
      if (t === 'number') return 'float';
      if (t === 'toggle') return 'bool';
      if (t === 'select') {
        const opts = Array.isArray(param.options) ? param.options : null;
        if (opts && opts.length) {
          const kinds = new Set(opts.map(o => typeof (o ? o.value : undefined)).filter(k => k !== 'undefined'));
          if (kinds.size === 1) {
            const only = Array.from(kinds)[0];
            if (only === 'boolean') return 'bool';
            if (only === 'number') return 'int';
            if (only === 'string') return 'string';
          }
        }
        const from = String(param.optionsFrom || '');
        if (INT_SOURCES.has(from)) return 'int';
        if (typeof param.default === 'boolean') return 'bool';
        if (typeof param.default === 'number') return 'int';
        if (typeof param.default === 'string') return 'string';
        return 'string';
      }
      return 'string';
    } catch (e) {
      return 'string';
    }
  };

  function clampInt(n, lo, hi) {
    const x = Number(n);
    if (!Number.isFinite(x)) return lo;
    if (x < lo) return lo;
    if (x > hi) return hi;
    return Math.trunc(x);
  }

  function deepClone(obj) {
    try { return JSON.parse(JSON.stringify(obj)); } catch (e) { return obj; }
  }

  /**
   * Apply hardware-dependent limits to validation schema.
   * Source of truth for numbers: firmware-reported hwCounts (/bootstrap),
   * which must match Limits::MAX_* on the device.
   */
  APP.schemaNormalize.applyHardwareLimits = function applyHardwareLimits(schema, hwCounts) {
    const sc = deepClone(schema);
    try {
      const hc = hwCounts && typeof hwCounts === 'object' ? hwCounts : {};
      const relays = clampInt(hc.relays, 0, 64);
      const inputs = clampInt(hc.inputs, 0, 64);
      const sensors = clampInt(hc.sensors, 0, 16);

      // Helper to set maximum in nested nodes if present.
      const setMax = (node, max) => {
        if (!node || typeof node !== 'object') return;
        node.maximum = max;
      };

      // vehicle.starter_relay_id: uses ids, no hwCount-based limit

      // engine_input_id: no maxItems-based limit (uses ids)

      // Arrays sized to hardware inventories. Skip 0 so Ajv init before hwCounts stays at schema defaults.
      if (sensors > 0) {
        try { sc.properties.sensors.maxItems = sensors; } catch (e) {}
      }
      if (inputs > 0) {
        try { sc.properties.inputs.maxItems = inputs; } catch (e) {}
      }

      return sc;
    } catch (e) {
      return sc;
    }
  };
})();

