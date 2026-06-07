"""desglose_ai.py — Sugerencia de recursos de desglose (breakdown) por escena, con IA.

Dada UNA escena de guion, devuelve sus recursos clasificados por categoria, como dict
JSON limpio. Pensado para alimentar el desglose nativo de Aula 122.

La llamada a la IA pasa por **ai_gateway** (Claude por defecto, 8B local opcional). La
limpieza tolerante del JSON se mantiene aqui (sobre todo util con el 8B, que arrastra
<tool_call> y fences): se limpian esos bloques, se extrae el primer objeto {...} balanceado,
y si todo falla se devuelve el dict con listas vacias + "_error" (nunca revienta). Solo
biblioteca estandar.

Uso:
    from desglose_ai import sugerir_recursos
    rec = sugerir_recursos("INT. COCINA - DIA. ...")            # backend por defecto (Claude)
    rec = sugerir_recursos("...", backend="local")              # forzar 8B offline

CLI de prueba:
    python3 desglose_ai.py                    # usa el backend por defecto
    AULA122_AI_BACKEND=local python3 desglose_ai.py
"""

import json
import re

import ai_gateway

CATEGORIAS = [
    "personajes", "props", "vestuario", "vehiculos",
    "animales", "efectos_especiales", "extras",
]


def _vacio():
    return {c: [] for c in CATEGORIAS}


def _extract_json(text):
    """Extrae el primer objeto JSON de un texto sucio (tool_call/fences/prosa)."""
    if not text:
        return None
    text = re.sub(r"<tool_call>.*?</tool_call>", " ", text, flags=re.DOTALL)
    text = re.sub(r"```(?:json)?", " ", text)
    try:
        return json.loads(text)
    except Exception:
        pass
    start = text.find("{")
    if start < 0:
        return None
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                try:
                    return json.loads(text[start:i + 1])
                except Exception:
                    return None
    return None


def sugerir_recursos(escena_texto, *, backend=None, timeout=120, endpoint=None, model=None):
    """Devuelve dict con las 7 categorias (listas de strings). Nunca lanza excepcion.

    backend: 'claude' | 'local' | None (auto, via ai_gateway). endpoint/model solo
    aplican al backend local.
    """
    out = _vacio()
    if not (escena_texto or "").strip():
        out["_error"] = "escena vacia"
        return out
    system = (
        "Eres un asistente de desglose (breakdown) de produccion cinematografica. "
        "Analizas UNA escena y listas sus recursos por categoria. Responde "
        "EXCLUSIVAMENTE con un objeto JSON valido, sin texto antes ni despues, sin "
        "markdown y sin <tool_call>."
    )
    user = (
        "Categorias (cada una es una lista de strings en espanol; [] si no aplica): "
        + ", ".join(CATEGORIAS) + ".\n"
        "Guia: personajes = los que hablan o actuan (con nombre); extras = figurantes "
        "o multitudes sin nombre; props = objetos que se manipulan; vestuario = ropa o "
        "uniformes notables; vehiculos; animales; efectos_especiales = fuego, humo, "
        "sangre, clima, disparos, dobles de riesgo.\n\n"
        "Escena:\n" + escena_texto.strip() + "\n\n"
        "Devuelve SOLO el objeto JSON con esas 7 claves."
    )
    try:
        content = ai_gateway.complete(
            system, user, backend=backend, timeout=timeout, endpoint=endpoint, model=model
        )
    except Exception as e:
        out["_error"] = "fallo la llamada a la IA: %s" % e
        return out
    data = _extract_json(content)
    if not isinstance(data, dict):
        out["_error"] = "la IA no devolvio JSON parseable"
        out["_raw"] = (content or "")[:400]
        return out
    for c in CATEGORIAS:
        v = data.get(c, [])
        if isinstance(v, str):
            v = [v]
        if isinstance(v, list):
            out[c] = [str(x).strip() for x in v if str(x).strip()]
    return out


if __name__ == "__main__":
    escena = ("INT. COCINA - DIA. Roman corta cebollas con un cuchillo. "
              "Su perro ladra. Suena el telefono.")
    print("Backend:", ai_gateway.resolve_backend())
    print("Escena:", escena)
    print(json.dumps(sugerir_recursos(escena), ensure_ascii=False, indent=2))
