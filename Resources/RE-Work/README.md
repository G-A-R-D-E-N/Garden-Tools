# RE-Work

Creation Kit and Fallout 4 runtime reverse-engineering notes for GARDEN tools.

This folder is docs, fact tables, and owner scripts. Game binaries, Ghidra project databases, and decompiled function bodies stay on the local workstation. See [LEGAL.md](LEGAL.md).

CK numbered notes live in `docs/` so their cross-links stay valid. Start from [docs/CKRE.md](docs/CKRE.md) or the categories below. Runtime notes live in [Runtime/](Runtime/).

## Categories

### Records and plugins
What Radicle cares about: form enum, file-load table, factory array, FileSaver / masterfile write.

- [docs/12-RECORD-DEFINITION.md](docs/12-RECORD-DEFINITION.md)
- [docs/record-definition-table.txt](docs/record-definition-table.txt)
- [docs/15-BATCH-PROCESSES.md](docs/15-BATCH-PROCESSES.md) (`-SaveDefaultPlugin`, MasterfileOptimizer)
- [dumps/tes-classes.txt](dumps/tes-classes.txt)

### Batch CLI
Headless switches, parser, exit codes, CompileTextExport / localized-string outputs.

- [docs/05-BATCH-CLI.md](docs/05-BATCH-CLI.md)
- [docs/13-CLI-PARSER.md](docs/13-CLI-PARSER.md)
- [docs/19-BATCH-EXIT-CODES.md](docs/19-BATCH-EXIT-CODES.md)
- [docs/22-BATCH-SETTING-DEPENDENCIES.md](docs/22-BATCH-SETTING-DEPENDENCIES.md)
- [dumps/decompile-CompileTextExport-FUN_140916980.md](dumps/decompile-CompileTextExport-FUN_140916980.md)
- [dumps/decompile-CompileTextExport-line-parser-FUN_14091f3d0.md](dumps/decompile-CompileTextExport-line-parser-FUN_14091f3d0.md)
- [dumps/localized-string-file-format.md](dumps/localized-string-file-format.md)

### Settings and INI
Embedded 1,629-key database, readers, write-back, path census, duplicate descriptors, runtime GameSetting table.

- [docs/01-CREATIONKIT-INI.md](docs/01-CREATIONKIT-INI.md)
- [docs/06-SETTINGS-KEYS.md](docs/06-SETTINGS-KEYS.md)
- [docs/10-SETTINGS-READER.md](docs/10-SETTINGS-READER.md)
- [docs/16-SETTINGS-WRITEBACK.md](docs/16-SETTINGS-WRITEBACK.md)
- [docs/17-INI-PATHS.md](docs/17-INI-PATHS.md)
- [dumps/ck-settings-keys.txt](dumps/ck-settings-keys.txt)
- [dumps/duplicate-ini-settings.md](dumps/duplicate-ini-settings.md)
- [dumps/game-setting-table.md](dumps/game-setting-table.md)
- [tools/validate_ckini.py](tools/validate_ckini.py)

### LOD and previs
CK LOD pipeline, precombined, `.scene` / `.uvd`.

- [docs/08-CK-LOD-SUBSYSTEM.md](docs/08-CK-LOD-SUBSYSTEM.md)
- [docs/09-PRECOMBINED-PREVIS.md](docs/09-PRECOMBINED-PREVIS.md)
- [docs/14-SCENE-FORMAT.md](docs/14-SCENE-FORMAT.md)
- [docs/21-UVD-FORMAT.md](docs/21-UVD-FORMAT.md)
- [dumps/lod-strings.txt](dumps/lod-strings.txt)

### Classes and symbols
RTTI map, Elric engine-symbol facts, NIF writer catalog.

- [docs/02-RTTI-CLASS-MAP.md](docs/02-RTTI-CLASS-MAP.md)
- [docs/18-ELRIC-SYMBOLS.md](docs/18-ELRIC-SYMBOLS.md)
- [dumps/rtti-classes.txt](dumps/rtti-classes.txt)
- [dumps/ck-nif-block-format-reference.md](dumps/ck-nif-block-format-reference.md)
- [dumps/nistream-block-save-map.md](dumps/nistream-block-save-map.md)
- [dumps/nistream-version-globals.md](dumps/nistream-version-globals.md)
- [dumps/decompile-SCOL-combined-mesh-writer.md](dumps/decompile-SCOL-combined-mesh-writer.md)
- [tools/extract_ck_nif_savebinary.py](tools/extract_ck_nif_savebinary.py)
- [tools/validate_nif.py](tools/validate_nif.py)

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
CK Ghidra headless helpers: [scripts/](scripts/).

## Runtime (`Fallout4.exe`)

OG 1.10.163 and AE 1.11.221. FormID / TESFile, layouts, naming.

- [Runtime/README.md](Runtime/README.md)
- [Runtime/03-FINDINGS.md](Runtime/03-FINDINGS.md)
- [Runtime/06-FUNCTION-INDEX.md](Runtime/06-FUNCTION-INDEX.md)
- [Runtime/Organized/INDEX.md](Runtime/Organized/INDEX.md)
- [Runtime/Organized/1.10.163/](Runtime/Organized/1.10.163/) subsystem notes (landscape, quests, HUD, workshop)
- [Runtime/Scripts/](Runtime/Scripts/)

## Layout

```
Resources/RE-Work/
├── README.md          this category map
├── LEGAL.md
├── MANIFEST.md
├── docs/              numbered CK notes (00–22) + index
├── dumps/             RTTI / TES* / settings / srcpath tables + analysis docs
├── scripts/           CK Ghidra Java queries
├── tools/             PE recon, CreationKit.ini oracle, NIF tools + fixtures
└── Runtime/           Fallout4.exe notes, scripts, Organized analysis
```
