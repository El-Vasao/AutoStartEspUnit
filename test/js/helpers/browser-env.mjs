// Loads browser IIFE modules from data/ into an isolated vm context that acts as `window`.
// No npm dependencies: node:vm + node:fs only.
import { readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

const here = path.dirname(fileURLToPath(import.meta.url));
export const projectRoot = path.resolve(here, '..', '..', '..');
export const dataDir = path.join(projectRoot, 'data');

export function readProjectFile(relPath) {
  return readFileSync(path.join(projectRoot, relPath), 'utf8');
}

export function readDataJson(relPath) {
  return JSON.parse(readFileSync(path.join(dataDir, relPath), 'utf8'));
}

function memoryStorage() {
  const m = new Map();
  return {
    getItem: (k) => (m.has(k) ? m.get(k) : null),
    setItem: (k, v) => { m.set(k, String(v)); },
    removeItem: (k) => { m.delete(k); },
    clear: () => { m.clear(); },
  };
}

/**
 * Creates a fresh browser-like global. `stores` pre-registers Alpine stores by name.
 * Returns { window, Alpine, load(relPathUnderData), APP }.
 */
export function createBrowserEnv({ stores = {}, search = '' } = {}) {
  const storeMap = new Map(Object.entries(stores));
  const dataComponents = new Map();
  const Alpine = {
    store(name, value) {
      if (value !== undefined) {
        storeMap.set(name, value);
        return value;
      }
      return storeMap.get(name);
    },
    data(name, factory) { dataComponents.set(name, factory); },
    nextTick(fn) { if (typeof fn === 'function') fn(); },
    _dataComponents: dataComponents,
  };

  const listeners = new Map();
  const win = {
    APP: {},
    Alpine,
    console,
    location: { search, href: 'http://device.local/' + search, reload() {} },
    localStorage: memoryStorage(),
    sessionStorage: memoryStorage(),
    setTimeout,
    clearTimeout,
    setInterval,
    clearInterval,
    queueMicrotask,
    Promise,
    URL,
    URLSearchParams,
    AbortController,
    TextEncoder,
    TextDecoder,
    addEventListener(type, fn) {
      if (!listeners.has(type)) listeners.set(type, []);
      listeners.get(type).push(fn);
    },
    removeEventListener(type, fn) {
      const arr = listeners.get(type) || [];
      const i = arr.indexOf(fn);
      if (i >= 0) arr.splice(i, 1);
    },
    dispatchEvent(ev) {
      for (const fn of listeners.get(ev?.type) || []) fn(ev);
      return true;
    },
  };
  win.window = win;
  win.globalThis = win;
  win.self = win;
  win.document = {
    addEventListener: win.addEventListener,
    removeEventListener: win.removeEventListener,
    dispatchEvent: win.dispatchEvent,
    getElementById: () => null,
    querySelector: () => null,
    querySelectorAll: () => [],
    visibilityState: 'visible',
  };
  vm.createContext(win);

  return {
    window: win,
    Alpine,
    get APP() { return win.APP; },
    /** Runs a script from data/ (e.g. 'js/app/coerce.js') in this context. */
    load(relPath) {
      const code = readFileSync(path.join(dataDir, relPath), 'utf8');
      vm.runInContext(code, win, { filename: relPath });
      return win.APP;
    },
    /** Runs arbitrary code in this context (e.g. a vendored library). */
    run(code, filename = 'inline.js') {
      return vm.runInContext(code, win, { filename });
    },
  };
}

/** Values created inside the vm have foreign prototypes; normalize before deepStrictEqual. */
export function plain(v) {
  return v === undefined ? undefined : JSON.parse(JSON.stringify(v));
}
