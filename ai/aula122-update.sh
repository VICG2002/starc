#!/usr/bin/env bash
# aula122-update.sh — utilidad GUARDADA de mantenimiento/redeploy de Aula 122.
#
# Pensada para que Hermes (semi-autónomo, CON CONFIRMACIÓN) la invoque por terminal,
# o para uso manual. Recompila y redespliega los cambios del propio software (Odiseo +
# el cerebro/Hermes) que ya están en el árbol. Los pasos destructivos exigen --confirm
# y hacen backup antes.
#
# Subcomandos:
#   status              SEGURO (solo lectura): rama/estado git, qué hay sin redesplegar,
#                       versiones vendorizadas, plugins de Hermes.
#   backup              SEGURO: snapshot reversible del estado NO regenerable del cerebro
#                       (config, ajustes, índice de memoria, kanban/sesiones) a
#                       "<DATA>/backups/<timestamp>/".
#   rebuild [--confirm] DESTRUCTIVO: make (C++) + sync de Odiseo al bundle + RELANZA la app.
#                       Sin --confirm: DRY-RUN (solo dice qué haría). Con --confirm: backup
#                       automático y luego aplica.
#
# GUARDRAILS (por diseño, no por instrucción a un modelo):
#   - NUNCA hace `git push`, ni commitea, ni re-vendoriza Hermes/odysseus desde upstream.
#   - Opera SOLO dentro del árbol de Aula 122 y los datos de la app.
#   - rebuild respalda antes de tocar nada y exige --confirm explícito.
set -euo pipefail

# PATH defensivo: cuando lo invoca Hermes (vía MCP), el entorno llega filtrado y
# podría no traer make/git/rsync/qmake. Aseguramos las rutas habituales.
export PATH="/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin:${PATH:-}"

REPO="${AULA122_REPO:-$HOME/Developer/starc-fork}"
APP="$REPO/src/_build/Aula_122.app"
APP_BIN="$APP/Contents/MacOS/Aula_122"
BRAIN_BUNDLE="$APP/Contents/Resources/brain"
DATA="$HOME/Library/Application Support/Diez50/Aula 122"
BACKUPS="$DATA/backups"

c_red=$'\033[31m'; c_grn=$'\033[32m'; c_yel=$'\033[33m'; c_dim=$'\033[2m'; c_off=$'\033[0m'
say()  { printf '%s\n' "$*"; }
ok()   { printf '%s%s%s\n' "$c_grn" "$*" "$c_off"; }
warn() { printf '%s%s%s\n' "$c_yel" "$*" "$c_off"; }
err()  { printf '%s%s%s\n' "$c_red" "$*" "$c_off" >&2; }

_guard_paths() {
  [ -d "$REPO/.git" ] || { err "No es el árbol de Aula 122: $REPO"; exit 2; }
  [ -d "$APP/Contents" ] || { err "No encuentro el .app: $APP"; exit 2; }
}

cmd_status() {
  _guard_paths
  say "== Aula 122 — status =="
  say "repo: $REPO"
  ( cd "$REPO"
    say "rama: $(git branch --show-current 2>/dev/null || echo '?')"
    local n; n=$(git status --short | grep -vcE '^\?\? src/3rd_party' || true)
    say "cambios sin commitear (sin submódulos): $n"
    say "último commit: $(git log -1 --oneline 2>/dev/null || echo '?')"
  )
  # ¿Hay código más nuevo que lo desplegado? Se compara contra los artefactos que el
  # rebuild ACTUALIZA: el dylib del núcleo (lo produce make) para C++, y el app.py del
  # bundle (lo produce el sync) para Odiseo. (No contra el ejecutable principal, que no
  # se relinka en un build incremental → daría un conteo falso de "todo es más nuevo".)
  local ref_cpp="$APP/Contents/PlugIns/libcoreplugin.dylib"
  local ref_py="$BRAIN_BUNDLE/odysseus/app.py"
  local stale=0 hit
  if [ -f "$ref_cpp" ]; then
    hit=$(find "$REPO/src/core" "$REPO/src/corelib" -type f \( -name '*.cpp' -o -name '*.h' \) -newer "$ref_cpp" 2>/dev/null | head -1 || true)
    [ -n "$hit" ] && { stale=1; warn "Código C++ más nuevo que el último build (p.ej. $(basename "$hit"))."; }
  fi
  if [ -f "$ref_py" ]; then
    hit=$(find "$REPO/ai/odysseus/routes" "$REPO/ai/odysseus/static" "$REPO/ai/odysseus/app.py" -type f -newer "$ref_py" 2>/dev/null | grep -v '__pycache__' | head -1 || true)
    [ -n "$hit" ] && { stale=1; warn "Código de Odiseo más nuevo que el bundle (p.ej. $(basename "$hit"))."; }
  fi
  [ "$stale" -eq 0 ] && ok "Lo desplegado está al día con el código del árbol." || warn "→ conviene 'rebuild'."
  # Versiones vendorizadas (informativo).
  local hv; hv=$(grep -m1 -E '^version' "$REPO/ai/hermes/pyproject.toml" 2>/dev/null | tr -d ' "' || true)
  say "Hermes vendorizado: ${hv:-?}"
  # Plugins de Hermes (incluye el pin propio).
  if [ -d "$DATA/brain/hermes-runtime/home/plugins" ]; then
    say "plugins de Hermes (usuario): $(ls "$DATA/brain/hermes-runtime/home/plugins" 2>/dev/null | tr '\n' ' ')"
  fi
  say "${c_dim}(status es solo lectura; no tocó nada.)${c_off}"
}

