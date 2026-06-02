# aula122-mcp — los "ojos" del cerebro sobre el proyecto

Servidor **MCP (Model Context Protocol)** que expone, en **solo lectura**, el modelo de un
proyecto de Aula 122 (formato `.starc` = SQLite con documentos XML) para que el cerebro de IA
de Aula 122 — **odysseus** — pueda *ver* el proyecto real: escenas, personajes, locaciones y
estadísticas del guion.

Es la fase **AI-2** del plan `~/.claude/plans/durante-el-desarrollo-del-jazzy-squirrel.md`
(§Capa AI — odysseus). Filosofía de Victor: **"cerebro primero, cuerpo después"** — primero el
cerebro ve el proyecto (AI-2), luego actúa con aprobación humana (AI-3).

## Principio de seguridad

- **No escribe nada.** Abre el `.starc` en modo `file:...?mode=ro` (read-only). No puede
  corromper ni borrar el proyecto.
- Las tools que **modifican** el proyecto (auto-desglose, asignar escenas a días, etc.) son
  **AI-3** y pasarán por **aceptar/descartar** del humano. No viven aquí.
- Todo local: el servidor solo lee archivos del disco; no hace red.

## Cómo se conecta a odysseus

Transporte **stdio**: odysseus lanza `server.py` como subproceso con su propio venv (que ya
trae el SDK `mcp`). Registro vía `POST /api/mcp/servers` (o la UI Admin → Tools → MCP Servers):

| Campo       | Valor |
|-------------|-------|
| `name`      | `Aula 122` |
| `transport` | `stdio` |
| `command`   | `/Users/vicgm3/Developer/odysseus/venv/bin/python` |
| `args`      | `["/Users/vicgm3/Developer/starc-fork/ai/aula122-mcp/server.py"]` |
| `env`       | `{"AULA122_PROJECT": "<ruta a un .starc>"}` |

odysseus descubre las tools automáticamente y las expone al agente como
`mcp__<server_id>__<tool>`.

## Variables de entorno

- `AULA122_PROJECT` — ruta al `.starc` activo. Si falta, usa el `.starc` más reciente de
  `~/Documents/starc/projects/` o `~/Documents/Aula 122/projects/`.
- `AULA122_SCREENPLAY_DOC` — (opcional) id del documento de guion (tipo 10104) a usar. Por
  defecto, el guion más grande (= el principal).

## Tools (solo lectura)

| Tool | Qué hace |
|------|----------|
| `listar_proyectos` | Proyectos `.starc` disponibles + cuál está activo |
| `usar_proyecto` | Cambia el proyecto activo (por nombre o ruta) |
| `proyecto_actual` | Proyecto activo, guion usado y conteos rápidos |
| `listar_escenas` | Todas las escenas: nº, INT/EXT, día/noche, locación, personajes |
| `obtener_escena` | Contenido completo de una escena por número |
| `estadisticas_guion` | Escenas, INT/EXT, día/noche, personajes, locaciones, diálogos, páginas (estimadas) |
| `listar_personajes` | Catálogo de personajes: rol, edad, género, nº de escenas en que hablan |
| `obtener_personaje` | Ficha de un personaje + relaciones resueltas a nombres |
| `listar_locaciones` | Catálogo de locaciones |
| `escenas_por_locacion` | Escenas agrupadas por locación, con los nº de escena (para plan de rodaje) |

## El formato `.starc` (referencia)

SQLite. Tabla `documents (id, uuid, type INTEGER, content BLOB=XML)`. Tipos relevantes:

- `10104` — texto del guion (escenas en XML: `<scene>` → `<scene_heading>`, `<action>`,
  `<character>`, `<dialogue>`, `<parenthetical>`; el texto va en `<v><![CDATA[…]]></v>`).
- `30001` — personajes (`<name>`, `<story_role>`, `<age>`, `<gender>`, `<relations>`…).
- `40001` — locaciones (`<name>`, `<story_role>`, descripciones sensoriales…).

## Autoprueba

```bash
AULA122_PROJECT="/Users/vicgm3/Documents/starc/projects/El encuentro de los perdidos.starc" \
  /Users/vicgm3/Developer/odysseus/venv/bin/python server.py --selftest
```

## Estado

- **AI-2 (este servidor):** lectura del proyecto. ✅
- **AI-3 (siguiente):** tools de escritura con aprobación humana (auto-desglose, plan de
  rodaje, borrador de presupuesto, generar sides, proponer planos).
