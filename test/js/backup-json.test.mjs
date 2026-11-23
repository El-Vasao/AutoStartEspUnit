import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createBrowserEnv, plain } from './helpers/browser-env.mjs';

function loadBackup() {
  const env = createBrowserEnv();
  env.load('js/app/ns.js');
  env.load('js/app/backup-json.js');
  return env.APP.backupJson;
}

function sampleProgram(id, name = 'P' + id) {
  return {
    id,
    name,
    steps: [{ step: 1, action: 'WAIT', ms: 100 }],
  };
}

test('parseSettingsBackup accepts a settings object', () => {
  const b = loadBackup();
  const res = b.parseSettingsBackup(JSON.stringify({ wifi: { ssid: 'x' }, setup_required: false }));
  assert.equal(res.ok, true);
  assert.equal(plain(res.data).wifi.ssid, 'x');
  assert.ok(typeof res.body === 'string');
});

test('parseSettingsBackup rejects arrays and empty text', () => {
  const b = loadBackup();
  assert.equal(b.parseSettingsBackup('[]').ok, false);
  assert.equal(b.parseSettingsBackup('').ok, false);
  assert.equal(b.parseSettingsBackup('{').ok, false);
});

test('parseProgramsBackup accepts array and single object', () => {
  const b = loadBackup();
  const one = b.parseProgramsBackup(JSON.stringify(sampleProgram(1)));
  assert.equal(one.ok, true);
  assert.equal(one.programs.length, 1);
  assert.equal(one.programs[0].id, 1);

  const many = b.parseProgramsBackup(JSON.stringify([sampleProgram(1), sampleProgram(2)]));
  assert.equal(many.ok, true);
  assert.equal(many.programs.length, 2);
});

test('parseProgramsBackup rejects duplicate ids', () => {
  const b = loadBackup();
  const res = b.parseProgramsBackup(JSON.stringify([sampleProgram(3), sampleProgram(3, 'dup')]));
  assert.equal(res.ok, false);
  assert.match(String(res.error), /Повтор id 3/);
});

test('parseProgramsBackup rejects more than MAX_PROGRAMS', () => {
  const b = loadBackup();
  const list = [];
  for (let i = 1; i <= b.MAX_PROGRAMS + 1; i++) list.push(sampleProgram(i));
  const res = b.parseProgramsBackup(JSON.stringify(list));
  assert.equal(res.ok, false);
});

test('checkProgramsMergeLimit allows overwrite of existing ids', () => {
  const b = loadBackup();
  const existing = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
  const ok = b.checkProgramsMergeLimit(existing, [{ id: 3 }, { id: 7 }]);
  assert.equal(ok.ok, true);
  assert.equal(ok.mergedCount, 10);
});

test('checkProgramsMergeLimit rejects when unique count exceeds max', () => {
  const b = loadBackup();
  const existing = [];
  for (let i = 1; i <= b.MAX_PROGRAMS - 1; i++) existing.push(i);
  const bad = b.checkProgramsMergeLimit(existing, [{ id: b.MAX_PROGRAMS }, { id: b.MAX_PROGRAMS + 1 }]);
  assert.equal(bad.ok, false);
  assert.match(String(bad.error), new RegExp(`лимит ${b.MAX_PROGRAMS}`));
});

test('nextFreeProgramId skips occupied high ids', () => {
  const b = loadBackup();
  assert.equal(b.nextFreeProgramId([1, 2, 230]), 3);
  assert.equal(b.nextFreeProgramId([]), 1);
  assert.equal(b.nextFreeProgramId([1]), 2);
});

test('nextFreeProgramId returns 0 at capacity', () => {
  const b = loadBackup();
  const full = [];
  for (let i = 1; i <= b.MAX_PROGRAMS; i++) full.push(i);
  assert.equal(b.nextFreeProgramId(full), 0);
});