cmd_backup() {
  _guard_paths
  local ts dst
  ts=$(date +%Y%m%d-%H%M%S)
  dst="$BACKUPS/$ts"
  mkdir -p "$dst"
  say "== backup → $dst =="
  # Solo lo NO regenerable y de tamaño razonable. (El .app, el venv, los modelos y
  # fastembed_cache se regeneran; no se respaldan.)
  local hh="$DATA/brain/hermes-runtime/home"
  local od="$DATA/brain/odysseus-runtime/data"
  [ -f "$hh/config.yaml" ] && { mkdir -p "$dst/hermes-home"; rsync -a --exclude='logs/' --exclude='__pycache__/' "$hh/" "$dst/hermes-home/" 2>/dev/null && ok "  · hermes-home (config, plugins, kanban, sesiones)"; }
  if [ -d "$od" ]; then
    mkdir -p "$dst/odysseus-data"
    # app.db (sesiones/usuarios), settings, prefs, presets, memory + el índice de la bóveda.
    rsync -a \
      --include='app.db' --include='settings.json' --include='user_prefs.json' \
      --include='presets.json' --include='memory.json' --include='auth.json' \
      --include='chroma/***' \
      --exclude='*' \
      "$od/" "$dst/odysseus-data/" 2>/dev/null && ok "  · odysseus-data (db, ajustes, índice de memoria)"
  fi
  # Apunta el commit fuente para reproducir el código exacto.
  ( cd "$REPO" && git rev-parse HEAD 2>/dev/null > "$dst/SOURCE_COMMIT.txt" ) || true
  ok "Backup completo. Para restaurar: copia de vuelta estas carpetas con la app cerrada."
  say "$dst"
}

