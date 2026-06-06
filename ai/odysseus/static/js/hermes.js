// js/hermes.js — Panel de AJUSTES de Hermes (el agente autonomo) dentro de Odiseo.
// Autocontenido: inyecta su propio modal + estilos. Lee /api/hermes/settings (GET) y
// guarda con POST /api/hermes/settings. Solo expone ajustes de BAJO RIESGO; las llaves
// gestionadas por el cerebro se muestran como solo-lectura. Los cambios se aplican al
// REINICIAR Aula 122 (el shell nativo re-hornea el config.yaml al arrancar).

let _modal = null;
let _bodyEl = null;
let _data = null; // {available, managed, user}
let _dirty = false;

const MCP_LABEL = {
  'aula122-mcp': 'Proyecto (.starc)',
  'memoria-mcp': 'Boveda (memoria-creativa)',
  'notion': 'Notion (Diez50)',
};

function _injectStyles() {
  if (document.getElementById('hermes-styles')) return;
  const s = document.createElement('style');
  s.id = 'hermes-styles';
  s.textContent = `
    #hermes-modal{position:fixed;inset:0;z-index:9000;display:flex;align-items:center;
      justify-content:center;background:rgba(0,0,0,.55);backdrop-filter:blur(2px);}
    #hermes-modal.hidden{display:none;}
    #hermes-box{width:min(760px,94vw);height:min(820px,90vh);display:flex;flex-direction:column;
      background:var(--bg,#16161a);color:var(--fg,#e6e6e6);border:1px solid var(--border,#333);
      border-radius:12px;overflow:hidden;box-shadow:0 18px 60px rgba(0,0,0,.5);}
    #hermes-head{display:flex;align-items:center;gap:10px;padding:12px 16px;
      border-bottom:1px solid var(--border,#333);background:var(--panel,#1c1c22);}
    #hermes-head .t{font-weight:600;display:flex;align-items:center;gap:8px;}
    #hermes-head .sub{color:var(--fg-muted,#8a8a93);font-size:.82em;margin-left:4px;}
    #hermes-head .x{margin-left:auto;cursor:pointer;border:none;background:transparent;
      color:var(--fg-muted,#8a8a93);font-size:1.4em;line-height:1;padding:2px 8px;border-radius:6px;}
    #hermes-head .x:hover{background:var(--border,#333);color:var(--fg,#fff);}
    #hermes-body{flex:1;overflow:auto;padding:18px 22px;min-width:0;}
    .hz-sec{margin:0 0 22px;}
    .hz-sec h3{font-size:.95em;margin:0 0 4px;display:flex;align-items:center;gap:7px;}
    .hz-sec .hint{color:var(--fg-muted,#8a8a93);font-size:.82em;margin:0 0 12px;line-height:1.5;}
    .hz-managed{display:grid;grid-template-columns:auto 1fr;gap:6px 14px;font-size:.86em;
      background:var(--panel,#1c1c22);border:1px solid var(--border,#2a2a31);border-radius:8px;padding:12px 14px;}
    .hz-managed .k{color:var(--fg-muted,#8a8a93);}
    .hz-managed .v{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;overflow-wrap:anywhere;}
    .hz-ok{color:#3fbf6b;} .hz-no{color:#e0664f;}
    .hz-field{margin:0 0 16px;}
    .hz-field .lbl{font-size:.9em;margin-bottom:6px;display:block;}
    .hz-seg{display:inline-flex;border:1px solid var(--border,#333);border-radius:8px;overflow:hidden;}
    .hz-seg button{background:var(--panel,#1c1c22);color:var(--fg,#e6e6e6);border:none;
      padding:6px 16px;font-size:.85em;cursor:pointer;border-right:1px solid var(--border,#333);}
    .hz-seg button:last-child{border-right:none;}
    .hz-seg button.on{background:var(--accent,#3a6df0);color:#fff;}
    .hz-toggle-row{display:flex;align-items:center;gap:10px;padding:9px 0;
      border-bottom:1px solid var(--border,#23232a);}
    .hz-toggle-row:last-child{border-bottom:none;}
    .hz-toggle-row .meta{flex:1;min-width:0;}
    .hz-toggle-row .nm{font-size:.9em;}
    .hz-toggle-row .ds{color:var(--fg-muted,#8a8a93);font-size:.8em;line-height:1.45;margin-top:2px;}
    .hz-warn{color:#d8a32f;font-size:.8em;margin-top:3px;}
    .hz-badge{display:inline-block;margin-left:7px;font-size:.7em;font-weight:600;
      color:#d8a32f;border:1px solid #d8a32f;border-radius:5px;padding:0 5px;vertical-align:middle;}
    .hz-badge.danger{color:#e0664f;border-color:#e0664f;}
    .hz-sw{position:relative;width:40px;height:22px;flex-shrink:0;cursor:pointer;}
    .hz-sw input{opacity:0;width:0;height:0;}
    .hz-sw .tr{position:absolute;inset:0;background:var(--border,#3a3a44);border-radius:22px;transition:.15s;}
    .hz-sw .tr:before{content:"";position:absolute;width:16px;height:16px;left:3px;top:3px;
      background:#fff;border-radius:50%;transition:.15s;}
    .hz-sw input:checked + .tr{background:var(--accent,#3a6df0);}
    .hz-sw input:checked + .tr:before{transform:translateX(18px);}
    #hermes-foot{display:flex;align-items:center;gap:12px;padding:12px 16px;
      border-top:1px solid var(--border,#333);background:var(--panel,#1c1c22);}
    #hermes-foot .note{color:var(--fg-muted,#8a8a93);font-size:.8em;flex:1;line-height:1.4;}
    #hermes-foot .ok{color:#3fbf6b;}
    .hz-btn{cursor:pointer;border:1px solid var(--border,#333);background:var(--panel,#23232a);
      color:var(--fg,#e6e6e6);border-radius:7px;padding:7px 16px;font-size:.85em;}
    .hz-btn:hover{border-color:var(--accent,#3a6df0);}
    .hz-btn.primary{background:var(--accent,#3a6df0);border-color:var(--accent,#3a6df0);color:#fff;}
    .hz-btn[disabled]{opacity:.5;cursor:default;}
    .hz-empty{color:var(--fg-muted,#8a8a93);margin-top:40px;text-align:center;line-height:1.6;}
  `;
  document.head.appendChild(s);
}

