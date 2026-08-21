# MANIFEST — what is in `Resources/RE-Work`

Standing rule: **never push a PDB or anything that can be copyrighted.** Full policy: [LEGAL.md](LEGAL.md).

The local workstation still holds ~42 GB of Ghidra projects, game binaries, PDBs, and the Havok SDK. Those stay local.

## Tracked

| Path | What |
|------|------|
| `CreationKit/` | CK 1.11.137.0 docs, INI oracle tools, fact tables (RTTI, TES* classes, settings keys, `__FILE__` paths), owner Ghidra scripts |
| `Runtime/` | Fallout 4 OG/AE pipeline docs, findings, Ghidra scripts, Organized analysis CSVs, curated `README-*.md` subsystem notes, f4-re MCP source |
| `Archive/` | FINDINGS-INDEX, WORK-LOG, ISSUES-MAPPING, CONTRIBUTING, LEGAL-README |

## Not tracked (and why)

| Local path | Why |
|------------|-----|
| `ReverseEngineering/Ghidra/` | stock Ghidra install |
| `ReverseEngineering/Projects/fwgh_proj_*` | Ghidra DBs embed decompiled game code |
| `ReverseEngineering/Decompiles/` | decompiled function bodies |
| `ReverseEngineering/Symbols/`, `EXE/`, `Bins/` | game exe/PDB and dumps |
| `ReverseEngineering/Havok/` | confidential Havok SDK |
| `ReverseEngineering/Organized/1.10.155/`, `generated/`, `1.10.163/*.txt` | regeneratable per-version dumps |
| `Organized/DECOMPILE-RECORDS.md` | decompiled bodies |
| `CKRE/ghidra-dump/*.log`, `FUN_*.txt`, `*.UVD`, `elric-pdb-symbols.txt` | decompiles, game previs samples, PDB dump |
| `re-mcp/node_modules`, `dist`, `*.sqlite`, `*.mcpb` | regeneratable |

## Integrity

Before commit:

```bash
find Resources/RE-Work -type f | grep -iE '\.(pdb|exe|dll|nif|hkx|ba2|bsa|esm|esp|idc|sqlite|mcpb|uvd)$'
```

Must print nothing.
