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
TYPE_SCREENPLAY_TEXT = 10104
TYPE_CHARACTER = 30001
TYPE_LOCATION = 40001

STORY_ROLE = {0: "Principal", 1: "Secundario", 2: "Terciario", 3: "Sin definir"}
GENDER = {0: "M", 1: "F", 2: "Otro"}

# Prefijo INT/EXT al inicio del encabezado de escena
_HEADING_PREFIX = re.compile(
    r"^\s*(INT\.?/EXT\.?|EXT\.?/INT\.?|INT\.?|EXT\.?|I/E\.?|E/I\.?|EST\.?)\s*",
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


# ---------------------------------------------------------------------------
# Servidor MCP
# ---------------------------------------------------------------------------

server = Server("aula122")


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
