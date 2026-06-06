# routes/idea_routes.py
"""API de SOLO LECTURA de la etapa «Idea» (.starc) para Aula 122.

La etapa Idea de pre-producción reúne los documentos de desarrollo del proyecto:
la **Sinopsis** (argumento; documento STARC tipo 10102) y el **Tratamiento**
(tipo 10103). Ambos son documentos de TEXTO SIMPLE (prosa libre), no el guion
estructurado.

Reutiliza el MISMO parser ya probado de `aula122-mcp/server.py` que usa
`guion_routes.py` (mismo `_load_parser` / `_candidate_dirs`), y expone los datos
como JSON para la página web `static/aula122/idea.html`. NUNCA escribe el .starc:
el parser abre la base SQLite en modo read-only (mode=ro). La edición real ocurre
en el editor nativo de Aula 122 (se abre vía el puente con postMessage 'edit-idea').

Endpoints:
  GET  /api/idea/proyectos  → {proyectos:[{nombre,path,activo,size_kb}], activo}
  POST /api/idea/usar       → fija el proyecto activo (form: proyecto)
  GET  /api/idea            → {proyecto, sinopsis:{texto,palabras,parrafos},
                               tratamiento:{texto,palabras,parrafos}}
"""

import importlib.util
import logging
import os
import sys
from pathlib import Path

from fastapi import APIRouter, HTTPException, Form

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Carga del parser de aula122-mcp (idéntico a guion_routes.py — sin tocar el MCP)
# ---------------------------------------------------------------------------

_S = None  # módulo server.py de aula122-mcp, cargado perezosamente


def _candidate_dirs():
    """Posibles ubicaciones de aula122-mcp/, en orden de prioridad.

    Misma lógica que guion_routes._candidate_dirs (dev: relativo a este archivo;
    producción: relativo a sys.executable, la Python del bundle).
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

    Reutiliza el mismo módulo cacheado por guion_routes si ya está en
    sys.modules['aula122_mcp_server'] — así el proyecto activo se comparte entre
    la página de Guion y la de Idea (un solo estado `_current_project`).
    """
    cached = sys.modules.get("aula122_mcp_server")
    if cached is not None:
        return cached
    seen = set()
    for c in _candidate_dirs():
        if c in seen:
            continue
        seen.add(c)
        sp = c / "server.py"
        if sp.is_file():
            spec = importlib.util.spec_from_file_location("aula122_mcp_server", str(sp))
            mod = importlib.util.module_from_spec(spec)
            sys.modules["aula122_mcp_server"] = mod
            spec.loader.exec_module(mod)
            logger.info("idea_routes: parser .starc cargado desde %s", sp)
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
# Selección de proyecto (idéntica a guion_routes)
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
    S._current_project = proj  # fija el activo en el módulo cargado (compartido con Guion)
    return {"ok": True, "activo": str(proj), "nombre": proj.stem}


def get_idea(proyecto=None):
    """Sinopsis + Tratamiento del proyecto activo (o el indicado).

    Devuelve siempre ambos bloques aunque alguno no exista en el .starc (texto
    vacío, palabras 0) para que la página los muestre como pestañas vacías y se
    pueda invitar a redactarlos / pedirlos a la IA.
    """
    S = _parser()
    proj = _resolve_project(S, proyecto)
    if not proj:
        raise KeyError("No hay ningún proyecto .starc cargado ni encontrado.")
    conn = S._connect(proj)
    try:
        sinopsis = S._simple_text_payload(conn, S.TYPE_SCREENPLAY_SYNOPSIS)
        tratamiento = S._simple_text_payload(conn, S.TYPE_SCREENPLAY_TREATMENT)
    finally:
        conn.close()
    return {
        "proyecto": proj.stem,
        "path": str(proj),
        "sinopsis": sinopsis,
        "tratamiento": tratamiento,
    }


# ---------------------------------------------------------------------------
# Router
# ---------------------------------------------------------------------------

def setup_idea_routes() -> APIRouter:
    router = APIRouter(prefix="/api/idea", tags=["idea"])

    def _guard(fn, *a, **k):
        """Mapea las excepciones de los helpers a HTTP limpio (igual que guion_routes)."""
        try:
            return fn(*a, **k)
        except KeyError as e:
            raise HTTPException(404, str(e).strip('"'))
        except ValueError as e:
            raise HTTPException(400, str(e))
        except RuntimeError as e:
            raise HTTPException(500, str(e))
        except Exception as e:  # noqa: BLE001
            logger.exception("idea_routes error en %s", getattr(fn, "__name__", fn))
            raise HTTPException(500, f"Error leyendo la idea: {e}")

    @router.get("/proyectos")
    async def proyectos(proyecto: str | None = None):
        return _guard(get_proyectos)

    @router.post("/usar")
    async def usar(proyecto: str = Form(...)):
        return _guard(set_proyecto, proyecto)

    @router.get("")
    async def idea(proyecto: str | None = None):
        return _guard(get_idea, proyecto)

    return router
