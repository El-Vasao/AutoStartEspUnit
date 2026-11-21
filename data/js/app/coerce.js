// Schema-driven value coercion (no Alpine dependency)
(function () {
  const APP = (window.APP = window.APP || {});
  APP.coerce = APP.coerce || {};

  function asString(v) {
    return (v === null || v === undefined) ? '' : String(v);
  }

  APP.coerce.byValueType = function byValueType(valueType, raw, fallback) {
    const t = String(valueType || '');
    if (!t) return raw;

    if (t === 'string') return asString(raw);

    if (t === 'bool') {
      if (raw === true || raw === false) return raw;
      const s = asString(raw).toLowerCase();
      if (s === 'true' || s === '1' || s === 'on' || s === 'yes') return true;
      if (s === 'false' || s === '0' || s === 'off' || s === 'no' || s === '') return false;
      return !!raw;
    }

    if (t === 'int') {
      if (typeof raw === 'number' && Number.isFinite(raw)) return Math.trunc(raw);
      const s = asString(raw).trim().replace(',', '.');
      if (s === '') return '';
      const n = Number(s);
      if (!Number.isFinite(n)) return (fallback !== undefined) ? fallback : raw;
      return Math.trunc(n);
    }

    if (t === 'float') {
      if (typeof raw === 'number' && Number.isFinite(raw)) return raw;
      const s = asString(raw).trim().replace(',', '.');
      if (s === '') return '';
      const n = Number(s);
      if (!Number.isFinite(n)) return (fallback !== undefined) ? fallback : raw;
      return n;
    }

    return raw;
  };
})();

