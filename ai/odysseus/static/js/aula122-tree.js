// js/aula122-tree.js — Refleja el ÁRBOL de documentos del proyecto (.starc) dentro
// de la barra "Aula 122" de Odiseo. Convierte "Personajes" y "Locaciones" en
// desplegables con los documentos REALES del proyecto (cada personaje, cada locación)
// y añade "Añadir documento". "Guion" es un ítem PLANO (sin desplegable): abre el guion;
// sus sub-documentos (Página de Título · Sinopsis · Tratamiento · Estadísticas) se muestran
// al mismo nivel plano, justo debajo (NO se anidan ni se pierden). El TEXTO del guion no se
// repite como ítem aparte → no hay "doble guion". Todo al mismo nivel que «Añadir documento».
//
// • Clic en la FILA de un grupo (Personajes/Locaciones) → su mapa nativo (relaciones)
//   vía el puente /open/<grupo>, manejado por application_manager; además despliega
//   la lista aquí (igual que STARC: mapa + lista).
// • Clic en el CARET → solo despliega/oculta la lista (sin navegar).
// • Clic en un DOCUMENTO → /open/doc/<uuid> → el shell lo abre en el editor nativo.
//
// Refleja el documento activo (resaltado), pinta un icono por tipo, y se refresca al
// desplegar (para no quedar desfasado tras añadir documentos o cambiar de proyecto).
// Autocontenido: inyecta sus estilos. Lee /api/guion/estructura (SOLO LECTURA).

const BRIDGE = 'http://aula122.bridge';

// Botón de la barra  →  kind del nodo-grupo en /api/guion/estructura.
//   count:true muestra un contador "(28)" junto a la etiqueta.
const GROUPS = [
  { btn: 'aula122-personajes-btn', kind: 'characters', label: 'Personajes', count: true },
  { btn: 'aula122-locaciones-btn', kind: 'locations', label: 'Locaciones', count: true },
];

let _tree = null; // último árbol cargado (lista de nodos top-level) o null
let _wired = false; // los botones ya tienen caret + contenedor de hijos
let _activeKey = null; // documento/grupo resaltado: uuid, o "group:<kind>"
let _projectPath = null; // .starc activo (lo fija el shell nativo al abrir un proyecto)
let _chatCollapsed = false; // chat de Odiseo oculto (la barra/menú SIEMPRE se queda)

// ── Iconos por tipo (inline SVG, mismo trazo fino que la barra) ─────────────
function _svg(inner) {
  return '<svg class="a122-ic" width="13" height="13" viewBox="0 0 24 24" fill="none" ' +
    'stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">' +
    inner + '</svg>';
}
const _IC = {
  character: _svg('<path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/><circle cx="12" cy="7" r="4"/>'),
  location: _svg('<path d="M21 10c0 7-9 13-9 13s-9-6-9-13a9 9 0 0 1 18 0z"/><circle cx="12" cy="10" r="3"/>'),
  'screenplay/title-page': _svg('<path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"/><path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"/>'),
  'screenplay/synopsis': _svg('<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><path d="M14 2v6h6M16 13H8M16 17H8M10 9H8"/>'),
  'screenplay/treatment': _svg('<rect x="3" y="3" width="7" height="7" rx="1"/><rect x="14" y="3" width="7" height="7" rx="1"/><rect x="14" y="14" width="7" height="7" rx="1"/><rect x="3" y="14" width="7" height="7" rx="1"/>'),
  'screenplay/text': _svg('<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><path d="M14 2v6h6M16 13H8M16 17H8"/>'),
  'screenplay/statistics': _svg('<path d="M3 3v18h18"/><path d="M18 17V9M13 17V5M8 17v-3"/>'),
  folder: _svg('<path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"/>'),
  text: _svg('<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><path d="M14 2v6h6M16 13H8M16 17H8M10 9H8"/>'),
};
function _iconFor(kind) {
  return _IC[kind] || _IC.text;
}

