# Empaquetar el cerebro (odysseus) dentro de Aula 122 — autocontenido

> Objetivo (decisión de Victor, 2026-06-01): **Aula 122 no depende de ningún servidor externo.**
> odysseus (su código MIT, **vendorizado** en `ai/odysseus/`), un **Python relocatable**, y el
> **runtime de modelo (llama.cpp)** viven DENTRO de `Aula_122.app`. La app **lanza y mata** el
> cerebro ella misma (sin LaunchAgents, sin Ollama externo, sin instalación aparte).

## Estado
- ✅ **Vendoreo:** `ai/odysseus/` = código de odysseus con nuestras modificaciones
  (`tool_parsing.py` parser `<tool_call>` JSON · `constants.py` temp 0.2 · `chat_routes.py`
  perfil `mcp_only` + disable de tools internas). **A partir de ahora editar AQUÍ** (no en
  `~/Developer/odysseus`, que queda deprecado cuando el bundle funcione).
- ✅ **Fase A — Python relocatable:** `ai/odysseus/runtime-python/` (CPython 3.12.13, 435 MB,
  deps de odysseus verificadas). Es el Python que viaja dentro del `.app`.
- ✅ **Fase B — Runtime de modelo:** `ai/bundle-llama.sh` → `ai/llama/` (llama-server + 9 dylibs,
  23 MB, relocatable, 0 fugas a Homebrew, inferencia Metal verificada) + `ai/fetch-model.sh`
  (materializa el GGUF al primer arranque: Ollama en dev / Hugging Face en producción).
- ✅ **Fase C — Lifecycle C++:** `BrainProcessManager` lanza/mata llama-server + odysseus + chromadb
  con el python bundleado, fija el endpoint del modelo (B3) y corre setup.py en el 1er arranque.
  Compila e integrado en el arranque/cierre de la app; **validado headless** (odysseus corre desde el
  runtime relocatable). Falta: probar en la GUI + data dir mutable en producción.
- ✅ **Fase D — Integración de build:** `ai/build-brain-bundle.sh` ensambla el cerebro dentro del
  `.app` (`Contents/Resources/brain/{python,llama,odysseus}`, rsync idempotente); hook qmake
  condicional (`CONFIG+=bundle_brain`) en `src/app/app.pro`. Verificado: `.app` 866 MB, el python
  relocatable corre desde el bundle. El manager corre odysseus desde una copia mutable en AppData
  (`syncOdysseusRuntime`) → funciona aun con el `.app` firmado/read-only.
- ⏳ Pendiente: firma/notarización (E) — no bloqueante para uso local de Victor.

## Layout objetivo en el .app
```
Aula_122.app/Contents/Resources/brain/
├── odysseus/         # copia de ai/odysseus/ (sin venv/data)
├── python/           # CPython relocatable + deps de requirements.txt (~500 MB)
└── llama/            # llama-server + dylibs (de /opt/homebrew, install_names arreglados)
```
Datos de runtime (mutables) van a `~/Library/Application Support/Diez50/Aula 122/brain/`
(chromadb, sesiones, modelos GGUF descargados). NO dentro del .app (read-only tras firmar).

## Fase A — Python relocatable (el núcleo técnico)
1. Bajar **python-build-standalone** CPython 3.12 para `aarch64-apple-darwin` (install_only).
   (github.com/astral-sh/python-build-standalone releases.)
2. Extraer a `ai/odysseus/runtime-python/`.
3. `runtime-python/bin/python3 -m pip install -r ai/odysseus/requirements.txt`
   (mismas wheels que el venv actual de 420 MB ya instaló OK → debería ir limpio:
   onnxruntime, chromadb(+rust bindings), fastembed, fastapi, uvicorn, mcp, lxml, cryptography…).
4. Verificar: `runtime-python/bin/python3 -c "import app"` desde `ai/odysseus/` y un
   `uvicorn app:app` de humo en 127.0.0.1.
   *Relocatable = al copiarlo dentro del .app sigue funcionando (sin rutas absolutas al venv).*

## Fase B — Runtime de modelo (llama.cpp) ✅ binario+modelo · B3 va en Fase C
- ✅ **B1 — bundle del binario** (`ai/bundle-llama.sh`): copia `llama-server` + deps resolviendo
  `@rpath`/`@loader_path` recursivamente (incluye openssl@3 y libomp, que `otool -L` no muestra
  a primera vista), reescribe install_names a `@rpath/<base>`, añade rpath `@loader_path/../lib`
  y re-firma ad-hoc. Salida `ai/llama/{bin,lib}` (~23 MB). **Verificado:** 0 referencias a
  `/opt/homebrew` + inferencia real (qwen2.5:7b, Metal, carga 4.4 GB en ~4 s) desde el bundle aislado.
  *(Los backends Metal/CPU vienen embebidos en `libggml` — no hay backends sueltos por `dlopen`.)*
- ✅ **B2 — modelo al primer arranque** (`ai/fetch-model.sh`): materializa el GGUF en
  `…/Application Support/Diez50/Aula 122/brain/models/`. En dev enlaza el blob de Ollama
  (instantáneo, 0 espacio extra); en producción `--hf <repo> <archivo>` o `AULA122_MODEL_COPY=1`.
  No se bundlean los ~5 GB en el `.app`. **Modelo por defecto qwen2.5:7b** — la elección final
  (7b ágil vs 14b más capaz) la decide Victor; ambos ya están en Ollama.
