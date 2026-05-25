# CLAUDE.md — Fork propio de STARC (rebautizado Aula 122 / Aula_122)

Bienvenido. Este es el fork de [Story Architect](https://github.com/dimkanovikov/starc)
propiedad del usuario, clonado a `~/Developer/starc-fork/` y trabajado en
la rama `assistant`.

**Visión actual (decisión 2026-05-25):** Aula 122 evoluciona de
"editor de guion con asistente Claude" a **software unificado de
pre-producción cinematográfica indie**. Cubre las 18 etapas del workflow
indie (idea → call sheet del primer día), asistido por IA, sin
suscripciones, soberano de datos. Estrategia de 3 capas:

1. **Core nativo Qt/C++** — guion + breakdown + schedule + crew + call
   sheets + shot list + mood board + budget (todo en `corelib` + plugins
   propios)
2. **AI** — Claude vía CLI ya integrado + OpenMontage opcional (Bloque 11)
3. **Bridges externos** — Storyboarder, xSTUDIO/Clapshot, FFmpeg (opcional
   por bloque 12)

**Identidad final** (post-segundo-rebrand, 2026-05-24):
- **Marca paraguas (organización):** Diez50
- **Producto / software:** Aula 122 (display) / `Aula_122` (técnico, con underscore — el espacio rompe `install_name_tool`)
- **Bundle compilado:** `Aula_122.app` con bundle ID `app.diez50.aula122`
- **Logo:** assets en `/Volumes/T9_DIEZ50/Pagina Web/Aula 122 logo/`

Esta es la versión corta para sesiones rápidas. **Para sesiones largas o
nuevas, leer en orden:**

1. `~/.claude/plans/durante-el-desarrollo-del-jazzy-squirrel.md` — plan
   completo de 12 bloques (156-170h), prioridad actual
2. `~/.claude/projects/-Users-vicgm3/memory/architecture_aula122_ecosystem.md`
   — visión completa del ecosistema (3 capas)
3. `~/.claude/projects/-Users-vicgm3/memory/project_aula122_diez50.md` —
   estado actual del proyecto + commits clave
4. `~/memoria-asistente-escritura/referencias/_workflow-preproduccion-indie.md`
   — las 18 etapas del workflow
5. `~/memoria-asistente-escritura/referencias/_herramientas-ecosistema.md`
   — tools open source evaluadas + decisiones
6. `~/memoria-asistente-escritura/metodologia/pasos-a-seguir.md` —
   playbook general del asistente de escritura

## Lo esencial

- **Stack:** C++ + **Qt 6.11.1** (vía Homebrew) + **qmake** (no CMake).
- **Build:** `cd src && qmake && make -j$(sysctl -n hw.ncpu)`.
- **Binario generado:** `~/Developer/starc-fork/src/_build/Aula_122.app` (~370 MB con 76 plugins).
- **Lanzar:** `open ~/Developer/starc-fork/src/_build/Aula_122.app`.
- **Rama de trabajo:** `assistant` (NUNCA tocar `master` salvo branding).
- **Remotes:**
  - `origin` → `github.com/VICG2002/starc` (fork del usuario).
  - `upstream` → `github.com/dimkanovikov/starc` (repo original).

## Identidad del fork (post-segundo-rebrand 2026-05-24)

| Campo                | Valor                                                  |
|----------------------|--------------------------------------------------------|
| Display name         | **Aula 122** (con espacio, mayúsculas, para humanos)    |
| Nombre técnico       | **`Aula_122`** (underscore — el espacio rompe builds)   |
| Bundle ID            | **`app.diez50.aula122`** (lowercase intencional)        |
| Organization (macOS) | **Diez50** (marca paraguas)                             |
| Organization domain  | `diez50.local`                                          |
| Ejecutable interno   | `Aula_122` (era `aula122`, `Diez50`, antes `starcapp`)  |
| `.app` generado      | `Aula_122.app` (era `Diez50.app`, antes `starcapp.app`)  |
| Icon                 | logo Aula 122 en `src/app/icon.icns`                    |

**Coexistencia con la app oficial:** la `/Applications/Story Architect.app`
del usuario (bundle ID `dev.storyapps.starc-beta`) y nuestro `Aula_122.app`
(`app.diez50.aula122`) son apps separadas para macOS — pueden estar
abiertas a la vez sin conflicto. El formato `.starc` es compartido.

## Política de ramas (no negociable)

- `master` queda casi limpio. Solo cambios sincronizados con `upstream/master`
  + branding mínimo si aplica. Política: mergear `upstream/master` →
  `master` periódicamente sin fricción.
- `assistant` es donde vive **todo** el rebrand a Diez50 + futuro código
  del asistente nativo (dock, comunicación con Claude, tools narrativas).
- Mergear `assistant` ← `master` cuando convenga traer mejoras upstream.

## Submódulos (importante para compilar)

El README upstream dice "solo `qbreakpad`", pero **eso es insuficiente**.
Para compilar también necesitas:

```bash
git submodule update --init --recursive \
  src/3rd_party/qbreakpad/ \
  src/3rd_party/pdfhummus/ \
  src/3rd_party/pdftextextraction/
```

Los 26 submódulos de `src/core/management_layer/plugins/` (en SSH `git@github.com:`)
son features opcionales — se cargan dinámicamente si están presentes.

## Estructura del repo

```
src/
├── starc.pro             # proyecto qmake raíz
├── app/                  # entry point — main.cpp + application.cpp
├── core/                 # núcleo + management_layer/plugins/ (29 plugins)
├── corelib/              # biblioteca core
├── interfaces/           # APIs públicas (CLAVE para nuestro plugin)
├── include/
├── 3rd_party/            # libs externas
├── cloud/                # submódulo cloud sync (opcional)
└── testapp/              # tests
```

**Hallazgo clave:** STARC ya tiene plugin system. Los 29 plugins existentes
de `src/core/management_layer/plugins/` son nuestro modelo a seguir para
el plugin del asistente. Ver `~/memoria-asistente-escritura/metodologia/anatomia-starc.md`.

## Qué NO tocar

- **`master`** salvo branding y patches upstreameables.
- **Submódulos** — los manejamos con `git submodule update`, no editamos
  su contenido (a menos que también forkemos ese submódulo, lo cual es
  decisión grande).
- **Estructura de archivos del upstream** — añadir lo nuestro, no
  reorganizar lo existente. Eso garantiza merges limpios.
- **`/Applications/Story Architect.app`** — esa es la app oficial del
  usuario, sigue intacta para uso normal. Aula_122.app convive sin pisarla.

## Dónde vive el código del asistente

Decisión cerrada en Fase 1 (2026-05-24): **opción C — plugin nativo en
el plugin system de STARC.**

- `src/core/management_layer/plugins/writing_assistant/` — el plugin del
  asistente, como uno más de los plugins existentes.
- `writing_assistant_manager.cpp` — `IDocumentManager` (carga + ciclo de vida).
- `writing_assistant_view.cpp/.h` — UI del chat (`QTextEdit` + `QLineEdit` + botón).
- `claude_client.cpp/.h` — cliente que invoca el CLI `claude` por `QProcess`.

Compilado a `Aula_122.app/Contents/PlugIns/libwritingassistantplugin.dylib`.
Activado desde el menú lateral con el botón "Writing assistant" (icono
lápiz, U+F0CB6). MIME interno `app/x-diez50/writing-assistant`.

## Trampas conocidas (lecciones de Fase 0)

Antes de tocar el código, conocer estas:

1. **Typo del upstream:** `Info.plist` tiene `CFBundleDisplyName` (sin la "a"
   de Display). NO lo arregles — el código probablemente lee ese key con
   el typo. Si lo cambias a `CFBundleDisplayName`, el nombre visible se rompe.

2. **Paths hardcodeados en .pro:** 40 archivos `.pro` y `.pri` tienen
   `_build/<bundle>.app/Contents/PlugIns` y `Frameworks` con el nombre
   del bundle hardcoded. Si renombras el bundle, hay que reemplazar
   masivamente. Ver commit `66f4af4a` como ejemplo.

3. **Sub-Makefiles se regeneran al hacer make** — `qmake` top-level solo
   crea el Makefile raíz; los de subdirs se crean al `cd <subdir> &&
   qmake -o Makefile <subdir>.pro` automáticamente. Para forzar regeneración,
   borrar los Makefiles y volver a hacer `make`.

4. **El plugin del asistente NO usa la API de Anthropic** (que cuesta dinero).
   `claude_client.cpp` invoca el CLI `claude` por subproceso
   (`claude --print --output-format json`), reutilizando la suscripción
   Claude Code del usuario. Tres trampas heredadas:
   - **NO pasar `--bare`** → ese flag bloquea OAuth/keychain, dice "Not logged in".
   - **Redirigir stdin a `/dev/null`** (`setStandardInputFile(QProcess::nullDevice())`)
     → sin esto, el CLI espera 3 s con warning "no stdin data received".
   - **Parsear el JSON antes del exit code** — `is_error: true` viene con
     JSON válido, hay que extraer `.result` para mensaje legible.

   Setup del usuario (una vez): `claude auth login --claudeai`. Detalle en
   `~/memoria-asistente-escritura/lecciones/patrones-exitosos.md`.

## Contexto del proyecto

- Usuario: cineasta mexicano. Escribe guion en STARC (dos proyectos
  activos en `~/Documents/starc/projects/*.starc`).
- Visión: que el asistente viva dentro de Diez50, lea el modelo del
  guion en vivo, y proponga cambios con flujo aceptar/descartar.
- Asistente análogo ya existe para edición documental (`~/cinema-assistant/`,
  `~/memoria-asistente-edicion/`, skill `/asistente-de-edicion`).
  El de escritura sigue el mismo patrón estructural.
- Existe una capa transversal `~/memoria-creativa/` compartida entre
  los dos asistentes (proyectos, entidades, patrones).

## Para una sesión nueva sobre el fork

1. **Leer este archivo.**
2. Leer `~/.claude/plans/` el plan vigente del asistente de escritura.
3. Leer `~/memoria-asistente-escritura/metodologia/anatomia-starc.md`
   para el mapeo del código (si la Fase 1 ya avanzó).
4. `git status` y `git branch --show-current` para confirmar que estás
   en `assistant`.
5. Si vas a compilar: verificar que los 3 submódulos `3rd_party` están
   inicializados.
6. Para abrir el binario: `open ~/Developer/starc-fork/src/_build/Aula_122.app`.
