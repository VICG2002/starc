# CLAUDE.md — Fork propio de STARC (rebautizado Aula 122 / Aula_122)

Bienvenido. Este es el fork de [Story Architect](https://github.com/dimkanovikov/starc)
propiedad del usuario, clonado a `~/Developer/starc-fork/`.

**Visión actual (decisión 2026-08-25):** Aula 122 es un **software de escritura
de guion optimizado**, sin IA propia embebida. La IA de todo el trabajo de
Victor es **Claude**, que opera DESDE FUERA a través de `~/memoria-creativa/`.

Esto revierte la etapa 2026-05/2026-06, en la que el fork llevaba dentro un
asistente completo (Odiseo: SPA FastAPI + `QWebEngineView`, cerebro local
llama-server, Hermes vendorizado y dos servidores MCP: ~5,100 archivos y 1.5 GB
en el bundle). Todo eso se eliminó. Lo que quedó:

1. **Core nativo Qt/C++** — el editor de guion de STARC más las
   personalizaciones del fork (ver abajo).
2. **Dos funciones puntuales con Claude**, que invocan el **CLI `claude`
   directo** (sin servidor, sin gateway): "Auto-extraer con Claude" en el
   desglose y "Revisión RAE (contextual)" en el editor.
3. **Puente de datos con la memoria creativa** — `starc-sync`, una herramienta
   externa que vive en `~/memoria-creativa/ingesta/`, NO en la app.

## Lo esencial

- **Stack:** C++ + **Qt 6.11.1** (vía Homebrew) + **qmake** (no CMake).
- **Build:** `cd src && qmake && make -j$(sysctl -n hw.ncpu)`.
- **Binario generado:** `~/Developer/starc-fork/src/_build/Aula_122.app`.
- **Lanzar:** `open ~/Developer/starc-fork/src/_build/Aula_122.app`.
- **Rama de trabajo:** `limpieza/sin-odiseo` (NUNCA tocar `master` salvo branding).
- **Remotes:**
  - `origin` → `github.com/VICG2002/starc` (fork del usuario).
  - `upstream` → `github.com/dimkanovikov/starc` (repo original).

## Identidad del fork

| Campo                | Valor                                                  |
|----------------------|--------------------------------------------------------|
| Display name         | **Aula 122** (con espacio, mayúsculas, para humanos)    |
| Nombre técnico       | **`Aula_122`** (underscore — el espacio rompe builds)   |
| Bundle ID            | **`app.diez50.aula122`** (lowercase intencional)        |
| Organization (macOS) | **Diez50** (marca paraguas)                             |
| Organization domain  | `diez50.local`                                          |
| Ejecutable interno   | `Aula_122` (era `starcapp` en upstream)                 |
| Icon                 | logo Aula 122 en `src/app/icon.icns`                    |

**Coexistencia con la app oficial:** la `/Applications/Story Architect.app`
del usuario (bundle ID `dev.storyapps.starc-beta`) y nuestro `Aula_122.app`
(`app.diez50.aula122`) son apps separadas para macOS — pueden estar abiertas a
la vez sin conflicto. El formato `.starc` es compartido.

## Política de ramas (no negociable)

- `master` queda casi limpio. Solo cambios sincronizados con `upstream/master`
  + branding mínimo si aplica.
- El trabajo del fork (rebrand + personalizaciones) vive en las ramas propias.
- Mergear `master` hacia la rama de trabajo cuando convenga traer mejoras
  upstream.

## Submódulos (importante para compilar)

El README upstream dice "solo `qbreakpad`", pero **eso es insuficiente**.
Para compilar también necesitas:

```bash
git submodule update --init --recursive src/3rd_party/qbreakpad/ src/3rd_party/pdfhummus/ src/3rd_party/pdftextextraction/
```

Los submódulos de `src/core/management_layer/plugins/` (en SSH `git@github.com:`)
son features opcionales — se cargan dinámicamente si están presentes.

## Estructura del repo

```
src/
├── starc.pro             # proyecto qmake raíz
├── app/                  # entry point — main.cpp + application.cpp
├── core/                 # núcleo + management_layer/plugins/
├── corelib/              # biblioteca core
├── interfaces/           # APIs públicas
├── 3rd_party/            # libs externas
└── cloud/                # submódulo cloud sync (opcional)
tools/
└── hunspell-es/          # diccionarios RAE + fix_flags.py (ver abajo)
```

## Lo que el fork añadió y hay que conservar

- **Rebranding completo** a Aula_122 (icono, `Info.plist`, ~40 `.pro`, logo,
  fuentes Fira Code).
- **Tema "aula122"** (negro/cian/rojo del logo) y `DesignSystem::setUiFontFamily()`.
- **Borradores fáciles**: botón "+", Sprint de escritura y Pantalla completa en
  la barra de borradores; comparación de borradores lado a lado.
- **Menús limpios**: la pre-producción (desglose, plan de rodaje) está oculta
  del menú lateral pero los plugins siguen compilados (`setVisible(true)` para
  reactivar, en `menu_view.cpp`).
- **Plugins propios**: `screenplay_breakdown_native` (desglose con tagging de
  recursos y export PDF/CSV) y `production_schedule` (strip board).
- **Story Structure** con 25 estructuras narrativas.
- **Fixes de crashes** del upstream (export, traducir documento, mind map,
  barra de borradores) y reportes corregidos.
- **Desbloqueo de features de pago** en `plugins_builder.cpp`.
- **Diccionarios RAE** en `tools/hunspell-es/` — ver la trampa 5 abajo.

## La IA: qué quedó y qué se fue

**Se eliminó** (commits de la rama `limpieza/sin-odiseo`, 2026-08-25): todo
`ai/`, `brain_process_manager`, `odysseus_workspace_view`, el botón "Odiseo"
del menú, el panel anfitrión de `application_view` y el puente
`aula122.bridge`. El historial de git lo conserva si hiciera falta consultarlo.

**Se conservó de upstream** (NO tocar): `src/corelib/ui/modules/ai_assistant/`
y las señales `rephraseRequested`/`expandRequested`/`generateX` de los 8
editores. Es la IA de pago por créditos de Story Architect; funciona con la
nube de ellos y es ajena a nuestro trabajo.

**Las dos funciones con Claude** invocan el CLI por `QProcess`:
- `screenplay_breakdown_native_view.cpp` — "Auto-extraer con Claude".
- `screenplay_text/text/screenplay_text_edit.cpp` — "Revisión RAE (contextual)".

Ambas usan `locateClaudeCli()` (busca en `~/.local/bin`, `/opt/homebrew/bin`,
`/usr/local/bin` y `which`). Setup del usuario, una vez:
`claude auth login --claudeai`.

## La conexión con la memoria creativa

El puente NO vive en la app: es `~/memoria-creativa/ingesta/starc-sync.py`,
una herramienta Python de stdlib puro que lee el `.starc` (SQLite, `mode=ro`) y
escribe fichas `.md` en la bóveda.

```bash
python3 ~/memoria-creativa/ingesta/starc-sync.py            # dry-run
python3 ~/memoria-creativa/ingesta/starc-sync.py --apply    # escribe (Aula 122 CERRADA)
```

- Espeja **entidades** (personajes, lugares, mundos, ficha de proyecto) en
  ambas direcciones, con merge aditivo: nunca pisa texto humano; los choques
  van a `_cambios/pendientes/` para que Victor decida.
- Espeja el **guion** (documento 10104) como `<Prefijo>-Guion.md`, solo de ida.
- Escribe dentro del `.starc` **solo con el proyecto cerrado** (comprueba el
  `.lock` y `lsof`), siempre con respaldo previo del archivo completo.
- Estado y config: `~/Library/Application Support/Diez50/Aula 122/sync/`.
- Pruebas: `python3 ~/memoria-creativa/ingesta/tests/test_starc_sync.py`.

## Trampas conocidas

1. **Typo del upstream:** `Info.plist` tiene `CFBundleDisplyName` (sin la "a"
   de Display). NO lo arregles — el código lee ese key con el typo.

2. **Paths hardcodeados en .pro:** ~40 archivos `.pro`/`.pri` tienen
   `_build/Aula_122.app/Contents/PlugIns` y `Frameworks` con el nombre del
   bundle hardcoded. Si renombras el bundle hay que reemplazar masivamente.

3. **Sub-Makefiles se regeneran al hacer make** — `qmake` top-level solo crea
   el Makefile raíz. Para forzar regeneración, borra los Makefiles y vuelve a
   `make`.

4. **CLI de claude, no la API de pago.** El CLI reutiliza la suscripción del
   usuario (costo 0). Trampas heredadas y verificadas:
   - **NO pasar `--bare`** → ese flag bloquea OAuth/keychain ("Not logged in").
   - **Redirigir stdin a `/dev/null`** (`setStandardInputFile(nullDevice())`)
     → sin esto el CLI espera 3 s con warning "no stdin data received".
   - **`--output-format text`**: stdout ES el resultado.

5. **`tools/hunspell-es/` arregla un SIGBUS real.** Los diccionarios RLA-ES
   v2.9 usan emojis como flags de afijos; el hunspell 1.3.2 vendorizado los
   representa en un `unsigned short` y la carga del `.dic` aborta, dejando
   `tablesize=0` → el primer `add()` en runtime crashea la app. Los archivos
   del repo YA están remapeados con `fix_flags.py`. Tras re-descargar de
   upstream hay que volver a correrlo (ver el README de esa carpeta).

## Para una sesión nueva sobre el fork

1. **Leer este archivo.**
2. `git status` y `git branch --show-current`.
3. Si vas a compilar: verificar que los 3 submódulos `3rd_party` están
   inicializados.
4. Para abrir el binario: `open ~/Developer/starc-fork/src/_build/Aula_122.app`.
