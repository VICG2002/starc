#!/usr/bin/env bash
# fetch-model.sh — Asegura el modelo GGUF del cerebro (Fase B del empaquetado).
# Lo invoca BrainProcessManager (Fase C) en el PRIMER arranque, antes de lanzar
# llama-server. El modelo NO viaja dentro de Aula_122.app (~5 GB): se materializa
# aquí, en datos mutables de usuario (fuera del .app read-only/firmado).
#
#   Destino:  ~/Library/Application Support/Diez50/Aula 122/brain/models/<tag>.gguf
#   Fuentes:  (1) blob local de Ollama  → instantáneo, ideal en dev (la máquina de Victor)
#             (2) Hugging Face          → vía universal para el usuario final
#
# Uso:
#   bash fetch-model.sh                          # default qwen2.5:7b, desde Ollama si existe
#   bash fetch-model.sh --ollama qwen2.5:14b     # otra etiqueta de Ollama
#   bash fetch-model.sh --hf <repo> <archivo.gguf>
#   AULA122_MODEL_COPY=1 bash fetch-model.sh     # COPIAR el blob (autocontenido real) en vez de symlink
set -euo pipefail

DEST_DIR="$HOME/Library/Application Support/Diez50/Aula 122/brain/models"
OLLAMA_TAG="qwen2.5:7b"; HF_REPO=""; HF_FILE=""; MODE="ollama"

while [ $# -gt 0 ]; do
  case "$1" in
    --ollama) OLLAMA_TAG="$2"; MODE="ollama"; shift 2 ;;
    --hf)     HF_REPO="$2"; HF_FILE="$3"; MODE="hf"; shift 3 ;;
    *) echo "arg desconocido: $1"; exit 2 ;;
  esac
done
mkdir -p "$DEST_DIR"

if [ "$MODE" = "ollama" ]; then
  BLOB="$(ollama show "$OLLAMA_TAG" --modelfile 2>/dev/null | awk '/^FROM /{print $2; exit}')" || true
  [ -n "${BLOB:-}" ] && [ -e "$BLOB" ] || { echo "ERROR: Ollama no tiene '$OLLAMA_TAG' (prueba: ollama pull $OLLAMA_TAG)"; exit 1; }
  NAME="$(printf '%s' "$OLLAMA_TAG" | tr ':/' '__').gguf"
  TARGET="$DEST_DIR/$NAME"
  if [ -e "$TARGET" ]; then echo "✓ modelo ya presente: $TARGET"; exit 0; fi
  if [ "${AULA122_MODEL_COPY:-0}" = "1" ]; then
    echo "Copiando $OLLAMA_TAG ($(du -h "$BLOB" | cut -f1)) -> $TARGET …"; cp "$BLOB" "$TARGET"
  else
    echo "Enlazando $OLLAMA_TAG -> $TARGET (symlink; AULA122_MODEL_COPY=1 para copiar)"; ln -sf "$BLOB" "$TARGET"
  fi
  echo "✓ $TARGET"
else
  command -v huggingface-cli >/dev/null || { echo "ERROR: falta huggingface-cli (pip install huggingface_hub) o usa --ollama"; exit 1; }
  [ -n "$HF_REPO" ] && [ -n "$HF_FILE" ] || { echo "uso: --hf <repo> <archivo.gguf>"; exit 2; }
  TARGET="$DEST_DIR/$HF_FILE"
  [ -e "$TARGET" ] && { echo "✓ ya presente: $TARGET"; exit 0; }
  echo "Descargando $HF_REPO / $HF_FILE -> $DEST_DIR …"
  huggingface-cli download "$HF_REPO" "$HF_FILE" --local-dir "$DEST_DIR"
fi
