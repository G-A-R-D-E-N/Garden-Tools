# CKRE — Fallout 4 Creation Kit Reverse Engineering

Reverse-engineering workspace for `CreationKit.exe` **1.11.137.0** (the Fallout 4 Steam CK).

**Target**: `.../steamapps/common/Fallout 4 1946160/CreationKit.exe`

## Docs

| Doc | Contents |
|---|---|
| [00-OVERVIEW.md](00-OVERVIEW.md) | binary identity, PE map, imports, toolchain, subsystem map |
| [01-CREATIONKIT-INI.md](01-CREATIONKIT-INI.md) | shipped `CreationKit.ini` — every key documented |
| [02-RTTI-CLASS-MAP.md](02-RTTI-CLASS-MAP.md) | 4,438 RTTI classes → subsystem map (record DB, render window, SDKs) |
| [03-FINDINGS.md](03-FINDINGS.md) | dated findings index (facts land here first) |
| [04-PIPELINE.md](04-PIPELINE.md) | how to run/query the Ghidra project |
| [05-BATCH-CLI.md](05-BATCH-CLI.md) | the reconciled headless batch CLI surface — all 31 switches with bit values and arg grammars, the one-switch-per-invocation rule, and resolved next steps |
| [06-SETTINGS-KEYS.md](06-SETTINGS-KEYS.md) | the embedded 1,629-key settings database |
| [07-SOURCE-TREE.md](07-SOURCE-TREE.md) | recovered `__FILE__` source layout (`code\construction set\` = the CK) |
| [08-CK-LOD-SUBSYSTEM.md](08-CK-LOD-SUBSYSTEM.md) | the CK's built-in LOD + precombined/previs pipeline |
| [12-RECORD-DEFINITION.md](12-RECORD-DEFINITION.md) | the record-definition dispatch — formEnumString enum, file-load table, factory array, RTTI class map |
| [09-PRECOMBINED-PREVIS.md](09-PRECOMBINED-PREVIS.md) | / full decompile walk (chunking, mesh naming, .scene output) |
| [10-SETTINGS-READER.md](10-SETTINGS-READER.md) | the `bKey:Section` settings reader — type-letter table, descriptor layout, registration pipeline, read-path verdict |
| [11-INIT-CHAIN.md](11-INIT-CHAIN.md) | full subsystem init chain from WinMain — phases, labeled initializers, and the Havok/lipsync/hook functions |
| [13-CLI-PARSER.md](13-CLI-PARSER.md) | the CLI token→bitmask parser (FUN_140487010) — the full 31-switch table and the dispatcher's complete bit→process map |
| [14-SCENE-FORMAT.md](14-SCENE-FORMAT.md) | PreVis `.scene` format from the decompiled writer (FUN_141815c90) + real-file byte parse of the shipped `.uvd` sibling |
| [15-BATCH-PROCESSES.md](15-BATCH-PROCESSES.md) | batch process internals — MasterfileOptimizer/Tagifier, MapMaker, CheckItInChappy, FileSaver + the `-SaveDefaultPlugin` masterfile pipeline |
| [16-SETTINGS-WRITEBACK.md](16-SETTINGS-WRITEBACK.md) | the settings write-back — notify setter FUN_1425c6b90, scatter-table registry API, the generic WriteSetting, and the read/write round-trip |
| [17-INI-PATHS.md](17-INI-PATHS.md) | the ini file census — all 5 touched .ini paths, each mapped to its reader function, with the full Get/WritePrivateProfile call-site tables |
| [18-ELRIC-PDB.md](18-ELRIC-PDB.md) | the Elric SDK PDB ground truth — 90,090 engine symbols confirming the CK decompiles (ReadSetting/WriteSetting, SettingT globals, BSTScatterTable, NiStream) |
| [19-BATCH-EXIT-CODES.md](19-BATCH-EXIT-CODES.md) | the batch exit-code contract — `bThrowProcessCompleteExceptions:General` gate setting, all 7 RaiseException sites, the 31-bit exit-code matrix, and the minidump termination path |
| [20-WINE-PAPYRUS-ZIP-CRASH.md](20-WINE-PAPYRUS-ZIP-CRASH.md) | the Wine startup crash explained — Papyrus source-zip extraction via Shell COM (`IShellDispatch::NameSpace` returns S_OK + NULL `Folder*`), with patch/workaround analysis |
| [21-UVD-FORMAT.md](21-UVD-FORMAT.md) | the compiled PreVis `.uvd` scene format (magic `0xD6000012`) — CRC-32C framing, full 0x150 header field map, and version string, verified against 3 shipped samples + the writer decompiles |

## Data dumps (`ghidra-dump/`)

| File | Contents |
|---|---|
| `rtti-classes.txt` | all 4,438 RTTI names |
| `tes-classes.txt` | the 126 `TES*` record classes |
| `ck-settings-keys.txt` | all 1,629 `bKey:Section` settings |
| `srcpaths.txt` | all 1,116 embedded `__FILE__` paths |
| `ck-src-files.txt` | the 86 `construction set` source files |
| `lod-strings.txt` | the 273 LOD-related strings |
| `scenewriter-*.log`, `previs-generator-0x1181210.log` | `.scene` writer wrapper/serializer/element decompiles (§14) |
| `batch-runner-fun1404906a0.log`, `vd-*.log`, `w?-*.log`, `checkitin-fun14048d4e0-full.txt` | batch-process runner/vtable/Execute decompiles (§15) |
| `scene-samples/Vis_Fallout4.esm_*.UVD` | 3 real shipped `.uvd` previs samples extracted from `Fallout4 - MeshesExtra.ba2` |
| `Query.java` | headless ghidra query script (substring/addr/decompile) |

## Tools

| Tool | Purpose |
|---|---|
| `tools/pe_recon.py` | PE header/sections/entropy/PDB/imports recon |
| `tools/validate_ckini.py` | CreationKit.ini oracle — replays the reader (`FUN_1425c7f90`) against the 1,629-key table + type letters; `--list-keys`/`--stats`, `--command`/`--batch-command` for doc-05/13 grammar, and `--exit-codes` for the doc-19 31-switch sentinel matrix; samples under `tools/samples/` |

## Status

- [x] Binary identity (not packed; PDB fingerprint; CI build)
- [x] Import/DLL surface + third-party SDK inventory
- [x] RTTI class map (4,438 classes)
- [x] Ini + embedded settings database (1,629 keys)
- [x] Source-tree recovery (1,116 `__FILE__` paths)
- [x] Batch CLI surface
- [x] LOD subsystem string contract
- [x] Ghidra auto-analysis (complete — RTTI structures recovered natively)
- [x] RTTI naming (analyzer-provided: `vftable`/`RTTI_Complete_Object_Locator` per class namespace)
- [x] Entry chain + WinMain (0x6946a0) + CLI dispatcher (0x4870f0) decompiled
- [ ] Decompile: init chain → plugin load/save → record table → ini reader → render window
- [ ] `.LOD` settings-file format (needs a CK run)
- [ ] `.LOD` settings-file format (no sample ships — needs a CK run)
- [ ] Precombined/previs pipeline entry (`-GeneratePreCombined` handler)