function _esc(s) {
  return String(s == null ? '' : s)
    .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

function _bridge(path) {
  const sep = path.includes('?') ? '&' : '?';
  try {
    globalThis.location.href = BRIDGE + path + sep + 't=' + Date.now();
  } catch (_e) {
    // Fuera del shell nativo (navegador normal): la navegación falla, inofensivo.
  }
}

function _injectStyles() {
  if (document.getElementById('aula122-tree-styles')) return;
  const s = document.createElement('style');
  s.id = 'aula122-tree-styles';
  s.textContent = `
    .a122-caret{width:14px;flex-shrink:0;display:inline-flex;align-items:center;
      justify-content:center;color:var(--fg-muted,#8a8a93);cursor:pointer;font-size:9px;
      transition:transform .15s ease;}
    .a122-caret:hover{color:var(--fg,#e6e6e6);}
    .a122-caret.open{transform:rotate(90deg);}
    .a122-count{font-size:10px;opacity:.5;margin-left:6px;font-variant-numeric:tabular-nums;}
    .a122-ic{flex-shrink:0;opacity:.55;}
    .a122-children{display:none;}
    .a122-children.open{display:block;}
    .a122-children .list-item{padding-left:30px;font-size:.92em;opacity:.9;}
    .a122-children .list-item:hover{opacity:1;}
    .a122-children .list-item .grow{overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}
    /* Documento/grupo activo: barra de acento a la izquierda + tinte suave (estado del sistema). */
    .list-item.a122-sel{background:color-mix(in srgb, var(--accent,#3a6df0) 14%, transparent);
      box-shadow:inset 2px 0 0 var(--accent,#3a6df0);}
    .list-item.a122-sel .a122-ic{opacity:.95;color:var(--accent,#3a6df0);}
    .a122-note{padding:4px 8px 6px 30px;font-size:.8em;color:var(--fg-muted,#8a8a93);
      line-height:1.4;}
    .a122-add .grow{color:var(--accent,#3a6df0);}
    .a122-add svg{stroke:var(--accent,#3a6df0);opacity:.9;}
    /* ── Modo "chat oculto": se va el chat, la barra/menú se QUEDA. Todo bajo
       html.aula122-chat-collapsed → sin efecto en el modo normal. ── */
    html.aula122-chat-collapsed #chat-container{display:none !important;}
    html.aula122-chat-collapsed .sidebar{position:relative !important;left:auto !important;
      right:auto !important;top:auto !important;bottom:auto !important;width:100% !important;
      max-width:none !important;transform:none !important;box-shadow:none !important;
      opacity:1 !important;z-index:auto !important;pointer-events:auto !important;}
    /* Con una HERRAMIENTA abierta (expandido) y el chat compactado, el menú vuelve a su ancho normal
       (240px) en vez de 100% → la herramienta tiene sitio a su derecha (si no, quedaba aplastada). */
    html.aula122-chat-collapsed.aula122-expanded .sidebar{width:240px !important;max-width:240px !important;}
    html.aula122-chat-collapsed .hamburger-btn,
    html.aula122-chat-collapsed #sidebar-backdrop,
    html.aula122-chat-collapsed .sidebar-backdrop{display:none !important;}
    /* NOTA: NO re-estilizamos la barra aquí. El panel compactado mide ~240px, que ya queda por
       ENCIMA del breakpoint móvil de la barra (bajado a 200px en style.css), así que la barra usa
       SIEMPRE su layout de escritorio — exactamente igual con el chat abierto o cerrado. Cero
       valores adivinados. */
    /* ── Modo "menú nativo": se OCULTA la barra/menú web de Odiseo y el chat ocupa todo (el menú
       pasa a ser el navegador nativo de STARC). Todo bajo html.aula122-menu-collapsed. ── */
    html.aula122-menu-collapsed .sidebar{display:none !important;}
    html.aula122-menu-collapsed .hamburger-btn,
    html.aula122-menu-collapsed #sidebar-backdrop,
    html.aula122-menu-collapsed .sidebar-backdrop{display:none !important;}
    html.aula122-menu-collapsed #chat-container{position:relative !important;left:0 !important;
      right:auto !important;width:100% !important;max-width:none !important;margin:0 !important;
      transform:none !important;}
    /* Botón flotante en el BORDE derecho de Odiseo (la frontera con el editor nativo) —
       donde estaba el "‹" del splitter. Solo afecta al chat; el menú se queda. */
    .a122-chat-fab{position:fixed;top:50%;right:0;transform:translateY(-50%);z-index:600;
      width:20px;height:56px;display:flex;align-items:center;justify-content:center;
      background:var(--panel,#1c1c22);color:var(--fg-muted,#8a8a93);
      border:1px solid var(--border,#333);border-right:none;border-radius:9px 0 0 9px;
      cursor:pointer;opacity:.6;transition:opacity .15s ease,color .15s ease;}
    .a122-chat-fab:hover{opacity:1;color:var(--fg,#e6e6e6);}
    .a122-chat-fab svg{transition:transform .15s ease;}
    .a122-chat-fab.open svg{transform:rotate(180deg);}
  `;
  document.head.appendChild(s);
}

function _findGroup(kind) {
  if (!Array.isArray(_tree)) return null;
  return _tree.find((n) => n.kind === kind) || null;
}

function _visibleChildren(group) {
  if (!group || !Array.isArray(group.children)) return [];
  return group.children.filter((c) => c.visible !== false);
}

// Resalta la fila (documento o grupo) cuya data-akey coincide; quita el resto.
function _applyActive() {
  const list = document.getElementById('aula122-list');
  if (!list) return;
  list.querySelectorAll('.list-item.a122-sel').forEach((x) => x.classList.remove('a122-sel'));
  if (!_activeKey) return;
  const sel = list.querySelector('[data-akey="' + (globalThis.CSS && CSS.escape
    ? CSS.escape(_activeKey)
    : _activeKey) + '"]');
  if (sel) sel.classList.add('a122-sel');
}

function _setActive(key) {
  _activeKey = key;
  _applyActive();
}

function _docRow(doc) {
  const row = document.createElement('div');
  row.className = 'list-item';
  row.title = doc.name || '';
  if (doc.uuid) row.dataset.akey = doc.uuid;
  row.innerHTML = _iconFor(doc.kind) + '<span class="grow">' + _esc(doc.name || '(sin nombre)') +
    '</span>';
  row.addEventListener('click', () => {
    if (!doc.uuid) return;
    _setActive(doc.uuid);
    _bridge('/open/doc/' + encodeURIComponent(doc.uuid));
  });
  return row;
}

function _renderChildren(g, kids) {
  kids.innerHTML = '';
  if (!Array.isArray(_tree)) {
    kids.innerHTML = '<div class="a122-note">Abre un proyecto para ver sus ' +
      _esc(g.label.toLowerCase()) + '.</div>';
    return;
  }
  const group = _findGroup(g.kind);
  const items = _visibleChildren(group);
  if (!items.length) {
    kids.innerHTML = '<div class="a122-note">Aún no hay ' + _esc(g.label.toLowerCase()) +
      '. Usa «Añadir documento».</div>';
    return;
  }
  items.forEach((doc) => kids.appendChild(_docRow(doc)));
  _applyActive();
}

function _setCount(g) {
  if (!g.count) return;
  const btn = document.getElementById(g.btn);
  if (!btn) return;
  const grow = btn.querySelector('.grow');
  if (!grow) return;
  let badge = grow.querySelector('.a122-count');
  if (!badge) {
    badge = document.createElement('span');
    badge.className = 'a122-count';
    grow.appendChild(badge);
  }
  const n = _visibleChildren(_findGroup(g.kind)).length;
  badge.textContent = n ? '(' + n + ')' : '';
}

function _wireGroups() {
  if (_wired) return;
  GROUPS.forEach((g) => {
    const btn = document.getElementById(g.btn);
    if (!btn) return;
    btn.dataset.akey = 'group:' + g.kind;

    // Caret de despliegue al inicio de la fila.
    const caret = document.createElement('span');
    caret.className = 'a122-caret';
    caret.textContent = '▶';
    caret.setAttribute('role', 'button');
    caret.setAttribute('aria-label', 'Mostrar/ocultar ' + g.label.toLowerCase());
    btn.insertBefore(caret, btn.firstChild);

    // Contenedor de hijos justo después de la fila.
    const kids = document.createElement('div');
    kids.className = 'a122-children';
    kids.id = g.btn + '-children';
    if (btn.parentNode) btn.parentNode.insertBefore(kids, btn.nextSibling);

    // Caret: alterna SIN navegar (no propaga al listener de la fila).
    caret.addEventListener('click', (e) => {
      e.stopPropagation();
      const open = kids.classList.toggle('open');
      caret.classList.toggle('open', open);
      if (open) {
        _renderChildren(g, kids);
        _load(); // refresco en segundo plano (refleja altas / cambio de proyecto)
      }
    });

    // Fila: app.js ya navega a /open/<kind> (grupo nativo = mapa de relaciones). Aquí
    // resaltamos el grupo y desplegamos la lista (STARC: mapa + lista a la vez).
    btn.addEventListener('click', () => {
      _setActive('group:' + g.kind);
      kids.classList.add('open');
      caret.classList.add('open');
      _renderChildren(g, kids);
      _load();
    });
  });
  _wired = true;
}

// Documentos SUELTOS de nivel superior (los que el usuario añade quedan aquí, al mismo nivel que
// el Guion). Excluimos los grupos ya representados por la barra y el primer guion (= "Guion").
const _HANDLED_KINDS = new Set(['project', 'characters', 'locations', 'worlds', 'recycle-bin']);
function _looseTopDocs() {
  if (!Array.isArray(_tree)) return [];
  let screenplaySeen = false;
  const out = [];
  for (const node of _tree) {
    if (_HANDLED_KINDS.has(node.kind)) continue;
    if (node.kind === 'screenplay' && !screenplaySeen) {
      screenplaySeen = true; // el primer guion ya es la fila "Guion"
      continue;
    }
    if (node.visible === false) continue;
    out.push(node);
  }
  return out;
}

function _renderLooseDocs() {
  const box = document.getElementById('aula122-loose-docs');
  if (!box) return;
  box.innerHTML = '';
  _looseTopDocs().forEach((doc) => box.appendChild(_docRow(doc)));
  _applyActive();
}

// ── Sub-documentos del GUION, promovidos al nivel PLANO ──────────────────────
// Al quitar el desplegable de "Guion" sus partes NO se pierden: se muestran como ítems planos
// (Página de Título · Sinopsis · Tratamiento · Estadísticas) justo debajo del botón "Guion", al
// mismo nivel que «Añadir documento». Saltamos el TEXTO del guion (screenplay/text), que ya ES
// el botón "Guion" → así no reaparece el "doble guion".
const _GUION_TEXT_KIND = 'screenplay/text';
function _guionParts() {
  const sp = _findGroup('screenplay');
  if (!sp || !Array.isArray(sp.children)) return [];
  return sp.children.filter((c) => c.visible !== false && c.kind !== _GUION_TEXT_KIND);
}

function _injectGuionParts() {
  if (document.getElementById('aula122-guion-parts')) return;
  const guionBtn = document.getElementById('aula122-guion-btn');
  if (!guionBtn || !guionBtn.parentNode) return;
  const box = document.createElement('div');
  box.id = 'aula122-guion-parts';
  guionBtn.parentNode.insertBefore(box, guionBtn.nextSibling);
}

function _renderGuionParts() {
  const box = document.getElementById('aula122-guion-parts');
  if (!box) return;
  box.innerHTML = '';
  _guionParts().forEach((doc) => box.appendChild(_docRow(doc)));
  _applyActive();
}

const _ADD_IC =
  '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" ' +
  'stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="flex-shrink:0;">' +
  '<path d="M12 5v14M5 12h14"/></svg>';
const _SAVE_IC =
  '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" ' +
  'stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="flex-shrink:0;opacity:.6;">' +
  '<path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z"/>' +
  '<polyline points="17 21 17 13 7 13 7 21"/><polyline points="7 3 7 8 15 8"/></svg>';
const _EXPORT_IC =
  '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" ' +
  'stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="flex-shrink:0;opacity:.6;">' +
  '<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/>' +
  '<polyline points="17 8 12 3 7 8"/><line x1="12" y1="3" x2="12" y2="15"/></svg>';

function _actionRow(id, label, iconSvg, path, extraClass) {
  const row = document.createElement('div');
  row.className = 'list-item' + (extraClass ? ' ' + extraClass : '');
  row.id = id;
  row.innerHTML = iconSvg + '<span class="grow">' + label + '</span>';
  row.addEventListener('click', () => _bridge(path));
  return row;
}

function _injectDocActions() {
  if (document.getElementById('aula122-add-doc-btn')) return;
  const locBtn = document.getElementById('aula122-locaciones-btn');
  const anchor = document.getElementById('aula122-locaciones-btn-children') || locBtn;
  if (!anchor || !anchor.parentNode) return;
  const parent = anchor.parentNode;
  const after = anchor.nextSibling;

  // Contenedor de documentos sueltos de nivel superior (sin sangría → mismo nivel que el Guion).
  const loose = document.createElement('div');
  loose.id = 'aula122-loose-docs';

  const add = _actionRow('aula122-add-doc-btn', 'Añadir documento', _ADD_IC,
    '/open/add-document', 'a122-add');
  add.title = 'Añadir un documento al proyecto (queda al mismo nivel que el Guion)';
  const save = _actionRow('aula122-save-btn', 'Guardar', _SAVE_IC, '/project/save');
  save.title = 'Guardar el proyecto';
  const exp = _actionRow('aula122-export-btn', 'Exportar', _EXPORT_IC, '/project/export');
  exp.title = 'Exportar el documento actual (PDF, DOCX, FDX…)';

  // Orden tras Locaciones: [documentos sueltos] · Añadir documento · Guardar · Exportar.
  [loose, add, save, exp].forEach((el) => parent.insertBefore(el, after));
}

// ── Acciones de APP (antes solo en el ☰ nativo) — ahora desde el menú ÚNICO de Odiseo ──────
// Cada fila navega a /action/<nombre>; el shell la enruta al MISMO slot del ☰ (paridad total, el
// ☰ y la barra de macOS siguen como respaldo). Se agrupan al final, tras "Ajustes".
function _ai(inner) {
  return '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" ' +
    'stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="flex-shrink:0;opacity:.6;">' +
    inner + '</svg>';
}
const _APP_ACTIONS = [
  { id: 'importar', label: 'Importar…', action: 'import',
    icon: _ai('<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/>') },
  { id: 'guardar-como', label: 'Guardar como…', action: 'save-as',
    icon: _ai('<path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z"/><polyline points="17 21 17 13 7 13 7 21"/><polyline points="7 3 7 8 15 8"/>') },
  { id: 'pantalla-completa', label: 'Pantalla completa', action: 'fullscreen',
    icon: _ai('<path d="M8 3H5a2 2 0 0 0-2 2v3m18 0V5a2 2 0 0 0-2-2h-3m0 18h3a2 2 0 0 0 2-2v-3M3 16v3a2 2 0 0 0 2 2h3"/>') },
  { id: 'asistente', label: 'Asistente', action: 'assistant',
    icon: _ai('<path d="M12 8V4H8"/><rect x="4" y="8" width="16" height="12" rx="2"/><path d="M2 14h2M20 14h2M15 13v2M9 13v2"/>') },
  { id: 'estadisticas', label: 'Estadísticas', action: 'stats',
    icon: _ai('<path d="M3 3v18h18"/><path d="M18 17V9M13 17V5M8 17v-3"/>') },
  { id: 'sprint', label: 'Sprint de escritura', action: 'sprint',
    icon: _ai('<circle cx="12" cy="13" r="8"/><path d="M12 9v4l2 2M5 3 2 6M22 6l-3-3"/>') },
  { id: 'cuenta', label: 'Cuenta', action: 'account',
    icon: _ai('<path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/><circle cx="12" cy="7" r="4"/>') },
];
function _injectAppActions() {
  if (document.getElementById('aula122-app-actions')) return;
  const ajustes = document.getElementById('aula122-ajustes-btn');
  if (!ajustes || !ajustes.parentNode) return;
  const box = document.createElement('div');
  box.id = 'aula122-app-actions';
  _APP_ACTIONS.forEach((a) => {
    const row = _actionRow('aula122-act-' + a.id, a.label, a.icon, '/action/' + a.action);
    box.appendChild(row);
  });
  // Tras "Ajustes" (último ítem de la sección Aula 122).
  ajustes.parentNode.insertBefore(box, ajustes.nextSibling);
}

async function _load() {
  try {
    // Si el shell nativo nos dijo qué proyecto está abierto, pedimos ESE (no el más
    // reciente por fecha) → la barra siempre coincide con el proyecto del editor.
    const url = '/api/guion/estructura' +
      (_projectPath ? '?proyecto=' + encodeURIComponent(_projectPath) : '');
    const r = await fetch(url, { credentials: 'same-origin' });
    if (!r.ok) {
      _tree = null; // sin proyecto / sin estructura: los grupos quedan vacíos
    } else {
      const data = await r.json();
      _tree = Array.isArray(data.arbol) ? data.arbol : [];
    }
  } catch (_e) {
    _tree = null;
  }
  // Actualiza contadores y re-pinta los grupos que estén abiertos.
  GROUPS.forEach((g) => {
    _setCount(g);
    const kids = document.getElementById(g.btn + '-children');
    if (kids && kids.classList.contains('open')) _renderChildren(g, kids);
  });
  _renderGuionParts(); // partes del guion (portada/sinopsis/tratamiento/estadísticas), planas
  _renderLooseDocs(); // documentos sueltos de nivel superior (altas del usuario)
  _applyActive();
}

// API pública ───────────────────────────────────────────────────────────────

// ── Compactar el CHAT dejando el MENÚ ───────────────────────────────────────
// El botón "‹" nativo escondía TODO Odiseo (chat + menú). Aquí el menú SIEMPRE se
// queda: ocultamos solo el chat y avisamos al shell para que encoja el panel de Odiseo
// al ancho de la barra (240px) → el editor nativo gana ese espacio.
const _CHAT_CHEVRON =
  '<svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" ' +
  'stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M15 18l-6-6 6-6"/></svg>';

function _setChatCollapsed(collapsed, quiet) {
  _chatCollapsed = !!collapsed;
  document.documentElement.classList.toggle('aula122-chat-collapsed', _chatCollapsed);
  if (_chatCollapsed) {
    // El menú SIEMPRE se queda: forzamos la barra visible (por si el auto-colapso del SPA la
    // había escondido) y quitamos el estado "sidebar-collapsed". El guard en sidebar-layout.js
    // impide que el resize a ~260px la vuelva a esconder.
    const sb = document.getElementById('sidebar');
    if (sb) sb.classList.remove('hidden');
    document.body.classList.remove('sidebar-collapsed');
  }
  const btn = document.getElementById('aula122-chat-toggle');
  if (btn) {
    btn.classList.toggle('open', _chatCollapsed);
    btn.title = _chatCollapsed
      ? 'Mostrar el chat de Odiseo'
      : 'Ocultar el chat de Odiseo (el menú se queda)';
  }
  // Avisa al shell nativo para que ajuste el ancho — salvo que el cambio VENGA del nativo (quiet),
  // que ya ajustó el ancho (evita el rebote al puente).
  if (!quiet) _bridge('/odiseo/chat?collapsed=' + (_chatCollapsed ? '1' : '0'));
}

function _injectChatToggle() {
  if (document.getElementById('aula122-chat-toggle')) return;
  // Botón FLOTANTE en el borde derecho de Odiseo (la frontera con el editor nativo), donde
  // estaba el "‹" del splitter. Vive en <body> (fixed) → presente en TODAS las pestañas.
  const btn = document.createElement('button');
  btn.className = 'a122-chat-fab';
  btn.id = 'aula122-chat-toggle';
  btn.title = 'Ocultar el chat de Odiseo (el menú se queda)';
  btn.innerHTML = _CHAT_CHEVRON;
  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    _setChatCollapsed(!_chatCollapsed);
  });
  document.body.appendChild(btn);
}

