// js/boveda.js — Visor/editor de la BÓVEDA (memoria creativa) dentro de Odiseo.
// Autocontenido: inyecta su propio modal + estilos. Lee /api/memoria/tree y
// /api/memoria/file (GET) y guarda con POST /api/memoria/file. Renderiza markdown
// con markdownModule. La seguridad (containment + respaldos) vive en el backend.
import markdownModule from './markdown.js';

let _modal = null;
let _treeEl = null;
let _contentEl = null;
let _current = null;     // {path, content, editable}
let _editing = false;

function _injectStyles() {
  if (document.getElementById('boveda-styles')) return;
  const s = document.createElement('style');
  s.id = 'boveda-styles';
  s.textContent = `
    #boveda-modal{position:fixed;inset:0;z-index:9000;display:flex;align-items:center;
      justify-content:center;background:rgba(0,0,0,.55);backdrop-filter:blur(2px);}
    #boveda-modal.hidden{display:none;}
    #boveda-box{width:min(1100px,94vw);height:min(820px,90vh);display:flex;flex-direction:column;
      background:var(--bg,#16161a);color:var(--fg,#e6e6e6);border:1px solid var(--border,#333);
      border-radius:12px;overflow:hidden;box-shadow:0 18px 60px rgba(0,0,0,.5);}
    #boveda-head{display:flex;align-items:center;gap:10px;padding:12px 16px;
      border-bottom:1px solid var(--border,#333);background:var(--panel,#1c1c22);}
    #boveda-head .t{font-weight:600;display:flex;align-items:center;gap:8px;}
    #boveda-head .sub{color:var(--fg-muted,#8a8a93);font-size:.82em;margin-left:4px;}
    #boveda-head .x{margin-left:auto;cursor:pointer;border:none;background:transparent;
      color:var(--fg-muted,#8a8a93);font-size:1.4em;line-height:1;padding:2px 8px;border-radius:6px;}
    #boveda-head .x:hover{background:var(--border,#333);color:var(--fg,#fff);}
    #boveda-body{flex:1;display:flex;min-height:0;}
    #boveda-tree{width:300px;flex-shrink:0;overflow:auto;padding:8px 6px;
      border-right:1px solid var(--border,#333);background:var(--panel,#1c1c22);font-size:.9em;}
    #boveda-content{flex:1;overflow:auto;padding:18px 22px;min-width:0;}
    .bv-row{display:flex;align-items:center;gap:5px;padding:3px 6px;border-radius:6px;cursor:pointer;
      white-space:nowrap;user-select:none;}
    .bv-row:hover{background:var(--border,#2a2a31);}
    .bv-row.sel{background:var(--accent,#3a6df0);color:#fff;}
    .bv-row .tw{width:12px;display:inline-block;text-align:center;color:var(--fg-muted,#8a8a93);}
    .bv-row .nm{overflow:hidden;text-overflow:ellipsis;}
    .bv-file .ic,.bv-dir .ic{opacity:.6;flex-shrink:0;}
    .bv-children{margin-left:12px;}
    .bv-hidden{display:none;}
    #boveda-bar{display:flex;align-items:center;gap:10px;margin-bottom:14px;
      padding-bottom:10px;border-bottom:1px solid var(--border,#333);}
    #boveda-bar .path{color:var(--fg-muted,#8a8a93);font-size:.85em;overflow:hidden;text-overflow:ellipsis;}
    #boveda-bar .sp{margin-left:auto;}
    .bv-btn{cursor:pointer;border:1px solid var(--border,#333);background:var(--panel,#23232a);
      color:var(--fg,#e6e6e6);border-radius:7px;padding:5px 12px;font-size:.85em;}
    .bv-btn:hover{border-color:var(--accent,#3a6df0);}
    .bv-btn.primary{background:var(--accent,#3a6df0);border-color:var(--accent,#3a6df0);color:#fff;}
    #boveda-edit{width:100%;height:calc(100% - 60px);min-height:380px;resize:none;
      background:var(--panel,#1c1c22);color:var(--fg,#e6e6e6);border:1px solid var(--border,#333);
      border-radius:8px;padding:12px;font-family:ui-monospace,SFMono-Regular,Menlo,monospace;
      font-size:.9em;line-height:1.5;box-sizing:border-box;}
    .bv-md{line-height:1.6;}
    .bv-md h1,.bv-md h2,.bv-md h3{border-bottom:1px solid var(--border,#2a2a31);padding-bottom:.2em;}
    .bv-md code{background:var(--panel,#23232a);padding:1px 5px;border-radius:4px;}
    .bv-md pre{background:var(--panel,#1c1c22);padding:12px;border-radius:8px;overflow:auto;}
    .bv-empty{color:var(--fg-muted,#8a8a93);margin-top:40px;text-align:center;}
  `;
  document.head.appendChild(s);
}

function _buildModal() {
  _modal = document.createElement('div');
  _modal.id = 'boveda-modal';
  _modal.className = 'hidden';
  _modal.innerHTML = `
    <div id="boveda-box" role="dialog" aria-label="Bóveda — memoria creativa">
      <div id="boveda-head">
        <span class="t">🗄️ Bóveda<span class="sub" id="boveda-root"></span></span>
        <button class="x" id="boveda-close" title="Cerrar (Esc)">&times;</button>
      </div>
      <div id="boveda-body">
        <div id="boveda-tree"></div>
        <div id="boveda-content"><div class="bv-empty">Elige una nota del árbol para verla.</div></div>
      </div>
    </div>`;
  document.body.appendChild(_modal);
  _treeEl = _modal.querySelector('#boveda-tree');
  _contentEl = _modal.querySelector('#boveda-content');
  _modal.querySelector('#boveda-close').addEventListener('click', close);
  _modal.addEventListener('mousedown', (e) => { if (e.target === _modal) close(); });
  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape' && _modal && !_modal.classList.contains('hidden')) close();
  });
}

