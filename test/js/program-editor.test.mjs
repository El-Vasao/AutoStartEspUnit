import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createBrowserEnv, readDataJson } from './helpers/browser-env.mjs';

function editor() {
  const env = createBrowserEnv();
  env.load('js/app/utils.js');
  env.load('js/app/program-editor.js');
  env.window.dispatchEvent({ type: 'alpine:init' });
  env.Alpine.store('deviceRuntime', { programEditorDirty: false });
  const factory = env.Alpine._dataComponents.get('programEditor');
  const ed = factory();
  const ui = readDataJson('ui/program-steps.ui.json').programSteps;
  ed.$store = { programStepsUiSchema: ui };
  ed.form = { id: 1, name: 't', steps: [] };
  return ed;
}

test('new CALL_OWNER step has no duration field', () => {
  const ed = editor();
  ed.selectedActionValue = 'CALL_OWNER';
  ed.addStepFromSelect();
  assert.equal(ed.form.steps.length, 1);
  assert.equal(ed.form.steps[0].action, 'CALL_OWNER');
  assert.equal(ed.form.steps[0].ms, undefined);
  assert.deepEqual(ed.form.steps[0].actionDef.params, []);
});

test('new WAIT step keeps the shared 1 second default', () => {
  const ed = editor();
  ed.selectedActionValue = 'WAIT';
  ed.addStepFromSelect();
  assert.equal(ed.form.steps[0].ms, 1);
});

test('relay_id and input_id stay silk strings through round-trip', () => {
  const ed = editor();
  const relayStep = ed.stepFromServer({ step: 1, action: 'RELAY_ON', relay_id: 'K2' });
  assert.equal(relayStep.relay_id, 'K2');
  assert.equal(ed.stepToServer(relayStep).relay_id, 'K2');

  const withInput = ed.$store.programStepsUiSchema.actionCategories
    .flatMap((c) => c.actions)
    .find((a) => Array.isArray(a.params) && a.params.includes('input_id'));
  assert.ok(withInput, 'schema has an input_id action');
  const inputStep = ed.stepFromServer({ step: 2, action: withInput.value, input_id: 'IN1' });
  assert.equal(inputStep.input_id, 'IN1');
  assert.equal(ed.stepToServer(inputStep).input_id, 'IN1');
});
