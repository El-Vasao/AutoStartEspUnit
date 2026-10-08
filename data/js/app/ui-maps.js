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
        boot: { text: 'Загрузка', tone: 'info' },
        emergency_ap: { text: 'Авария', tone: 'danger' },
        setup_ap: { text: 'Настройка', tone: 'warning' },
        normal: { text: 'Норма', tone: 'success' },
        normal_silent: { text: 'Тихий', tone: 'success' },
        ota_update: { text: 'Прошивка', tone: 'warning' },
        reboot_required: { text: 'Нужна перезагрузка', tone: 'danger' },
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
        IDLE: { text: 'GSM: ожидание', tone: 'neutral' },
        INIT: { text: 'GSM: запуск', tone: 'info' },
        REGISTERING: { text: 'GSM: сеть', tone: 'info' },
        GPRS_SETUP: { text: 'GSM: интернет', tone: 'info' },
        GPRS_ATTACH: { text: 'GSM: подключение', tone: 'info' },
        GPRS_GETIP: { text: 'GSM: адрес', tone: 'info' },
        READY: { text: 'GSM: готов', tone: 'success' },
        ERROR: { text: 'GSM: ошибка', tone: 'danger' },
      };

      const hit = map[v];
      if (hit) return { text: hit.text, class: toneClass(hit.tone) };
      if (!v) return { text: 'GSM: —', class: toneClass('neutral') };
      return { text: `GSM: ${v}`, class: toneClass('neutral') };
    }
  });
})();