function _buildModal() {
  _modal = document.createElement('div');
  _modal.id = 'hermes-modal';
  _modal.className = 'hidden';
  _modal.innerHTML = `
    <div id="hermes-box" role="dialog" aria-label="Ajustes de Hermes">
      <div id="hermes-head">
        <span class="t">Hermes<span class="sub">el agente autonomo</span></span>
        <button class="x" id="hermes-close" title="Cerrar (Esc)">&times;</button>
      </div>
      <div id="hermes-body"></div>
      <div id="hermes-foot">
        <span class="note" id="hermes-note">Los cambios se aplican al reiniciar Aula 122.</span>
        <button class="hz-btn primary" id="hermes-save" disabled>Guardar</button>
      </div>
    </div>`;
  document.body.appendChild(_modal);
  _bodyEl = _modal.querySelector('#hermes-body');
  _modal.querySelector('#hermes-close').addEventListener('click', close);
  _modal.querySelector('#hermes-save').addEventListener('click', _save);
  _modal.addEventListener('mousedown', (e) => { if (e.target === _modal) close(); });
  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape' && _modal && !_modal.classList.contains('hidden')) close();
  });
}

export async function open() {
  _injectStyles();
  if (!_modal) _buildModal();
  _modal.classList.remove('hidden');
  await _load();
}

export function close() { if (_modal) _modal.classList.add('hidden'); }

async function _load() {
  _dirty = false;
  _setSaveState();
  _bodyEl.innerHTML = '<div class="hz-empty">Cargando ajustes...</div>';
  try {
    const r = await fetch('/api/hermes/settings', { credentials: 'same-origin' });
    if (!r.ok) throw new Error('HTTP ' + r.status);
    _data = await r.json();
  } catch (e) {
    _bodyEl.innerHTML = '<div class="hz-empty">No pude cargar los ajustes.<br><small>' +
      (e && e.message ? e.message : '') + '</small></div>';
    return;
  }
  if (!_data || !_data.available) {
    _bodyEl.innerHTML = '<div class="hz-empty">Hermes no esta instalado en este equipo.<br>' +
      '<small>El cerebro corre sin el 4o servicio.</small></div>';
    return;
  }
  _render();
}

