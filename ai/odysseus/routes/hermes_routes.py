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
  toolsets              : lista de claves del catalogo curado ([] = solo MCP). Si el
                          panel NO manda esta clave (catalogo no disponible al render),
                          se PRESERVA el valor previo (no se pisa con el default).

Endpoints (ambos exigen require_user: un fallo de middleware no los deja abiertos):
  GET  /api/hermes/settings  -> {available, managed, user}
  POST /api/hermes/settings  -> valida y guarda overrides; {ok, restart_required, user}
"""

import json
import logging
import os

from fastapi import APIRouter, Request, HTTPException, Body, Depends

from src.auth_helpers import require_user

logger = logging.getLogger(__name__)

_MCP_KEYS = ("aula122-mcp", "memoria-mcp", "notion")
_TOOL_SEARCH_VALUES = {"auto", "on", "off"}
_DEFAULT_TOOLSETS = ["terminal", "file"]
# Catalogo completo de toolsets (ESPEJO de hermesToolsetCatalog() en C++). Solo se usa
# como fallback de VALIDACION cuando hermes-managed.json no trae el catalogo (build viejo
# / version-skew); el origen normal es ese descriptor. Mantener en sync con el C++ para
# que Python y C++ no diverjan sobre que claves son validas.
_KNOWN_TOOLSET_KEYS = [
    "terminal", "file", "todo", "delegation", "session_search", "cronjob",
    "web", "memory", "browser", "vision", "skills",
]
_DEFAULTS = {
    "tool_search": "auto",
    "tool_use_enforcement": True,
    "mcp_enabled": {k: True for k in _MCP_KEYS},
    "toolsets": list(_DEFAULT_TOOLSETS),
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


def _valid_toolset_keys():
    """Claves de toolset válidas, leídas del catálogo GESTIONADO (fuente única en C++).

    Así el panel valida contra exactamente lo que el shell ofrece, sin duplicar la
    lista. Si el descriptor falta, cae a un default conservador (terminal+file).
    """
    managed = _read_json(_managed_path()) or {}
    cat = managed.get("toolset_catalog")
    if isinstance(cat, list):
        keys = [t.get("key") for t in cat if isinstance(t, dict) and t.get("key")]
        if keys:
            return keys
    # Fallback SEGURO: el set completo conocido (no terminal+file). Asi un managed.json
    # sin catalogo no encoge la whitelist ni rechaza claves legitimas ni descarta lo ya
    # guardado al leer — Python queda alineado con el catalogo completo del C++.
    return list(_KNOWN_TOOLSET_KEYS)


def _read_user():
    """Overrides actuales normalizados con defaults (robusto a archivo ausente/corrupto)."""
    raw = _read_json(_user_path()) or {}
    out = {
        "tool_search": _DEFAULTS["tool_search"],
        "tool_use_enforcement": _DEFAULTS["tool_use_enforcement"],
        "mcp_enabled": dict(_DEFAULTS["mcp_enabled"]),
        "toolsets": list(_DEFAULTS["toolsets"]),
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
    ts = raw.get("toolsets")
    if isinstance(ts, list):
        valid = _valid_toolset_keys()
        picked = []
        for x in ts:
            if x in valid and x not in picked:
                picked.append(x)
        out["toolsets"] = picked  # respeta lista vacía (= solo MCP)
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
        "toolsets": list(_DEFAULTS["toolsets"]),
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
    if "toolsets" in payload:
        ts = payload["toolsets"]
        if not isinstance(ts, list) or not all(isinstance(x, str) for x in ts):
            raise HTTPException(400, "toolsets debe ser una lista de strings")
        valid = _valid_toolset_keys()
        bad = [x for x in ts if x not in valid]
        if bad:
            raise HTTPException(400, "toolsets desconocidos: %s" % ", ".join(bad))
        picked = []
        for x in ts:  # dedup preservando orden; lista vacía permitida (= solo MCP)
            if x not in picked:
                picked.append(x)
        clean["toolsets"] = picked
    return clean


def setup_hermes_routes() -> APIRouter:
    router = APIRouter(prefix="/api/hermes", tags=["hermes"])

    @router.get("/settings")
    async def get_settings(request: Request, _owner: str = Depends(require_user)):
        managed = _read_json(_managed_path())
        if not managed or not managed.get("available"):
            # Hermes no materializado en este equipo (sin venv) o aun sin hornear.
            return {"available": False}
        return {"available": True, "managed": managed, "user": _read_user()}

    @router.post("/settings")
    async def save_settings(
        request: Request,
        payload: dict = Body(...),
        owner: str = Depends(require_user),
    ):
        path = _user_path()
        if not path:
            raise HTTPException(503, "Hermes no esta disponible en este equipo")
        clean = _validate(payload)
        # Si el panel NO mandó 'toolsets' (p.ej. el catalogo no estaba disponible al
        # renderizar, build viejo), NO pisamos la seleccion del usuario con el default:
        # preservamos lo ya guardado. Evita el borrado silencioso a "solo MCP".
        if "toolsets" not in payload:
            existing = _read_json(path) or {}
            prev = existing.get("toolsets")
            if isinstance(prev, list):
                clean["toolsets"] = [x for x in prev if isinstance(x, str)]
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
