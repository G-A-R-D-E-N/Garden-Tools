# CKRE — Fallout 4 Creation Kit Reverse Engineering

Reverse-engineering workspace for `CreationKit.exe` **1.11.137.0** (the Fallout 4 Steam CK).

**Target**: `.../steamapps/common/Fallout 4 1946160/CreationKit.exe`

**Status**: [STATUS.md](STATUS.md) — completion state of every doc and tool, plus next open questions.

## Docs

| Doc | Contents |
|---|---|
| [00-OVERVIEW.md](00-OVERVIEW.md) | binary identity, PE map, imports, toolchain, subsystem map |
| [01-CREATIONKIT-INI.md](01-CREATIONKIT-INI.md) | shipped `CreationKit.ini` — every key documented |
| [02-RTTI-CLASS-MAP.md](02-RTTI-CLASS-MAP.md) | 4,438 RTTI classes → subsystem map (record DB, render window, SDKs) |
| [03-FINDINGS.md](03-FINDINGS.md) | dated findings index (facts land here first) |
| [04-PIPELINE.md](04-PIPELINE.md) | how to run/query the Ghidra project |
| [05-BATCH-CLI.md](05-BATCH-CLI.md) | the reconciled headless batch CLI surface — all 31 switches with bit values and arg grammars, the one-switch-per-invocation rule, and resolved next steps |
| [ghidra-dump/decompile-CompileTextExport-FUN_140916980.md](ghidra-dump/decompile-CompileTextExport-FUN_140916980.md) | `-CompileTextExport` inputs, output paths, failure modes, and the exact `TextExport.txt` record contract |
| [ghidra-dump/localized-string-file-format.md](ghidra-dump/localized-string-file-format.md) | byte-level `.STRINGS`/`.DLSTRINGS`/`.ILSTRINGS` format — header, offset table, record encodings, writer/loader anchors, verified on 33 shipped FO4 files |
| [06-SETTINGS-KEYS.md](06-SETTINGS-KEYS.md) | the embedded 1,629 colon-bearing settings plus 25 bare `[MAIN]` records |
| [07-SOURCE-TREE.md](07-SOURCE-TREE.md) | recovered `__FILE__` source layout (`code\construction set\` = the CK) |
| [24-TESFORMS-SOURCE-MAP.md](24-TESFORMS-SOURCE-MAP.md) | file→class map for the entire tesforms/world module — all 158 runtime files + 43 editor forks + adjacent water/distantterrain/extradata, cross-validated against RTTI |
| [08-CK-LOD-SUBSYSTEM.md](08-CK-LOD-SUBSYSTEM.md) | the CK's built-in LOD + precombined/previs pipeline |
| [12-RECORD-DEFINITION.md](12-RECORD-DEFINITION.md) | the record-definition dispatch — formEnumString enum, file-load table, factory array, RTTI class map |
| [09-PRECOMBINED-PREVIS.md](09-PRECOMBINED-PREVIS.md) | / full decompile walk (chunking, mesh naming, .scene output) |
| [ghidra-dump/decompile-SCOL-combined-mesh-writer.md](ghidra-dump/decompile-SCOL-combined-mesh-writer.md) | SCOL path helper plus confirmed `_OC`/`_Physics` naming and the NiStream payload boundary |
| [ghidra-dump/nistream-version-globals.md](ghidra-dump/nistream-version-globals.md) | complete mutation census for the NIF version globals and the fixed `20.2.0.7` emission result |
| [10-SETTINGS-READER.md](10-SETTINGS-READER.md) | the `bKey:Section` settings reader — type-letter table, descriptor layout, registration pipeline, read-path verdict |
| [11-INIT-CHAIN.md](11-INIT-CHAIN.md) | full subsystem init chain from WinMain — phases, labeled initializers, and the Havok/lipsync/hook functions |
| [13-CLI-PARSER.md](13-CLI-PARSER.md) | the CLI token→bitmask parser (FUN_140487010) — the full 31-switch table and the dispatcher's complete bit→process map |
| [14-SCENE-FORMAT.md](14-SCENE-FORMAT.md) | PreVis `.scene` format from the decompiled writer (FUN_141815c90) + real-file byte parse of the shipped `.uvd` sibling |
| [15-BATCH-PROCESSES.md](15-BATCH-PROCESSES.md) | batch process internals — MasterfileOptimizer/Tagifier, MapMaker, CheckItInChappy, FileSaver + the `-SaveDefaultPlugin` masterfile pipeline |
| [16-SETTINGS-WRITEBACK.md](16-SETTINGS-WRITEBACK.md) | the settings write-back — notify setter FUN_1425c6b90, scatter-table registry API, the generic WriteSetting, and the read/write round-trip |
| [17-INI-PATHS.md](17-INI-PATHS.md) | the ini file census — all 5 touched .ini paths, each mapped to its reader function, with the full Get/WritePrivateProfile call-site tables |
| [18-ELRIC-PDB.md](18-ELRIC-PDB.md) | the Elric SDK PDB ground truth — 90,090 engine symbols confirming the CK decompiles (ReadSetting/WriteSetting, SettingT globals, BSTScatterTable, NiStream) |
| [ghidra-dump/game-setting-table.md](ghidra-dump/game-setting-table.md) | RTTI and byte census of the 5,137 bare-name runtime `GameSetting` records under `SettingT<GameSettingCollection>` |
| [ghidra-dump/duplicate-ini-settings.md](ghidra-dump/duplicate-ini-settings.md) | all nine duplicated `bKey:Section` descriptors — registration targets, initializer/data runs, and read/write collision semantics |
| [19-BATCH-EXIT-CODES.md](19-BATCH-EXIT-CODES.md) | the batch exit-code contract — `bThrowProcessCompleteExceptions:General` gate setting, all 7 RaiseException sites, the 31-bit exit-code matrix, and the minidump termination path |
| [22-BATCH-SETTING-DEPENDENCIES.md](22-BATCH-SETTING-DEPENDENCIES.md) | batch-pipeline setting dependencies — direct descriptor value references for PreCombined/PreVis, dialogue/text/FaceGen exporters, VCS services, the negative `iPlacedObjectRender` result, and dependency tiers |
| [23-CELL-WORLDSPACE.md](23-CELL-WORLDSPACE.md) | the CELL/Worldspace record model — RTTI→vftable inventory for WRLD/CELL/LAND/REFR, the `ConcreteFormFactory` correction to doc 12 (only REFR is special-cased), CreateForm/NewCell dispatch, the GRUP group-type enum, and source mapping |
| [20-WINE-PAPYRUS-ZIP-CRASH.md](20-WINE-PAPYRUS-ZIP-CRASH.md) | the Wine startup crash explained — Papyrus source-zip extraction via Shell COM (`IShellDispatch::NameSpace` returns S_OK + NULL `Folder*`), with patch/workaround analysis |
| [21-UVD-FORMAT.md](21-UVD-FORMAT.md) | the compiled PreVis `.uvd` scene format (magic `0xD6000012`) — CRC-32C framing, full 0x150 header field map, and version string, verified against 3 shipped samples + the writer decompiles |

## Data dumps (`ghidra-dump/`)

| File | Contents |
|---|---|
| `rtti-classes.txt` | all 4,438 RTTI names |
| `tes-classes.txt` | the 126 `TES*` record classes |
| `ck-settings-keys.txt` | the 1,629 colon-bearing `bKey:Section` literals; the validator augments these with 25 byte-verified bare `[MAIN]` records |
| `game-setting-table.md` | `SettingT<GameSettingCollection>` type identity, 5,144-record scan, type counts, and representative record addresses |
| `duplicate-ini-settings.md` | the nine repeated INI descriptor pairs, collection getters, and pointer-keyed collision behavior |
| `srcpaths.txt` | all 1,116 embedded `__FILE__` paths |
| `ck-src-files.txt` | the 86 `construction set` source files |
| `lod-strings.txt` | the 273 LOD-related strings |
| `scenewriter-*.log`, `previs-generator-0x1181210.log` | `.scene` writer wrapper/serializer/element decompiles (§14) |
| `decompile-SCOL-combined-mesh-writer.md` | SCOL/`_OC`/`_Physics` path decompile and NiStream payload boundary |
| `nistream-version-globals.md` | all version-global xrefs, mutation census, and emitted-version conclusion |
| `nistream-block-save-map.md` | SaveStream `0xd8/0xe0/0x110` dispatch and the RTTI → `SaveBinary` writer catalog for `_Physics` NIFs |
| `ck-nif-block-format-reference.md` | exhaustive factory-backed CK block-type inventory, 64-bit slot-`0xe0` body addresses/ranges, disassembly-derived payload patterns, and unresolved/helper-type boundaries |
| `decompile-CompileTextExport-line-parser-FUN_14091f3d0.md` | annotated `TextExport.txt` line parser, delimiter behavior, StringID rules, and the 18-category table |
| `elric-engine-ghidra-import.md` | persistent Ghidra project containing ElricInterop plus the complete native Ni/BS engine DLL layer and coverage report |
| `batch-runner-fun1404906a0.log`, `vd-*.log`, `w?-*.log`, `checkitin-fun14048d4e0-full.txt` | batch-process runner/vtable/Execute decompiles (§15) |
| `scene-samples/Vis_Fallout4.esm_*.UVD` | 3 real shipped `.uvd` previs samples extracted from `Fallout4 - MeshesExtra.ba2` |
| `Query.java` | headless ghidra query script (substring/addr/decompile) |

## Tools

| Tool | Purpose |
|---|---|
| `tools/pe_recon.py` | PE header/sections/entropy/PDB/imports recon |
| `tools/validate_ckini.py` | CreationKit.ini oracle — replays the reader (`FUN_1425c7f90`) against the 1,629-key table plus 25 bare `[MAIN]` records and type letters; `--list-keys`/`--stats`, `--command`/`--batch-command` for doc-05/13 grammar, `--exit-codes` for the doc-19 31-switch sentinel matrix, `--compile-text` for the `FUN_140916980` input-root contract (quoted roots, backup precedence, 8-hex supplemental stems, reserved/duplicate IDs, categories) plus per-record output-table routing (dialog categories {1,6,7,10} → `.DLSTRINGS`, other categories → `.STRINGS`, supplemental files → `.ILSTRINGS`), and `--strings` for compiled `.STRINGS`/`.DLSTRINGS`/`.ILSTRINGS` files (header/size invariant, sorted table, record encoding, per-language ID partition — verified on all 33 shipped FO4 files; with `--text-export` the partition report also counts how many IDs per table came from each dialog category, and the combined `--compile-text --strings` cross-check reports the same per-table composition automatically, per language stem; `--json` emits the routing predictions, diagnostics, cross-check verdicts, and composition as structured JSON for machine consumption; `--verbose` lists all 18 categories with counts per table in composition reports; composition reports also flag routing anomalies as warnings (dialog ID in `.STRINGS`, ordinary ID in `.DLSTRINGS`, TextExport-derived ID in `.ILSTRINGS`); `--composition-out` writes the per-language-stem composition as deterministic JSON for CI diffing, and `--composition-in` diffs a run against such a file, reporting only the changed stems and tables with a DRIFT/IDENTICAL verdict); fixtures under `tools/samples/`, including a committed cross-check set in `compile-text/cross-check/` reproducing the MATCH/MISMATCH/WARNING paths without the game files |
| `tools/extract_ck_nif_savebinary.py` | reconstructs the CK factory/RTTI/vtable map, resolves slot `+0xe0`, bounds every SaveBinary body with `.pdata`, and optionally disassembles unique bodies |
| `tools/validate_nif.py` | validates the §4e NIF header, block-type table, root type indices, object-size/payload partition, fixed-string pool, object groups, and trailing link IDs |

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
