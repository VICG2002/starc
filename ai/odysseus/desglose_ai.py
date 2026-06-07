"""desglose_ai.py — Sugerencia de recursos de desglose (breakdown) por escena, con IA.

Dada UNA escena de guion, devuelve sus recursos clasificados por categoria, como dict
JSON limpio. Pensado para alimentar el desglose nativo de Aula 122 (boton "Sugerir con IA").

Robusto a propio del modelo local 8B (leccion aprendida): limpia bloques <tool_call> y
fences markdown, extrae el primer objeto {...} balanceado, y si todo falla devuelve el
dict con listas vacias + "_error" (nunca revienta). Solo usa la biblioteca estandar.

Uso:
    from desglose_ai import sugerir_recursos
    rec = sugerir_recursos("INT. COCINA - DIA. ...")

CLI de prueba:
    python3 desglose_ai.py
"""

import json
import os
import re
import urllib.request

DEFAULT_ENDPOINT = os.environ.get(
    "AULA122_LLM_ENDPOINT", "http://127.0.0.1:8533/v1/chat/completions"
)

CATEGORIAS = [
    "personajes", "props", "vestuario", "vehiculos",
    "animales", "efectos_especiales", "extras",
]


def _vacio():
    return {c: [] for c in CATEGORIAS}


def _modelo_local(endpoint):
    """Pide el id del modelo cargado en el endpoint OpenAI-compatible (o None)."""
    try:
        murl = endpoint.replace("/chat/completions", "/models")
        req = urllib.request.Request(murl, headers={"Authorization": "Bearer x"})
        with urllib.request.urlopen(req, timeout=10) as r:
            return json.loads(r.read().decode("utf-8"))["data"][0]["id"]
    except Exception:
        return None


def _llm(endpoint, model, system, user, timeout):
    body = json.dumps({
        "model": model,
        "messages": [
            {"role": "system", "content": system},
            {"role": "user", "content": user},
        ],
        "temperature": 0.2,
        "max_tokens": 700,
        "stream": False,
    }).encode("utf-8")
    req = urllib.request.Request(
        endpoint, data=body,
        headers={"Authorization": "Bearer x", "Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=timeout) as r:
        d = json.loads(r.read().decode("utf-8"))
    return d["choices"][0]["message"]["content"]


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


def sugerir_recursos(escena_texto, endpoint=DEFAULT_ENDPOINT, model=None, timeout=120):
    """Devuelve dict con las 7 categorias (listas de strings). Nunca lanza excepcion."""
    out = _vacio()
    if not (escena_texto or "").strip():
        out["_error"] = "escena vacia"
        return out
    if model is None:
        model = _modelo_local(endpoint) or "local"
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
        content = _llm(endpoint, model, system, user, timeout)
    except Exception as e:
        out["_error"] = "fallo la llamada al modelo: %s" % e
        return out
    data = _extract_json(content)
    if not isinstance(data, dict):
        out["_error"] = "el modelo no devolvio JSON parseable"
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
    print("Endpoint:", DEFAULT_ENDPOINT)
    print("Escena:", escena)
    print(json.dumps(sugerir_recursos(escena), ensure_ascii=False, indent=2))
