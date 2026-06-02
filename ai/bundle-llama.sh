#!/usr/bin/env bash
# bundle-llama.sh — Empaqueta llama-server + sus dylibs como bundle RELOCATABLE,
# para que el runtime de modelo viaje DENTRO de Aula_122.app (Fase B del cerebro).
#
#   Entrada:  llama.cpp instalado por Homebrew (/opt/homebrew).
#   Salida:   ai/llama/{bin,lib}  con install_names @rpath/@loader_path + firma ad-hoc.
#
# El contenido de ai/llama/ es un ARTEFACTO (gitignored); ESTE script es la fuente
# de verdad reproducible (lo invoca la Fase D del build, ai/PACKAGING.md).
# Uso:  bash ai/bundle-llama.sh
set -euo pipefail

LLAMA_PREFIX="$(brew --prefix llama.cpp)"
GGML_LIB="$(brew --prefix ggml)/lib"
SRC_BIN="$LLAMA_PREFIX/bin/llama-server"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$HERE/llama"; BIN_DIR="$OUT/bin"; LIB_DIR="$OUT/lib"

[ -x "$SRC_BIN" ] || { echo "ERROR: falta $SRC_BIN (¿brew install llama.cpp?)"; exit 1; }
echo "==> llama.cpp=$LLAMA_PREFIX"
echo "==> ggml=$GGML_LIB"
rm -rf "$OUT"; mkdir -p "$BIN_DIR" "$LIB_DIR"

# Directorios donde se resuelven los @rpath/* del binario y las libs.
RPATHS=("$LLAMA_PREFIX/lib" "$GGML_LIB")

# resolve <dep> <odir> -> path real en disco (falla si es del sistema / no se halla)
resolve() {
  local d="$1" od="$2" b
  case "$d" in
    @rpath/*) b="${d#@rpath/}"
      for rp in "${RPATHS[@]}"; do [ -e "$rp/$b" ] && { printf '%s' "$rp/$b"; return 0; }; done
      return 1 ;;
    @loader_path/*)     b="${d#@loader_path/}";     [ -e "$od/$b" ] && { printf '%s' "$od/$b"; return 0; }; return 1 ;;
    @executable_path/*) b="${d#@executable_path/}"; [ -e "$od/$b" ] && { printf '%s' "$od/$b"; return 0; }; return 1 ;;
    /opt/homebrew/*)    [ -e "$d" ] && { printf '%s' "$d"; return 0; }; return 1 ;;
    *) return 1 ;;   # /usr/lib, /System/* -> dylib del sistema, no se empaqueta
  esac
}

# Copia recursiva (BFS) de todas las deps no-sistema a LIB_DIR.
# Set de "ya copiados" basado en string (bash 3.2 de macOS no tiene `declare -A`).
COPIED=" "
copy_deps() {  # <mach-o> <odir-para-@loader_path>
  local f="$1" od="$2" dep real base
  while IFS= read -r dep; do
    [ -n "$dep" ] || continue
    real="$(resolve "$dep" "$od")" || continue
    base="$(basename "$dep")"
    case "$COPIED" in *" $base "*) continue ;; esac
    COPIED="$COPIED$base "
    cp -L "$real" "$LIB_DIR/$base"; chmod u+w "$LIB_DIR/$base"
    copy_deps "$LIB_DIR/$base" "$(dirname "$real")"
  done < <(otool -L "$f" | tail -n +2 | awk '{print $1}')
}

echo "==> Copiando llama-server + deps (recursivo)"
cp "$SRC_BIN" "$BIN_DIR/llama-server"; chmod u+w "$BIN_DIR/llama-server"
copy_deps "$BIN_DIR/llama-server" "$(dirname "$SRC_BIN")"
echo "==> Dylibs empaquetados:"; ls -1 "$LIB_DIR"

# Reescribe install_names: toda referencia no-sistema -> @rpath/<base>.
fix() {  # <archivo>
  local f="$1" dep base
  case "$f" in *.dylib) install_name_tool -id "@rpath/$(basename "$f")" "$f" ;; esac
  while IFS= read -r dep; do
    [ -n "$dep" ] || continue
    case "$dep" in
      @rpath/*|/opt/homebrew/*) base="$(basename "$dep")"
        install_name_tool -change "$dep" "@rpath/$base" "$f" 2>/dev/null || true ;;
    esac
  done < <(otool -L "$f" | tail -n +2 | awk '{print $1}')
}

echo "==> Arreglando install_names -> @rpath"
for d in "$LIB_DIR"/*.dylib; do
  fix "$d"
  install_name_tool -add_rpath "@loader_path" "$d" 2>/dev/null || true   # @rpath -> su propio dir
done
fix "$BIN_DIR/llama-server"
install_name_tool -add_rpath "@loader_path/../lib" "$BIN_DIR/llama-server" 2>/dev/null || true

echo "==> Re-firma ad-hoc (install_name_tool invalida la firma)"
for d in "$LIB_DIR"/*.dylib; do codesign -f -s - "$d" 2>/dev/null; done
codesign -f -s - "$BIN_DIR/llama-server" 2>/dev/null

echo "==> Verificación: fugas a /opt/homebrew (debe ser 0)"
LEAK=$( { otool -L "$BIN_DIR/llama-server"; for d in "$LIB_DIR"/*.dylib; do otool -L "$d"; done; } | grep -c "/opt/homebrew" || true )
echo "    fugas a Homebrew: $LEAK"
echo "==> llama-server --version (desde el bundle):"
"$BIN_DIR/llama-server" --version 2>&1 | head -4 || true
du -sh "$OUT"
[ "$LEAK" -eq 0 ] && echo "✅ Bundle relocatable OK" || { echo "❌ Quedan rutas a Homebrew"; exit 1; }
