// Грузит браузерные IIFE-модули из data/ в изолированный контекст vm, который играет роль `window`.
// Без зависимостей npm: только node:vm и node:fs.
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
 * Новый глобальный объект, похожий на браузер. `stores` заранее регистрирует сторы Alpine по имени.
 * Возвращает { window, Alpine, load(relPathUnderData), APP }.
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
    /** Выполняет скрипт из data/ (например, 'js/app/coerce.js') в этом контексте. */
    load(relPath) {
      const code = readFileSync(path.join(dataDir, relPath), 'utf8');
      vm.runInContext(code, win, { filename: relPath });
      return win.APP;
    },
    /** Выполняет произвольный код в этом контексте (например, постороннюю библиотеку). */
    run(code, filename = 'inline.js') {
      return vm.runInContext(code, win, { filename });
    },
  };
}

/** Значения из vm имеют чужие прототипы; нормализовать перед deepStrictEqual. */
export function plain(v) {
  return v === undefined ? undefined : JSON.parse(JSON.stringify(v));
}
