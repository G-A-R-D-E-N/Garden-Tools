# MANIFEST

Creation Kit (`CreationKit.exe` 1.11.137.0) and runtime (`Fallout4.exe` OG 1.10.163 / AE 1.11.240) notes.

Never push game binaries, debug files, or decompiled function bodies.

## Tracked

| Path | What |
|------|------|
| `docs/` | numbered CK notes (00–22) plus original inventory |
| `dumps/` | CK fact tables (RTTI names, TES* classes, settings keys, `__FILE__` paths, LOD strings) plus the NIF/TextExport/string-file analysis docs |
| `scripts/` | CK Ghidra Java query scripts |
| `tools/` | `pe_recon.py`, `validate_ckini.py`, `extract_ck_nif_savebinary.py`, `validate_nif.py`, synthetic INI + CompileTextExport fixtures |
| `STATUS.md` | completion state of every doc and tool, next open questions |
| `Runtime/` | runtime pipeline docs, findings, Ghidra scripts, Organized analysis CSVs, subsystem notes |

## Not tracked

| Local path | Why |
|------------|-----|
| CK `ghidra-dump/*.log`, `FUN_*.txt`, `WINMAIN-*`, `*.asm` | decompiled CK bodies |
| CK `ghidra-dump/scene-samples/*.UVD` | game previs from `Fallout4 - MeshesExtra.ba2` |
| `elric-symbols.txt` | local symbol dump |
| `ReverseEngineering/Ghidra/`, `Projects/fwgh_proj_*` | Ghidra DBs embed decompiled game code |
| `ReverseEngineering/Decompiles/` | decompiled function bodies |
| `ReverseEngineering/Symbols/`, `EXE/`, `Bins/` | game binaries and dumps |
| `ReverseEngineering/Havok/` | third-party SDK, local only |
| `re-mcp/` | local MCP (indexes private symbol dumps) |
| Ghidra project databases | embed decompiled code |
| `CreationKit.exe`, `Fallout4.exe` | copyrighted binaries |
