# routes/guion_routes.py
"""API de SOLO LECTURA del guion (.starc) para la página de Guion de Aula 122.

Reutiliza el parser ya probado de `aula122-mcp/server.py` (el mismo que usan las
tools MCP del cerebro) importándolo desde el directorio hermano `aula122-mcp`, y
expone los datos como JSON para el visor web (`static/js/guion.js`). NUNCA escribe
el .starc: el parser abre la base SQLite en modo read-only (mode=ro).

Endpoints:
  GET  /api/guion/proyectos          → {proyectos:[{nombre,path,activo,size_kb}], activo}
  POST /api/guion/usar               → fija el proyecto activo (form: proyecto)
  GET  /api/guion/escenas            → {proyecto, escenas:[...], stats:{...}}
  GET  /api/guion/escena/{numero}    → {numero, bloques:[[tipo,texto],...]}
  GET  /api/guion/personajes         → {proyecto, n, personajes:[...]}
  GET  /api/guion/personaje/{nombre} → ficha + relaciones resueltas + escenas
  GET  /api/guion/estructura         → {proyecto, path, arbol:[{uuid,kind,name,visible,children}]}

La lógica de agregación (stats, presencia de personajes) replica exactamente la
de las tools `estadisticas_guion` / `listar_personajes` para que coincidan los
números entre el chat del cerebro y la página.
"""

import importlib.util
import logging
import os
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from fastapi import APIRouter, HTTPException, Form

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Carga del parser de aula122-mcp (sin tocar el servidor MCP que ya funciona)
# ---------------------------------------------------------------------------

_S = None  # módulo server.py de aula122-mcp, cargado perezosamente


def _candidate_dirs():
    """Posibles ubicaciones de aula122-mcp/, en orden de prioridad.

    - AULA122_MCP_DIR (override explícito).
    - Relativo a este archivo (árbol de dev: ai/odysseus/routes → ai/aula122-mcp).
    - Relativo a sys.executable: en producción odysseus corre con la Python del
      bundle (<brain>/python/bin/pythonX), y aula122-mcp es <brain>/aula122-mcp;
      el runtime mutable (odysseus-runtime/) NO copia aula122-mcp, así que la ruta
      relativa al __file__ no sirve ahí — la del intérprete sí.
    """
    cands = []
    env = os.environ.get("AULA122_MCP_DIR")
    if env:
        cands.append(Path(env).expanduser())
    here = Path(__file__).resolve()
    for i in (2, 1, 3):
        if i < len(here.parents):
            cands.append(here.parents[i] / "aula122-mcp")
    try:
        exe = Path(sys.executable).resolve()
        for i in (2, 3):
            if i < len(exe.parents):
                cands.append(exe.parents[i] / "aula122-mcp")
    except Exception:  # noqa: BLE001
        pass
    return cands


def _load_parser():
    """Carga aula122-mcp/server.py como módulo aislado (sin correr su __main__).

    Usa un nombre de módulo único para no chocar con ningún otro `server` en
    sys.modules, y exec_module respeta el guard `if __name__ == "__main__"`.
    """
    seen = set()
    for c in _candidate_dirs():
        if c in seen:
            continue
        seen.add(c)
        sp = c / "server.py"
        if sp.is_file():
            # El parser solo usa stdlib + el SDK `mcp` (presente en la Python del
            # bundle, compartida con el MCP). spec_from_file_location evita pisar
            # sys.modules['server'] y exec_module respeta el guard __main__.
            spec = importlib.util.spec_from_file_location("aula122_mcp_server", str(sp))
            mod = importlib.util.module_from_spec(spec)
            sys.modules["aula122_mcp_server"] = mod
            spec.loader.exec_module(mod)
            logger.info("guion_routes: parser .starc cargado desde %s", sp)
            return mod
    raise RuntimeError(
        "No encontré aula122-mcp/server.py (parser del .starc) junto a odysseus."
    )


def _parser():
    global _S
    if _S is None:
        _S = _load_parser()
    return _S


