# Diccionarios hunspell de español (semilla)

Copia vendorizada de los diccionarios **RLA-ES** (es_ES / es_MX), descargados del
mirror oficial de LibreOffice el **2026-06-09**:

- Fuente: `https://raw.githubusercontent.com/LibreOffice/dictionaries/master/es/`
- Upstream original: `https://github.com/sbosio/rla-es` (v2.9 al momento de la descarga)
- Mantienen la ortografía RAE vigente (reforma 2010+: «guion»/«solo» sin tilde, etc.)

## ⚠️ Remapeo de flags (OBLIGATORIO para el hunspell 1.3.2 vendorizado)

RLA-ES v2.9 usa **emojis como flags de afijos** (🥇 💯 📏 … codepoints fuera del
BMP, UTF-8 de 4 bytes). El hunspell 1.3.2 vendorizado (`src/3rd_party/hunspell/`)
representa cada flag en un `unsigned short`: con esos flags la carga del `.dic`
**aborta a la mitad** y deja `tablesize=0` → el primer `add()` en runtime (palabras
de usuario / nombres de personajes) **crashea la app** (SIGBUS en `HashMgr::add_word`,
diagnosticado 2026-06-09 con minidump + bisección).

Los archivos de esta carpeta YA están remapeados con [fix_flags.py](fix_flags.py)
(emoji → carácter PUA U+E000+, consistente en `.aff` y `.dic` — no cambia ninguna
palabra, solo los identificadores internos de los afijos). **Tras re-descargar de
upstream, SIEMPRE correr:**

```bash
python3 fix_flags.py es_ES.aff es_ES.dic es_ES.aff es_ES.dic
python3 fix_flags.py es_MX.aff es_MX.dic es_MX.aff es_MX.dic
```

Verificación rápida (sin crash y carga completa): compilar `/tmp/hunstest` contra
`src/_build/libs/libhunspell.a` o simplemente abrir un proyecto y escribir — si el
corrector marca TODO como error o la app crashea al abrir un editor, el remapeo falta.

## Destino en runtime

La app los lee de `~/Library/Application Support/Diez50/Aula 122/hunspell/`
con estos nombres (el código usa el código de idioma como nombre de archivo):

| Semilla     | Destino     |
|-------------|-------------|
| `es_ES.aff` | `es.aff`    |
| `es_ES.dic` | `es.dic`    |
| `es_MX.aff` | `es-MX.aff` |
| `es_MX.dic` | `es-MX.dic` |

`user_dictionary.dict` (palabras añadidas por el usuario) NUNCA se toca.

## Actualizar

```bash
cd tools/hunspell-es
for f in es_MX.aff es_MX.dic es_ES.aff es_ES.dic; do
  curl -fsSL -o "$f" "https://raw.githubusercontent.com/LibreOffice/dictionaries/master/es/$f"
done
DIR="$HOME/Library/Application Support/Diez50/Aula 122/hunspell"
cp es_ES.aff "$DIR/es.aff"; cp es_ES.dic "$DIR/es.dic"
cp es_MX.aff "$DIR/es-MX.aff"; cp es_MX.dic "$DIR/es-MX.dic"
```

Relanzar Aula 122 para que el SpellChecker recargue el idioma.
