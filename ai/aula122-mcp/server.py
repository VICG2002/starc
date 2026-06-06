#!/usr/bin/env python3
"""
aula122-mcp — servidor MCP de Aula 122 (los "ojos" del cerebro odysseus sobre el .starc).

Expone, en **solo lectura**, el modelo de un proyecto de Aula 122 (formato .starc = base
SQLite con documentos XML): escenas, personajes, locaciones y estadísticas del guion. El
agente de odysseus invoca estas tools para *ver* el proyecto real antes de actuar.

Es la fase **AI-2** del plan ~/.claude/plans/durante-el-desarrollo-del-jazzy-squirrel.md
(Capa AI — odysseus). Las tools que MODIFICAN el .starc llegarán en **AI-3**, con aprobación
humana. Este servidor NO escribe nada: abre la base SQLite en modo read-only (mode=ro).

Transporte: stdio. odysseus lo lanza como subproceso con su propio venv (que trae el SDK `mcp`).
Proyecto activo: variable de entorno AULA122_PROJECT (ruta a un .starc); si falta, el .starc
más reciente en las carpetas conocidas. Se cambia en caliente con la tool `usar_proyecto`.

Autoprueba (sin arrancar MCP):  python server.py --selftest
"""

import asyncio
import os
import re
import sqlite3
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from urllib.parse import quote

from mcp.server import Server
from mcp.server.stdio import stdio_server
from mcp.types import Tool, TextContent

# ---------------------------------------------------------------------------
# Configuración
# ---------------------------------------------------------------------------

PROJECT_DIRS = [
    Path.home() / "Documents" / "starc" / "projects",
    Path.home() / "Documents" / "Aula 122" / "projects",
]

# Tipos de documento del .starc (enum DocumentObjectType de STARC)
# Ver src/corelib/domain/document_object.h:
#   ScreenplaySynopsis = 10102, ScreenplayTreatment = 10103, ScreenplayText = 10104.
TYPE_SCREENPLAY_SYNOPSIS = 10102   # "Sinopsis" (argumento) — documento de texto simple
TYPE_SCREENPLAY_TREATMENT = 10103  # "Tratamiento" — documento de texto simple
TYPE_SCREENPLAY_TEXT = 10104
TYPE_CHARACTER = 30001
TYPE_LOCATION = 40001

# Tags de bloque de encabezado en los documentos de TEXTO SIMPLE (sinopsis/tratamiento).
# Ver src/corelib/business_layer/templates/text_template.cpp: los párrafos se serializan
# como <heading_1..6> / <text> / <unformatted_text>, cada uno con un <v><![CDATA[…]]></v>.
_SIMPLE_HEADING_TAGS = {
    "heading_1", "heading_2", "heading_3",
    "heading_4", "heading_5", "heading_6",
}

STORY_ROLE = {0: "Principal", 1: "Secundario", 2: "Terciario", 3: "Sin definir"}
GENDER = {0: "M", 1: "F", 2: "Otro"}

# Prefijo INT/EXT al inicio del encabezado de escena
_HEADING_PREFIX = re.compile(
    r"^\s*(INT\.?/EXT\.?|EXT\.?/INT\.?|INT\.?|EXT\.?|I/E\.?|E/I\.?|EST\.?)\s*",
    re.IGNORECASE,
)
# Prefijo de página/viñeta: algunos guiones (novela gráfica como "Tales of a Man
# Without Powers") encabezan la escena con "PÁGINA #N:" / "PAGE #N:" / "PANEL N:".
# Sin quitarlo, INT/EXT (anclado al inicio) no se detectaba. p. ej.
# "PAGINA #1: INT. HABITACIÓN DE ILAN - NOCHE" -> "INT. HABITACIÓN DE ILAN - NOCHE".
_PAGE_PREFIX = re.compile(
    r"^\s*(?:P[ÁA]G(?:INA|\.)?|PAGE|PANEL|VI[ÑN]ETA)\s*#?\s*\d+\s*[:\.\-–]\s*",
    re.IGNORECASE,
)
# Extensiones del cue de personaje: (V.O.), (CONT'D), (O.S.), (FUERA DE CAMPO)...
_CUE_EXT = re.compile(r"\s*\(.*?\)\s*$")

_current_project = None  # Path | None


# ---------------------------------------------------------------------------
# Descubrimiento / selección de proyecto
# ---------------------------------------------------------------------------

def _discover_projects():
    """Lista (path, size, mtime) de todos los .starc en las carpetas conocidas."""
    found = []
    for d in PROJECT_DIRS:
        if d.is_dir():
            for p in sorted(d.glob("*.starc")):
                try:
                    st = p.stat()
                    found.append((p, st.st_size, st.st_mtime))
                except OSError:
                    pass
    return found


def _default_project():
    env = os.environ.get("AULA122_PROJECT")
    if env:
        p = Path(env).expanduser()
        if p.is_file():
            return p
    projs = _discover_projects()
    if not projs:
        return None
    projs.sort(key=lambda t: t[2], reverse=True)  # más reciente primero
    return projs[0][0]


def _project():
    global _current_project
    if _current_project is None:
        _current_project = _default_project()
    return _current_project


def _connect(project: Path):
    """Conexión SQLite en SOLO LECTURA (mode=ro). Nunca escribe el .starc."""
    uri = "file:" + quote(str(project), safe="/") + "?mode=ro"
    return sqlite3.connect(uri, uri=True)


# ---------------------------------------------------------------------------
# Helpers de parseo XML
# ---------------------------------------------------------------------------

def _text_of(el):
    """Texto de un bloque <tag><v><![CDATA[...]]></v></tag> (o texto directo)."""
    if el is None:
        return ""
    v = el.find("v")
    if v is not None:
        return (v.text or "").strip()
    return (el.text or "").strip()


def _norm_character(cue):
    """Normaliza el cue de personaje quitando extensiones (V.O.)/(CONT'D)/etc."""
    name = (cue or "").strip()
    name = _CUE_EXT.sub("", name).strip()
    return name