# ---------------------------------------------------------------------------
# Selección de proyecto
# ---------------------------------------------------------------------------

def _resolve_project(S, proyecto):
    """Path del .starc a usar: ruta/nombre dado, o el activo/por defecto."""
    if proyecto:
        p = Path(proyecto).expanduser()
        if p.is_file():
            return p
        for cand, _sz, _mt in S._discover_projects():
            if proyecto.lower() in cand.name.lower():
                return cand
        return None
    return S._project()


# ---------------------------------------------------------------------------
# Helpers de datos (nivel de módulo → testeables sin levantar el server)
# ---------------------------------------------------------------------------

def get_proyectos():
    S = _parser()
    activo = S._project()
    out = []
    for p, size, mt in sorted(S._discover_projects(), key=lambda t: t[2], reverse=True):
        out.append({
            "nombre": p.stem,
            "path": str(p),
            "carpeta": S._loc_label(p),
            "size_kb": size // 1024,
            "activo": bool(activo and p == activo),
        })
    return {"proyectos": out, "activo": str(activo) if activo else None}


def set_proyecto(proyecto):
    S = _parser()
    proj = _resolve_project(S, proyecto)
    if not proj:
        raise KeyError(f"No encontré un proyecto .starc que coincida con «{proyecto}».")
    S._current_project = proj  # fija el activo en el módulo cargado
    return {"ok": True, "activo": str(proj), "nombre": proj.stem}


def _role_label(S, c):
    try:
        return S.STORY_ROLE.get(int(c["story_role"]), c["story_role"] or "—")
    except (ValueError, TypeError):
        return "—"


def _gender_label(S, c):
    g = c.get("gender", "")
    return S.GENDER.get(int(g), "—") if str(g).isdigit() else "—"


def _compute_stats(S, scenes, cat_chars, cat_locs):
    """Mismos cálculos que t_estadisticas_guion + presencia de reparto para barras."""
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
    wpp = getattr(S, "_WORDS_PER_PAGE", 180)
    paginas = max(1, round(palabras / wpp)) if palabras else 0
    # presencia de reparto: nº de escenas en las que habla cada cue (para barras)
    counts = {}
    for s in scenes:
        for p in s["personajes"]:
            counts[p] = counts.get(p, 0) + 1
    reparto = sorted(
        ({"nombre": k, "escenas": v} for k, v in counts.items()),
        key=lambda d: (-d["escenas"], d["nombre"]),
    )
    return {
        "escenas": n,
        "int": int_, "ext": ext_, "int_ext": mix_,
        "dia": dia, "noche": noche, "sin_definir": n - dia - noche,
        "personajes_con_dialogo": len(hablan),
        "catalogo_personajes": cat_chars,
        "locaciones_encabezado": len(locs_heading),
        "catalogo_locaciones": cat_locs,
        "dialogos": dialogos,
        "palabras": palabras,
        "paginas_estimadas": paginas,
        "duracion_min_estimada": paginas,  # ~1 min por página de guion
        "reparto": reparto,
    }


def get_escenas(proyecto=None):
    S = _parser()
    proj = _resolve_project(S, proyecto)
    if not proj:
        raise KeyError("No hay ningún proyecto .starc cargado ni encontrado.")
    conn = S._connect(proj)
    try:
        sp = S._screenplay_doc(conn)
        if not sp:
            raise ValueError("El proyecto no tiene guion (documento de tipo 10104).")
        scenes = S._parse_scenes(sp[2])
        cat_chars = len(S._all_docs(conn, S.TYPE_CHARACTER))
        cat_locs = len(S._all_docs(conn, S.TYPE_LOCATION))
    finally:
        conn.close()
    return {
        "proyecto": proj.stem,
        "path": str(proj),
        "escenas": scenes,
        "stats": _compute_stats(S, scenes, cat_chars, cat_locs),
    }


