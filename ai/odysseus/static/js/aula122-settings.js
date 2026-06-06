// js/aula122-settings.js — Panel "Aula 122" dentro de los Ajustes de Odiseo.
// Edita INLINE los ajustes NATIVOS de la app vía el canal nativo↔web (puente aula122.bridge):
//  • Lectura: el shell empuja window.aula122ApplyNativeSettings({...}) (nativo→web), poblando el form.
//  • Escritura: cada control navega a /settings/native/set?key=..&value=.. (web→nativo); el shell
//    aplica el cambio reusando la lógica del panel de Ajustes nativo (SettingsManager), sin duplicar.
// Tema/idioma/carpeta/idioma-corrector son de SOLO LECTURA aquí (el tema lo controla Odiseo; el resto
// abre diálogos nativos). Fuera del shell nativo el puente no hace nada (inofensivo).

const BRIDGE = 'http://aula122.bridge';
let _applying = false; // anti-eco: no reenviar cambios mientras poblamos desde el nativo
let _wired = false;

function _bridge(path) {
  try {
    globalThis.location.href = BRIDGE + path + (path.includes('?') ? '&' : '?') + 't=' + Date.now();
  } catch (_e) {
    // navegador normal (sin shell): inofensivo
  }
}
function _el(id) { return document.getElementById(id); }
function _setNative(key, value) {
  if (_applying) return; // el cambio vino del nativo, no lo reboto
  _bridge('/settings/native/set?key=' + encodeURIComponent(key) + '&value=' + encodeURIComponent(value));
}

const _THEME = { 0: 'Oscuro', 1: 'Claro', 2: 'Oscuro y claro', 3: 'Personalizado' };

function _populate(s) {
  _applying = true;
  try {
    if (_el('a122s-theme')) {
      _el('a122s-theme').textContent = _THEME[s.theme] != null ? _THEME[s.theme] : ('#' + s.theme);
    }
    if (_el('a122s-density')) _el('a122s-density').value = String(s.density || 0);
    const scale = (typeof s.scale === 'number' && s.scale > 0) ? s.scale : 1;
    if (_el('a122s-scale')) _el('a122s-scale').value = String(scale);
    if (_el('a122s-scale-val')) _el('a122s-scale-val').textContent = Math.round(scale * 100) + '%';
    if (_el('a122s-language')) {
      _el('a122s-language').textContent = (s.language != null ? ('código ' + s.language) : '—');
    }
    if (_el('a122s-autosave')) _el('a122s-autosave').checked = !!s.autoSave;
    if (_el('a122s-backups')) _el('a122s-backups').checked = !!s.saveBackups;
    if (_el('a122s-backups-qty')) {
      _el('a122s-backups-qty').value = String(s.backupsQty != null ? s.backupsQty : 7);
    }
    if (_el('a122s-backups-folder')) {
      _el('a122s-backups-folder').textContent = s.backupsFolder || '(predeterminada)';
    }
    if (_el('a122s-spell')) _el('a122s-spell').checked = !!s.useSpellChecker;
    if (_el('a122s-spell-lang')) _el('a122s-spell-lang').textContent = s.spellLanguage || '—';
    if (_el('a122s-logging')) _el('a122s-logging').checked = !!s.extendedLogging;
  } finally {
    _applying = false;
  }
}

function _wire() {
  if (_wired) return;
  _wired = true;
  const onChk = (id, key) => {
    const e = _el(id);
    if (e) e.addEventListener('change', () => _setNative(key, e.checked ? 'true' : 'false'));
  };
  const onSel = (id, key) => {
    const e = _el(id);
    if (e) e.addEventListener('change', () => _setNative(key, e.value));
  };
  onSel('a122s-density', 'density');
  onChk('a122s-autosave', 'autoSave');
  onChk('a122s-backups', 'saveBackups');
  onChk('a122s-spell', 'useSpellChecker');
  onChk('a122s-logging', 'extendedLogging');
  const qty = _el('a122s-backups-qty');
  if (qty) qty.addEventListener('change', () => _setNative('backupsQty', qty.value));
  const scale = _el('a122s-scale');
  if (scale) {
    let t = null;
    scale.addEventListener('input', () => {
      if (_el('a122s-scale-val')) {
        _el('a122s-scale-val').textContent = Math.round(parseFloat(scale.value) * 100) + '%';
      }
      if (_applying) return;
      clearTimeout(t);
      t = setTimeout(() => _setNative('scale', scale.value), 250); // debounce: re-layout global
    });
  }
}

// API pública: el nativo empuja aquí los ajustes actuales (canal nativo→web).
globalThis.aula122ApplyNativeSettings = function (s) {
  try { _wire(); _populate(s || {}); } catch (_e) { /* inofensivo */ }
};

// El panel pide al nativo los ajustes actuales (al abrir la pestaña "Aula 122").
function refresh() {
  _wire();
  _bridge('/settings/native/get');
}
globalThis.aula122SettingsRefresh = refresh;

export default { refresh };
