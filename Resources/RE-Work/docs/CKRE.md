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
| [../dumps/decompile-CompileTextExport-FUN_140916980.md](../dumps/decompile-CompileTextExport-FUN_140916980.md) | `-CompileTextExport` inputs, output paths, failure modes, and the exact `TextExport.txt` record contract |
| [../dumps/localized-string-file-format.md](../dumps/localized-string-file-format.md) | byte-level `.STRINGS`/`.DLSTRINGS`/`.ILSTRINGS` format — header, offset table, record encodings, writer/loader anchors, verified on 33 shipped FO4 files |
| [06-SETTINGS-KEYS.md](06-SETTINGS-KEYS.md) | the embedded 1,629 colon-bearing settings plus 25 bare `[MAIN]` records |
| [07-SOURCE-TREE.md](07-SOURCE-TREE.md) | recovered `__FILE__` source layout (`code\construction set\` = the CK) |
| [08-CK-LOD-SUBSYSTEM.md](08-CK-LOD-SUBSYSTEM.md) | the CK's built-in LOD + precombined/previs pipeline |
| [12-RECORD-DEFINITION.md](12-RECORD-DEFINITION.md) | the record-definition dispatch — formEnumString enum, file-load table, factory array, RTTI class map |
| [09-PRECOMBINED-PREVIS.md](09-PRECOMBINED-PREVIS.md) | / full decompile walk (chunking, mesh naming, .scene output) |
| [../dumps/decompile-SCOL-combined-mesh-writer.md](../dumps/decompile-SCOL-combined-mesh-writer.md) | SCOL path helper plus confirmed `_OC`/`_Physics` naming and the NiStream payload boundary |
| [../dumps/nistream-version-globals.md](../dumps/nistream-version-globals.md) | complete mutation census for the NIF version globals and the fixed `20.2.0.7` emission result |
| [10-SETTINGS-READER.md](10-SETTINGS-READER.md) | the `bKey:Section` settings reader — type-letter table, descriptor layout, registration pipeline, read-path verdict |
| [11-INIT-CHAIN.md](11-INIT-CHAIN.md) | full subsystem init chain from WinMain — phases, labeled initializers, and the Havok/lipsync/hook functions |
| [13-CLI-PARSER.md](13-CLI-PARSER.md) | the CLI token→bitmask parser (FUN_140487010) — the full 31-switch table and the dispatcher's complete bit→process map |
| [14-SCENE-FORMAT.md](14-SCENE-FORMAT.md) | PreVis `.scene` format from the decompiled writer (FUN_141815c90) + real-file byte parse of the shipped `.uvd` sibling |
| [15-BATCH-PROCESSES.md](15-BATCH-PROCESSES.md) | batch process internals — MasterfileOptimizer/Tagifier, MapMaker, CheckItInChappy, FileSaver + the `-SaveDefaultPlugin` masterfile pipeline |
| [16-SETTINGS-WRITEBACK.md](16-SETTINGS-WRITEBACK.md) | the settings write-back — notify setter FUN_1425c6b90, scatter-table registry API, the generic WriteSetting, and the read/write round-trip |
| [17-INI-PATHS.md](17-INI-PATHS.md) | the ini file census — all 5 touched .ini paths, each mapped to its reader function, with the full Get/WritePrivateProfile call-site tables |
| [18-ELRIC-SYMBOLS.md](18-ELRIC-SYMBOLS.md) | the Elric SDK PDB ground truth — 90,090 engine symbols confirming the CK decompiles (ReadSetting/WriteSetting, SettingT globals, BSTScatterTable, NiStream) |
| [../dumps/game-setting-table.md](../dumps/game-setting-table.md) | RTTI and byte census of the 5,137 bare-name runtime `GameSetting` records under `SettingT<GameSettingCollection>` |
| [../dumps/duplicate-ini-settings.md](../dumps/duplicate-ini-settings.md) | all nine duplicated `bKey:Section` descriptors — registration targets, initializer/data runs, and read/write collision semantics |
| [19-BATCH-EXIT-CODES.md](19-BATCH-EXIT-CODES.md) | the batch exit-code contract — `bThrowProcessCompleteExceptions:General` gate setting, all 7 RaiseException sites, the 31-bit exit-code matrix, and the minidump termination path |
| [22-BATCH-SETTING-DEPENDENCIES.md](22-BATCH-SETTING-DEPENDENCIES.md) | batch-pipeline setting dependencies — direct descriptor value references for PreCombined/PreVis, dialogue/text/FaceGen exporters, VCS services, the negative `iPlacedObjectRender` result, and dependency tiers |
| [20-WINE-PAPYRUS-ZIP-CRASH.md](20-WINE-PAPYRUS-ZIP-CRASH.md) | the Wine startup crash explained — Papyrus source-zip extraction via Shell COM (`IShellDispatch::NameSpace` returns S_OK + NULL `Folder*`), with patch/workaround analysis |
| [21-UVD-FORMAT.md](21-UVD-FORMAT.md) | the compiled PreVis `.uvd` scene format (magic `0xD6000012`) — CRC-32C framing, full 0x150 header field map, and version string, verified against 3 shipped samples + the writer decompiles |
| [26-RE-TARGETS.md](26-RE-TARGETS.md) | the high-value RE targets — OBTE/OBTS object-template block layout (3,297 four-master payloads, template FULL invariant, OMOD includes), string-table resolution (reader-variant dispatch, no ID bits, reader recipe in §2a), XCLC trailing field = Force-Hide-Land byte + 3B padding (38,970 exterior cells), and the Fallout4.exe section-delta RVA rebase rule with the 1.10.155-vs-1.10.163 build-mismatch note |
| [35-CLAS-RECORD.md](35-CLAS-RECORD.md) | CLAS LoadRecord (11 tags) + 32-record census: shipped DATA is always the legacy 8-byte shape |
| [34-FACT-RECORD.md](34-FACT-RECORD.md) | FACT LoadRecord via ck_decompile end-to-end (23 tags, version-branched DATA) + 845-record census; rank-block RNAM/MNAM/FNAM/INAM unit |
| [33-RACE-RECORD.md](33-RACE-RECORD.md) | RACE LoadRecord (FUN_140aeff50 - largest: 9.6 KB, 125 tags) + 84-record census across 101 types; version-branched RBPC reader |
| [32-NPC-RECORD.md](32-NPC-RECORD.md) | NPC_ LoadRecord (FUN_140a89910 - largest documented: 55 readers, 100 tags) + 4,319-record census; mandatory set and computed-tag families |
| [31-SCEN-RECORD.md](31-SCEN-RECORD.md) | SCEN LoadRecord (FUN_140a20ae0) + 4,714-record census with the scene-action computed-tag family - completes the S-tier set |
| [30-PACK-RECORD.md](30-PACK-RECORD.md) | PACK LoadRecord (FUN_140790640, 39 tags) + 4,844-record census; mandatory PKDT/PSDT/PKCU/XNAM/POBA/POEA/POCA set; computed-tag procedure fields |
| [29-DIAL-INFO-RECORDS.md](29-DIAL-INFO-RECORDS.md) | DIAL/INFO LoadRecord addresses + shipped census (147k records): mandatory subrecord sets, the atomic TRDA+NAM1..4 response block, computed-tag CIS1/CIS2 |
| [28-QUST-RECORD.md](28-QUST-RECORD.md) | QUST LoadRecord (FUN_140abdaf0) + SaveRecord addresses, 45-tag static dispatch, and the full shipped census: 1,862 records, alias-family computed tags, VMAD-only script attachment |
| [27-PROJ-EXPL-HAZD-DATA.md](27-PROJ-EXPL-HAZD-DATA.md) | PROJ DNAM (93 B), EXPL DATA (84 B canonical + short variants), HAZD DNAM (52 B) full field maps with FormID-slot verdicts — unblocks typed creation for #76 |

## Data dumps (`../dumps/`)

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
| `batch-runner-fun1404906a0.log`, `vd-*.log`, `w?-*.log`, `checkitin-fun14048d4e0-full.txt` | batch-process runner/vtable/Execute decompiles (§15) |
| `scene-samples/Vis_Fallout4.esm_*.UVD` | 3 real shipped `.uvd` previs samples extracted from `Fallout4 - MeshesExtra.ba2` |
| `../scripts/Query.java` | headless ghidra query script (substring/addr/decompile) |

Decompile logs, `.uvd` samples, and symbol dumps stay local. They are not in this repo.

## Tools

| Tool | Purpose |
|---|---|
| `../tools/pe_recon.py` | PE header/sections/entropy/PDB/imports recon |
| `../tools/validate_ckini.py` | CreationKit.ini oracle — replays the reader (`FUN_1425c7f90`) against the 1,629-key table plus 25 bare `[MAIN]` records and type letters; `--list-keys`/`--stats`, `--command`/`--batch-command` for doc-05/13 grammar, `--exit-codes` for the doc-19 31-switch sentinel matrix, and `--compile-text` for the `FUN_140916980` input-root contract (quoted roots, backup precedence, 8-hex supplemental stems, reserved/duplicate IDs, categories) with fixtures under `../tools/samples/compile-text/`; samples under `../tools/samples/` |
| `../tools/extract_ck_nif_savebinary.py` | reconstructs the CK factory/RTTI/vtable map, resolves slot `+0xe0`, bounds every SaveBinary body with `.pdata`, and optionally disassembles unique bodies |
| `../tools/validate_nif.py` | validates the §4e NIF header, block-type table, root type indices, object-size/payload partition, fixed-string pool, object groups, and trailing link IDs |

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