def _parse_heading(heading):
    """Devuelve (int_ext, locacion, tiempo, dia_noche) de un encabezado de escena."""
    raw = heading or ""
    # Quita un prefijo de página/viñeta ("PÁGINA #1:", "PANEL 2:") si lo hay, para
    # que INT/EXT se detecte en guiones con ese formato (novela gráfica).
    raw = _PAGE_PREFIX.sub("", raw, count=1)
    int_ext = "—"
    rest = raw
    m = _HEADING_PREFIX.match(raw)
    if m:
        token = m.group(1).upper().replace(".", "").replace(" ", "")
        rest = raw[m.end():]
        if token in ("INT/EXT", "EXT/INT", "IE", "EI"):
            int_ext = "INT/EXT"
        elif token.startswith("INT"):
            int_ext = "INT"
        elif token.startswith("EXT"):
            int_ext = "EXT"
        elif token.startswith("EST"):
            int_ext = "EST"

    location = rest.strip(" .-")
    time_of_day = ""
    if " - " in rest:
        parts = rest.split(" - ")
        location = parts[0].strip(" .-")
        time_of_day = parts[-1].strip()

    up = (time_of_day + " " + raw).upper()
    if any(w in up for w in ("NOCHE", "NIGHT", "MADRUGADA")):
        dia_noche = "NOCHE"
    elif any(w in up for w in ("DÍA", "DIA", "DAY", "MAÑANA", "MANANA",
                               "TARDE", "AMANECER", "ATARDECER")):
        dia_noche = "DÍA"
    else:
        dia_noche = "—"
    return int_ext, location, time_of_day, dia_noche


def _iter_scene_elements(root):
    """Itera los <scene> con encabezado, en orden (a cualquier profundidad)."""
    for sc in root.iter("scene"):
        content_el = sc.find("content")
        if content_el is None:
            continue
        if content_el.find("scene_heading") is None:
            continue
        yield sc, content_el


def _parse_scenes(content):
    """Lista de dicts, una por escena, con metadatos derivados del propio guion."""
    scenes = []
    try:
        root = ET.fromstring(content)
    except ET.ParseError:
        return scenes
    num = 0
    for sc, content_el in _iter_scene_elements(root):
        num += 1
        heading = _text_of(content_el.find("scene_heading"))
        int_ext, location, tod, dn = _parse_heading(heading)
        chars, n_dialogo, palabras = [], 0, 0
        for child in list(content_el):
            tag = child.tag
            if tag == "character":
                nm = _norm_character(_text_of(child))
                if nm and nm not in chars:
                    chars.append(nm)
            elif tag == "dialogue":
                n_dialogo += 1
                palabras += len(_text_of(child).split())
            elif tag == "action":
                palabras += len(_text_of(child).split())
        scenes.append({
            "numero": num,
            "uuid": sc.get("uuid", ""),
            "encabezado": heading,
            "int_ext": int_ext,
            "tiempo": tod,
            "dia_noche": dn,
            "locacion": location,
            "personajes": chars,
            "n_dialogos": n_dialogo,
            "palabras": palabras,
        })
    return scenes


def _scene_full(content, numero):
    """(numero, [(tipo, texto), ...]) del contenido completo y ordenado de una escena."""
    try:
        root = ET.fromstring(content)
    except ET.ParseError:
        return None, None
    num = 0
    for sc, content_el in _iter_scene_elements(root):
        num += 1
        if num != numero:
            continue
        blocks = []
        for child in list(content_el):
            blocks.append((child.tag, _text_of(child)))
        return num, blocks
    return None, None


def _parse_character(content):
    try:
        root = ET.fromstring(content)
    except ET.ParseError:
        return None

    def g(tag):
        el = root.find(tag)
        return (el.text or "").strip() if el is not None else ""

    relations = []
    rels = root.find("relations")
    if rels is not None:
        for r in rels.findall("relation"):
            w = r.find("with")
            uuid = (w.text or "").strip() if w is not None else ""
            det = r.find("details")
            relations.append({
                "with": uuid.strip("{}").lower(),
                "details": (det.text or "").strip() if det is not None else "",
            })
    return {
        "name": g("name"),
        "story_role": g("story_role"),
        "age": g("age"),
        "gender": g("gender"),
        "nickname": g("nickname"),
        "one_sentence": g("one_sentence_description"),
        "long": g("long_description"),
        "relations": relations,
    }


def _parse_location(content):
    try:
        root = ET.fromstring(content)
    except ET.ParseError:
        return None

    def g(tag):
        el = root.find(tag)
        return (el.text or "").strip() if el is not None else ""

    return {
        "name": g("name"),
        "story_role": g("story_role"),
        "one_sentence": g("one_sentence_description"),
        "long": g("long_description"),
    }


# ---------------------------------------------------------------------------
# Parseo de documentos de TEXTO SIMPLE (Sinopsis / Tratamiento)
# ---------------------------------------------------------------------------
#
# Sinopsis (10102) y Tratamiento (10103) son SimpleTextModel: prosa libre, NO
# guion estructurado. El XML los serializa como una secuencia de párrafos cuyo
# tag es el tipo de párrafo (heading_1..6, text, unformatted_text), cada uno con
# su texto en <v><![CDATA[…]]></v> — exactamente el patrón que ya lee `_text_of`.
# Los párrafos pueden ir envueltos en <folder>/<chapter_N> con <content>, así que
# recorremos recursivamente y recogemos CUALQUIER bloque que tenga un <v> hijo.
#
# SUPUESTO (verificado contra el código C++, no contra un .starc en vivo aquí):
#   - root = <document mime-type="…" version="1.0">.
#   - cada párrafo = <tag>…<v><![CDATA[texto]]></v>…</tag>, en orden de lectura.
#   - <heading_1..6> ⇒ encabezado/sección; el resto ⇒ párrafo de cuerpo.
# Si un proyecto guardara estos documentos con otra forma, esto extrae el texto
# de todos los <v> disponibles igualmente (degradación elegante).