function _render() {
  const m = _data.managed || {};
  const u = _data.user || {};
  const cat = Array.isArray(m.mcp_catalog) ? m.mcp_catalog : [];
  const tokenOk = !!m.notion_token_present;

  // --- Gestionado (solo lectura) ---
  let managedRows = '';
  const row = (k, v) => '<span class="k">' + _esc(k) + '</span><span class="v">' + v + '</span>';
  managedRows += row('Modelo', _esc(m.model || '?'));
  managedRows += row('Endpoint', _esc(m.base_url || '?'));
  managedRows += row('Gateway', '127.0.0.1:' + _esc(String(m.api_server_port || '?')));
  managedRows += row('Contexto', _esc(String(m.context_length || '?')) + ' tokens');
  managedRows += row('Token Notion',
    tokenOk ? '<span class="hz-ok">presente</span>' : '<span class="hz-no">ausente</span>');

  // --- tool_search (segmented) ---
  const ts = u.tool_search || 'auto';
  const seg = ['auto', 'on', 'off'].map((v) =>
    '<button data-ts="' + v + '" class="' + (ts === v ? 'on' : '') + '">' + v + '</button>').join('');

  // --- MCP toggles ---
  let mcpRows = '';
  cat.forEach((srv) => {
    const name = srv.name;
    const on = u.mcp_enabled && u.mcp_enabled[name] !== false;
    const warn = (srv.requires_token && !tokenOk)
      ? '<div class="hz-warn">Sin token: aunque este activo, no conectara hasta que exista ~/.config/diez50/notion.env.</div>'
      : '';
    mcpRows +=
      '<div class="hz-toggle-row">' +
        '<div class="meta"><div class="nm">' + _esc(MCP_LABEL[name] || name) + '</div>' +
          '<div class="ds">' + _esc(srv.desc || '') + '</div>' + warn + '</div>' +
        '<label class="hz-sw"><input type="checkbox" data-mcp="' + _esc(name) + '"' +
          (on ? ' checked' : '') + '><span class="tr"></span></label>' +
      '</div>';
  });

  // --- toolsets internos (editable) ---
  const tsetCat = Array.isArray(m.toolset_catalog) ? m.toolset_catalog : [];
  const userTs = Array.isArray(u.toolsets) ? u.toolsets : [];
  let tsetRows = '';
  tsetCat.forEach((t) => {
    const on = userTs.indexOf(t.key) !== -1;
    const riskyBadge = t.risky ? '<span class="hz-badge danger">sensible</span>' : '';
    const heavyBadge = t.heavy ? '<span class="hz-badge">pesado</span>' : '';
    const badge = riskyBadge + heavyBadge;
    tsetRows +=
      '<div class="hz-toggle-row">' +
        '<div class="meta"><div class="nm">' + _esc(t.label) + badge + '</div>' +
          '<div class="ds">' + _esc(t.desc || '') + '</div></div>' +
        '<label class="hz-sw"><input type="checkbox" data-tset="' + _esc(t.key) + '"' +
          (on ? ' checked' : '') + '><span class="tr"></span></label>' +
      '</div>';
  });

  // --- tool_use_enforcement ---
  const tue = u.tool_use_enforcement !== false;

  _bodyEl.innerHTML =
    '<div class="hz-sec">' +
      '<h3>Gestionado por el cerebro</h3>' +
      '<p class="hint">Compartido con Odiseo y fijado por Aula 122. Solo lectura.</p>' +
      '<div class="hz-managed">' + managedRows + '</div>' +
    '</div>' +
    '<div class="hz-sec">' +
      '<h3>Herramientas (MCP)</h3>' +
      '<p class="hint">Lo que Hermes puede tocar de forma autonoma. Apaga uno para cortarle ' +
        'el acceso (p.ej. Notion) sin desinstalar nada.</p>' +
      mcpRows +
    '</div>' +
    '<div class="hz-sec">' +
      '<h3>Herramientas internas (toolsets)</h3>' +
      '<p class="hint">Capacidades propias de Hermes. <b>pesado</b> = suma tokens al ' +
        'prompt (primera respuesta mas lenta; skills ~11K). <b style="color:#e0664f">' +
        'sensible</b> = capacidad potente (ejecutar comandos, actuar en la web): dasela ' +
        'a un modelo que puede equivocarse solo si la necesitas. Lo minimo util es ' +
        'terminal + file; los MCP de arriba no cuentan aqui.</p>' +
      tsetRows +
    '</div>' +
    '<div class="hz-sec">' +
      '<h3>Comportamiento</h3>' +
      '<div class="hz-field"><span class="lbl">Busqueda de herramientas (tool_search)</span>' +
        '<div class="hz-seg" id="hz-tool-search">' + seg + '</div>' +
        '<p class="hint" style="margin-top:7px">auto = recomendado. on las difiere tras un umbral ' +
          '(con pocas tools puede esconderlas del 8B). off siempre visibles.</p>' +
      '</div>' +
      '<div class="hz-toggle-row" style="border:none;padding-top:4px">' +
        '<div class="meta"><div class="nm">Forzar uso de herramientas (tool_use_enforcement)</div>' +
          '<div class="ds">Obliga a Hermes a actuar via tools en vez de solo responder texto.</div></div>' +
        '<label class="hz-sw"><input type="checkbox" id="hz-tue"' + (tue ? ' checked' : '') +
          '><span class="tr"></span></label>' +
      '</div>' +
    '</div>';

  // listeners
  _bodyEl.querySelectorAll('#hz-tool-search button').forEach((b) => {
    b.addEventListener('click', () => {
      _bodyEl.querySelectorAll('#hz-tool-search button').forEach((x) => x.classList.remove('on'));
      b.classList.add('on');
      _markDirty();
    });
  });
  _bodyEl.querySelectorAll('input[type="checkbox"]').forEach((c) => {
    c.addEventListener('change', _markDirty);
  });
}

