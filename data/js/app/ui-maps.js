(function () {
  const APP = (window.APP = window.APP || {});
  APP.uiMaps = APP.uiMaps || {};

  /**
   * Badge mapping registry.
   *
   * Goal: keep “device JSON value → FE label + color” decisions in one place.
   * Shape:
   *   APP.uiMaps.badges.<name>(rawValue) -> { text, class }
   */
  const toneClass = (tone) => {
    switch (tone) {
      case 'success': return 'tone-success';
      case 'warning': return 'tone-warning';
      case 'danger': return 'tone-danger';
      case 'info': return 'tone-info';
      default: return 'tone-neutral';
    }
  };

  APP.uiMaps.badges = Object.assign(APP.uiMaps.badges || {}, {
    /** From `/status`: `mode` (kernel/core mode string). */
    statusMode(raw) {
      const v = String(raw || '').trim();
      const map = {
        boot: { text: 'Система: Загрузка', tone: 'info' },
        emergency_ap: { text: 'Система: ⚠ Авария', tone: 'danger' },
        setup_ap: { text: 'Система: ⚠ Настройка', tone: 'warning' },
        normal: { text: 'Система: ✓ Норма', tone: 'success' },
        normal_silent: { text: 'Система: ✓ Тихий', tone: 'success' },
        ota_update: { text: 'Система: Обновление', tone: 'warning' },
        reboot_required: { text: 'Система: ⚠ Перезагрузите', tone: 'danger' },
      };

      const hit = map[v];
      if (hit) return { text: hit.text, class: toneClass(hit.tone) };
      if (!v) return { text: '—', class: toneClass('neutral') };
      return { text: v, class: toneClass('neutral') };
    }
    ,

    /** From `/status`: `gsmState` (GSM FSM stage string). */
    gsmState(raw) {
      const v = String(raw || '').trim().toUpperCase();
      const map = {
        IDLE: { text: 'GSM: Ожидание', tone: 'neutral' },
        INIT: { text: 'GSM: Инициализация', tone: 'info' },
        REGISTERING: { text: 'GSM: Регистрация', tone: 'info' },
        GPRS_SETUP: { text: 'GSM: APN', tone: 'info' },
        GPRS_ATTACH: { text: 'GSM: GPRS attach', tone: 'info' },
        GPRS_GETIP: { text: 'GSM: IP', tone: 'info' },
        READY: { text: 'GSM: ✓ Готов', tone: 'success' },
        ERROR: { text: 'GSM: ✗ Ошибка', tone: 'danger' },
      };

      const hit = map[v];
      if (hit) return { text: hit.text, class: toneClass(hit.tone) };
      if (!v) return { text: 'GSM: —', class: toneClass('neutral') };
      return { text: `GSM: ${v}`, class: toneClass('neutral') };
    }
  });
})();