# Tags que NO son párrafos de contenido (estructura/colofón) — se ignoran al
# recolectar, pero sus descendientes sí se visitan.
_SIMPLE_SKIP_TAGS = {"document", "content"}


def _iter_simple_blocks(content):
    """Itera (tag, texto) de los párrafos de un documento de texto simple, en orden.

    Recorre el árbol en profundidad: un nodo se considera "párrafo" si tiene un
    hijo directo <v>; su texto es el de ese <v>. Los contenedores (folder,
    chapter_N, content, document) no aportan texto propio pero se descienden.
    """
    try:
        root = ET.fromstring(content)
    except ET.ParseError:
        return
    # DFS manual preservando el orden de los hijos (ET no da padres/orden global
    # con iter()). Un nodo con hijo directo <v> es un párrafo; los contenedores
    # (folder, chapter_N, content) no aportan texto propio pero se descienden.
    def walk(el):
        v = el.find("v")
        if v is not None:
            yield el.tag, (v.text or "").strip()
            return  # su <v> ya se consumió; no recursar dentro del párrafo
        for child in list(el):
            yield from walk(child)

    for child in list(root):  # se salta el propio <document>, recorre sus hijos
        yield from walk(child)


def _parse_simple_text(content):
    """Convierte un documento de texto simple en {texto, palabras, parrafos:[…]}.

    - `parrafos`: lista de {tipo: 'heading'|'body', nivel?: int, texto: str}.
    - `texto`: el texto plano completo (párrafos unidos por doble salto de línea).
    - `palabras`: conteo de palabras del texto plano.
    """
    parrafos = []
    for tag, text in _iter_simple_blocks(content):
        if tag in _SIMPLE_SKIP_TAGS:
            continue
        if not text:
            continue
        if tag in _SIMPLE_HEADING_TAGS:
            try:
                nivel = int(tag.rsplit("_", 1)[1])
            except (IndexError, ValueError):
                nivel = 1
            parrafos.append({"tipo": "heading", "nivel": nivel, "texto": text})
        else:
            parrafos.append({"tipo": "body", "texto": text})
    texto = "\n\n".join(p["texto"] for p in parrafos)
    palabras = len(texto.split())
    return {"texto": texto, "palabras": palabras, "parrafos": parrafos}


def _simple_doc(conn, doctype):
    """(id, uuid, content) del documento de texto simple más grande de un tipo, o None.

    Igual criterio que `_screenplay_doc`: el de mayor contenido (hay proyectos con
    documentos vacíos placeholder además del real).
    """
    rows = conn.execute(
        "SELECT id, uuid, CAST(content AS TEXT) FROM documents "
        "WHERE type=? AND content IS NOT NULL ORDER BY length(content) DESC",
        (doctype,),
    ).fetchall()
    return rows[0] if rows else None


def _simple_text_payload(conn, doctype):
    """{texto, palabras, parrafos} del documento de `doctype`, o vacío si no existe."""
    doc = _simple_doc(conn, doctype)
    if not doc:
        return {"texto": "", "palabras": 0, "parrafos": []}
    return _parse_simple_text(doc[2])


# ---------------------------------------------------------------------------
# Acceso a documentos
# ---------------------------------------------------------------------------

def _screenplay_doc(conn):
    """(id, uuid, content) del guion principal (el doc 10104 más grande, o AULA122_SCREENPLAY_DOC)."""
    rows = conn.execute(
        "SELECT id, uuid, CAST(content AS TEXT) FROM documents "
        "WHERE type=? AND content IS NOT NULL ORDER BY length(content) DESC",
        (TYPE_SCREENPLAY_TEXT,),
    ).fetchall()
    if not rows:
        return None
    env_doc = os.environ.get("AULA122_SCREENPLAY_DOC")
    if env_doc:
        for r in rows:
            if str(r[0]) == str(env_doc):
                return r
    return rows[0]


def _all_docs(conn, doctype):
    return conn.execute(
        "SELECT id, uuid, CAST(content AS TEXT) FROM documents "
        "WHERE type=? AND content IS NOT NULL ORDER BY id",
        (doctype,),
    ).fetchall()


def _character_name_map(conn):
    """{uuid_sin_llaves_lower: nombre} de todos los personajes (para resolver relaciones)."""
    out = {}
    for _id, uuid, content in _all_docs(conn, TYPE_CHARACTER):
        c = _parse_character(content)
        if c and c["name"]:
            out[(uuid or "").strip("{}").lower()] = c["name"]
    return out


# ---------------------------------------------------------------------------
# Implementación de las tools (devuelven texto en español para el cerebro)
# ---------------------------------------------------------------------------

def _err_no_project():
    projs = _discover_projects()
    if not projs:
        return ("No hay ningún proyecto .starc cargado ni encontrado. Define la variable de "
                "entorno AULA122_PROJECT con la ruta a un .starc, o usa la tool `usar_proyecto`.")
    lines = ["No hay proyecto activo. Proyectos disponibles (usa `usar_proyecto`):"]
    for p, size, _ in projs:
        lines.append(f"- {p.name}  ({size // 1024} KB)  —  {p}")
    return "\n".join(lines)


def _loc_label(p: Path):
    """Etiqueta corta de carpeta para desambiguar proyectos homónimos."""
    try:
        return f"{p.parent.parent.name}/{p.parent.name}"
    except Exception:  # noqa: BLE001
        return p.parent.name


def t_listar_proyectos():
    projs = _discover_projects()
    if not projs:
        return "No se encontraron archivos .starc en las carpetas conocidas."
    active = _project()
    lines = ["Proyectos .starc disponibles:"]
    for p, size, _ in sorted(projs, key=lambda t: t[2], reverse=True):
        mark = "  ◀ activo" if active and p == active else ""
        lines.append(f"- {p.name}  ({size // 1024} KB)  [{_loc_label(p)}]{mark}")
    lines.append("\nCambia de proyecto con `usar_proyecto` (por nombre o ruta; "
                 "si hay homónimos, pasa la ruta completa).")
    return "\n".join(lines)