// ── Auto-descompactar el chat al ABRIR/crear una conversación ────────────────
// Si el chat está compactado (el usuario lo ocultó para dar aire al editor de STARC) y
// luego pica un chat de la lista —o "New Chat"—, el chat debe REAPARECER solo: si no, la
// conversación se abre en un panel oculto y parece que "no pasa nada". Enganchamos con
// delegación en nodos ESTABLES (la barra y el icon-rail) → sobrevive al re-render de la
// lista de sesiones. Solo actúa cuando _chatCollapsed está activo → cero efecto en normal.
const _OPEN_SESSION_SKIP = '.item-drag-handle,.session-fav,.hamburger,.session-dropdown,' +
  '.session-rename-input,.session-select-cb,button';
const _NEW_CHAT_SEL = '#chat-new-btn,#sidebar-new-chat-btn,#sidebar-brand-btn,#rail-new-session';

function _opensConversation(target) {
  if (!target || !target.closest) return false;
  // Botón de "chat nuevo" → necesita el chat visible.
  if (target.closest(_NEW_CHAT_SEL)) return true;
  // Clic en la FILA de una sesión (no en sus controles: menú, estrella, arrastre, selección…)
  // → abre esa conversación. Mismas exclusiones que el handler nativo de la fila.
  const row = target.closest('.session-item');
  return !!row && !target.closest(_OPEN_SESSION_SKIP);
}