function _collect() {
  const tsBtn = _bodyEl.querySelector('#hz-tool-search button.on');
  const mcp = {};
  _bodyEl.querySelectorAll('input[data-mcp]').forEach((c) => {
    mcp[c.dataset.mcp] = c.checked;
  });
  const tue = _bodyEl.querySelector('#hz-tue');
  const out = {
    tool_search: tsBtn ? tsBtn.dataset.ts : 'auto',
    tool_use_enforcement: tue ? tue.checked : true,
    mcp_enabled: mcp,
  };
  // Solo incluimos 'toolsets' si la seccion se renderizo (hay toggles). Si el catalogo
  // no estaba disponible (0 filas, p.ej. build viejo) OMITIMOS la clave para que el
  // backend PRESERVE la seleccion previa, en vez de borrarla a "solo MCP" al guardar
  // un cambio no relacionado. (Lista vacia con filas presentes = el usuario los apago.)
  const tsetInputs = _bodyEl.querySelectorAll('input[data-tset]');
  if (tsetInputs.length > 0) {
    const toolsets = [];
    tsetInputs.forEach((c) => { if (c.checked) toolsets.push(c.dataset.tset); });
    out.toolsets = toolsets;
  }
  return out;
}

function _markDirty() { _dirty = true; _setSaveState(); }

function _setSaveState() {
  const btn = _modal && _modal.querySelector('#hermes-save');
  if (btn) btn.disabled = !_dirty;
}

async function _save() {
  const btn = _modal.querySelector('#hermes-save');
  const note = _modal.querySelector('#hermes-note');
  if (btn) { btn.textContent = 'Guardando...'; btn.disabled = true; }
  try {
    const r = await fetch('/api/hermes/settings', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      credentials: 'same-origin',
      body: JSON.stringify(_collect()),
    });
    if (!r.ok) {
      const t = await r.text();
      throw new Error(t || ('HTTP ' + r.status));
    }
    const res = await r.json();
    if (res && res.user) { _data.user = res.user; }
    _dirty = false;
    if (btn) btn.textContent = 'Guardar';
    _setSaveState();
    if (note) {
      note.innerHTML = '<span class="ok">Guardado.</span> Reinicia Aula 122 para aplicar los cambios.';
    }
  } catch (e) {
    if (btn) { btn.textContent = 'Guardar'; btn.disabled = false; }
    alert('No se pudo guardar: ' + (e && e.message ? e.message : e));
  }
}

function _esc(s) {
  return String(s == null ? '' : s)
    .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

export default { open, close };