def t_usar_proyecto(arg):
    global _current_project
    arg = (arg or "").strip()
    if not arg:
        return "Indica el nombre o la ruta del .starc a usar."
    cand = Path(arg).expanduser()
    if cand.is_file() and cand.suffix == ".starc":
        _current_project = cand
        return f"Proyecto activo: {cand.name}"
    low = arg.lower()
    for p, _, _ in _discover_projects():
        if low in p.name.lower():
            _current_project = p
            return f"Proyecto activo: {p.name}"
    return f"No se encontró un proyecto que coincida con «{arg}». Usa `listar_proyectos`."


def t_proyecto_actual():
    proj = _project()
    if not proj:
        return _err_no_project()
    conn = _connect(proj)
    try:
        sp = _screenplay_doc(conn)
        n_chars = len(_all_docs(conn, TYPE_CHARACTER))
        n_locs = len(_all_docs(conn, TYPE_LOCATION))
        n_scenes = len(_parse_scenes(sp[2])) if sp else 0
        doc_info = f"doc id {sp[0]} · uuid {sp[1]}" if sp else "—"
    finally:
        conn.close()
    return (f"Proyecto activo: {proj.name}\n"
            f"Ruta: {proj}\n"
            f"Guion principal: {doc_info}\n"
            f"Escenas: {n_scenes}  ·  Personajes (catálogo): {n_chars}  ·  "
            f"Locaciones (catálogo): {n_locs}")


def t_listar_escenas():
    proj = _project()
    if not proj:
        return _err_no_project()
    conn = _connect(proj)
    try:
        sp = _screenplay_doc(conn)
        if not sp:
            return "El proyecto no tiene guion (documento de tipo 10104)."
        scenes = _parse_scenes(sp[2])
    finally:
        conn.close()
    if not scenes:
        return "No se encontraron escenas en el guion."
    lines = [f"{len(scenes)} escenas en «{proj.stem}»:"]
    for s in scenes:
        per = ", ".join(s["personajes"]) if s["personajes"] else "—"
        lines.append(
            f"#{s['numero']:>3} | {s['int_ext']:<7} | {s['dia_noche']:<5} | "
            f"{s['locacion']}  —  personajes: {per}"
        )
    return "\n".join(lines)


def t_obtener_escena(numero):
    proj = _project()
    if not proj:
        return _err_no_project()
    try:
        numero = int(numero)
    except (TypeError, ValueError):
        return "Indica el número de escena (entero)."
    conn = _connect(proj)
    try:
        sp = _screenplay_doc(conn)
        if not sp:
            return "El proyecto no tiene guion."
        num, blocks = _scene_full(sp[2], numero)
    finally:
        conn.close()
    if not blocks:
        return f"No existe la escena #{numero}."
    out = [f"ESCENA #{num}", ""]
    for tag, text in blocks:
        if not text:
            continue
        if tag == "scene_heading":
            out.append(text.upper())
        elif tag == "action":
            out.append(text)
        elif tag == "character":
            out.append("\t\t\t" + text)
        elif tag == "parenthetical":
            out.append("\t\t" + text)
        elif tag in ("dialogue", "lyrics"):
            out.append("\t" + text)
        else:
            out.append(text)
    return "\n".join(out)


def t_estadisticas_guion():
    proj = _project()
    if not proj:
        return _err_no_project()
    conn = _connect(proj)
    try:
        sp = _screenplay_doc(conn)
        if not sp:
            return "El proyecto no tiene guion."
        scenes = _parse_scenes(sp[2])
        cat_chars = len(_all_docs(conn, TYPE_CHARACTER))
        cat_locs = len(_all_docs(conn, TYPE_LOCATION))
    finally:
        conn.close()
    if not scenes:
        return "No se encontraron escenas en el guion."

    n = len(scenes)
    int_ = sum(1 for s in scenes if s["int_ext"] == "INT")
    ext_ = sum(1 for s in scenes if s["int_ext"] == "EXT")
    mix_ = sum(1 for s in scenes if s["int_ext"] in ("INT/EXT", "EST"))
    dia = sum(1 for s in scenes if s["dia_noche"] == "DÍA")
    noche = sum(1 for s in scenes if s["dia_noche"] == "NOCHE")
    palabras = sum(s["palabras"] for s in scenes)
    dialogos = sum(s["n_dialogos"] for s in scenes)
    hablan = sorted({p for s in scenes for p in s["personajes"]})
    locs_heading = sorted({s["locacion"] for s in scenes if s["locacion"]})
    paginas = max(1, round(palabras / 180))  # estimación aproximada

    return (
        f"ESTADÍSTICAS — {proj.stem}\n"
        f"Escenas: {n}\n"
        f"  INT: {int_}   EXT: {ext_}   INT/EXT·EST: {mix_}\n"
        f"  DÍA: {dia}   NOCHE: {noche}   sin definir: {n - dia - noche}\n"
        f"Personajes con diálogo: {len(hablan)}   (catálogo de personajes: {cat_chars})\n"
        f"Locaciones distintas en encabezados: {len(locs_heading)}   "
        f"(catálogo de locaciones: {cat_locs})\n"
        f"Bloques de diálogo: {dialogos}\n"
        f"Palabras (acción + diálogo): {palabras}\n"
        f"Páginas estimadas: ~{paginas}  (estimación aproximada, no el conteo exacto de STARC)"
    )


