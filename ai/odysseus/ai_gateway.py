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


def _complete_local(system, user, timeout, endpoint=None, model=None):
    endpoint = endpoint or DEFAULT_LOCAL_ENDPOINT
    if model is None:
        model = _modelo_local(endpoint) or "local"
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


def _complete_claude(system, user, timeout):
    cli = locate_claude_cli()
    if not cli:
        raise RuntimeError("CLI de claude no encontrado (instalar + 'claude auth login --claudeai')")
    prompt = (system + "\n\n" + user) if system else user
    #
    # Patron YA PROBADO (lado C++, AutoExtractDialog): --print --output-format text,
    # SIN --bare, stdin a /dev/null. stdout es el resultado; error solo si exitcode != 0
    # y stdout vacio. Trampas documentadas en ~/Developer/starc-fork/CLAUDE.md.
    #
    proc = subprocess.run(
        [cli, "--print", "--output-format", "text", prompt],
        stdin=subprocess.DEVNULL,
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
    """'claude' si el CLI esta disponible; si no, 'local'."""
    return "claude" if locate_claude_cli() else "local"


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


def complete(system, user, *, backend=None, timeout=120, endpoint=None, model=None):
    """Devuelve el texto de la respuesta de la IA. Lanza excepcion clara ante fallo.

    backend: 'claude' | 'local' | None (auto). endpoint/model solo aplican a 'local'.
    """
    b = resolve_backend(backend)
    if b == "claude":
        return _complete_claude(system, user, timeout)
    if b == "local":
        return _complete_local(system, user, timeout, endpoint=endpoint, model=model)
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


def complete_messages(messages, *, backend=None, timeout=120):
    """Como complete(), pero recibe una conversacion (lista de mensajes OpenAI).

    Usado por el chat normal de Odiseo para enrutar a Claude (CLI). Nunca usado en
    modo agente (ese conserva su propio loop con tools sobre el 8B).
    """
    b = resolve_backend(backend)
    system, convo = _messages_split(messages)
    if b == "claude":
        return _complete_claude(system, convo, timeout)
    if b == "local":
        return _complete_local(system, convo, timeout)
    raise ValueError("backend de IA desconocido: %s" % b)


if __name__ == "__main__":
    print("backends disponibles:", available_backends())
    print("backend por defecto:", default_backend())
    print("CLI claude:", locate_claude_cli())