function _wireChatAutoExpand() {
  ['sidebar', 'icon-rail'].forEach((id) => {
    const host = document.getElementById(id);
    if (!host || host.dataset.a122ChatExpand) return;
    host.dataset.a122ChatExpand = '1';
    host.addEventListener('click', (e) => {
      if (!_chatCollapsed) return; // solo cuando el chat está compactado
      if (_opensConversation(e.target)) _setChatCollapsed(false);
    });
  });
}

// ── Auto-pantalla-completa de Odiseo al abrir una HERRAMIENTA ────────────────
// Cuando se abre cualquier herramienta de Odiseo (Brain, Email, Calendario, Tasks, Gallery,
// Cookbook, Compare, Deep Research, Ajustes, Biblioteca…), su modal es position:fixed pero queda
// atrapado en el panel izquierdo. Pedimos al shell nativo que dé TODO el ancho al panel de Odiseo
// (el editor nativo se oculta detrás) → la herramienta cubre toda el área con la barra/menú visible,
// igual que en Odiseo standalone. Al cerrar la ÚLTIMA, restauramos. Solo embebido; fuera, no-op.
// Robusto a TODAS las vías de cierre (X, swipe, Escape, dock) porque observamos la clase/estilo del
// modal en lugar de escuchar un evento concreto.
const _TOOL_MODAL_IDS = new Set([
  'cookbook-modal', 'calendar-modal', 'gallery-modal', 'tasks-modal', 'doclib-modal',
  'memory-modal', 'email-lib-modal', 'research-overlay', 'theme-modal', 'settings-modal',
  'compare-model-overlay',
]);
let _toolExpanded = false; // último estado de expansión enviado al nativo (evita reenvíos)
let _toolExpandTimer = null; // debounce para no rebotar al cambiar de una herramienta a otra