def t_listar_personajes():
    proj = _project()
    if not proj:
        return _err_no_project()
    conn = _connect(proj)
    try:
        sp = _screenplay_doc(conn)
        scenes = _parse_scenes(sp[2]) if sp else []
        docs = _all_docs(conn, TYPE_CHARACTER)
        chars = [c for c in (_parse_character(d[2]) for d in docs) if c and c["name"]]
    finally:
        conn.close()
    if not chars:
        return "El proyecto no tiene personajes en el catálogo."
    # nº de escenas donde habla cada personaje (match por nombre normalizado)
    counts = {}
    for s in scenes:
        for p in s["personajes"]:
            counts[p.upper()] = counts.get(p.upper(), 0) + 1

    def role_label(c):
        try:
            return STORY_ROLE.get(int(c["story_role"]), c["story_role"] or "—")
        except (ValueError, TypeError):
            return "—"

    chars.sort(key=lambda c: (-counts.get(c["name"].upper(), 0), c["name"]))
    lines = [f"{len(chars)} personajes en «{proj.stem}»:"]
    for c in chars:
        esc = counts.get(c["name"].upper(), 0)
        edad = f", {c['age']}a" if c["age"] else ""
        gen = GENDER.get(int(c["gender"]), "") if c["gender"].isdigit() else ""
        gen = f", {gen}" if gen else ""
        desc = f" — {c['one_sentence']}" if c["one_sentence"] else ""
        lines.append(f"- {c['name']} ({role_label(c)}{edad}{gen}) · habla en {esc} escena(s){desc}")
    return "\n".join(lines)


def t_obtener_personaje(nombre):
    proj = _project()
    if not proj:
        return _err_no_project()
    nombre = (nombre or "").strip()
    if not nombre:
        return "Indica el nombre del personaje."
    conn = _connect(proj)
    try:
        docs = _all_docs(conn, TYPE_CHARACTER)
        namemap = _character_name_map(conn)
        sp = _screenplay_doc(conn)
        scenes = _parse_scenes(sp[2]) if sp else []
        target = None
        for d in docs:
            c = _parse_character(d[2])
            if c and c["name"] and c["name"].lower() == nombre.lower():
                target = c
                break
        if target is None:  # coincidencia parcial
            for d in docs:
                c = _parse_character(d[2])
                if c and c["name"] and nombre.lower() in c["name"].lower():
                    target = c
                    break
    finally:
        conn.close()
    if target is None:
        return f"No se encontró el personaje «{nombre}». Usa `listar_personajes`."

    try:
        role = STORY_ROLE.get(int(target["story_role"]), target["story_role"] or "—")
    except (ValueError, TypeError):
        role = "—"
    gen = GENDER.get(int(target["gender"]), "—") if target["gender"].isdigit() else "—"
    escenas = [s["numero"] for s in scenes if target["name"].upper() in
               [p.upper() for p in s["personajes"]]]

    out = [f"PERSONAJE: {target['name']}"]
    if target["nickname"]:
        out.append(f"Apodo/nombre completo: {target['nickname']}")
    out.append(f"Rol narrativo: {role}   Edad: {target['age'] or '—'}   Género: {gen}")
    if target["one_sentence"]:
        out.append(f"En una frase: {target['one_sentence']}")
    if target["long"]:
        out.append(f"Descripción: {target['long']}")
    if escenas:
        out.append(f"Habla en {len(escenas)} escena(s): {', '.join('#' + str(e) for e in escenas)}")
    if target["relations"]:
        rel_lines = []
        for r in target["relations"]:
            other = namemap.get(r["with"], r["with"] or "?")
            det = f" — {r['details']}" if r["details"] else ""
            rel_lines.append(f"  · {other}{det}")
        out.append("Relaciones:\n" + "\n".join(rel_lines))
    return "\n".join(out)


def t_listar_locaciones():
    proj = _project()
    if not proj:
        return _err_no_project()
    conn = _connect(proj)
    try:
        docs = _all_docs(conn, TYPE_LOCATION)
        locs = [l for l in (_parse_location(d[2]) for d in docs) if l and l["name"]]
    finally:
        conn.close()
    if not locs:
        return "El proyecto no tiene locaciones en el catálogo."
    locs.sort(key=lambda l: l["name"])
    lines = [f"{len(locs)} locaciones en el catálogo de «{proj.stem}»:"]
    for l in locs:
        desc = f" — {l['one_sentence']}" if l["one_sentence"] else ""
        lines.append(f"- {l['name']}{desc}")
    lines.append("\n(Para agrupar escenas por locación tal como aparecen en los encabezados, "
                 "usa `escenas_por_locacion`.)")
    return "\n".join(lines)


def t_escenas_por_locacion():
    proj = _project()
    if not proj:
        return _err_no_project()
    conn = _connect(proj)
    try:
        sp = _screenplay_doc(conn)
        if not sp:
            return "El proyecto no tiene guion."
        scenes = _parse_scenes(sp[2])
    finally:
        conn.close()
    if not scenes:
        return "No se encontraron escenas en el guion."
    grupos = {}
    for s in scenes:
        loc = s["locacion"] or "(sin locación)"
        grupos.setdefault(loc, []).append(s)
    lines = [f"Escenas agrupadas por locación ({len(grupos)} locaciones):"]
    for loc in sorted(grupos, key=lambda k: (-len(grupos[k]), k)):
        nums = grupos[loc]
        ie = "/".join(sorted({n["int_ext"] for n in nums}))
        scene_nums = ", ".join("#" + str(n["numero"]) for n in nums)
        lines.append(f"\n▸ {loc}  [{ie}] — {len(nums)} escena(s)\n   {scene_nums}")
    return "\n".join(lines)


_WORDS_PER_PAGE = 180  # estimación (coincide con ~181 medido en EDLP)


def _eighths(words):
    """Páginas en octavos (la unidad estándar del desglose). p. ej. '1 3/8'."""
    pages = max(words / _WORDS_PER_PAGE, 0.0)
    e = max(1, round(pages * 8))  # mínimo 1/8
    whole, frac = divmod(e, 8)
    if whole and frac:
        return f"{whole} {frac}/8"
    if whole:
        return f"{whole}"
    return f"{frac}/8"


