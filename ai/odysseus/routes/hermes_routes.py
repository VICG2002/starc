# routes/hermes_routes.py
"""API para VER y EDITAR los ajustes de BAJO RIESGO de Hermes (4o servicio) desde Odiseo.

Hermes es el agente autonomo de Aula 122 / Diez50. Su config.yaml lo HORNEA el shell
nativo (C++ BrainProcessManager) en cada arranque a partir de (a) llaves GESTIONADAS
fijas (modelo compartido, endpoint local :8533, api_server, rutas de los MCP) y (b) un
JSON de overrides EDITABLES que posee este panel. Por eso aqui SOLO tocamos ese JSON;
las llaves gestionadas son de solo-lectura. Los cambios se aplican al REINICIAR Aula 122
(el shell re-hornea config.yaml al arrancar; el config es un artefacto derivado).

Archivos (en HERMES_RUNTIME_DIR, lo pasa el shell nativo por entorno):
  hermes-managed.json        (solo lectura; lo escribe C++)  -> modelo, endpoint, catalogo MCP
  hermes-user-settings.json  (lectura/escritura; lo posee este panel)

Llaves editables (las unicas que se persisten; cualquier otra se ignora):
  tool_search           : "auto" | "on" | "off"
  tool_use_enforcement  : bool
  mcp_enabled           : {"aula122-mcp": bool, "memoria-mcp": bool, "notion": bool}

Endpoints:
  GET  /api/hermes/settings  -> {available, managed, user}
  POST /api/hermes/settings  -> valida y guarda overrides; {ok, restart_required, user}
"""

import json
import logging
import os

from fastapi import APIRouter, Request, HTTPException, Body

from src.auth_helpers import get_current_user

logger = logging.getLogger(__name__)

_MCP_KEYS = ("aula122-mcp", "memoria-mcp", "notion")
_TOOL_SEARCH_VALUES = {"auto", "on", "off"}
_DEFAULTS = {
    "tool_search": "auto",
    "tool_use_enforcement": True,
    "mcp_enabled": {k: True for k in _MCP_KEYS},
}


def _runtime_dir():
    d = os.environ.get("HERMES_RUNTIME_DIR")
    return os.path.expanduser(d) if d else None


def _managed_path():
    d = _runtime_dir()
    return os.path.join(d, "hermes-managed.json") if d else None


def _user_path():
    d = _runtime_dir()
    return os.path.join(d, "hermes-user-settings.json") if d else None


def _read_json(path):
    if not path:
        return None
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return None


def _read_user():
    """Overrides actuales normalizados con defaults (robusto a archivo ausente/corrupto)."""
    raw = _read_json(_user_path()) or {}
    out = {
        "tool_search": _DEFAULTS["tool_search"],
        "tool_use_enforcement": _DEFAULTS["tool_use_enforcement"],
        "mcp_enabled": dict(_DEFAULTS["mcp_enabled"]),
    }
    if raw.get("tool_search") in _TOOL_SEARCH_VALUES:
        out["tool_search"] = raw["tool_search"]
    if isinstance(raw.get("tool_use_enforcement"), bool):
        out["tool_use_enforcement"] = raw["tool_use_enforcement"]
    m = raw.get("mcp_enabled")
    if isinstance(m, dict):
        for k in _MCP_KEYS:
            if isinstance(m.get(k), bool):
                out["mcp_enabled"][k] = m[k]
    return out


def _validate(payload):
    """Devuelve un dict limpio SOLO con llaves permitidas, o lanza HTTPException(400).

    Cualquier llave fuera de la lista blanca se descarta en silencio: el panel no
    puede inyectar llaves gestionadas (modelo, base_url, rutas de comandos) por aqui.
    """
    if not isinstance(payload, dict):
        raise HTTPException(400, "Cuerpo invalido")
    clean = {
        "tool_search": _DEFAULTS["tool_search"],
        "tool_use_enforcement": _DEFAULTS["tool_use_enforcement"],
        "mcp_enabled": dict(_DEFAULTS["mcp_enabled"]),
    }
    if "tool_search" in payload:
        if payload["tool_search"] not in _TOOL_SEARCH_VALUES:
            raise HTTPException(400, "tool_search debe ser auto, on u off")
        clean["tool_search"] = payload["tool_search"]
    if "tool_use_enforcement" in payload:
        if not isinstance(payload["tool_use_enforcement"], bool):
            raise HTTPException(400, "tool_use_enforcement debe ser booleano")
        clean["tool_use_enforcement"] = payload["tool_use_enforcement"]
    if "mcp_enabled" in payload:
        m = payload["mcp_enabled"]
        if not isinstance(m, dict):
            raise HTTPException(400, "mcp_enabled debe ser un objeto")
        for k in _MCP_KEYS:
            if k in m:
                if not isinstance(m[k], bool):
                    raise HTTPException(400, "mcp_enabled.%s debe ser booleano" % k)
                clean["mcp_enabled"][k] = m[k]
    return clean


def setup_hermes_routes() -> APIRouter:
    router = APIRouter(prefix="/api/hermes", tags=["hermes"])

    @router.get("/settings")
    async def get_settings(request: Request):
        managed = _read_json(_managed_path())
        if not managed or not managed.get("available"):
            # Hermes no materializado en este equipo (sin venv) o aun sin hornear.
            return {"available": False}
        return {"available": True, "managed": managed, "user": _read_user()}

    @router.post("/settings")
    async def save_settings(request: Request, payload: dict = Body(...)):
        owner = get_current_user(request)
        path = _user_path()
        if not path:
            raise HTTPException(503, "Hermes no esta disponible en este equipo")
        clean = _validate(payload)
        try:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            tmp = path + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(clean, f, ensure_ascii=False, indent=2)
            os.replace(tmp, path)
        except Exception as e:
            raise HTTPException(500, "No pude guardar: %s" % e)
        logger.info("hermes settings saved by %r: %s", owner, clean)
        return {"ok": True, "restart_required": True, "user": clean}

    return router
