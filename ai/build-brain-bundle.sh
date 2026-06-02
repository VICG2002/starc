#!/usr/bin/env bash
# build-brain-bundle.sh — Fase D: ensambla el cerebro DENTRO de Aula_122.app.
#
# Copia (rsync, idempotente) los tres componentes al layout canónico que espera
# BrainProcessManager:
#   <app>/Contents/Resources/brain/python    <- ai/odysseus/runtime-python  (Fase A)
#   <app>/Contents/Resources/brain/llama     <- ai/llama                    (Fase B)
#   <app>/Contents/Resources/brain/odysseus  <- ai/odysseus (SOLO código)
#
# Idempotente: rsync copia solo diferencias (lento la 1ª vez, ~470 MB).
# Lo invoca qmake con `CONFIG+=bundle_brain`, o se corre a mano:
#   bash ai/build-brain-bundle.sh /ruta/a/Aula_122.app
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP="${1:-$HERE/../src/_build/Aula_122.app}"
[ -d "$APP/Contents" ] || { echo "ERROR: no es un .app válido: $APP"; exit 1; }
[ -x "$HERE/odysseus/runtime-python/bin/python3" ] || { echo "ERROR: falta Fase A (runtime-python)."; exit 1; }
[ -x "$HERE/llama/bin/llama-server" ] || { echo "ERROR: falta Fase B (ai/llama). Corre bundle-llama.sh"; exit 1; }
BRAIN="$APP/Contents/Resources/brain"

echo "==> Ensamblando cerebro en $BRAIN"
mkdir -p "$BRAIN"

echo "  · python   (CPython relocatable + deps, Fase A)"
rsync -a --delete "$HERE/odysseus/runtime-python/" "$BRAIN/python/"

echo "  · llama    (llama-server + dylibs, Fase B)"
rsync -a --delete "$HERE/llama/" "$BRAIN/llama/"

echo "  · odysseus (código vendorizado; excluye runtime-python/data/venv)"
# Exclusiones ANCLADAS a la raíz (/) para NO repetir el bug que se llevó
# services/docs: '/data' excluye sólo <odysseus>/data, no */data.
rsync -a --delete \
  --exclude='/runtime-python' \
  --exclude='/data' \
  --exclude='/venv' --exclude='/.venv' \
  --exclude='__pycache__/' --exclude='*.pyc' \
  --exclude='/.git' --exclude='.DS_Store' --exclude='*.gguf' \
  "$HERE/odysseus/" "$BRAIN/odysseus/"

echo "==> Cerebro ensamblado:"
du -sh "$BRAIN"/python "$BRAIN"/llama "$BRAIN"/odysseus 2>/dev/null
echo "==> .app total:"; du -sh "$APP"
