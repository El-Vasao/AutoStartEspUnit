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

  /** `K1|K2|…` / `IN1|…` / `T1|…` for Ajv string patterns from hwCounts. */
  APP.schemaNormalize.silkIdPattern = function silkIdPattern(prefix, count, allowEmpty) {
    const n = clampInt(count, 0, 64);
    const ids = [];
    for (let i = 1; i <= n; i++) ids.push(String(prefix) + i);
    const body = ids.join('|');
    if (!body) return allowEmpty ? '^$' : '^(?!)$';
    return allowEmpty ? `^(?:|${body})$` : `^(?:${body})$`;
  };

  function patchSilkPatternsInSchema(node, relays, inputs, sensors) {
    if (!node || typeof node !== 'object') return;
    if (Array.isArray(node)) {
      for (const item of node) patchSilkPatternsInSchema(item, relays, inputs, sensors);
      return;
    }
    const pat = node.pattern;
    if (typeof pat === 'string') {
      const allowEmpty = pat.includes('(?:|');
      if (relays > 0 && /K\[1-\d+\]/.test(pat)) {
        node.pattern = APP.schemaNormalize.silkIdPattern('K', relays, allowEmpty);
      } else if (inputs > 0 && /IN\[1-\d+\]/.test(pat)) {
        node.pattern = APP.schemaNormalize.silkIdPattern('IN', inputs, allowEmpty);
      } else if (sensors > 0 && /T\[1-\d+\]/.test(pat)) {
        node.pattern = APP.schemaNormalize.silkIdPattern('T', sensors, allowEmpty);
      }
    }
    for (const v of Object.values(node)) {
      if (v && typeof v === 'object') patchSilkPatternsInSchema(v, relays, inputs, sensors);
    }
  }

  /**
   * Apply hardware-dependent limits to validation schema.
   * Source of truth for numbers: firmware-reported hwCounts (/bootstrap),
   * which must match HardwareLimits on the device.
   */
  APP.schemaNormalize.applyHardwareLimits = function applyHardwareLimits(schema, hwCounts) {
    const sc = deepClone(schema);
    try {
      const hc = hwCounts && typeof hwCounts === 'object' ? hwCounts : {};
      const relays = clampInt(hc.relays, 0, 64);
      const inputs = clampInt(hc.inputs, 0, 64);
      const sensors = clampInt(hc.sensors, 0, 16);

      // Arrays sized to hardware inventories. Skip 0 so Ajv init before hwCounts stays at schema defaults.
      if (sensors > 0) {
        try { sc.properties.sensors.maxItems = sensors; } catch (e) {}
      }
      if (inputs > 0) {
        try { sc.properties.inputs.maxItems = inputs; } catch (e) {}
      }
      if (relays > 0) {
        try { sc.properties.relays.maxItems = relays; } catch (e) {}
      }

      // Silk id patterns in settings/program schemas (K[1-5], IN[1-3], T[1-3]) follow hwCounts.
      patchSilkPatternsInSchema(sc, relays, inputs, sensors);

      return sc;
    } catch (e) {
      return sc;
    }
  };
})();

