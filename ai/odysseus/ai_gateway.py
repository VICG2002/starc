"""ai_gateway.py — Punto unico de acceso a la IA para Aula 122 (Odiseo).

Una sola interfaz, backends enchufables. Por defecto usa **Claude via el CLI**
(reusa la suscripcion del usuario, costo marginal cero — el "ganador silencioso");
el modelo local 8B (llama-server) queda como **fallback offline opcional**.

Primer paso incremental hacia "un solo cerebro, Claude por defecto"
(ver ~/.claude/plans/structured-orbiting-valley.md). El primer consumidor migrado
a este gateway es el desglose IA (desglose_ai.py).

Uso:
    from ai_gateway import complete
    texto = complete("Eres un asistente...", "Analiza esta escena...")

Seleccion de backend:
    argumento `backend` -> env AULA122_AI_BACKEND -> default "claude" si el CLI
    esta disponible, si no "local".

Solo biblioteca estandar.
"""

import json
import os
import shutil
import subprocess
import urllib.request


# ---------------------------------------------------------------------------
# Backend local (llama-server, OpenAI-compatible)
# ---------------------------------------------------------------------------

DEFAULT_LOCAL_ENDPOINT = os.environ.get(
    "AULA122_LLM_ENDPOINT", "http://127.0.0.1:8533/v1/chat/completions"
)


def _modelo_local(endpoint):
    """Id del modelo cargado en el endpoint OpenAI-compatible (o None)."""
    try:
        murl = endpoint.replace("/chat/completions", "/models")
        req = urllib.request.Request(murl, headers={"Authorization": "Bearer x"})
        with urllib.request.urlopen(req, timeout=10) as r:
            return json.loads(r.read().decode("utf-8"))["data"][0]["id"]
    except Exception:
        return None


