// idea-page.js — área de Idea de Aula 122 (visor fiel a design/idea.html).
// Lee la API read-only /api/idea (Sinopsis + Tratamiento reales del .starc). La
// EDICIÓN real ocurre en el editor nativo (se abre AL LADO de Odiseo): doble clic
// en la página o en el botón Editar → postMessage 'edit-idea' al SPA padre. Sin
// escritura web. Calcado del patrón de guion-page.js.
(function () {
  'use strict';
  var proyecto = '', docs = { sinopsis: null, tratamiento: null }, current = 'tratamiento';

  function $(id) { return document.getElementById(id); }
  function esc(s) {
    return String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/</g, '&lt;')
      .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }
  function api(path) {
    return fetch(path, { credentials: 'same-origin' }).then(function (r) {
      if (!r.ok) throw new Error('HTTP ' + r.status); return r.json();
    });
  }
  // Pide al SPA padre que actúe (abrir chat de Rita / abrir el editor nativo vía puente).
  function tellParent(action) {
    try { window.parent.postMessage({ aula122: action }, '*'); } catch (e) {}
  }

  // ── etiquetas legibles de cada documento ──
  var LABEL = { sinopsis: 'Sinopsis', tratamiento: 'Tratamiento' };

  // ── carga inicial ──
  function loadIdea() {
    api('/api/idea').then(function (data) {
      proyecto = data.proyecto || '';
      docs.sinopsis = data.sinopsis || { texto: '', palabras: 0, parrafos: [] };
      docs.tratamiento = data.tratamiento || { texto: '', palabras: 0, parrafos: [] };
      renderMeta();
      // Si el tratamiento está vacío pero la sinopsis tiene texto, abre la que tenga contenido.
      if (!(docs.tratamiento.parrafos || []).length && (docs.sinopsis.parrafos || []).length) {
        current = 'sinopsis';
      }
      setTab(current);
    }).catch(function (e) {
      $('doc-area').innerHTML = '<div class="prose"><div class="empty">No pude cargar la idea.<br><small>' +
        esc(e.message) + '</small></div></div>';
    });
  }

  // ── render de la página de prosa (un documento de desarrollo) ──
  function renderDoc(key) {
    var d = docs[key] || { texto: '', palabras: 0, parrafos: [] };
    var parr = d.parrafos || [];
    var html = '<div class="prose">';
    html += '<h1>' + esc(LABEL[key] || key) + '</h1>';
    var srcLabel = key === 'sinopsis'
      ? 'Sinopsis (argumento) del proyecto — documento del .starc'
      : 'Tratamiento del proyecto — documento del .starc';
    html += '<div class="srcnote"><span class="mi">article</span> ' + esc(srcLabel) +
      (proyecto ? ' · ' + esc(proyecto) : '') +
      ' · <b>' + d.palabras + '</b> palabra' + (d.palabras === 1 ? '' : 's') + '</div>';
    if (!parr.length) {
      html += '<div class="empty">Aún no hay ' + esc((LABEL[key] || key).toLowerCase()) +
        ' en este proyecto. Escríbela en el editor nativo o pídesela a la IA.</div>';
    } else {
      parr.forEach(function (p) {
        var t = esc(p.texto);
        if (p.tipo === 'heading') {
          html += '<h2 class="seq">' + t + '</h2>';
        } else {
          html += '<p>' + t + '</p>';
        }
      });
    }
    html += '</div>';
    var area = $('doc-area');
    area.innerHTML = html;
    area.scrollTop = 0;
  }

  // ── metadatos (panel derecho) ──
  function summary(d) {
    if (!d || !(d.parrafos || []).length) return null;
    // Primer párrafo de cuerpo como vistazo; si solo hay encabezados, el primero.
    var body = (d.parrafos || []).filter(function (p) { return p.tipo === 'body'; });
    var first = (body[0] || d.parrafos[0]).texto || '';
    if (first.length > 160) first = first.slice(0, 157).replace(/\s+\S*$/, '') + '…';
    return first;
  }
  function renderMeta() {
    $('meta-titulo').textContent = proyecto || '—';
    var s = summary(docs.sinopsis), t = summary(docs.tratamiento);
    $('meta-sinopsis').innerHTML = s
      ? esc(s) + ' <span class="muted t-caption">· ' + docs.sinopsis.palabras + ' palabras</span>'
      : '<i>(sin redactar)</i>';
    $('meta-tratamiento').innerHTML = t
      ? esc(t) + ' <span class="muted t-caption">· ' + docs.tratamiento.palabras + ' palabras</span>'
      : '<i>(sin redactar)</i>';
  }

  // ── pestañas ──
  function setTab(key) {
    current = key;
    document.querySelectorAll('.dev-tab[data-doc]').forEach(function (b) {
      b.classList.toggle('is-active', b.dataset.doc === key);
    });
    renderDoc(key);
  }

  // ── eventos ──
  document.addEventListener('DOMContentLoaded', function () {
    // Pestañas de documento (Sinopsis / Tratamiento).
    document.querySelectorAll('.dev-tab[data-doc]').forEach(function (b) {
      b.addEventListener('click', function () { setTab(b.dataset.doc); });
    });
    // IA → chat de Rita.
    var tbIa = $('tb-ia'); if (tbIa) tbIa.addEventListener('click', function () { tellParent('chat'); });
    // Editar → editor nativo al lado de Odiseo (sin botón "STARC").
    var tbEdit = $('tb-edit'); if (tbEdit) tbEdit.addEventListener('click', function () { tellParent('edit-idea'); });
    // Doble clic en la página → editar en el editor nativo.
    var area = $('doc-area'); if (area) area.addEventListener('dblclick', function () { tellParent('edit-idea'); });

    loadIdea();
  });
})();
