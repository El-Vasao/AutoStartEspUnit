// Разбор файлов копии. Лимиты совпадают с Limits::MAX_PROGRAMS и HttpPostJson::MAX_BYTES.
(function () {
  const APP = (window.APP = window.APP || {});

  const MAX_PROGRAMS = 100;
  const MAX_FILE_JSON_BYTES = 5120;

  function utf8ByteLength(str) {
    return new TextEncoder().encode(String(str || '')).length;
  }

  function parseJsonText(text) {
    const raw = String(text == null ? '' : text).replace(/^\uFEFF/, '').trim();
    if (!raw) return { ok: false, error: 'Пустой файл' };
    try {
      return { ok: true, data: JSON.parse(raw) };
    } catch (e) {
      return { ok: false, error: 'Неверный JSON' };
    }
  }

  function isPlainObject(value) {
    return !!value && typeof value === 'object' && !Array.isArray(value);
  }

  function compactBody(value) {
    const body = JSON.stringify(value);
    return utf8ByteLength(body) > MAX_FILE_JSON_BYTES ? null : body;
  }

  function parseSettingsBackup(text) {
    const parsed = parseJsonText(text);
    if (!parsed.ok) return parsed;
    if (!isPlainObject(parsed.data)) return { ok: false, error: 'Ожидался объект настроек' };
    const body = compactBody(parsed.data);
    if (body == null) return { ok: false, error: 'Файл слишком большой для устройства' };
    return { ok: true, data: parsed.data, body };
  }

  function parseProgramsBackup(text) {
    const parsed = parseJsonText(text);
    if (!parsed.ok) return parsed;

    let list;
    if (Array.isArray(parsed.data)) list = parsed.data;
    else if (isPlainObject(parsed.data)) list = [parsed.data];
    else return { ok: false, error: 'Ожидался массив программ или одна программа' };

    if (!list.length) return { ok: false, error: 'Нет программ в файле' };
    if (list.length > MAX_PROGRAMS) return { ok: false, error: `Не больше ${MAX_PROGRAMS} программ` };

    const seen = new Set();
    const programs = [];
    for (let i = 0; i < list.length; i++) {
      const program = list[i];
      const place = `Программа #${i + 1}`;
      if (!isPlainObject(program)) return { ok: false, error: `${place}: неверный объект` };
      const id = Number(program.id);
      if (!Number.isInteger(id) || id < 1 || id > 255) return { ok: false, error: `${place}: неверный id` };
      if (seen.has(id)) return { ok: false, error: `Повтор id ${id} в файле` };
      seen.add(id);
      const body = compactBody(program);
      if (body == null) return { ok: false, error: `Программа id ${id}: слишком большая для устройства` };
      programs.push({ id, data: program, body });
    }
    return { ok: true, programs };
  }

  function checkProgramsMergeLimit(existingIds, importPrograms) {
    const ids = new Set();
    for (const id of existingIds || []) {
      const n = Number(id);
      if (Number.isInteger(n) && n > 0) ids.add(n);
    }
    for (const program of importPrograms || []) {
      const n = Number(program?.id);
      if (Number.isInteger(n) && n > 0) ids.add(n);
    }
    if (ids.size > MAX_PROGRAMS) {
      return { ok: false, error: `После импорта будет ${ids.size} программ (лимит ${MAX_PROGRAMS})` };
    }
    return { ok: true, mergedCount: ids.size };
  }

  /// Наименьший свободный id 1…255, либо 0 если мест нет или уже MAX_PROGRAMS.
  function nextFreeProgramId(existingIds) {
    const used = new Set();
    for (const id of existingIds || []) {
      const n = Number(id);
      if (Number.isInteger(n) && n >= 1 && n <= 255) used.add(n);
    }
    if (used.size >= MAX_PROGRAMS) return 0;
    for (let id = 1; id <= 255; id++) {
      if (!used.has(id)) return id;
    }
    return 0;
  }

  APP.backupJson = {
    MAX_PROGRAMS,
    MAX_FILE_JSON_BYTES,
    parseSettingsBackup,
    parseProgramsBackup,
    checkProgramsMergeLimit,
    nextFreeProgramId,
  };
})();