def _complete_local(system, user, timeout, endpoint=None, model=None, response_format=None):
    endpoint = endpoint or DEFAULT_LOCAL_ENDPOINT
    if model is None:
        model = _modelo_local(endpoint) or "local"
    body_dict = {
        "model": model,
        "messages": [
            {"role": "system", "content": system},
            {"role": "user", "content": user},
        ],
        "temperature": 0.2,
        "max_tokens": 700,
        "stream": False,
    }
    # Salida forzada (constrained decoding de llama.cpp): con response_format
    # json_schema el 8B NO puede devolver basura (mata el <tool_call> alucinado y el
    # JSON roto). Solo se pasa cuando el consumidor pide estructura (p.ej. el desglose).
    if response_format:
        body_dict["response_format"] = response_format
    body = json.dumps(body_dict).encode("utf-8")
    req = urllib.request.Request(
        endpoint, data=body,
        headers={"Authorization": "Bearer x", "Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=timeout) as r:
        d = json.loads(r.read().decode("utf-8"))
    return d["choices"][0]["message"]["content"]


# ---------------------------------------------------------------------------
# Backend Claude (CLI, reusa la suscripcion)
# ---------------------------------------------------------------------------

def locate_claude_cli():
    """Localiza el CLI `claude` (mismo orden que el lado C++ locateClaudeCli())."""
    candidates = [
        os.path.expanduser("~/.local/bin/claude"),
        "/opt/homebrew/bin/claude",
        "/usr/local/bin/claude",
    ]
    for p in candidates:
        if os.path.isfile(p) and os.access(p, os.X_OK):
            return p
    return shutil.which("claude")


def _complete_claude(system, user, timeout, allow_web=False):
    cli = locate_claude_cli()
    if not cli:
        raise RuntimeError("CLI de claude no encontrado (instalar + 'claude auth login --claudeai')")
    if allow_web:
        # El system prompt de Odiseo (rita) lo enmarca como asistente local y hace que
        # Claude crea que no tiene internet. Esta instruccion explicita lo corrige: SI
        # tiene WebSearch/WebFetch (pre-aprobadas abajo) y debe usarlas SOLO, sin avisar.
        _web = ("IMPORTANTE: tienes las herramientas WebSearch y WebFetch disponibles y "
                "funcionando. Cuando la pregunta requiera informacion actual, reciente, "
                "posterior a tu entrenamiento, o que no sepas con certeza, USALAS de forma "
                "AUTONOMA (sin pedir permiso ni avisar) y cita las fuentes. NUNCA digas que "
                "no tienes acceso a internet: si lo necesitas, navega.")
        system = (system + "\n\n" + _web) if system else _web
    prompt = (system + "\n\n" + user) if system else user
    #
    # Patron YA PROBADO (lado C++, AutoExtractDialog): --print --output-format text,
    # SIN --bare, stdin a /dev/null. stdout es el resultado; error solo si exitcode != 0
    # y stdout vacio. Trampas documentadas en ~/Developer/starc-fork/CLAUDE.md.
    #
    args = [cli, "--print", "--output-format", "text"]
    if allow_web:
        # Pre-aprobar SOLO las tools web: en modo --print (no interactivo) Claude no
        # puede pedir permiso, asi que sin esto NUNCA navega aunque la pregunta lo
        # amerite. Acotado a WebSearch/WebFetch -> nada de Bash/archivos.
        # OJO: --allowedTools es variadic; si el prompt va como argumento posicional se
        # lo come. Por eso el prompt va por STDIN (input=), no como posicional.
        args += ["--allowedTools", "WebSearch,WebFetch"]
    proc = subprocess.run(
        args,
        input=prompt,
        capture_output=True,
        timeout=timeout,
        text=True,
    )
    out = (proc.stdout or "").strip()
    if proc.returncode != 0 and not out:
        raise RuntimeError(
            "claude CLI fallo (codigo %s): %s" % (proc.returncode, (proc.stderr or "")[:300])
        )
    return out


# ---------------------------------------------------------------------------
# Seleccion de backend + API publica
# ---------------------------------------------------------------------------

def default_backend():
    """Default SOBERANO: el modelo local interno (8B). Diez50 corre con su PROPIA IA;
    Claude NO es el cerebro del runtime. Claude solo si se pide explicito (backend='claude'
    o env AULA122_AI_BACKEND=claude) — queda como herramienta de desarrollo / fallback.
    Decision de Victor 2026-06-07: 'el chat no deberia ser de claude; usa las herramientas
    internas, tu solo supervisas'."""
    return "local"


def available_backends():
    backs = ["local"]
    if locate_claude_cli():
        backs.insert(0, "claude")
    return backs


def resolve_backend(backend=None):
    """Backend efectivo: arg -> env AULA122_AI_BACKEND -> default."""
    if backend:
        return backend.strip().lower()
    env = os.environ.get("AULA122_AI_BACKEND")
    if env and env.strip():
        return env.strip().lower()
    return default_backend()


def complete(system, user, *, backend=None, timeout=120, endpoint=None, model=None,
             response_format=None, allow_web=False):
    """Devuelve el texto de la respuesta de la IA. Lanza excepcion clara ante fallo.

    backend: 'claude' | 'local' | None (auto). endpoint/model y response_format solo
    aplican a 'local' (Claude ya devuelve JSON limpio cuando se le pide; el grammar es
    el arreglo para el 8B). allow_web solo aplica a 'claude' (pre-aprueba WebSearch/WebFetch).
    """
    b = resolve_backend(backend)
    if b == "claude":
        return _complete_claude(system, user, timeout, allow_web=allow_web)
    if b == "local":
        return _complete_local(system, user, timeout, endpoint=endpoint, model=model,
                               response_format=response_format)
    raise ValueError("backend de IA desconocido: %s" % b)


def _messages_split(messages):
    """Aplana una lista de mensajes estilo OpenAI en (system, conversacion).

    Los mensajes 'system' se juntan; el resto se renderiza como turnos etiquetados
    (Usuario/Asistente) — formato que el CLI de Claude responde bien en --print.
    """
    sys_parts, convo = [], []
    for m in messages or []:
        role = m.get("role")
        content = m.get("content") or ""
        if isinstance(content, list):  # multimodal: quedarnos con las partes de texto
            content = " ".join(
                p.get("text", "") for p in content if isinstance(p, dict) and p.get("text")
            )
        content = (content or "").strip()
        if not content:
            continue
        if role == "system":
            sys_parts.append(content)
        else:
            label = "Usuario" if role == "user" else "Asistente"
            convo.append("%s: %s" % (label, content))
    return "\n\n".join(sys_parts), "\n\n".join(convo)


def complete_messages(messages, *, backend=None, timeout=120, allow_web=False):
    """Como complete(), pero recibe una conversacion (lista de mensajes OpenAI).

    Usado por el chat normal de Odiseo para enrutar a Claude (CLI). Nunca usado en
    modo agente (ese conserva su propio loop con tools sobre el 8B). allow_web pre-aprueba
    WebSearch/WebFetch para que Claude navegue cuando la pregunta lo amerite.
    """
    b = resolve_backend(backend)
    system, convo = _messages_split(messages)
    if b == "claude":
        return _complete_claude(system, convo, timeout, allow_web=allow_web)
    if b == "local":
        return _complete_local(system, convo, timeout)
    raise ValueError("backend de IA desconocido: %s" % b)


if __name__ == "__main__":
    print("backends disponibles:", available_backends())
    print("backend por defecto:", default_backend())
    print("CLI claude:", locate_claude_cli())
