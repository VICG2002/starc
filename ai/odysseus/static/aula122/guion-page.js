// guion-page.js — área de Guion de Aula 122 (visor fiel a design/guion.html).
// Lee la API read-only /api/guion/* (datos reales del .starc). La EDICIÓN real
// ocurre en el editor nativo (se abre AL LADO de Odiseo): doble clic en la página
// o en una acción de edición → postMessage 'edit-guion' al SPA padre. Sin escritura web.
(function () {
  'use strict';
  var scenes = [], stats = null, proyecto = '', chars = null, panel = 'estadisticas';

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

  // ── carga inicial ──
  function loadEscenas() {
    api('/api/guion/escenas').then(function (data) {
      scenes = data.escenas || []; stats = data.stats || null; proyecto = data.proyecto || '';
      renderTree(); renderStats();
      $('status-left').innerHTML = scenes.length + ' escenas&nbsp;&nbsp;·&nbsp;&nbsp;<b>' + esc(proyecto) + '</b>';
      if (scenes.length) openScene(scenes[0].numero);
      else $('ed-pagewrap').innerHTML = '<div class="sp-page"><div class="sp-empty">Este proyecto no tiene escenas.</div></div>';
    }).catch(function (e) {
      $('ed-tree').innerHTML = '<div class="grp">Error</div>';
      $('ed-pagewrap').innerHTML = '<div class="sp-page"><div class="sp-empty">No pude cargar el guion.<br><small>' + esc(e.message) + '</small></div></div>';
    });
  }

  function renderTree() {
    var html = '<div class="grp">' + esc(proyecto || 'Escenas') + '</div>';
    scenes.forEach(function (s) {
      var head = (s.encabezado || ('Escena ' + s.numero)).replace(/\s+/g, ' ').trim();
      html += '<div class="scene" data-n="' + s.numero + '"><span class="n">' + s.numero +
        '</span><span class="h" title="' + esc(head) + '">' + esc(head) + '</span></div>';
    });
    var tree = $('ed-tree'); tree.innerHTML = html;
    tree.querySelectorAll('.scene').forEach(function (row) {
      row.addEventListener('click', function () { openScene(parseInt(row.dataset.n, 10)); });
      row.addEventListener('dblclick', function () { tellParent('edit-guion'); });
    });
  }

  function openScene(numero) {
    document.querySelectorAll('.ed-tree .scene').forEach(function (x) {
      x.classList.toggle('is-active', parseInt(x.dataset.n, 10) === numero);
    });
    $('status-right').textContent = 'Escena ' + numero + ' de ' + scenes.length;
    $('ed-pagewrap').innerHTML = '<div class="sp-page"><div class="sp-empty">Abriendo escena ' + numero + '…</div></div>';
    api('/api/guion/escena/' + numero).then(function (data) {
      renderPage(numero, data.bloques || []);
    }).catch(function () {
      $('ed-pagewrap').innerHTML = '<div class="sp-page"><div class="sp-empty">No pude abrir la escena.</div></div>';
    });
  }

  function renderPage(numero, bloques) {
    var html = '', slug = false;
    bloques.forEach(function (b) {
      var tag = b[0], t = esc(b[1]);
      if (!t && tag !== 'scene_heading') return;
      if (tag === 'scene_heading') {
        html += '<p class="sp-slug"><span>' + numero + '&nbsp;&nbsp;&nbsp;' + (t || '').toUpperCase() +
          '</span><span class="pg">' + numero + '</span></p>'; slug = true;
      } else if (tag === 'action') { html += '<p class="sp-action">' + t + '</p>'; }
      else if (tag === 'character') { html += '<p class="sp-char">' + t + '</p>'; }
      else if (tag === 'parenthetical') { html += '<p class="sp-paren">' + t + '</p>'; }
      else if (tag === 'dialogue' || tag === 'lyrics') { html += '<p class="sp-dialog">' + t + '</p>'; }
      else { html += '<p class="sp-action">' + t + '</p>'; }
    });
    if (!slug) html = '<p class="sp-slug"><span>' + numero + '</span><span class="pg">' + numero + '</span></p>' + html;
    var wrap = $('ed-pagewrap');
    wrap.innerHTML = '<div class="sp-page">' + (html || '<div class="sp-empty">Escena vacía.</div>') + '</div>';
    wrap.scrollTop = 0;
  }

  // ── Estadísticas (layout calcado del diseño) ──
  function bar(label, value, max, alt, color) {
    var pct = max > 0 ? Math.round((value / max) * 100) : 0;
    var st = 'width:' + pct + '%' + (color ? ';background:' + color : '');
    return '<div class="sline"><div class="lbl"><span>' + esc(label) + '</span><span>' + value +
      '</span></div><div class="sbar' + (alt ? ' alt' : '') + '"><i style="' + st + '"></i></div></div>';
  }
  function cell(b, l) { return '<div class="cell"><div class="b">' + esc(String(b)) + '</div><div class="l">' + esc(l) + '</div></div>'; }

  function renderStats() {
    if (!stats) { $('stats-body').innerHTML = '<div class="muted">Sin estadísticas.</div>'; return; }
    var s = stats;
    var html = '<div class="sgrid">' +
      cell(s.escenas, 'Escenas') + cell('~' + s.paginas_estimadas, 'Páginas') +
      cell(s.duracion_min_estimada + ' min', 'Duración est.') +
      cell(s.catalogo_personajes != null ? s.catalogo_personajes : s.personajes_con_dialogo, 'Personajes') + '</div>';

    var extTotal = (s.ext || 0) + (s.int_ext || 0);
    var ie = Math.max(s.int || 0, extTotal, 1);
    html += '<div class="sect">Interior / Exterior</div>' +
      bar('INT.', s.int || 0, ie) + bar('EXT. (incl. mixtas)', extTotal, ie);

    var dn = Math.max(s.noche || 0, s.dia || 0, s.sin_definir || 0, 1);
    html += '<div class="sect">Día / Noche</div>' +
      bar('Noche', s.noche || 0, dn, true) + bar('Día', s.dia || 0, dn, true);
    if (s.sin_definir) html += bar('Otras (continuo, flashback…)', s.sin_definir, dn, true, '#bdbdbd');

    var rep = (s.reparto || []).slice(0, 8);
    if (rep.length) {
      var rm = rep[0].escenas || 1;
      html += '<div class="sect">Reparto por presencia (escenas)</div>';
      rep.forEach(function (r) { html += bar(r.nombre, r.escenas, rm); });
    }
    if ((s.noche || 0) > (s.dia || 0)) {
      html += '<p class="t-caption muted" style="margin-top:var(--px12)">Calculado del guion. <b>Predominio nocturno</b> (' +
        s.noche + ' noche vs ' + s.dia + ' día) — pésalo al planear iluminación y jornadas.</p>';
    }
    $('stats-body').innerHTML = html;
  }

  // ── Personajes ──
  function renderPersonajes() {
    var body = $('chars-body');
    if (chars === null) {
      body.innerHTML = '<div class="muted">Cargando…</div>';
      api('/api/guion/personajes').then(function (data) {
        chars = data.personajes || []; $('chars-count').textContent = chars.length; paintChars();
      }).catch(function () { body.innerHTML = '<div class="muted">No pude cargar personajes.</div>'; });
    } else { paintChars(); }
  }
  function paintChars() {
    var body = $('chars-body');
    if (!chars.length) { body.innerHTML = '<div class="muted">Sin personajes en el catálogo.</div>'; return; }
    var html = '<div class="chl">';
    chars.forEach(function (c, i) {
      html += '<button class="' + (i === 0 ? 'is-active' : '') + '" data-nm="' + esc(c.nombre) + '">' + esc(c.nombre) + '</button>';
    });
    html += '</div><div class="cdetail" id="cdetail"></div>';
    body.innerHTML = html;
    body.querySelectorAll('.chl button').forEach(function (b) {
      b.addEventListener('click', function () {
        body.querySelectorAll('.chl button').forEach(function (x) { x.classList.remove('is-active'); });
        b.classList.add('is-active'); openPersonaje(b.dataset.nm);
      });
    });
    openPersonaje(chars[0].nombre);
  }
  function openPersonaje(nombre) {
    var cd = $('cdetail'); if (!cd) return;
    cd.innerHTML = '<div class="muted">Cargando…</div>';
    api('/api/guion/personaje/' + encodeURIComponent(nombre)).then(function (d) {
      var meta = [d.rol, d.edad ? d.edad + ' años' : '', d.genero && d.genero !== '—' ? d.genero : '']
        .filter(Boolean).join(' · ');
      var html = '<div class="nm">' + esc((d.nombre || '').toUpperCase()) + '</div>';
      html += '<div class="muted t-caption">' + esc(meta || '—') + '</div>';
      html += '<label>Biografía / descripción</label>';
      html += d.descripcion ? '<div class="v">' + esc(d.descripcion) + '</div>'
        : (d.one_sentence ? '<div class="v">' + esc(d.one_sentence) + '</div>'
          : '<div class="v empty">Sin biografía aún — escríbela o pídesela a la IA.</div>');
      if (d.relaciones && d.relaciones.length) {
        html += '<label>Relaciones</label><div class="v">' + d.relaciones.map(function (r) {
          return esc(r.nombre) + (r.detalle ? ' (' + esc(r.detalle) + ')' : '');
        }).join(' · ') + '</div>';
      }
      html += '<label>Aparece en</label>';
      html += (d.escenas && d.escenas.length)
        ? '<div class="v">' + d.escenas.length + ' escena(s) · ' +
          d.escenas.slice(0, 12).map(function (n) { return 'esc. ' + n; }).join(', ') + (d.escenas.length > 12 ? '…' : '') + '</div>'
        : '<div class="v empty">No habla en ninguna escena.</div>';
      html += '<div class="links">' +
        '<button class="a122-btn--text" data-open-personajes>Abrir en Personajes</button>' +
        '<button class="a122-btn--text" data-ia><span class="mi mi-sm">auto_awesome</span> Análisis psicológico</button></div>';
      cd.innerHTML = html;
      var oa = cd.querySelector('[data-open-personajes]'); if (oa) oa.addEventListener('click', function () { tellParent('personajes'); });
      var ia = cd.querySelector('[data-ia]'); if (ia) ia.addEventListener('click', function () { tellParent('chat'); });
    }).catch(function () { cd.innerHTML = '<div class="muted">No pude abrir el personaje.</div>'; });
  }

  function setPanel(name) {
    panel = name;
    document.querySelectorAll('.ed-rail button[data-panel]').forEach(function (b) {
      b.classList.toggle('is-active', b.dataset.panel === name);
    });
    document.querySelectorAll('.ed-panel[data-panel]').forEach(function (p) {
      p.hidden = (p.dataset.panel !== name);
    });
    if (name === 'personajes') renderPersonajes();
  }

  // ── eventos ──
  document.addEventListener('DOMContentLoaded', function () {
    // Acciones de EDICIÓN → abren el editor nativo al lado de Odiseo (sin botón "STARC").
    document.querySelectorAll('[data-edit]').forEach(function (b) {
      b.addEventListener('click', function () { tellParent('edit-guion'); });
    });
    // Paneles del rail (Estadísticas / Personajes).
    document.querySelectorAll('.ed-rail button[data-panel]').forEach(function (b) {
      b.addEventListener('click', function () { setPanel(b.dataset.panel); });
    });
    // IA → chat de Rita.
    var tbIa = $('tb-ia'), railIa = $('rail-ia');
    if (tbIa) tbIa.addEventListener('click', function () { tellParent('chat'); });
    if (railIa) railIa.addEventListener('click', function () { tellParent('chat'); });
    // Buscar → por ahora abre el editor nativo (búsqueda real en el motor).
    var search = $('tb-search'); if (search) search.addEventListener('click', function () { tellParent('edit-guion'); });
    // Doble clic en la página → editar en el editor nativo.
    var pw = $('ed-pagewrap'); if (pw) pw.addEventListener('dblclick', function () { tellParent('edit-guion'); });

    loadEscenas();
  });
})();
