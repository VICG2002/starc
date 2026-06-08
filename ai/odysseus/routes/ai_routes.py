# routes/ai_routes.py
"""Punto unico de IA para Aula 122 (consumido tambien por el nucleo C++).

Expone el AIGateway por HTTP para que CUALQUIER parte de Aula 122 (incluido el
core nativo) use un solo punto de entrada a la IA, con Claude por defecto (CLI,
costo 0) y el 8B local como fallback. Asi no hay logica de IA duplicada entre
Python y C++: el C++ manda su prompt aqui y el backend lo decide el gateway.

Endpoint (exige require_user):
  POST /api/ai/complete  body {"prompt": "<texto>", "system"?: "<...>",
                                "backend"?: "claude"|"local"}
        -> {"text": "<respuesta>", "backend": "claude"|"local"}

Sincrono (el gateway usa subprocess/urllib); se declara `def` para que FastAPI lo
corra en su threadpool y no bloquee el event loop. Timeout amplio: el desglose del
guion completo puede tardar (~30-90s).
"""

from fastapi import APIRouter, Body, Depends, HTTPException, Request

from src.auth_helpers import require_user

import ai_gateway


def setup_ai_routes() -> APIRouter:
    router = APIRouter(prefix="/api/ai", tags=["ai"])

    @router.post("/complete")
    def complete(request: Request, payload: dict = Body(...),
                 _owner: str = Depends(require_user)) -> dict:
        prompt = (payload or {}).get("prompt", "")
        if not isinstance(prompt, str) or not prompt.strip():
            raise HTTPException(400, "Falta 'prompt'.")
        system = (payload or {}).get("system") or ""
        if not isinstance(system, str):
            system = ""
        backend = (payload or {}).get("backend")
        backend = backend.strip().lower() if isinstance(backend, str) and backend.strip() else None
        try:
            text = ai_gateway.complete(system, prompt, backend=backend, timeout=300)
        except Exception as e:
            raise HTTPException(502, "Fallo la IA: %s" % e)
        return {"text": text, "backend": ai_gateway.resolve_backend(backend)}

    return router
