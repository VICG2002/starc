# CLAUDE.md — Fork propio de STARC

Bienvenido. Este es el fork de [Story Architect](https://github.com/dimkanovikov/starc)
propiedad del usuario, clonado a `~/Developer/starc-fork/` y trabajado
en la rama `assistant`. Su propósito es **embeber un asistente de
escritura de guion nativo dentro de STARC** (panel/dock + comunicación
con Claude).

Esta es la versión corta para sesiones rápidas. **Para el plan completo
y el contexto narrativo lee primero `~/.claude/plans/` (el plan vigente)
y `~/memoria-asistente-escritura/metodologia/pasos-a-seguir.md`.**

## Lo esencial

- **Stack:** C++ + **Qt 6.11.1** (vía Homebrew) + **qmake** (no CMake).
- **Build:** `cd src && qmake && make -j$(sysctl -n hw.ncpu)`.
- **Rama de trabajo:** `assistant` (NUNCA tocar `master` salvo branding).
- **Remotes:**
  - `origin` → `github.com/VICG2002/starc` (fork del usuario).
  - `upstream` → `github.com/dimkanovikov/starc` (repo original).

## Política de ramas (no negociable)

- `master` queda casi limpio. Solo branding y patches que enviemos como
  PR al upstream. Sincronizable con `upstream/master` sin merge hell.
- `assistant` es donde vive todo el código del asistente nativo (dock,
  comunicación con Claude, tools narrativas).
- Mergear `master` ← `upstream/master` periódicamente; `assistant` ←
  `master` cuando convenga traer mejoras upstream.

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
├── app/                  # entry point, main.cpp probable
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
  usuario, sigue intacta para uso normal. Nuestro binario compilado
  convive sin pisarla.

## Dónde vivirá el código del asistente (cuando exista)

Aún no creado. Plan tentativo:

- `src/core/management_layer/plugins/assistant/` — el plugin del
  asistente, como uno más de los 29 existentes.
- O alternativamente `src/assistant/` si decidimos no usar el plugin
  system y embedir directo en el core.

**La decisión se toma al cerrar Fase 1** del plan (ver `~/.claude/plans/`).

## Contexto del proyecto

- Usuario: cineasta mexicano. Escribe guion en STARC (dos proyectos
  activos en `~/Documents/starc/projects/*.starc`).
- Visión: que el asistente viva dentro de STARC, lea el modelo del
  guion en vivo, y proponga cambios con flujo aceptar/descartar.
- Asistente análogo ya existe para edición documental (`~/cinema-assistant/`,
  `~/memoria-asistente-edicion/`, skill `/asistente-de-edicion`).
  El de escritura sigue el mismo patrón estructural.

## Para una sesión nueva sobre el fork

1. **Leer este archivo.**
2. Leer `~/.claude/plans/` el plan vigente del asistente de escritura.
3. Leer `~/memoria-asistente-escritura/metodologia/anatomia-starc.md`
   para el mapeo del código (si la Fase 1 ya avanzó).
4. `git status` y `git branch --show-current` para confirmar que estás
   en `assistant`.
5. Si vas a compilar: verificar que los 3 submódulos `3rd_party` están
   inicializados.
