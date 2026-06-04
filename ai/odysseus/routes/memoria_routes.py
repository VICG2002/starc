# routes/memoria_routes.py
"""API para VISUALIZAR y EDITAR la bóveda (memoria creativa) dentro de Odiseo.

Lee/escribe la COPIA de trabajo (`memoria-creativa-odiseo`), nunca la canónica.
Toda la seguridad (containment dentro de la raíz, respaldos antes de escribir)
vive en los helpers de `tool_implementations`, que estas rutas reutilizan.
Endpoints:
  GET  /api/memoria/tree           → árbol de carpetas/.md de la bóveda
  GET  /api/memoria/file?path=...  → {path, content, editable}
  POST /api/memoria/file           → guarda (path, content); respalda la previa
"""

import logging
import os

from fastapi import APIRouter, Request, HTTPException, Form

from src.auth_helpers import get_current_user
from src.tool_implementations import (
    _memoria_root, _safe_memoria_path, do_escribir_memoria, _EDITABLE_EXT,
)

logger = logging.getLogger(__name__)

_SKIP_DIRS = {".git", ".obsidian", "node_modules", ".trash"}
_VIEW_EXT = (".md", ".markdown", ".txt", ".canvas")


def _build_tree(root: str):
    """Árbol anidado {name, path, type, children?} (dirs primero, alfabético)."""
    def walk(dirpath: str, relbase: str):
        children = []
        try:
            entries = sorted(os.listdir(dirpath), key=lambda s: s.lower())
        except Exception:
            return children
        dirs = [e for e in entries
                if os.path.isdir(os.path.join(dirpath, e))
                and e not in _SKIP_DIRS and not e.startswith(".")]
        files = [e for e in entries
                 if os.path.isfile(os.path.join(dirpath, e))
                 and e.lower().endswith(_VIEW_EXT)]
        for d in dirs:
            rel = (relbase + "/" + d) if relbase else d
            children.append({
                "name": d, "path": rel, "type": "dir",
                "children": walk(os.path.join(dirpath, d), rel),
            })
        for f in files:
            rel = (relbase + "/" + f) if relbase else f
            children.append({"name": f, "path": rel, "type": "file"})
        return children
    return walk(root, "")


def setup_memoria_routes() -> APIRouter:
    router = APIRouter(prefix="/api/memoria", tags=["memoria"])

    @router.get("/tree")
    async def tree(request: Request):
        root = _memoria_root()
        if not os.path.isdir(root):
            raise HTTPException(404, f"Bóveda no encontrada en {root}")
        return {"root": os.path.basename(root), "tree": _build_tree(root)}

    @router.get("/file")
    async def get_file(request: Request, path: str):
        root = _memoria_root()
        full = _safe_memoria_path(path)
        if not full or not os.path.isfile(full):
            raise HTTPException(404, "Archivo no encontrado")
        if not full.lower().endswith(_VIEW_EXT):
            raise HTTPException(400, "Tipo de archivo no soportado")
        try:
            with open(full, "r", encoding="utf-8", errors="replace") as f:
                content = f.read()
        except Exception as e:
            raise HTTPException(500, f"Error leyendo: {e}")
        return {
            "path": os.path.relpath(full, root),
            "content": content,
            "editable": full.lower().endswith(_EDITABLE_EXT),
        }

    @router.post("/file")
    async def save_file(request: Request, path: str = Form(...), content: str = Form(...)):
        owner = get_current_user(request)
        res = await do_escribir_memoria(path, content)
        msg = res.get("result", "")
        if any(k in msg for k in ("Error", "inválida", "Solo se permiten", "no está disponible")):
            raise HTTPException(400, msg)
        logger.info("memoria save by %r: %s", owner, path)
        return res

    return router
