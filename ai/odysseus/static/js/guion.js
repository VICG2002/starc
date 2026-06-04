// js/guion.js — abre la página de Guion de Aula 122 (fiel a design/guion.html)
// como un iframe que ocupa el ÁREA PRINCIPAL, al lado de la barra de chats
// (no es modal ni otra pestaña). La página vive en /static/aula122/guion.html y
// lee la API read-only /api/guion/*. Editar abre el editor de texto NATIVO,
// embebido al lado de Odiseo en la misma ventana (M2), vía el puente.

let _overlay = null;
let _iframe = null;
let _msgWired = false;

function _host() {
  // El área principal del SPA (a la derecha de la barra de chats).
  return document.getElementById('chat-container') || document.body;
}

function _build() {
  const host = _host();
  if (getComputedStyle(host).position === 'static') host.style.position = 'relative';
  _overlay = document.createElement('div');
  _overlay.id = 'aula122-guion-overlay';
  _overlay.style.cssText =
    'position:absolute;inset:0;z-index:60;display:flex;flex-direction:column;background:#fff;';
  _overlay.innerHTML =
    '<div style="display:flex;align-items:center;gap:10px;padding:6px 12px;border-bottom:1px solid rgba(0,0,0,.12);' +
    'background:#f3f3f3;color:#222;font-size:13px;flex:none;">' +
    '<button id="aula122-back" style="border:0;background:transparent;cursor:pointer;color:#2f6fd6;' +
    'display:inline-flex;align-items:center;gap:5px;font-size:13px;padding:4px 8px;border-radius:6px;">' +
    '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" ' +
    'stroke-linecap="round" stroke-linejoin="round"><line x1="19" y1="12" x2="5" y2="12"/>' +
    '<polyline points="12 19 5 12 12 5"/></svg> Chat</button>' +
    '<b style="font-weight:600;">Aula 122 · Guion</b></div>' +
    '<iframe id="aula122-guion-frame" src="about:blank" ' +
    'style="flex:1;width:100%;border:0;background:#fff;"></iframe>';
  host.appendChild(_overlay);
  _iframe = _overlay.querySelector('#aula122-guion-frame');
  _overlay.querySelector('#aula122-back').addEventListener('click', close);
  if (!_msgWired) {
    window.addEventListener('message', (e) => {
      const a = e.data && e.data.aula122;
      if (a === 'chat') close();
      else if (a === 'edit-guion') {
        // Puente → editor de texto NATIVO, embebido al lado de Odiseo (M2).
        try { window.location.href = 'http://aula122.bridge/open/guion?t=' + Date.now(); } catch (_e) {}
      } else if (a === 'personajes') {
        // Etapa Personajes (se construye en M5); por ahora el puente la rutea cuando exista.
        try { window.location.href = 'http://aula122.bridge/open/personajes?t=' + Date.now(); } catch (_e) {}
      }
    });
    _msgWired = true;
  }
}

export function open() {
  if (!_overlay) _build();
  // (Re)cargar para datos frescos del proyecto activo.
  if (_iframe.getAttribute('src') !== '/static/aula122/guion.html') {
    _iframe.src = '/static/aula122/guion.html';
  }
  _overlay.style.display = 'flex';
}

export function close() {
  if (_overlay) _overlay.style.display = 'none';
}

export default { open, close };
