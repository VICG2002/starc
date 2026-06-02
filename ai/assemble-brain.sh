#!/usr/bin/env bash
# assemble-brain.sh — Ensambla el layout canónico del cerebro que espera
# BrainProcessManager:  <brain>/{python,llama,odysseus}
# a partir de los artefactos ya construidos en ai/.
#
# En DESARROLLO crea symlinks (instantáneo, 0 copia). La Fase D hará el
# ensamblado real (copia) dentro de Aula_122.app/Contents/Resources/brain.
#
# Para correr la app en dev apuntándola a este brain:
#   export AULA122_BRAIN_DIR="$(cd ai/brain && pwd)"
#   open src/_build/Aula_122.app      # o lanzarla desde la terminal con esa env
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BRAIN="$HERE/brain"

[ -x "$HERE/odysseus/runtime-python/bin/python3" ] || { echo "ERROR: falta Fase A (runtime-python). Corre el build de Fase A."; exit 1; }
[ -x "$HERE/llama/bin/llama-server" ]              || { echo "ERROR: falta Fase B (ai/llama). Corre: bash ai/bundle-llama.sh"; exit 1; }
[ -f "$HERE/odysseus/app.py" ]                     || { echo "ERROR: falta odysseus vendorizado (ai/odysseus)."; exit 1; }

rm -rf "$BRAIN"; mkdir -p "$BRAIN"
ln -s "$HERE/odysseus/runtime-python" "$BRAIN/python"    # Fase A — CPython relocatable + deps
ln -s "$HERE/llama"                   "$BRAIN/llama"      # Fase B — llama-server + dylibs
ln -s "$HERE/odysseus"                "$BRAIN/odysseus"    # código de odysseus vendorizado
ln -s "$HERE/aula122-mcp"             "$BRAIN/aula122-mcp" # servidor MCP del proyecto (.starc)

echo "✓ brain ensamblado en $BRAIN"
ls -la "$BRAIN"
echo
echo "Para la app en dev:  export AULA122_BRAIN_DIR=\"$BRAIN\""
