// Helpers moved to data/js/app/utils.js (window.APP.utils)

// SETTINGS_SUBTAB_KEYS moved into settings-store (schema-driven, loaded async).

// apiJson/waitForFlashCommit/handleBusy409 moved to data/js/app/api.js (window.APP.api)

function isOtaInProgress() {
  return window.APP?.utils?.LS?.get?.('otaInProgress') === '1';
}

function initAlpineApp() {
  // Tabs are not persisted across reload (always «Панель» + wifi sub-tab).

  // settings UI schema + form stores moved to:
  // - data/js/app/stores/settings-ui-schema-store.js
  // - data/js/app/stores/settings-form-ui-store.js

  // UI helper stores moved to data/js/app/stores/ui-*-store.js

  // device actions store moved to data/js/app/stores/device-maintenance-actions-store.js
  // device status store moved to data/js/app/stores/device-status-store.js

  // notification compat store moved to data/js/app/stores/ui-notification-compat-store.js

  // logs store moved to data/js/app/stores/ui-log-console-store.js

  // constants endpoint/store removed (SSE is the source of truth)

  // programEditorDirty будет добавлен в runtime store ниже (чтобы не перезаписать его методы)

  // programs store moved to data/js/app/stores/programs-api-store.js

  // settings store moved to data/js/app/stores/settings-store.js

  // ota/runtime stores moved to:
  // - data/js/app/stores/device-ota-update-store.js
  // - data/js/app/stores/device-runtime-controls-store.js

  // ui state + dialog stores moved to:
  // - data/js/app/stores/ui-state-store.js
  // - data/js/app/stores/ui-confirm-dialog-store.js

  // Status UI (banners/toasts) removed: no effect needed.

  // Dirty tracking for settings: any user input inside Settings marks it dirty (after load).
  let settingsDirtyTimer = null;
  document.addEventListener('input', (e) => {
    try {
      if (Alpine.store('deviceOta')?.started) return;
      const s = Alpine.store('settings');
      if (!s?.loaded) return;
      const t = e.target;
      if (!t || !(t instanceof Element)) return;
      if (!t.closest('#tab-settings')) return;
      clearTimeout(settingsDirtyTimer);
      settingsDirtyTimer = setTimeout(() => s.recomputeDirty(), 150);
    } catch (err) {}
  }, { passive: true });

  // Prevent accidental refresh/close with unsaved changes
  window.addEventListener('beforeunload', (e) => {
    try {
      if (Alpine.store('deviceOta')?.started) return;
      const s = Alpine.store('settings');
      const progEditing = Alpine.store('programs')?.current;
      const progDirty = Alpine.store('deviceRuntime')?.programEditorDirty;
      if ((s?.dirty) || (progEditing && progDirty)) {
        e.preventDefault();
        e.returnValue = '';
      }
    } catch (err) {}
  });

  // program editor component moved to data/js/app/program-editor.js

  // ---------- Компонент приложения ----------
  Alpine.data('app', () => ({
    activeTab: 'panel',
  switchTab(next) {
    const go = () => {
      try {
        this.$store.uiState.setActiveTab(next);
      } catch (e) {
        this.activeTab = next;
      }
      this.activeTab = next;
      try {
        if (next === 'programs') this.$store.programs?.loadList?.();
        if (next === 'settings') this.$store.settings?.load?.();
      } catch (e) {}
    };

    try {
      // Guard: leaving Program editor with unsaved changes
      if (this.activeTab === 'programs' && next !== 'programs') {
        const progEditing = this.$store.programs?.current;
        const progDirty = this.$store.deviceRuntime?.programEditorDirty;
        if (progEditing && progDirty) {
          this.$store.uiState.unsavedHint = true;
          this.$store.uiDialog?.show?.({
            title: 'Несохранённые изменения',
            message: 'Есть несохранённые изменения программы.',
            extraLabel: 'Отменить',
            cancelLabel: 'Остаться',
            confirmLabel: 'Сохранить',
            onCancel: () => {
              // Leave without saving (changes remain in editor)
              go();
            },
            onExtra: () => {
              try {
                if (typeof window.__programEditorRevert === 'function') window.__programEditorRevert();
                this.$store.programs?.cancelEdit?.();
              } catch (e) {}
              try { this.$store.deviceRuntime.programEditorDirty = false; } catch (e) {}
              try { window.__programEditorSave = null; } catch (e) {}
              try { window.__programEditorRevert = null; } catch (e) {}
              go();
            },
            onConfirm: () => {
              (async () => {
                try {
                  // Trigger save on the active editor instance (best-effort).
                  if (typeof window.__programEditorSave === 'function') window.__programEditorSave();
                  // Wait a bit for store updates (programs.save() clears current on success).
                  for (let i = 0; i < 30; i++) {
                    await new Promise(r => setTimeout(r, 80));
                    const stillEditing = !!this.$store.programs?.current;
                    const stillDirty = !!this.$store.deviceRuntime?.programEditorDirty;
                    if (!stillEditing || !stillDirty) break;
                  }
                  if (!this.$store.programs?.current && !this.$store.deviceRuntime?.programEditorDirty) go();
                } catch (e) {}
              })();
            }
          });
          return;
        }
      }

      if (this.activeTab === 'settings' && next !== 'settings' && this.$store.settings?.dirty) {
        this.$store.uiState.unsavedHint = true;
        this.$store.uiDialog?.show?.({
          title: 'Несохранённые изменения',
          message: 'Есть несохранённые изменения настроек.',
          extraLabel: 'Отменить',
          cancelLabel: 'Остаться',
          confirmLabel: 'Сохранить',
          onCancel: () => {
            go();
          },
          onExtra: () => {
            try { this.$store.settings?.revertAll?.(); } catch (e) {}
            go();
          },
          onConfirm: () => {
            (async () => {
              try {
                await this.$store.settings?.save?.();
                if (!this.$store.settings?.dirty) go();
              } catch (e) {}
            })();
          }
        });
        return;
      }
    } catch (e) {}

    go();
  },
    init() {
      // Always open «Панель» on load / refresh (do not restore last main tab).
      try { this.$store.uiState.setActiveTab('panel'); } catch (e) {
        try { this.$store.uiState.activeTab = 'panel'; } catch (e2) {}
      }
      this.activeTab = 'panel';

      // Двусторонняя синхронизация: programmatic store → local (enter-hook already ran in setActiveTab)
      this.$watch(() => this.$store.uiState.activeTab, v => {
        if (v && v !== this.activeTab) this.activeTab = v;
      });
      this.$watch('activeTab', val => {
        if (val && val !== this.$store.uiState.activeTab) {
          try { this.$store.uiState.setActiveTab(val); } catch (e) {
            this.$store.uiState.activeTab = val;
          }
        }
      });
    }
  }));

  // doubleTap handler moved into runtime store (no window.*)
}

document.addEventListener('alpine:init', initAlpineApp);

// Gestures + SSE are initialized from data/js/app/(gestures|sse).js