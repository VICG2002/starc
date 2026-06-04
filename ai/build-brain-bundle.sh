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

echo "  · aula122-mcp (servidor MCP del proyecto, lee el .starc)"
rsync -a --delete --exclude='__pycache__/' --exclude='*.pyc' "$HERE/aula122-mcp/" "$BRAIN/aula122-mcp/"

echo "  · seed/ (conocimiento de Rita, Fase 2: índice RAG embebido + modelo de embeddings)"
# El index RAG (ChromaDB embebido) y el modelo de embeddings local NO son parte del
# código (viven en data/, excluido arriba). Viajan en seed/ y BrainProcessManager los
# siembra en la data mutable al primer arranque (copia-si-falta). Así el .app conoce a
# Rita sin descargar nada ni depender de un servidor chroma externo.
SEED="$BRAIN/seed"
mkdir -p "$SEED"
if [ -f "$HERE/odysseus/data/chroma/chroma.sqlite3" ]; then
  rsync -a --delete "$HERE/odysseus/data/chroma/" "$SEED/chroma/"
else
  echo "    ⚠ falta ai/odysseus/data/chroma/chroma.sqlite3 — corre la ingesta de Rita antes (RAG quedará vacío)."
fi
if [ -d "$HERE/odysseus/data/fastembed_cache" ]; then
  rsync -a --delete "$HERE/odysseus/data/fastembed_cache/" "$SEED/fastembed_cache/"
else
  echo "    ⚠ falta ai/odysseus/data/fastembed_cache — el retrieval intentaría descargar el modelo."
fi

echo "==> Cerebro ensamblado:"
du -sh "$BRAIN"/python "$BRAIN"/llama "$BRAIN"/odysseus 2>/dev/null
echo "==> .app total:"; du -sh "$APP"