cmd_rebuild() {
  _guard_paths
  local confirm="no" token=""
  while [ $# -gt 0 ]; do
    case "$1" in
      --confirm) confirm="yes"; shift ;;
      --token)   token="${2:-}"; shift 2 ;;
      *)         shift ;;
    esac
  done

  say "== rebuild (redeploy de cambios del árbol) =="
  say "Plan:"
  say "  1) backup del estado no regenerable del cerebro"
  say "  2) make -C $REPO/src/core   (recompila el núcleo C++)"
  say "  3) rsync de ai/odysseus + servidores MCP → bundle"
  say "  4) relanzar Aula_122.app"
  say "GUARDRAILS: sin git push, sin re-vendorizado upstream."

  if [ "$confirm" != "yes" ]; then
    warn ""
    warn "DRY-RUN: no se tocó nada. Para aplicar de verdad:"
    warn "    bash $0 rebuild --confirm"
    return 0
  fi

  # ── GATE DE CONFIRMACIÓN DURA (out-of-band) ──────────────────────────────
  # Aplicar exige un token de un solo uso que NO se imprime: se escribe a un
  # archivo que el HUMANO debe abrir y dictar. Así un agente no puede aplicar
  # solo por decidir pasar --confirm; necesita un dato que solo el humano ve.
  # (Honesto: un agente con terminal PODRÍA leer el archivo; esto frena errores
  # del modelo, no a un agente decidido — para eso, UI nativa o quitarle terminal.)
  local tokfile="$DATA/update-confirm-token.txt"
  if [ -z "$token" ]; then
    local newtok; newtok=$(openssl rand -hex 4 | tr '[:lower:]' '[:upper:]')
    printf '%s\n' "$newtok" > "$tokfile"; chmod 600 "$tokfile"
    err "CONFIRMACIÓN REQUERIDA — gate duro."
    say "Token de un solo uso escrito en (NO se imprime aquí a propósito):"
    say "  $tokfile"
    say "El HUMANO debe abrir ese archivo y dictar el token; luego reintenta:"
    say "  rebuild --confirm --token <TOKEN>"
    say "Caduca en 10 min. NO leas el archivo tú: el token debe venir del humano."
    return 10
  fi
  if [ ! -f "$tokfile" ]; then
    err "No hay confirmación pendiente. Reintenta SIN --token para generar una."
    return 11
  fi
  local stored age
  stored=$(tr -d '[:space:]' < "$tokfile" 2>/dev/null)
  age=$(( $(date +%s) - $(stat -f %m "$tokfile" 2>/dev/null || echo 0) ))
  if [ "$token" != "$stored" ]; then
    err "Token inválido. Pídele al humano el token vigente del archivo."
    return 12
  fi
  if [ "$age" -gt 600 ]; then
    rm -f "$tokfile"
    err "Token expirado (>10 min). Reintenta sin --token para generar uno nuevo."
    return 13
  fi
  rm -f "$tokfile"  # un solo uso

  say ""; say "Token válido. Aplicando:"
  cmd_backup
  say "→ make (C++ core)…"
  ( cd "$REPO/src/core" && make -j"$(sysctl -n hw.ncpu)" >/tmp/aula122-make.log 2>&1 ) \
    || { err "make falló. Revisa /tmp/aula122-make.log. NO se relanzó la app."; exit 3; }
  ok "  · núcleo recompilado"
  say "→ sync de Odiseo + servidores MCP al bundle…"
  rsync -a --delete \
    --exclude='/runtime-python' --exclude='/data' --exclude='/venv' --exclude='/.venv' \
    --exclude='__pycache__/' --exclude='*.pyc' --exclude='/.git' --exclude='.DS_Store' --exclude='*.gguf' \
    "$REPO/ai/odysseus/" "$BRAIN_BUNDLE/odysseus/" \
    && ok "  · Odiseo (rutas/SPA) sincronizado"
  # Los servidores MCP (las "manos/ojos" del cerebro) también viven en el bundle.
  for mcp in aula122-mcp memoria-mcp; do
    if [ -d "$REPO/ai/$mcp" ]; then
      rsync -a --delete --exclude='__pycache__/' --exclude='*.pyc' \
        "$REPO/ai/$mcp/" "$BRAIN_BUNDLE/$mcp/" && ok "  · $mcp sincronizado"
    fi
  done
  say "→ programando reinicio DESACOPLADO…"
  # El reinicio (quit+open) va en un proceso detached: al salir este script queda
  # reparentado a launchd, así que matar la app —y con ella Hermes y este propio
  # script, si lo invocó Hermes— NO interrumpe el relanzamiento. El sleep da tiempo a
  # que el tool/respuesta de Hermes terminen antes de tumbar la app.
  nohup bash -c "sleep 8; osascript -e 'tell application \"Aula_122\" to quit' >/dev/null 2>&1; for _ in \$(seq 1 15); do pgrep -f 'MacOS/Aula_122' >/dev/null 2>&1 && sleep 1 || break; done; open '$APP'" >/dev/null 2>&1 &
  disown 2>/dev/null || true
  ok "Redeploy aplicado (núcleo + Odiseo). La app se reiniciará sola en unos segundos."
}

main() {
  local cmd="${1:-status}"; shift || true
  case "$cmd" in
    status)  cmd_status "$@" ;;
    backup)  cmd_backup "$@" ;;
    rebuild) cmd_rebuild "$@" ;;
    *) err "Uso: $0 {status|backup|rebuild [--confirm]}"; exit 64 ;;
  esac
}
main "$@"