def get_escena(numero, proyecto=None):
    S = _parser()
    try:
        numero = int(numero)
    except (TypeError, ValueError):
        raise ValueError("El número de escena debe ser un entero.")
    proj = _resolve_project(S, proyecto)
    if not proj:
        raise KeyError("No hay ningún proyecto .starc cargado ni encontrado.")
    conn = S._connect(proj)
    try:
        sp = S._screenplay_doc(conn)
        if not sp:
            raise ValueError("El proyecto no tiene guion.")
        num, blocks = S._scene_full(sp[2], numero)
    finally:
        conn.close()
    if not blocks:
        raise KeyError(f"No existe la escena #{numero}.")
    return {"numero": num, "bloques": blocks}


def get_personajes(proyecto=None):
    S = _parser()
    proj = _resolve_project(S, proyecto)
    if not proj:
        raise KeyError("No hay ningún proyecto .starc cargado ni encontrado.")
    conn = S._connect(proj)
    try:
        sp = S._screenplay_doc(conn)
        scenes = S._parse_scenes(sp[2]) if sp else []
        docs = S._all_docs(conn, S.TYPE_CHARACTER)
        chars = [c for c in (S._parse_character(d[2]) for d in docs) if c and c["name"]]
    finally:
        conn.close()
    # nº de escenas donde habla cada personaje (match por nombre normalizado)
    counts = {}
    for s in scenes:
        for p in s["personajes"]:
            counts[p.upper()] = counts.get(p.upper(), 0) + 1
    chars.sort(key=lambda c: (-counts.get(c["name"].upper(), 0), c["name"]))
    out = []
    for c in chars:
        out.append({
            "nombre": c["name"],
            "rol": _role_label(S, c),
            "edad": c.get("age", ""),
            "genero": _gender_label(S, c),
            "apodo": c.get("nickname", ""),
            "one_sentence": c.get("one_sentence", ""),
            "escenas": counts.get(c["name"].upper(), 0),
        })
    return {"proyecto": proj.stem, "n": len(out), "personajes": out}


def get_personaje(nombre, proyecto=None):
    S = _parser()
    nombre = (nombre or "").strip()
    if not nombre:
        raise ValueError("Indica el nombre del personaje.")
    proj = _resolve_project(S, proyecto)
    if not proj:
        raise KeyError("No hay ningún proyecto .starc cargado ni encontrado.")
    conn = S._connect(proj)
    try:
        docs = S._all_docs(conn, S.TYPE_CHARACTER)
        namemap = S._character_name_map(conn)
        sp = S._screenplay_doc(conn)
        scenes = S._parse_scenes(sp[2]) if sp else []
        target = None
        for d in docs:
            c = S._parse_character(d[2])
            if c and c["name"] and c["name"].lower() == nombre.lower():
                target = c
                break
        if target is None:  # coincidencia parcial
            for d in docs:
                c = S._parse_character(d[2])
                if c and c["name"] and nombre.lower() in c["name"].lower():
                    target = c
                    break
    finally:
        conn.close()
    if target is None:
        raise KeyError(f"No se encontró el personaje «{nombre}».")
    escenas = [s["numero"] for s in scenes
               if target["name"].upper() in [p.upper() for p in s["personajes"]]]
    relaciones = []
    for r in target.get("relations", []):
        relaciones.append({
            "nombre": namemap.get(r["with"], r["with"] or "?"),
            "detalle": r.get("details", ""),
        })
    return {
        "nombre": target["name"],
        "rol": _role_label(S, target),
        "edad": target.get("age", ""),
        "genero": _gender_label(S, target),
        "apodo": target.get("nickname", ""),
        "one_sentence": target.get("one_sentence", ""),
        "descripcion": target.get("long", ""),
        "escenas": escenas,
        "relaciones": relaciones,
    }


# ---------------------------------------------------------------------------
# Estructura del proyecto (el árbol que pinta el navegador nativo de STARC)
# ---------------------------------------------------------------------------

# El documento de ESTRUCTURA del .starc (mime "application/x-starc/document/structure")
# es el tipo 1 en la tabla `documents`. Contiene el árbol completo de documentos como
# <item uuid type name visible> anidados — exactamente lo que el navegador nativo muestra.
TYPE_STRUCTURE = 1
_MIME_PREFIX = "application/x-starc/document/"