export async function open() {
  _injectStyles();
  if (!_modal) _buildModal();
  _modal.classList.remove('hidden');
  await _loadTree();
}

export function close() { if (_modal) _modal.classList.add('hidden'); }

async function _loadTree() {
  _treeEl.innerHTML = '<div class="bv-empty" style="margin-top:18px">Cargando…</div>';
  try {
    const r = await fetch('/api/memoria/tree', { credentials: 'same-origin' });
    if (!r.ok) throw new Error('HTTP ' + r.status);
    const data = await r.json();
    const rootLbl = _modal.querySelector('#boveda-root');
    if (rootLbl) rootLbl.textContent = data.root || '';
    _treeEl.innerHTML = '';
    _renderNodes(data.tree || [], _treeEl, 0);
  } catch (e) {
    _treeEl.innerHTML = '<div class="bv-empty" style="margin-top:18px">No pude cargar la bóveda.<br><small>' +
      (e && e.message ? e.message : '') + '</small></div>';
  }
}

const FILE_IC = '<svg class="ic" width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><path d="M14 2v6h6"/></svg>';
const DIR_IC = '<svg class="ic" width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"/></svg>';

function _renderNodes(nodes, container, depth) {
  nodes.forEach((node) => {
    if (node.type === 'dir') {
      const row = document.createElement('div');
      row.className = 'bv-row bv-dir';
      row.innerHTML = '<span class="tw">▸</span>' + DIR_IC + '<span class="nm">' + _esc(node.name) + '</span>';
      const kids = document.createElement('div');
      kids.className = 'bv-children bv-hidden';
      _renderNodes(node.children || [], kids, depth + 1);
      row.addEventListener('click', (e) => {
        e.stopPropagation();
        const open = kids.classList.toggle('bv-hidden');
        row.querySelector('.tw').textContent = open ? '▸' : '▾';
      });
      container.appendChild(row);
      container.appendChild(kids);
    } else {
      const row = document.createElement('div');
      row.className = 'bv-row bv-file';
      row.dataset.path = node.path;
      row.innerHTML = '<span class="tw"></span>' + FILE_IC + '<span class="nm">' + _esc(node.name) + '</span>';
      row.addEventListener('click', (e) => {
        e.stopPropagation();
        _treeEl.querySelectorAll('.bv-row.sel').forEach((x) => x.classList.remove('sel'));
        row.classList.add('sel');
        _openFile(node.path);
      });
      container.appendChild(row);
    }
  });
}

async function _openFile(path) {
  _editing = false;
  _contentEl.innerHTML = '<div class="bv-empty">Abriendo…</div>';
  try {
    const r = await fetch('/api/memoria/file?path=' + encodeURIComponent(path), { credentials: 'same-origin' });
    if (!r.ok) throw new Error('HTTP ' + r.status);
    _current = await r.json();
    _renderView();
  } catch (e) {
    _contentEl.innerHTML = '<div class="bv-empty">No pude abrir el archivo.<br><small>' +
      (e && e.message ? e.message : '') + '</small></div>';
  }
}

function _bar(rightHtml) {
  return '<div id="boveda-bar"><span class="path">' + _esc(_current.path) + '</span>' +
    '<span class="sp"></span>' + (rightHtml || '') + '</div>';
}

function _renderView() {
  let bodyHtml;
  try { bodyHtml = markdownModule.mdToHtml(_current.content || ''); }
  catch { bodyHtml = '<pre>' + _esc(_current.content || '') + '</pre>'; }
  const editBtn = _current.editable ? '<button class="bv-btn" id="bv-edit-btn">✎ Editar</button>' : '';
  _contentEl.innerHTML = _bar(editBtn) + '<div class="bv-md">' + bodyHtml + '</div>';
  const eb = _contentEl.querySelector('#bv-edit-btn');
  if (eb) eb.addEventListener('click', _renderEditor);
}

function _renderEditor() {
  _editing = true;
  _contentEl.innerHTML = _bar(
    '<button class="bv-btn" id="bv-cancel-btn">Cancelar</button>' +
    '<button class="bv-btn primary" id="bv-save-btn">Guardar</button>'
  ) + '<textarea id="boveda-edit" spellcheck="false"></textarea>';
  const ta = _contentEl.querySelector('#boveda-edit');
  ta.value = _current.content || '';
  ta.focus();
  _contentEl.querySelector('#bv-cancel-btn').addEventListener('click', _renderView);
  _contentEl.querySelector('#bv-save-btn').addEventListener('click', () => _save(ta.value));
}

async function _save(newContent) {
  const btn = _contentEl.querySelector('#bv-save-btn');
  if (btn) { btn.textContent = 'Guardando…'; btn.disabled = true; }
  try {
    const fd = new FormData();
    fd.append('path', _current.path);
    fd.append('content', newContent);
    const r = await fetch('/api/memoria/file', { method: 'POST', body: fd, credentials: 'same-origin' });
    if (!r.ok) {
      const t = await r.text();
      throw new Error(t || ('HTTP ' + r.status));
    }
    _current.content = newContent;
    _editing = false;
    _renderView();
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