function _aToolIsOpen() {
  for (const id of _TOOL_MODAL_IDS) {
    const m = document.getElementById(id);
    if (!m) continue;
    if (m.classList.contains('modal-minimized')) continue; // minimizado al dock ≠ abierto
    if (!m.classList.contains('hidden') && getComputedStyle(m).display !== 'none') return true;
  }
  // Notas no es .modal: su panel abierto marca body.notes-view.
  if (document.body.classList.contains('notes-view')) return true;
  return false;
}

function _syncToolExpand() {
  const open = _aToolIsOpen();
  if (open === _toolExpanded) return; // sin cambio neto → no molestar al nativo
  _toolExpanded = open;
  // Marca "herramienta abierta" para el CSS (devuelve el menú a su ancho normal aunque el chat esté
  // compactado → la herramienta tiene sitio en vez de quedar aplastada por el sidebar 100%).
  document.documentElement.classList.toggle('aula122-expanded', open);
  _bridge('/odiseo/expand?on=' + (open ? '1' : '0'));
}

function _scheduleToolExpandSync() {
  if (_toolExpandTimer) clearTimeout(_toolExpandTimer);
  // ~60ms: coalesce el solape al CAMBIAR de una herramienta a otra (una se oculta y otra se muestra
  // en el mismo tick) → no restauramos el editor entre ambas.
  _toolExpandTimer = setTimeout(_syncToolExpand, 60);
}