def _estructura_item(el):
    """Convierte un <item> del árbol de estructura a {uuid,kind,name,visible,children}.

    `kind` es el sufijo del mime tras "application/x-starc/document/" (p. ej.
    "characters", "character", "locations", "location", "screenplay",
    "screenplay/text", "screenplay/synopsis", "project", "text", "recycle-bin").
    """
    raw_type = el.get("type", "")
    kind = raw_type[len(_MIME_PREFIX):] if raw_type.startswith(_MIME_PREFIX) else raw_type
    node = {
        "uuid": (el.get("uuid") or "").strip("{}"),
        "kind": kind,
        "name": el.get("name", ""),
        "visible": (el.get("visible", "true") == "true"),
        "children": [_estructura_item(c) for c in el.findall("item")],
    }
    return node


def get_estructura(proyecto=None):
    """Árbol de documentos REAL del proyecto (el mismo que pinta el navegador nativo).

    Lee el documento de estructura (tipo 1) del .starc en modo read-only y lo devuelve
    como JSON anidado para que la barra de Odiseo refleje los documentos reales:
    Personajes → cada personaje, Locaciones → cada locación, el guion y sus
    subdocumentos. Excluye la papelera (recycle-bin), que no es navegación normal.
    """
    S = _parser()
    proj = _resolve_project(S, proyecto)
    if not proj:
        raise KeyError("No hay ningún proyecto .starc cargado ni encontrado.")
    conn = S._connect(proj)
    try:
        row = conn.execute(
            "SELECT CAST(content AS TEXT) FROM documents WHERE type=? LIMIT 1",
            (TYPE_STRUCTURE,),
        ).fetchone()
    finally:
        conn.close()
    arbol = []
    if row and row[0]:
        try:
            root = ET.fromstring(row[0])
        except ET.ParseError as e:
            raise RuntimeError(f"No pude leer la estructura del proyecto: {e}")
        for el in root.findall("item"):
            node = _estructura_item(el)
            if node["kind"] == "recycle-bin":
                continue
            arbol.append(node)
    return {"proyecto": proj.stem, "path": str(proj), "arbol": arbol}


# ---------------------------------------------------------------------------
# Router
# ---------------------------------------------------------------------------

def setup_guion_routes() -> APIRouter:
    router = APIRouter(prefix="/api/guion", tags=["guion"])

    def _guard(fn, *a, **k):
        """Mapea las excepciones de los helpers a HTTP limpio."""
        try:
            return fn(*a, **k)
        except KeyError as e:
            raise HTTPException(404, str(e).strip('"'))
        except ValueError as e:
            raise HTTPException(400, str(e))
        except RuntimeError as e:
            raise HTTPException(500, str(e))
        except Exception as e:  # noqa: BLE001
            logger.exception("guion_routes error en %s", getattr(fn, "__name__", fn))
            raise HTTPException(500, f"Error leyendo el guion: {e}")

    @router.get("/proyectos")
    async def proyectos(proyecto: str | None = None):
        return _guard(get_proyectos)

    @router.post("/usar")
    async def usar(proyecto: str = Form(...)):
        return _guard(set_proyecto, proyecto)

    @router.get("/escenas")
    async def escenas(proyecto: str | None = None):
        return _guard(get_escenas, proyecto)

    @router.get("/escena/{numero}")
    async def escena(numero: int, proyecto: str | None = None):
        return _guard(get_escena, numero, proyecto)

    @router.get("/personajes")
    async def personajes(proyecto: str | None = None):
        return _guard(get_personajes, proyecto)

    @router.get("/personaje/{nombre}")
    async def personaje(nombre: str, proyecto: str | None = None):
        return _guard(get_personaje, nombre, proyecto)

    @router.get("/estructura")
    async def estructura(proyecto: str | None = None):
        return _guard(get_estructura, proyecto)

    return router
