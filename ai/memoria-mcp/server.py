#!/usr/bin/env python3
"""
memoria-mcp — servidor MCP de la bóveda de memoria-creativa (las "manos" del cerebro
sobre el conocimiento durable: fichas .md con frontmatter YAML + wikilinks).

Expone las tools steward de odysseus (do_*_memoria de src/tool_implementations.py) por
MCP/stdio, para que CUALQUIER agente MCP-cliente (Hermes, Odiseo) lea, busque, escriba,
edite, audite y proponga cambios en la bóveda — siempre sobre la COPIA DE TRABAJO
(MEMORIA_CREATIVA_DIR; por defecto la copia bajo git `memoria-creativa-odiseo`), con
auto-backup en _cambios/_individuales/. Es la Fase D del plan
~/.claude/plans/vamos-a-planear-cuidadosamente-nested-planet.md (cross-wire + escritura).

Además añade `validar_formato_memoria` (Fase D-3): verifica el formato establecido
(frontmatter por tipo, prefijos de naming, sin emojis, integridad básica de enlaces)
ANTES de escribir, para que el "formato entrenado" sea exigible y no solo prompteado.

Transporte: stdio. Lo lanza el cliente como subproceso con el python del bundle (que trae
odysseus + el SDK `mcp`). Reusa el motor de escritura de odysseus — no hay engine nuevo.

Autoprueba (sin arrancar MCP):  python server.py --selftest
"""

import asyncio
import json
import os
import re
import sys
from pathlib import Path

from mcp.server import Server
from mcp.server.stdio import stdio_server
from mcp.types import Tool, TextContent

# odysseus/src en el path: <brain>/memoria-mcp/server.py -> <brain>/odysseus
_HERE = Path(__file__).resolve().parent
_ODYSSEUS = _HERE.parent / "odysseus"
for _p in (str(_ODYSSEUS), str(_ODYSSEUS / "src")):
    if _p not in sys.path:
        sys.path.insert(0, _p)

# Las tools steward reales (motor de escritura + backup + _safe_memoria_path).
from src import tool_implementations as ti  # noqa: E402

server = Server("memoria-mcp")

# Rangos de emoji (la bóveda NO usa emojis — regla perpetua sin-emojis).
_EMOJI_RE = re.compile(
    "[\U0001F300-\U0001FAFF\U00002600-\U000027BF\U0001F000-\U0001F0FF\U00002190-\U000021FF\U00002B00-\U00002BFF️♀-♂]"
)
# Prefijos de naming válidos por nivel/proyecto (de las reglas de la bóveda).
_PREFIXES = ("Tales-", "EDLP-", "HDUHSP-", "Diez50-", "Autor-", "Protegida-", "Rita-")
# Tipos de ficha conocidos (frontmatter `tipo:`).
_TIPOS = (
    "proyecto", "personaje", "lugar", "colaborador", "sesion", "regla-perpetua",
    "perfil-psicologico", "proyecto-no-narrativo", "entidad", "patron", "objetivo",
)


def _validar_formato(path: str | None, contenido: str | None) -> dict:
    """Valida el formato de una ficha de la bóveda. Devuelve {ok, issues:[...]}."""
    issues: list[str] = []
    text = contenido
    nombre = ""
    if text is None:
        if not path:
            return {"ok": False, "issues": ["Da un `path` o `contenido` para validar."]}
        full = ti._safe_memoria_path(path)
        if not full or not os.path.exists(full):
            return {"ok": False, "issues": [f"No existe en la bóveda: {path}"]}
        try:
            text = open(full, encoding="utf-8").read()
        except OSError as e:
            return {"ok": False, "issues": [f"No se pudo leer: {e}"]}
        nombre = os.path.basename(full)
    elif path:
        nombre = os.path.basename(path)

    # 1) Frontmatter YAML con `tipo:`
    fm = re.match(r"^---\n(.*?)\n---\n", text, re.DOTALL)
    if not fm:
        issues.append("Falta el frontmatter YAML (--- ... ---) al inicio.")
        tipo = None
    else:
        body_fm = fm.group(1)
        mt = re.search(r"^tipo:\s*(\S+)", body_fm, re.MULTILINE)
        tipo = mt.group(1).strip() if mt else None
        if not tipo:
            issues.append("El frontmatter no declara `tipo:`.")
        elif tipo not in _TIPOS:
            issues.append(f"`tipo: {tipo}` no es un tipo conocido {_TIPOS}.")
        if not re.search(r"^slug:\s*\S+", body_fm, re.MULTILINE):
            issues.append("El frontmatter no declara `slug:`.")

    # 2) Sin emojis (regla perpetua)
    if _EMOJI_RE.search(text):
        issues.append("Contiene emojis; la bóveda NO usa emojis (regla sin-emojis).")

    # 3) Prefijo de naming en el archivo (si hay nombre y no es índice/regla)
    if nombre and nombre.endswith(".md"):
        base = nombre[:-3]
        is_index = base.lower() in ("readme", "claude", "_foco", "index")
        if not is_index and not base.startswith(_PREFIXES):
            issues.append(
                f"El nombre '{nombre}' no usa un prefijo de naming {_PREFIXES}."
            )

    # 4) Integridad básica de enlaces relativos (.md) — solo si validamos un path real
    if path:
        base_dir = os.path.dirname(ti._safe_memoria_path(path) or "")
        for label, target in re.findall(r"\[([^\]]*)\]\(([^)]+)\)", text):
            t = target.strip()
            if t.startswith(("http://", "https://", "mailto:", "#", "obsidian://")):
                continue
            if t in ("url", "...", "<slug>", "path/relativo.md"):
                continue  # placeholder de plantilla
            resolved = os.path.normpath(os.path.join(base_dir, t.split("#")[0]))
            if t.endswith(".md") and not os.path.exists(resolved):
                issues.append(f"Enlace roto: [{label}]({t}) no resuelve.")

    return {"ok": len(issues) == 0, "issues": issues, "tipo": tipo if fm else None}


