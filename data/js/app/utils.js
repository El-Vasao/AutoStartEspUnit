// Shared utilities (no Alpine dependency at load time)
(function () {
  const APP = (window.APP = window.APP || {});
  const utils = (APP.utils = APP.utils || {});

  utils.LS = {
    get(key, def = null) {
      try {
        const v = localStorage.getItem(key);
        return v === null ? def : v;
      } catch (e) {
        return def;
      }
    },
    set(key, value) {
      try { localStorage.setItem(key, String(value)); } catch (e) {}
    },
    del(key) {
      try { localStorage.removeItem(key); } catch (e) {}
    }
  };

  // Lightweight logger (disabled by default; enable via ?debug=1 or localStorage.debug=1)
  (function initLogger() {
    function isEnabled() {
      try {
        if (utils.LS?.get?.('debug') === '1') return true;
        const qs = String(window.location?.search || '');
        return /(?:\?|&)debug=1(?:&|$)/.test(qs);
      } catch (e) {
        return false;
      }
    }

    const prefix = '[UI]';
    utils.log = utils.log || {};
    utils.log.enabled = isEnabled();
    utils.log.setEnabled = function setEnabled(v) {
      utils.LS?.set?.('debug', v ? '1' : '0');
      utils.log.enabled = !!v;
    };
    utils.log.debug = function debug(...args) { if (utils.log.enabled) { try { console.debug(prefix, ...args); } catch (e) {} } };
    utils.log.info = function info(...args) { if (utils.log.enabled) { try { console.info(prefix, ...args); } catch (e) {} } };
    utils.log.warn = function warn(...args) { try { console.warn(prefix, ...args); } catch (e) {} };
    utils.log.error = function error(...args) { try { console.error(prefix, ...args); } catch (e) {} };
  })();

  utils.sleep = function sleep(ms) {
    return new Promise(r => setTimeout(r, ms));
  };

  function isIndexSeg(s) { return /^\d+$/.test(String(s)); }

  APP.path = APP.path || {
    get(obj, path) {
      try {
        if (!obj) return undefined;
        const parts = String(path || '').split('.').filter(Boolean);
        let cur = obj;
        for (const p of parts) {
          if (cur == null) return undefined;
          cur = isIndexSeg(p) ? cur[Number(p)] : cur[p];
        }
        return cur;
      } catch (e) {
        return undefined;
      }
    },
    set(obj, path, value) {
      try {
        if (!obj) return false;
        const parts = String(path || '').split('.').filter(Boolean);
        if (!parts.length) return false;
        let cur = obj;
        for (let i = 0; i < parts.length - 1; i++) {
          const p = parts[i];
          const next = parts[i + 1];
          const idx = isIndexSeg(p) ? Number(p) : null;
          if (idx !== null) {
            if (!Array.isArray(cur)) return false;
            if (cur[idx] == null || typeof cur[idx] !== 'object') cur[idx] = isIndexSeg(next) ? [] : {};
            cur = cur[idx];
          } else {
            if (cur[p] == null || typeof cur[p] !== 'object') cur[p] = isIndexSeg(next) ? [] : {};
            cur = cur[p];
          }
        }
        const last = parts[parts.length - 1];
        if (isIndexSeg(last)) {
          const idx = Number(last);
          if (!Array.isArray(cur)) return false;
          cur[idx] = value;
        } else {
          cur[last] = value;
        }
        return true;
      } catch (e) {
        return false;
      }
    }
  };

  /**
   * Convert JS number (or numeric string) to plain decimal string without exponent.
   * Intended for UI inputs to avoid `1e+21` / `1e-7` representations.
   */
  utils.numberToPlainString = function numberToPlainString(v) {
    try {
      if (v === null || v === undefined) return '';
      if (typeof v === 'string') {
        const s = v.trim();
        if (!s) return '';
        // If it's already non-exponent, keep as-is (do not reformat user decimals).
        if (!/[eE]/.test(s)) return s;
        const n = Number(s);
        if (!Number.isFinite(n)) return s;
        v = n;
      }
      if (typeof v !== 'number' || !Number.isFinite(v)) return '';
      const s = String(v);
      if (!/[eE]/.test(s)) return s;

      // Expand `d.ddde±k` into plain decimal.
      const m = /^([+-]?)(\d+)(?:\.(\d+))?[eE]([+-]?\d+)$/.exec(s);
      if (!m) return s;
      const sign = m[1] || '';
      const intPart = m[2] || '0';
      const fracPart = m[3] || '';
      const exp = parseInt(m[4], 10) || 0;
      const digits = (intPart + fracPart).replace(/^0+(?=\d)/, ''); // keep one leading zero if needed
      const decPos = intPart.length + exp;

      if (decPos <= 0) {
        return sign + '0.' + '0'.repeat(Math.abs(decPos)) + digits;
      }
      if (decPos >= digits.length) {
        return sign + digits + '0'.repeat(decPos - digits.length);
      }
      return sign + digits.slice(0, decPos) + '.' + digits.slice(decPos);
    } catch (e) {
      return '';
    }
  };

  utils.stableStringify = function stableStringify(value) {
    const seen = new WeakSet();
    const walk = (v) => {
      if (v === null || typeof v !== 'object') return v;
      if (seen.has(v)) return null;
      seen.add(v);
      if (Array.isArray(v)) return v.map(walk);
      const out = {};
      const keys = Object.keys(v).sort();
      for (const k of keys) out[k] = walk(v[k]);
      return out;
    };
    try { return JSON.stringify(walk(value)); } catch (e) { return ''; }
  };

  utils.deepClone = function deepClone(v) {
    try { return JSON.parse(JSON.stringify(v)); } catch (e) { return v; }
  };

  utils.fastEqual = function fastEqual(a, b) {
    if (Object.is(a, b)) return true;
    const aObj = a !== null && typeof a === 'object';
    const bObj = b !== null && typeof b === 'object';
    if (!aObj || !bObj) return false;
    return utils.stableStringify(a) === utils.stableStringify(b);
  };

  /** Собирает пути до отличающихся листьев (dot-нотация), для подсветки полей */
  utils.collectLeafDiffPaths = function collectLeafDiffPaths(a, b, pathParts, out) {
    if (Object.is(a, b)) return;

    const aIsArr = Array.isArray(a);
    const bIsArr = Array.isArray(b);

    if (aIsArr && bIsArr) {
      const n = Math.max(a.length, b.length);
      for (let i = 0; i < n; i++) {
        utils.collectLeafDiffPaths(a[i], b[i], [...pathParts, String(i)], out);
      }
      return;
    }

    const aObj = a !== null && typeof a === 'object' && !aIsArr;
    const bObj = b !== null && typeof b === 'object' && !bIsArr;

    if (aObj && bObj) {
      const aKeys = Object.keys(a);
      const bKeys = Object.keys(b);
      const keys = new Set(aKeys);
      for (const k of bKeys) keys.add(k);
      const sorted = Array.from(keys).sort();
      for (const k of sorted) {
        utils.collectLeafDiffPaths(a[k], b[k], [...pathParts, k], out);
      }
      return;
    }

    const prefix = pathParts.join('.');
    if (prefix) out[prefix] = true;
  };
})();

