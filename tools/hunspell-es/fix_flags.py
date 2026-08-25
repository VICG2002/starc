#!/usr/bin/env python3
"""Remapea flags fuera del BMP (emojis de RLA-ES v2.9) a caracteres PUA (U+E000+)
para compatibilidad con hunspell 1.3.2 (FLAG UTF-8 = un unsigned short por flag)."""
import sys

aff_path, dic_path, out_aff, out_dic = sys.argv[1:5]
aff = open(aff_path, encoding="utf-8").read()
dic = open(dic_path, encoding="utf-8").read()

# flags astrales = cualquier codepoint > U+FFFF presente en cualquiera de los dos
astral = sorted({ch for ch in aff + dic if ord(ch) > 0xFFFF})
print("flags astrales detectados:", " ".join(f"{ch} (U+{ord(ch):X})" for ch in astral))

# elegir PUA libres (que no aparezcan ya en ninguno de los archivos)
used = set(aff) | set(dic)
mapping = {}
candidate = 0xE000
for ch in astral:
    while chr(candidate) in used:
        candidate += 1
    mapping[ch] = chr(candidate)
    candidate += 1

# sanity: los astrales solo deben aparecer como FLAGS, nunca dentro de palabras
for i, line in enumerate(dic.splitlines()[1:], start=2):
    word = line.split("/", 1)[0]
    if any(ord(c) > 0xFFFF for c in word):
        print(f"OJO: palabra con char astral en linea {i}: {line!r}")

for old, new in mapping.items():
    aff = aff.replace(old, new)
    dic = dic.replace(old, new)

open(out_aff, "w", encoding="utf-8").write(aff)
open(out_dic, "w", encoding="utf-8").write(dic)
print(f"escritos {out_aff} y {out_dic}; {len(mapping)} flags remapeados")
