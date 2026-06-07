# routes/desglose_routes.py
"""API de desglose con IA para Aula 122.

Expone la sugerencia de recursos por escena (personajes, props, vestuario, vehiculos,
animales, efectos_especiales, extras) para alimentar el desglose nativo (boton
"Sugerir con IA"). La logica vive en desglose_ai.sugerir_recursos (robusta a JSON sucio).

Endpoint (exige require_user):
  POST /api/desglose/sugerir   body {"escena": "<texto>"}  -> {categorias...} (o {_error})

La operacion es SINCRONA (urllib al modelo local); se declara como `def` (no async) para
que FastAPI la corra en su threadpool y no bloquee el event loop.
"""

from fastapi import APIRouter, Body, Depends, HTTPException, Request

from src.auth_helpers import require_user

from desglose_ai import sugerir_recursos


def setup_desglose_routes() -> APIRouter:
    router = APIRouter(prefix="/api/desglose", tags=["desglose"])

    @router.post("/sugerir")
    def sugerir(request: Request, payload: dict = Body(...),
                _owner: str = Depends(require_user)) -> dict:
        escena = (payload or {}).get("escena", "")
        if not isinstance(escena, str) or not escena.strip():
            raise HTTPException(400, "Falta 'escena' (el texto de la escena a desglosar).")
        # Override opcional de endpoint/modelo (por defecto: modelo local del cerebro).
        endpoint = (payload or {}).get("endpoint")
        model = (payload or {}).get("model")
        kwargs = {}
        if isinstance(endpoint, str) and endpoint.strip():
            kwargs["endpoint"] = endpoint.strip()
        if isinstance(model, str) and model.strip():
            kwargs["model"] = model.strip()
        return sugerir_recursos(escena, **kwargs)

    return router