- ⏳ **B3 — apuntar odysseus a este llama-server local** (OpenAI-compat, reemplaza Ollama): se hace
  junto al lifecycle (Fase C), porque depende de que la app lance el server. temp 0.2 + perfil
  `mcp_only` ya resuelven la fiabilidad de tools.

## Fase C — Lifecycle desde Aula 122 (C++) ✅ núcleo / falta GUI + prod
Implementado: `src/core/management_layer/brain_process_manager.{h,cpp}` (`BrainProcessManager`,
patrón pimpl), instanciado en `ApplicationManager`, enganchado al arranque post-UI (`exec()`) y al
cierre (`exit()` + destructor); declarado en `core.pro`. **Compila e integra en `libcoreplugin.dylib`.**
- Resuelve `<brain>` por `$AULA122_BRAIN_DIR` (dev) || `<.app>/Contents/Resources/brain` (release);
  datos mutables en AppDataLocation/brain. Sin brain válido → no-op (degradación elegante).
- `startAll()`: 1) chromadb (si el runtime trae el binario), 2) llama-server (loopback :8533, Metal),
  3) `ensureOdysseusData()` (corre setup.py si falta la DB), 4) odysseus (`uvicorn app:app`,
  `LOCALHOST_BYPASS=true` → loopback sin login, :7860), 5) escribe `odysseus_model.json` apuntando al
  llama-server local (**B3**), 6) health-check no bloqueante (QTcpSocket) → `ready()`.
- `stopAll()`: TERM y, si no cooperan, KILL, en orden inverso.
- **Validado headless** (réplica de startAll): odysseus arranca **desde el python relocatable** (Fase A),
  HTTP 200, 59 tools indexadas, ChromaDB conectado; llama-server sirve OpenAI-compat. `ai/assemble-brain.sh`
  arma el layout dev (symlinks). Bug de vendoreo arreglado: faltaba `services/docs/` (el filtro excluyó
  "docs" y se llevó ese subpaquete de código).

**Pendiente de Fase C:**
- Probar en la **GUI real** (abrir Aula 122 con `AULA122_BRAIN_DIR` y los LaunchAgents detenidos).
- **Producción:** odysseus deriva su data dir del código (`BASE_DIR/data`, CWD `./data`) → en el .app
  read-only no es escribible. La Fase D debe ubicar odysseus (o su data) en zona mutable: copiar el
  código a AppData en el 1er arranque, **o** parchear odysseus para honrar `ODYSSEUS_DATA_DIR` global.
- **Retirar los LaunchAgents** `com.diez50.{odysseus,ollama,chromadb}` cuando el lifecycle in-app quede confirmado.
- **Re-registrar `aula122-mcp`** en el data dir nuevo del cerebro bundleado (el data fresco no lo trae).

## Fase D — Integración de build (qmake) ✅
`ai/build-brain-bundle.sh <app>` ensambla (rsync idempotente) el cerebro dentro del bundle:
`brain/python` ← runtime-python (A) · `brain/llama` ← ai/llama (B) · `brain/odysseus` ← código
vendorizado (excluye runtime-python/data/venv; exclusiones ancladas a `/` para NO repetir el bug de
`services/docs`). Enganchado a qmake en `src/app/app.pro` vía `QMAKE_POST_LINK` **condicional**
(`qmake CONFIG+=bundle_brain`) — por defecto NO corre; los builds de dev usan `$AULA122_BRAIN_DIR`
(symlinks de `assemble-brain.sh`). **Verificado:** `.app` 394→866 MB; el CPython relocatable copiado
corre (3.12.13, fastapi/uvicorn/mcp/chromadb OK); llama-server 0 fugas a Homebrew; las rutas que el
manager resuelve están presentes. **Data dir mutable resuelto** (era pendiente de C→D): el manager
copia odysseus a `…/Application Support/Diez50/Aula 122/brain/odysseus-runtime` (`syncOdysseusRuntime`,
rsync que preserva `data/`) y corre desde ahí → escribe su DB aunque el `.app` esté firmado/read-only.
Artefactos pesados (runtime-python, models, *.gguf, brain/) gitignored; las fuentes (*.sh) sí se versionan.

## Fase E — Firma / notarización
Firmar los binarios embebidos (python, llama-server, dylibs) con hardened runtime + notarizar
el .app para distribuir sin Gatekeeper. (Para uso local de Victor no es bloqueante.)

## Notas / riesgos
- **Loopback only** (127.0.0.1) — nunca exponer a la red (regla de `/seguridad`).
- **Tamaño:** ~500 MB (python+deps) + ~50 MB (llama) → .app de ~900 MB–1 GB. El modelo (~5 GB)
  se descarga aparte (no en el .app).
- **Drift:** editar SIEMPRE `ai/odysseus/` (no la copia vieja). El bundle se construye de aquí.
- **ChromaDB/embeddings:** primer arranque baja el modelo de embeddings (FastEmbed) — prever espera.
- Las mejoras a odysseus (parser, temp, perfiles) viajan porque están en `ai/odysseus/`.
