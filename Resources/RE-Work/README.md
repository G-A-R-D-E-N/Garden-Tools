# CKRE

Creation Kit reverse engineering for `CreationKit.exe` **1.11.137.0** (Fallout 4 Steam CK).

This folder is docs, fact tables, and owner scripts. Game binaries, PDBs, Ghidra project databases, and decompiled function bodies stay on the local workstation. See [LEGAL.md](LEGAL.md).

Numbered notes live in `docs/` so their cross-links stay valid. Start from [docs/CKRE.md](docs/CKRE.md) for the original inventory, or use the categories below.

## Categories

### Records and plugins
What Radicle cares about: form enum, file-load table, factory array, FileSaver / masterfile write.

- [docs/12-RECORD-DEFINITION.md](docs/12-RECORD-DEFINITION.md)
- [docs/record-definition-table.txt](docs/record-definition-table.txt)
- [docs/15-BATCH-PROCESSES.md](docs/15-BATCH-PROCESSES.md) (`-SaveDefaultPlugin`, MasterfileOptimizer)
- [dumps/tes-classes.txt](dumps/tes-classes.txt)

### Batch CLI
Headless switches, parser, exit codes.

- [docs/05-BATCH-CLI.md](docs/05-BATCH-CLI.md)
- [docs/13-CLI-PARSER.md](docs/13-CLI-PARSER.md)
- [docs/19-BATCH-EXIT-CODES.md](docs/19-BATCH-EXIT-CODES.md)

### Settings and INI
Embedded 1,629-key database, readers, write-back, path census.

- [docs/01-CREATIONKIT-INI.md](docs/01-CREATIONKIT-INI.md)
- [docs/06-SETTINGS-KEYS.md](docs/06-SETTINGS-KEYS.md)
- [docs/10-SETTINGS-READER.md](docs/10-SETTINGS-READER.md)
- [docs/16-SETTINGS-WRITEBACK.md](docs/16-SETTINGS-WRITEBACK.md)
- [docs/17-INI-PATHS.md](docs/17-INI-PATHS.md)
- [dumps/ck-settings-keys.txt](dumps/ck-settings-keys.txt)
- [tools/validate_ckini.py](tools/validate_ckini.py)

### LOD and previs
CK LOD pipeline, precombined, `.scene` / `.uvd`.

- [docs/08-CK-LOD-SUBSYSTEM.md](docs/08-CK-LOD-SUBSYSTEM.md)
- [docs/09-PRECOMBINED-PREVIS.md](docs/09-PRECOMBINED-PREVIS.md)
- [docs/14-SCENE-FORMAT.md](docs/14-SCENE-FORMAT.md)
- [docs/21-UVD-FORMAT.md](docs/21-UVD-FORMAT.md)
- [dumps/lod-strings.txt](dumps/lod-strings.txt)

### Classes and symbols
RTTI map, Elric PDB facts (no PDB file).

- [docs/02-RTTI-CLASS-MAP.md](docs/02-RTTI-CLASS-MAP.md)
- [docs/18-ELRIC-PDB.md](docs/18-ELRIC-PDB.md)
- [dumps/rtti-classes.txt](dumps/rtti-classes.txt)

### Binary identity and init
PE map, source-tree recovery, WinMain init chain, Ghidra query how-to.

- [docs/00-OVERVIEW.md](docs/00-OVERVIEW.md)
- [docs/04-PIPELINE.md](docs/04-PIPELINE.md)
- [docs/07-SOURCE-TREE.md](docs/07-SOURCE-TREE.md)
- [docs/11-INIT-CHAIN.md](docs/11-INIT-CHAIN.md)
- [dumps/srcpaths.txt](dumps/srcpaths.txt)
- [dumps/ck-src-files.txt](dumps/ck-src-files.txt)
- [tools/pe_recon.py](tools/pe_recon.py)

### Findings
Dated fact log, plus the Wine Papyrus-zip startup crash.

- [docs/03-FINDINGS.md](docs/03-FINDINGS.md)
- [docs/20-WINE-PAPYRUS-ZIP-CRASH.md](docs/20-WINE-PAPYRUS-ZIP-CRASH.md)

### Scripts
Owner Ghidra headless helpers: [scripts/](scripts/).

## Layout

```
Resources/RE-Work/
├── README.md          this category map
├── LEGAL.md
├── MANIFEST.md
├── docs/              numbered CK notes
├── dumps/             RTTI / TES* / settings / srcpath tables
├── scripts/           Ghidra Java queries
└── tools/             PE recon, CreationKit.ini oracle
```
