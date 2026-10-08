// Shared Ajv (JSON Schema) validation helpers
(function () {
  const APP = (window.APP = window.APP || {});
  APP.validation = APP.validation || {};

  APP.validation.pointerToDotted = function pointerToDotted(instancePath) {
    try {
      const p = String(instancePath || '');
      if (!p) return '';
      const parts = p.split('/').filter(Boolean).map(s => s.replaceAll('~1', '/').replaceAll('~0', '~'));
      return parts.join('.');
    } catch (e) {
      return '';
    }
  };

  APP.validation.errorToPath = function errorToPath(e) {
    try {
      const base = APP.validation.pointerToDotted(e?.instancePath || '');
      const kw = String(e?.keyword || '');
      const params = e?.params || {};
      if (kw === 'additionalProperties' && params.additionalProperty) {
        return base ? `${base}.${params.additionalProperty}` : String(params.additionalProperty);
      }
      if (kw === 'required' && params.missingProperty) {
        return base ? `${base}.${params.missingProperty}` : String(params.missingProperty);
      }
      return base;
    } catch (e2) {
      return '';
    }
  };

  APP.validation.errorToMessage = function errorToMessage(e) {
    try {
      const kw = String(e?.keyword || '');
      const params = e?.params || {};
      if (kw === 'maxLength') return `Не длиннее ${params.limit}`;
      if (kw === 'minLength') return `Не короче ${params.limit}`;
      if (kw === 'minimum') return `Не меньше ${params.limit}`;
      if (kw === 'maximum') return `Не больше ${params.limit}`;
      if (kw === 'pattern') return 'Неверный формат';
      if (kw === 'type') return 'Неверное значение';
      if (kw === 'enum') return 'Недопустимое значение';
      if (kw === 'additionalProperties') return 'Лишнее поле';
      if (kw === 'required') return 'Обязательно';
      return String(e?.message || 'Неверное значение');
    } catch (e2) {
      return 'Неверное значение';
    }
  };
})();