function _wireToolExpand() {
  if (!document.documentElement.classList.contains('aula122-embedded')) return; // no-op fuera del shell
  if (document.documentElement.dataset.a122ToolExpand) return; // idempotente
  document.documentElement.dataset.a122ToolExpand = '1';
  const obs = new MutationObserver((muts) => {
    for (const m of muts) {
      if (m.type !== 'attributes') continue;
      const t = m.target;
      if (!(t instanceof HTMLElement)) continue;
      // Cambios de clase/estilo de cualquier .modal, de un contenedor de herramienta por ID, o del
      // <body> (notes-view) → recomputar.
      if (t === document.body || t.classList.contains('modal') || _TOOL_MODAL_IDS.has(t.id)) {
        _scheduleToolExpandSync();
        return;
      }
    }
  });
  obs.observe(document.body, { subtree: true, attributes: true, attributeFilter: ['class', 'style'] });
}

// ── Aula 122 (opción A): FIJAR el menú completo abierto en el shell embebido ──────────────────
// Varias rutas de Odiseo (auto-acople de herramientas, etc.) ocultan el sidebar dejándolo como barra
// de iconos. El usuario quiere el menú COMPLETO siempre. Observamos su clase: si algo le pone
// "hidden" estando embebidos (y NO en chat-compactado/menú-nativo, que gestionan el sidebar aparte),
// se lo quitamos al instante. Re-quitar "hidden" no vuelve a disparar acción → sin bucle.
function _pinSidebarOpen() {
  const root = document.documentElement.classList;
  if (!root.contains('aula122-embedded')) return;
  const sb = document.getElementById('sidebar');
  if (!sb || document.documentElement.dataset.a122PinSidebar) return;
  document.documentElement.dataset.a122PinSidebar = '1';
  const reShow = () => {
    // Solo NO forzamos en modo "menú nativo" (ahí el sidebar se oculta a propósito). En chat
    // compactado SÍ lo mantenemos visible: el menú completo debe quedarse aunque se abra una tool.
    if (root.contains('aula122-menu-collapsed')) return;
    if (sb.classList.contains('hidden')) {
      sb.classList.remove('hidden');
      document.body.classList.remove('sidebar-collapsed');
      try { if (globalThis.syncRailSide) globalThis.syncRailSide(); } catch (_e) { /* noop */ }
    }
  };
  const obs = new MutationObserver(reShow);
  obs.observe(sb, { attributes: true, attributeFilter: ['class'] });
  obs.observe(document.body, { attributes: true, attributeFilter: ['class'] });
}