def t_generar_desglose():
    """Genera un DESGLOSE estructural en markdown desde el guion: tabla por escena
    (INT/EXT, locación, día/noche, personajes, páginas en octavos), más rollups por
    locación (base del strip board) y por personaje (base del Day-Out-of-Days). Es la
    materia prima determinista; el tagging de los 21 elementos es la capa siguiente."""
    proj = _project()
    if not proj:
        return _err_no_project()
    conn = _connect(proj)
    try:
        sp = _screenplay_doc(conn)
        if not sp:
            return "El proyecto no tiene guion (documento de tipo 10104)."
        scenes = _parse_scenes(sp[2])
    finally:
        conn.close()
    if not scenes:
        return "No se encontraron escenas en el guion."

    n = len(scenes)
    n_int = sum(1 for s in scenes if s["int_ext"] == "INT")
    n_ext = sum(1 for s in scenes if s["int_ext"] == "EXT")
    n_ie = sum(1 for s in scenes if s["int_ext"] in ("INT/EXT", "EST"))
    n_dia = sum(1 for s in scenes if s["dia_noche"] == "DÍA")
    n_noche = sum(1 for s in scenes if s["dia_noche"] == "NOCHE")
    total_words = sum(s["palabras"] for s in scenes)
    locs = sorted({s["locacion"] for s in scenes if s["locacion"]})
    chars = sorted({c for s in scenes for c in s["personajes"]})

    out = [f"# Desglose — {proj.stem}\n",
           "> Desglose **estructural** generado automáticamente desde el guion (.starc). "
           "«Personajes» = los que tienen diálogo/cue en la escena (presencia aproximada). "
           "El tagging de los 21 elementos (props, vestuario, vehículos, SFX, extras…) es la "
           "capa siguiente, escena por escena. **Revisión humana antes de programar/presupuestar.**\n",
           "## Resumen",
           f"- **Escenas:** {n}  ·  INT: {n_int}  EXT: {n_ext}  INT/EXT·EST: {n_ie}",
           f"- **Día:** {n_dia}  Noche: {n_noche}  sin definir: {n - n_dia - n_noche}",
           f"- **Páginas estimadas:** ~{total_words / _WORDS_PER_PAGE:.0f}  ({total_words} palabras)",
           f"- **Locaciones:** {len(locs)}  ·  **Personajes con presencia:** {len(chars)}\n",
           "## Desglose por escena",
           "| # | INT/EXT | Locación | D/N | Personajes | Págs |",
           "|---|---------|----------|-----|------------|------|"]
    for s in scenes:
        per = ", ".join(s["personajes"]) if s["personajes"] else "—"
        loc = s["locacion"] or "—"
        out.append(f"| {s['numero']} | {s['int_ext']} | {loc} | {s['dia_noche']} | {per} | {_eighths(s['palabras'])} |")

    out.append("\n## Escenas por locación  *(base del strip board / agrupar el rodaje)*")
    by_loc = {}
    for s in scenes:
        by_loc.setdefault(s["locacion"] or "(sin locación)", []).append(s["numero"])
    for loc in sorted(by_loc, key=lambda k: (-len(by_loc[k]), k)):
        nums = by_loc[loc]
        out.append(f"- **{loc}** — {len(nums)} escena(s): {', '.join('#'+str(x) for x in nums)}")

    out.append("\n## Personajes por escena  *(base del Day-Out-of-Days)*")
    by_char = {}
    for s in scenes:
        for c in s["personajes"]:
            by_char.setdefault(c, []).append(s["numero"])
    for c in sorted(by_char, key=lambda k: (-len(by_char[k]), k)):
        nums = by_char[c]
        out.append(f"- **{c}** — {len(nums)} escena(s): {', '.join('#'+str(x) for x in nums)}")

    return "\n".join(out)


def t_generar_plan_rodaje(paginas_por_dia=5):
    """Borrador de PLAN DE RODAJE (strip board) + Day-Out-of-Days desde el guion:
    agrupa las escenas por locación (para minimizar movimientos de compañía), las
    ordena INT→EXT / DÍA→NOCHE y las empaca en días de ~`paginas_por_dia` páginas.
    Es un BORRADOR determinista; el AD lo ajusta por disponibilidad de cast/locación."""
    proj = _project()
    if not proj:
        return _err_no_project()
    try:
        ppd = max(1.0, float(paginas_por_dia or 5))
    except (TypeError, ValueError):
        ppd = 5.0
    conn = _connect(proj)
    try:
        sp = _screenplay_doc(conn)
        if not sp:
            return "El proyecto no tiene guion (documento de tipo 10104)."
        scenes = _parse_scenes(sp[2])
    finally:
        conn.close()
    if not scenes:
        return "No se encontraron escenas en el guion."

    # Agrupa por locación (locaciones grandes primero) y ordena dentro: INT antes
    # que EXT, DÍA antes que NOCHE — para batchear setups de luz.
    by_loc = {}
    for s in scenes:
        by_loc.setdefault(s["locacion"] or "(sin locación)", []).append(s)
    _ie_rank = {"INT": 0, "INT/EXT": 1, "EST": 1, "EXT": 2, "—": 3}
    _dn_rank = {"DÍA": 0, "—": 1, "NOCHE": 2}
    ordered = []
    for loc in sorted(by_loc, key=lambda k: (-len(by_loc[k]), k)):
        grp = sorted(by_loc[loc], key=lambda s: (_ie_rank.get(s["int_ext"], 3),
                                                 _dn_rank.get(s["dia_noche"], 1),
                                                 s["numero"]))
        ordered.extend(grp)

    # Empaca en días por páginas estimadas.
    dias, cur, cur_pg = [], [], 0.0
    for s in ordered:
        pg = max(s["palabras"] / _WORDS_PER_PAGE, 1 / 8)
        if cur and cur_pg + pg > ppd:
            dias.append(cur); cur, cur_pg = [], 0.0
        cur.append(s); cur_pg += pg
    if cur:
        dias.append(cur)

    out = [f"# Plan de rodaje (borrador) — {proj.stem}\n",
           f"> Strip board determinista: escenas agrupadas por locación y empacadas en "
           f"días de ~{ppd:.0f} páginas. **Es un borrador** — el 1er AD lo ajusta por "
           f"disponibilidad de cast/locación, luz, permisos y continuidad.\n",
           f"**{len(dias)} días de rodaje** · {len(scenes)} escenas · "
           f"~{sum(s['palabras'] for s in scenes)/_WORDS_PER_PAGE:.0f} páginas\n",
           "## Strip board por día"]
    # Día asignado a cada escena (para el DOOD)
    dia_de = {}
    for i, dia in enumerate(dias, 1):
        locs_dia = []
        for s in dia:
            if s["locacion"] not in locs_dia:
                locs_dia.append(s["locacion"] or "(sin locación)")
        pgs = sum(max(s["palabras"]/_WORDS_PER_PAGE, 1/8) for s in dia)
        out.append(f"\n### Día {i} — {', '.join(locs_dia)}  ({len(dia)} esc · ~{pgs:.1f} pág)")
        out.append("| Esc | INT/EXT | D/N | Locación | Personajes |")
        out.append("|-----|---------|-----|----------|------------|")
        for s in dia:
            dia_de.setdefault(s["numero"], i)
            per = ", ".join(s["personajes"]) if s["personajes"] else "—"
            out.append(f"| {s['numero']} | {s['int_ext']} | {s['dia_noche']} | "
                       f"{s['locacion'] or '—'} | {per} |")
        for s in dia:
            dia_de[s["numero"]] = i

    # Day-Out-of-Days: por personaje, en qué días trabaja (Start/Work/Hold/Finish).
    out.append("\n## Day-Out-of-Days (cast)")
    out.append("| Personaje | Días | Inicio | Fin | Hold |")
    out.append("|-----------|------|--------|-----|------|")
    by_char = {}
    for s in scenes:
        d = dia_de.get(s["numero"])
        if d is None:
            continue
        for c in s["personajes"]:
            by_char.setdefault(c, set()).add(d)
    for c in sorted(by_char, key=lambda k: (min(by_char[k]), -len(by_char[k]), k)):
        ds = sorted(by_char[c])
        hold = [d for d in range(ds[0], ds[-1] + 1) if d not in by_char[c]]
        hold_s = ", ".join("D" + str(d) for d in hold) if hold else "—"
        out.append(f"| {c} | {', '.join('D'+str(d) for d in ds)} | D{ds[0]} | D{ds[-1]} | {hold_s} |")

    return "\n".join(out)