@server.list_tools()
async def list_tools() -> list[Tool]:
    return [
        Tool(
            name="buscar_memoria",
            description="Busca en la bóveda de memoria-creativa (FTS/RAG). Devuelve paths + fragmentos relevantes.",
            inputSchema={
                "type": "object",
                "properties": {
                    "query": {"type": "string", "description": "Tema, frase, personaje, proyecto…"},
                    "k": {"type": "integer", "description": "Nº de resultados (default 8)"},
                },
                "required": ["query"],
            },
        ),
        Tool(
            name="leer_memoria",
            description="Lee una ficha de la bóveda por su path relativo (devuelve el contenido).",
            inputSchema={
                "type": "object",
                "properties": {
                    "path": {"type": "string", "description": "Path relativo dentro de la bóveda"},
                    "max_chars": {"type": "integer", "description": "Límite de chars (default 8000)"},
                },
                "required": ["path"],
            },
        ),
        Tool(
            name="validar_formato_memoria",
            description="Valida el formato de una ficha (frontmatter por tipo, slug, prefijo de naming, sin emojis, enlaces). Úsalo ANTES de escribir. Da `path` (ficha existente) o `contenido`.",
            inputSchema={
                "type": "object",
                "properties": {
                    "path": {"type": "string", "description": "Path relativo de una ficha existente"},
                    "contenido": {"type": "string", "description": "Contenido a validar antes de escribir"},
                },
            },
        ),
        Tool(
            name="escribir_memoria",
            description="Crea o sobrescribe una ficha de la bóveda (con auto-backup + git). Valida el formato antes con validar_formato_memoria.",
            inputSchema={
                "type": "object",
                "properties": {
                    "path": {"type": "string", "description": "Path relativo (con prefijo de naming y .md)"},
                    "contenido": {"type": "string", "description": "Contenido completo (frontmatter + cuerpo)"},
                },
                "required": ["path", "contenido"],
            },
        ),
        Tool(
            name="editar_memoria",
            description="Edita una ficha por buscar/reemplazar exacto (con auto-backup).",
            inputSchema={
                "type": "object",
                "properties": {
                    "path": {"type": "string", "description": "Path relativo de la ficha"},
                    "buscar": {"type": "string", "description": "Texto exacto a reemplazar"},
                    "reemplazar": {"type": "string", "description": "Texto nuevo"},
                },
                "required": ["path", "buscar", "reemplazar"],
            },
        ),
        Tool(
            name="proponer_cambio_memoria",
            description="Propone un cambio (no lo aplica): lo deja en _cambios/pendientes/ para revisión humana. Úsalo para reorganización estructural o cambios delicados.",
            inputSchema={
                "type": "object",
                "properties": {
                    "titulo": {"type": "string"},
                    "contenido": {"type": "string"},
                    "motivo": {"type": "string"},
                },
                "required": ["titulo", "contenido"],
            },
        ),
        Tool(
            name="auditar_memoria",
            description="Audita la bóveda (huérfanos, enlaces, consistencia) en un scope dado.",
            inputSchema={
                "type": "object",
                "properties": {"scope": {"type": "string", "description": "Subcarpeta o '' para todo"}},
            },
        ),
    ]


@server.call_tool()
async def call_tool(name: str, arguments: dict) -> list[TextContent]:
    def out(obj) -> list[TextContent]:
        if isinstance(obj, str):
            return [TextContent(type="text", text=obj)]
        return [TextContent(type="text", text=json.dumps(obj, ensure_ascii=False, indent=2))]
    try:
        if name == "buscar_memoria":
            r = await ti.do_buscar_memoria(arguments["query"], int(arguments.get("k", 8)))
        elif name == "leer_memoria":
            r = await ti.do_leer_memoria(arguments["path"], int(arguments.get("max_chars", 8000)))
        elif name == "validar_formato_memoria":
            r = _validar_formato(arguments.get("path"), arguments.get("contenido"))
        elif name == "escribir_memoria":
            r = await ti.do_escribir_memoria(arguments["path"], arguments["contenido"])
        elif name == "editar_memoria":
            r = await ti.do_editar_memoria(arguments["path"], arguments["buscar"], arguments["reemplazar"])
        elif name == "proponer_cambio_memoria":
            r = await ti.do_proponer_cambio_memoria(
                arguments["titulo"], arguments["contenido"], arguments.get("motivo", "")
            )
        elif name == "auditar_memoria":
            r = await ti.do_auditar_memoria(arguments.get("scope", ""))
        else:
            return out(f"Unknown tool: {name}")
        return out(r)
    except Exception as e:  # noqa: BLE001
        return out({"error": f"{type(e).__name__}: {e}"})


async def _main():
    async with stdio_server() as (read_stream, write_stream):
        await server.run(read_stream, write_stream, server.create_initialization_options())


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        print("memoria root:", ti._memoria_root())
        print("validar (contenido sin frontmatter):", _validar_formato(None, "hola"))
        ok = _validar_formato(
            None,
            "---\ntipo: personaje\nslug: tales-prueba\n---\n# Prueba\n",
        )
        print("validar (frontmatter ok):", ok)
        sys.exit(0)
    asyncio.run(_main())
