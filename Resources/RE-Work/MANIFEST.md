# MANIFEST

CKRE only. Target: `CreationKit.exe` 1.11.137.0.

Standing rule: **never push a PDB or anything that can be copyrighted.**

## Tracked

| Path | What |
|------|------|
| `docs/` | numbered CK notes (00–21) plus original inventory |
| `dumps/` | fact tables: RTTI names, TES* classes, settings keys, `__FILE__` paths, LOD strings |
| `scripts/` | owner Ghidra Java query scripts |
| `tools/` | `pe_recon.py`, `validate_ckini.py`, synthetic INI samples |

## Not tracked

| Local CKRE path | Why |
|-----------------|-----|
| `ghidra-dump/*.log`, `FUN_*.txt`, `WINMAIN-*`, `*.asm` | decompiled CK bodies |
| `ghidra-dump/scene-samples/*.UVD` | game previs extracted from `Fallout4 - MeshesExtra.ba2` |
| `ghidra-dump/elric-pdb-symbols.txt` | PDB dump |
| Ghidra project databases | embed decompiled code |
| `CreationKit.exe` | copyrighted binary |