# ---------------------------------------------------------------------------
# Servidor MCP
# ---------------------------------------------------------------------------

server = Server("aula122")


# ── Mantenimiento / actualizaciones de Aula 122 (semi-auto, CON confirmacion) ──
# Envuelven la utilidad GUARDADA ai/aula122-update.sh como tools MCP — mucho mas
# fiable de invocar para el modelo local que recordar un comando de shell. Los
# guardrails DUROS (backup antes, sin git push, sin re-vendorizado upstream, dry-run
# sin --confirm, solo dentro del arbol) viven en el SCRIPT, no aqui.

def _update_script() -> str:
    repo = os.environ.get("AULA122_REPO") or os.path.expanduser("~/Developer/starc-fork")
    return os.path.join(repo, "ai", "aula122-update.sh")


def _run_update(args: list, timeout: int) -> str:
    script = _update_script()
    if not os.path.isfile(script):
        return (f"No encuentro la utilidad de actualizacion en {script}. Solo esta "
                "disponible en el arbol de desarrollo (define AULA122_REPO si esta en otra ruta).")
    try:
        p = subprocess.run(["bash", script, *args],
                           capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return f"La operacion excedio {timeout}s y se aborto."
    except Exception as exc:  # noqa: BLE001
        return f"Error ejecutando la utilidad: {exc}"
    out = (p.stdout or "")
    if p.stderr and p.stderr.strip():
        out += "\n[stderr]\n" + p.stderr
    out = re.sub(r"\x1b\[[0-9;]*m", "", out)  # quita color ANSI
    return out.strip() or "(sin salida)"


def t_revisar_actualizacion() -> str:
    return _run_update(["status"], timeout=60)


def t_respaldar_aula122() -> str:
    return _run_update(["backup"], timeout=180)


def t_redesplegar_aula122(confirmar: bool = False) -> str:
    if confirmar is True:
        return _run_update(["rebuild", "--confirm"], timeout=300)
    return "DRY-RUN (no se aplico nada; falta confirmacion humana).\n" + \
        _run_update(["rebuild"], timeout=60)


@server.list_tools()
async def list_tools() -> list[Tool]:
    return [
        Tool(
            name="listar_proyectos",
            description="Lista los proyectos .starc de Aula 122 disponibles e indica cuál está activo.",
            inputSchema={"type": "object", "properties": {}},
        ),
        Tool(
            name="usar_proyecto",
            description="Selecciona el proyecto .starc activo para las consultas siguientes (por nombre o ruta).",
            inputSchema={
                "type": "object",
                "properties": {"proyecto": {"type": "string",
                                            "description": "Nombre (parcial) o ruta del .starc"}},
                "required": ["proyecto"],
            },
        ),
        Tool(
            name="proyecto_actual",
            description="Informa el proyecto activo, el guion principal usado y conteos rápidos (escenas, personajes, locaciones).",
            inputSchema={"type": "object", "properties": {}},
        ),
        Tool(
            name="listar_escenas",
            description="Lista todas las escenas del guion con su número, INT/EXT, día/noche, locación y personajes que hablan.",
            inputSchema={"type": "object", "properties": {}},
        ),
        Tool(
            name="obtener_escena",
            description="Devuelve el contenido completo de una escena (encabezado, acción, personajes, diálogos) por su número.",
            inputSchema={
                "type": "object",
                "properties": {"numero": {"type": "integer", "description": "Número de escena (1-based)"}},
                "required": ["numero"],
            },
        ),
        Tool(
            name="estadisticas_guion",
            description="Estadísticas del guion: nº de escenas, INT/EXT, día/noche, personajes con diálogo, locaciones, diálogos, palabras y páginas estimadas.",
            inputSchema={"type": "object", "properties": {}},
        ),
        Tool(
            name="listar_personajes",
            description="Lista los personajes del catálogo del proyecto con su rol narrativo, edad, género y en cuántas escenas hablan.",
            inputSchema={"type": "object", "properties": {}},
        ),
        Tool(
            name="obtener_personaje",
            description="Ficha de un personaje: rol, edad, género, descripción, escenas en las que habla y relaciones (resueltas a nombres).",
            inputSchema={
                "type": "object",
                "properties": {"nombre": {"type": "string", "description": "Nombre del personaje"}},
                "required": ["nombre"],
            },
        ),
        Tool(
            name="listar_locaciones",
            description="Lista las locaciones del catálogo del proyecto.",
            inputSchema={"type": "object", "properties": {}},
        ),
        Tool(
            name="escenas_por_locacion",
            description="Agrupa las escenas por la locación de su encabezado, mostrando los números de escena de cada locación (útil para plan de rodaje).",
            inputSchema={"type": "object", "properties": {}},
        ),
        Tool(
            name="generar_desglose",
            description="Genera el DESGLOSE estructural completo del guion en markdown: tabla por escena (INT/EXT, locación, día/noche, personajes, páginas en octavos) + escenas por locación (base del strip board) + personajes por escena (base del Day-Out-of-Days). Es la materia prima determinista para programar y presupuestar. Tras generarlo, guárdalo con create_document para que quede como documento del proyecto.",
            inputSchema={"type": "object", "properties": {}},
        ),
        Tool(
            name="generar_plan_rodaje",
            description="Genera un BORRADOR de plan de rodaje (strip board) + Day-Out-of-Days desde el guion: agrupa las escenas por locación, las ordena INT→EXT/DÍA→NOCHE y las empaca en días de ~N páginas. Borrador determinista; el 1er AD lo ajusta por disponibilidad. Tras generarlo, guárdalo con create_document.",
            inputSchema={"type": "object", "properties": {"paginas_por_dia": {"type": "number", "description": "Páginas objetivo por día de rodaje (default 5)."}}},
        ),
        Tool(
            name="revisar_actualizacion",
            description="Mantenimiento de Aula 122: revisa si hay cambios del software (Odiseo + el cerebro) pendientes de REDESPLEGAR. SOLO LECTURA, seguro. Devuelve rama git, qué falta construir, versión de Hermes y plugins. Úsala cuando te pregunten por actualizaciones o estado del software.",
            inputSchema={"type": "object", "properties": {}},
        ),
        Tool(
            name="respaldar_aula122",
            description="Mantenimiento de Aula 122: crea un RESPALDO reversible del estado no regenerable del cerebro (config, ajustes, índice de memoria, sesiones). Seguro. Hazlo SIEMPRE antes de redesplegar.",
            inputSchema={"type": "object", "properties": {}},
        ),
        Tool(
            name="redesplegar_aula122",
            description="Mantenimiento de Aula 122: APLICA los cambios pendientes (recompila el núcleo + sincroniza Odiseo + reinicia la app). DESTRUCTIVO y te reinicia a ti. PROTOCOLO OBLIGATORIO: 1) revisar_actualizacion, 2) respaldar_aula122, 3) PIDE CONFIRMACIÓN EXPLÍCITA al humano en el chat, 4) solo si confirma, llama con confirmar=true. Sin confirmar=true hace DRY-RUN (no toca nada). NUNCA pongas confirmar=true sin que el humano lo haya dicho explícitamente.",
            inputSchema={"type": "object", "properties": {"confirmar": {"type": "boolean", "description": "true SOLO tras confirmación humana explícita en el chat. Default false = dry-run."}}},
        ),
    ]


_DISPATCH = {
    "listar_proyectos": lambda a: t_listar_proyectos(),
    "usar_proyecto": lambda a: t_usar_proyecto(a.get("proyecto")),
    "proyecto_actual": lambda a: t_proyecto_actual(),
    "listar_escenas": lambda a: t_listar_escenas(),
    "obtener_escena": lambda a: t_obtener_escena(a.get("numero")),
    "estadisticas_guion": lambda a: t_estadisticas_guion(),
    "listar_personajes": lambda a: t_listar_personajes(),
    "obtener_personaje": lambda a: t_obtener_personaje(a.get("nombre")),
    "listar_locaciones": lambda a: t_listar_locaciones(),
    "escenas_por_locacion": lambda a: t_escenas_por_locacion(),
    "generar_desglose": lambda a: t_generar_desglose(),
    "generar_plan_rodaje": lambda a: t_generar_plan_rodaje(a.get("paginas_por_dia", 5)),
    "revisar_actualizacion": lambda a: t_revisar_actualizacion(),
    "respaldar_aula122": lambda a: t_respaldar_aula122(),
    "redesplegar_aula122": lambda a: t_redesplegar_aula122(a.get("confirmar", False) is True),
}


@server.call_tool()
async def call_tool(name: str, arguments: dict) -> list[TextContent]:
    fn = _DISPATCH.get(name)
    if fn is None:
        return [TextContent(type="text", text=f"Tool desconocida: {name}")]
    try:
        result = fn(arguments or {})
    except Exception as exc:  # noqa: BLE001 — devolver el error al agente, no caer
        result = f"Error ejecutando «{name}»: {exc}"
    return [TextContent(type="text", text=result)]


async def run():
    async with stdio_server() as (read_stream, write_stream):
        await server.run(read_stream, write_stream, server.create_initialization_options())


def _selftest():
    proj = _project()
    print("=== aula122-mcp · autoprueba ===")
    print(t_listar_proyectos())
    print("\n--- proyecto_actual ---")
    print(t_proyecto_actual())
    print("\n--- estadisticas_guion ---")
    print(t_estadisticas_guion())
    print("\n--- listar_escenas (primeras 5 líneas) ---")
    print("\n".join(t_listar_escenas().splitlines()[:6]))
    print("\n--- listar_personajes (primeras 6 líneas) ---")
    print("\n".join(t_listar_personajes().splitlines()[:7]))
    print("\n--- escenas_por_locacion (primeras 8 líneas) ---")
    print("\n".join(t_escenas_por_locacion().splitlines()[:8]))


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        _selftest()
    else:
        asyncio.run(run())
