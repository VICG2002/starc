# routes/ai_routes.py
"""Punto unico de IA para Aula 122 (consumido tambien por el nucleo C++).

Expone el AIGateway por HTTP para que CUALQUIER parte de Aula 122 (incluido el
core nativo) use un solo punto de entrada a la IA, con Claude por defecto (CLI,
costo 0) y el 8B local como fallback. Asi no hay logica de IA duplicada entre
Python y C++: el C++ manda su prompt aqui y el backend lo decide el gateway.

Endpoints (exigen require_user):
  POST /api/ai/complete   body {"prompt": "<texto>", "system"?: "<...>",
                                 "backend"?: "claude"|"local"}
        -> {"text": "<respuesta>", "backend": "claude"|"local"}
  POST /api/ai/proofread  body {"text": "<escena/parrafo>", "language"?: "es",
                                 "backend"?: "claude"|"local"}
        -> {"suggestions": [{"original", "correccion", "tipo", "explicacion"}],
            "raw": "<respuesta cruda si no se pudo parsear>",
            "backend": "claude"|"local"}
     Revision ortografica/gramatical CONTEXTUAL conforme a la RAE vigente
     (lo que hunspell palabra-por-palabra no ve). Consumida por la accion
     "Revision RAE" del editor nativo.

Sincrono (el gateway usa subprocess/urllib); se declara `def` para que FastAPI lo
corra en su threadpool y no bloquee el event loop. Timeout amplio: el desglose del
guion completo puede tardar (~30-90s).
"""

import json

from fastapi import APIRouter, Body, Depends, HTTPException, Request

from src.auth_helpers import require_user

import ai_gateway

# Tope de texto por peticion: una escena larga cabe; el 8B local (fallback,
# contexto 16K) no revienta y Claude responde agil.
_PROOFREAD_MAX_CHARS = 12000

_PROOFREAD_SYSTEM = """Eres un corrector ortotipográfico profesional de español, \
con la Ortografía de la RAE vigente (2010 y posteriores). Revisas texto de guiones \
cinematográficos: respeta la voz del autor, los modismos mexicanos y la oralidad \
deliberada de los diálogos; los nombres propios de personajes y locaciones NO son \
erratas. Aplica en particular: «solo» y los demostrativos sin tilde; «guion», \
«truhan», «fie» sin tilde; prefijos unidos a la base («exmarido»); concordancia; \
homófonos por contexto (a ver/haber, haya/halla, porqué/porque/por qué, sino/si no, \
echo/hecho); dequeísmo/queísmo; puntuación de incisos y vocativos; raya de diálogo \
y signos de apertura ¿ ¡.

Devuelve SOLO un arreglo JSON (sin texto adicional ni fences). Cada elemento: \
{"original": "<fragmento exacto con el error>", "correccion": "<fragmento corregido>", \
"tipo": "ortografia"|"gramatica"|"puntuacion"|"estilo", "explicacion": "<regla, breve>"}. \
Si no hay nada que corregir devuelve []."""


def _extract_suggestions(raw: str) -> list:
    """Saca el arreglo JSON de la respuesta del modelo (tolera fences y prosa)."""
    text = (raw or "").strip()
    if text.startswith("```"):
        text = text.strip("`")
        if text.lower().startswith("json"):
            text = text[4:]
        text = text.strip()
    for candidate in (text, text[text.find("[") : text.rfind("]") + 1]):
        if not candidate:
            continue
        try:
            data = json.loads(candidate)
        except (ValueError, TypeError):
            continue
        if isinstance(data, list):
            return [s for s in data if isinstance(s, dict) and s.get("original")]
    return []


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

    @router.post("/proofread")
    def proofread(request: Request, payload: dict = Body(...),
                  _owner: str = Depends(require_user)) -> dict:
        text = (payload or {}).get("text", "")
        if not isinstance(text, str) or not text.strip():
            raise HTTPException(400, "Falta 'text'.")
        text = text[:_PROOFREAD_MAX_CHARS]
        language = (payload or {}).get("language") or "es"
        backend = (payload or {}).get("backend")
        backend = backend.strip().lower() if isinstance(backend, str) and backend.strip() else None
        system = _PROOFREAD_SYSTEM
        if isinstance(language, str) and language.lower().startswith("en"):
            system = system.replace("de español", "de inglés")
        prompt = "Revisa este texto:\n\n%s" % text
        # La correccion NORMATIVA exige fiabilidad con la RAE 2010+ (probado: el 8B
        # "corrige" «Solo»→«Sólo», la regla vieja). Por eso esta tarea prefiere
        # Claude (CLI, costo 0) aunque el default soberano del gateway sea local,
        # con fallback automatico al 8B si el CLI no esta. Igual que el
        # "Auto-extraer con Claude" del desglose. Override: body {"backend": "local"}.
        used_backend = backend or "claude"
        try:
            raw = ai_gateway.complete(system, prompt, backend=used_backend, timeout=300)
        except Exception as primary_error:
            if backend is not None:
                raise HTTPException(502, "Fallo la IA: %s" % primary_error)
            used_backend = "local"
            try:
                raw = ai_gateway.complete(system, prompt, backend=used_backend, timeout=300)
            except Exception as e:
                raise HTTPException(502, "Fallo la IA: %s" % e)
        suggestions = _extract_suggestions(raw)
        return {
            "suggestions": suggestions,
            "raw": "" if suggestions else raw,
            "backend": used_backend,
        }

    return router