// El shell nativo llama a esto al ABRIR un proyecto: fija el .starc activo y recarga la
// barra para ese proyecto (arregla "abro Tales y las pestañas siguen mostrando EDLP").
function _setProject(path) {
  const next = path || null;
  if (next === _projectPath) {
    _load(); // mismo proyecto: refresca por si hubo altas
    return;
  }
  _projectPath = next;
  _activeKey = null; // el documento activo del proyecto anterior ya no aplica
  _load();
}

// ── Modo "menú nativo": ocultar la barra/menú WEB de Odiseo (el menú pasa a ser el navegador nativo
// de STARC); el chat se queda y ocupa todo. Lo invoca el shell nativo al abrir un proyecto.
function _setMenuCollapsed(collapsed) {
  document.documentElement.classList.toggle('aula122-menu-collapsed', !!collapsed);
  // En modo menú-nativo el chat es lo ÚNICO de Odiseo → asegurar que se vea (quitar chat-collapse).
  if (collapsed) document.documentElement.classList.remove('aula122-chat-collapsed');
}

export function init() {
  // Aula 122 (opción A del usuario): el MENÚ debe estar SIEMPRE visible, en cualquier pestaña (IA o
  // STARC). Marcamos el shell embebido y forzamos la barra visible; el guard en sidebar-layout.js
  // evita que el auto-colapso por ancho la esconda.
  document.documentElement.classList.add('aula122-embedded');
  const _sb = document.getElementById('sidebar');
  if (_sb) _sb.classList.remove('hidden');
  document.body.classList.remove('sidebar-collapsed');
  _injectStyles();
  _wireGroups();
  _injectDocActions();
  _injectAppActions();
  _injectGuionParts();
  _injectChatToggle();
  _wireChatAutoExpand();
  _wireToolExpand();
  _pinSidebarOpen();
  // El shell nativo llama a estas tras abrir un proyecto / añadir un documento.
  globalThis.aula122RefreshTree = refresh;
  globalThis.aula122SetProject = _setProject;
  globalThis.aula122SetMenuCollapsed = _setMenuCollapsed;
  globalThis.aula122SetChatCollapsed = function (c) { _setChatCollapsed(!!c, true); };
  _load();
}

export async function refresh() {
  await _load();
  // Respaldo: el alta de un documento puede tardar un pelín más en quedar visible en el .startc;
  // un reintento evita que la barra se quede con la vista vieja sin que el usuario tenga que hacer
  // nada. Barato (lectura solo-lectura) y solo tras un refresco explícito.
  setTimeout(() => { _load(); }, 1200);
}

export default { init, refresh };
