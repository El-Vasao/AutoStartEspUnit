// Contract tests: data/ (UI schemas, validation schemas, endpoints) must match the firmware sources.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readdirSync } from 'node:fs';
import path from 'node:path';
import { createBrowserEnv, plain, projectRoot, readDataJson, readProjectFile } from './helpers/browser-env.mjs';

function defaultConfigJson() {
  const header = readProjectFile('include/config/DefaultConfig.h');
  const m = /R"\(([\s\S]*?)\)"/.exec(header);
  assert.ok(m, 'DEFAULT_CONFIG_JSON raw string literal not found');
  return JSON.parse(m[1]);
}

function createAjv() {
  const env = createBrowserEnv();
  env.run(readProjectFile('data/js/ajv2020.min.js'), 'ajv2020.min.js');
  const Ajv2020 = env.window.ajv2020;
  assert.equal(typeof Ajv2020, 'function', 'window.ajv2020 missing after loading the bundle');
  // Same options as data/js/app/stores/settings-validator-store.js.
  return new Ajv2020({ allErrors: true, strict: true, allowUnionTypes: true });
}

test('factory DEFAULT_CONFIG_JSON passes settings.schema.json', () => {
  const ajv = createAjv();
  const validate = ajv.compile(readDataJson('validation/settings.schema.json'));
  const ok = validate(defaultConfigJson());
  assert.ok(ok, 'schema errors: ' + JSON.stringify(plain(validate.errors), null, 2));
});

test('program.schema.json and programs.index.schema.json compile in strict mode', () => {
  const ajv = createAjv();
  ajv.compile(readDataJson('validation/program.schema.json'));
  ajv.compile(readDataJson('validation/programs.index.schema.json'));
});

function collectUiPaths(node, out) {
  if (Array.isArray(node)) {
    for (const v of node) collectUiPaths(v, out);
    return out;
  }
  if (!node || typeof node !== 'object') return out;
  if (typeof node.path === 'string') out.add(node.path);
  if (typeof node.pathTmpl === 'string') out.add(node.pathTmpl);
  for (const v of Object.values(node)) {
    if (v && typeof v === 'object') collectUiPaths(v, out);
  }
  return out;
}

function resolveSchemaPath(schema, dotted) {
  let node = schema;
  for (const seg of dotted.split('.')) {
    if (!node) return null;
    if (node.type === 'array' || node.items) {
      if (seg !== '{i}' && !/^\d+$/.test(seg)) return null;
      node = node.items;
      continue;
    }
    node = node.properties ? node.properties[seg] : undefined;
  }
  return node || null;
}

test('every settings.ui.json field path exists in settings.schema.json', () => {
  const schema = readDataJson('validation/settings.schema.json');
  const ui = readDataJson('ui/settings.ui.json');
  const paths = collectUiPaths(ui, new Set());
  assert.ok(paths.size > 50, 'expected many UI paths, got ' + paths.size);
  const missing = [...paths].filter((p) => !resolveSchemaPath(schema, p));
  assert.deepEqual(missing, []);
});

test('showIf conditions reference existing schema paths', () => {
  const schema = readDataJson('validation/settings.schema.json');
  const ui = readDataJson('ui/settings.ui.json');
  const refs = new Set();
  const walk = (n) => {
    if (Array.isArray(n)) return n.forEach(walk);
    if (!n || typeof n !== 'object') return;
    for (const [k, v] of Object.entries(n)) {
      if (k === 'path' && typeof v === 'string' && n !== ui && ('value' in n || 'values' in n)) refs.add(v);
      if (v && typeof v === 'object') walk(v);
    }
  };
  for (const section of Object.values(ui.settings)) {
    for (const f of section.fields || []) if (f.showIf) walk(f.showIf);
  }
  const missing = [...refs].filter((p) => !resolveSchemaPath(schema, p.replace(/\.\d+\./g, '.{i}.')));
  assert.deepEqual(missing, []);
});

function firmwareActionNames() {
  const src = readProjectFile('src/program/ProgramAction.cpp');
  const names = new Set();
  for (const m of src.matchAll(/strcmp\(action, "([A-Z_]+)"\)/g)) names.add(m[1]);
  return names;
}

test('program editor actions match firmware ProgramAction.cpp', () => {
  const ui = readDataJson('ui/program-steps.ui.json').programSteps;
  const uiActions = new Set();
  for (const cat of ui.actionCategories) for (const a of cat.actions) uiActions.add(a.value);
  assert.deepEqual([...uiActions].sort(), [...firmwareActionNames()].sort());
});

test('program editor action params are declared and fit program.schema.json', () => {
  const ui = readDataJson('ui/program-steps.ui.json').programSteps;
  const stepProps = readDataJson('validation/program.schema.json').properties.steps.items.properties;
  const maxActionLen = stepProps.action.maxLength;
  for (const cat of ui.actionCategories) {
    for (const a of cat.actions) {
      assert.ok(a.value.length <= maxActionLen, `${a.value} longer than ${maxActionLen}`);
      for (const p of a.params || []) {
        assert.ok(ui.params[p], `${a.value}: param ${p} not declared in params`);
        assert.ok(stepProps[p], `${a.value}: param ${p} not allowed by program.schema.json`);
      }
    }
  }
});

function firmwareRoutes() {
  const dir = path.join(projectRoot, 'src', 'web');
  const routes = new Set(['/events']);  // AsyncEventSource events("/events") in WebServer.h
  for (const f of readdirSync(dir)) {
    if (!f.endsWith('.cpp')) continue;
    const src = readProjectFile('src/web/' + f);
    for (const m of src.matchAll(/server\.on\("([^"]+)"/g)) routes.add(m[1]);
    for (const m of src.matchAll(/registerJsonPostBodyToTmp\(server, "([^"]+)"/g)) routes.add(m[1]);
  }
  return routes;
}

test('every contract.js endpoint is a registered firmware route', () => {
  const env = createBrowserEnv();
  env.load('js/app/contract.js');
  const endpoints = plain(env.APP.contract.api.endpoints);
  const routes = firmwareRoutes();
  const missing = Object.entries(endpoints).filter(([, url]) => !routes.has(url));
  assert.deepEqual(missing, []);
  const header = readProjectFile('include/web/WebServer.h');
  assert.match(header, /"\/events"/, 'SSE path in WebServer.h changed');
});

test('docs/modules has one markdown file per src module', () => {
  const srcMods = readdirSync(path.join(projectRoot, 'src'), { withFileTypes: true })
    .filter((e) => e.isDirectory())
    .map((e) => e.name)
    .sort();
  const docsDir = path.join(projectRoot, 'docs', 'modules');
  const missing = srcMods.filter((m) => {
    try {
      return !readdirSync(docsDir).includes(`${m}.md`);
    } catch (e) {
      return true;
    }
  });
  assert.deepEqual(missing, []);
  assert.equal(
    readdirSync(docsDir).includes('gsm_modem.md'),
    false,
    'gsm.md and modem.md are the module docs; gsm_modem.md must not exist'
  );
});
