# RE-Work

Fallout 4 reverse-engineering notes used by GARDEN tools (Radicle, F4LODGEN, F4SE plugins).

This is **analysis and scripts**, not a Ghidra project dump. Game binaries, PDBs, Havok SDK material, and decompiled function bodies stay on the local workstation. See [LEGAL.md](LEGAL.md) and [MANIFEST.md](MANIFEST.md).

Evidence order for Radicle: FO4 runtime, then CK, then this tree, then xEdit, then Mutagen as oracle.

## Layout

```
Resources/RE-Work/
├── README.md                 this file
├── LEGAL.md                  provenance and the hard no list
├── MANIFEST.md               what is here vs kept local
├── CreationKit/              CreationKit.exe 1.11.137.0
│   ├── 00-OVERVIEW.md … 21-UVD-FORMAT.md
│   ├── record-definition-table.txt
│   ├── ghidra-dump/          fact tables + owner Ghidra scripts
│   └── tools/                PE recon, CreationKit.ini oracle
├── Runtime/                  Fallout4.exe OG 1.10.163 / AE 1.11.221
│   ├── 01-SETUP.md … 07-DECOMPILE-CANDIDATES.md
│   ├── 03-FINDINGS.md        verified runtime facts
│   ├── Scripts/              headless Ghidra query / naming scripts
│   ├── Organized/            cross-ref CSVs + subsystem notes
│   └── mcp/                  f4-re MCP server source
└── Archive/                  workspace narrative (findings index, work log)
```

## Categories

| Category | Path | Use for |
|----------|------|---------|
| Plugin / record format | `CreationKit/12-RECORD-DEFINITION.md`, `record-definition-table.txt` | Radicle FormKey, TES4, factories |
| CK batch / FileSaver | `CreationKit/05-BATCH-CLI.md`, `13-CLI-PARSER.md`, `15-BATCH-PROCESSES.md` | masterfile optimizer, save pipeline |
| CK settings / INI | `CreationKit/01-CREATIONKIT-INI.md`, `10-SETTINGS-READER.md`, `16-SETTINGS-WRITEBACK.md` | not plugin IO |
| LOD / previs | `CreationKit/08-CK-LOD-SUBSYSTEM.md`, `09-PRECOMBINED-PREVIS.md`, `14-SCENE-FORMAT.md`, `21-UVD-FORMAT.md` | F4LODGEN |
| Runtime FormID / TESFile | `Runtime/03-FINDINGS.md`, `Runtime/06-FUNCTION-INDEX.md` | on-disk vs compile index, SmallFile |
| Runtime layouts | `Runtime/Organized/1.10.163/README-*.md` | CELL/landscape, quests, HUD |
| Naming / RTTI | `Runtime/Organized/*.csv`, `CreationKit/ghidra-dump/rtti-classes.txt` | class map, vtable slots |
| Tooling | `Runtime/Scripts/`, `CreationKit/tools/`, `Runtime/mcp/` | how to query Ghidra locally |

## Local-only (not in git)

Ghidra 12.1 install, `fwgh_proj_1.10.163_unpacked`, `fwgh_proj_1.11.221_unpacked`, `Fallout4.exe` / `.pdb`, Havok SDK, `Decompiles/`. Paths are documented in `Runtime/01-SETUP.md` and `Runtime/CLAUDE.md`.
