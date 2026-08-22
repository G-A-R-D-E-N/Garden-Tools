# CKRE Status

Completion state of every analysis doc and tool in this workspace. Target:
`CreationKit.exe` **1.11.137.0** (Fallout 4 Steam CK). Filenames are plain
names (no links) so this doc reads identically in the local workspace and the
GARDEN-TOOLS `Resources/RE-Work` mirror (where doc 18 is renamed
`18-ELRIC-SYMBOLS.md` and the analysis docs live under `dumps/`).

Legend: ✅ done/verified · 🟡 partial (known gaps in-doc) · 🔴 blocked/not started

## Numbered docs

| Doc | Status | Next open questions |
|---|---|---|
| 00-OVERVIEW | ✅ binary identity, PE map, imports, toolchain, subsystem map | — |
| 01-CREATIONKIT-INI | ✅ shipped `CreationKit.ini`, every key documented | — |
| 02-RTTI-CLASS-MAP | ✅ 4,438 RTTI classes → subsystem map | — |
| 03-FINDINGS | ✅ living dated findings index (facts land here first) | keep appending |
| 04-PIPELINE | ✅ how to run/query the Ghidra project | — |
| 05-BATCH-CLI | ✅ 31 switches, bit values, arg grammars, one-switch rule | — |
| 06-SETTINGS-KEYS | ✅ 1,629 colon-bearing keys + 25 bare `[MAIN]` records | — |
| 07-SOURCE-TREE | ✅ recovered `__FILE__` layout (`code\construction set\`) | — |
| 08-CK-LOD-SUBSYSTEM | ✅ LOD + precombined/previs pipeline map | geometry-level LOD topics tracked in the xLODGen analysis, not here |
| 09-PRECOMBINED-PREVIS | ✅ chunking, mesh naming, `.scene` output walk | precombined NIF geometry details live in `ck-nif-block-format-reference.md` |
| 10-SETTINGS-READER | ✅ reader FUN_1425c7f90, type-letter table, registration pipeline | — |
| 11-INIT-CHAIN | ✅ full WinMain subsystem init chain | — |
| 12-RECORD-DEFINITION | 🟡 formEnumString enum, file-load table, factory array, RTTI map | full record load/save set via the ghidra-demangled factory symbols; doc 23 corrects the CELL/WRLD/LAND "no factory" rows (they have `ConcreteFormFactory`; only REFR is special-cased) |
| 13-CLI-PARSER | ✅ token→bitmask parser, 31-switch table, bit→process map | — |
| 14-SCENE-FORMAT | ✅ `.scene` from writer decompile + shipped `.uvd` byte parse | real `.scene` generation blocked — CK won't run under Wine (doc 20); format rests on writer decompile + `.uvd` siblings |
| 15-BATCH-PROCESSES | ✅ MasterfileOptimizer/Tagifier, MapMaker, CheckItInChappy, FileSaver, `-SaveDefaultPlugin` | — |
| 16-SETTINGS-WRITEBACK | ✅ notify setter, scatter-table registry, WriteSetting, round-trip | — |
| 17-INI-PATHS | ✅ all 5 touched .ini paths → reader functions | — |
| 18-ELRIC-PDB | ✅ 90,090 engine symbols confirming the decompiles | raw symbol dumps stay local per repo policy |
| 19-BATCH-EXIT-CODES | ✅ 7 RaiseException sites, 31-bit matrix, gate setting, minidump path | success/failure is runtime-dependent; live verification needs a working CK launch (blocked, doc 20) |
| 20-WINE-PAPYRUS-ZIP-CRASH | ✅ root cause (Shell COM `NameSpace` → NULL `Folder*`) + workaround analysis | a working Wine launch would unblock several runtime verifications |
| 21-UVD-FORMAT | ✅ magic `0xD6000012`, CRC-32C framing, 0x150 header map, verified on 3 shipped samples | — |
| 22-BATCH-SETTING-DEPENDENCIES | ✅ direct descriptor reads per batch process, negative `iPlacedObjectRender` result, dependency tiers | — |
| 23-CELL-WORLDSPACE | ✅ RTTI→vftable inventory for WRLD/CELL/LAND/REFR, factory correction (doc 12), CreateForm/NewCell dispatch, GRUP enum, source mapping, full TESObjectCELL member layout from ctor + NewCell setter chain (form-type byte at +0x26, cell-data blocks, worldspace bounds), REFR factory/JSON-serializer split, exteriorcellloader streaming contract (§9), LoadRecord/SaveRecord slots 24/27 named with `__FILE__` anchors (§10: load anchors in class `.cpp`, save anchors in `tesform.cpp`/`tesform.h`) | 0x20/0x88 cell-data block internals; whether any CK feature emits non-standard WRLD/CELL subrecords |
| 24-TESFORMS-SOURCE-MAP | ✅ file→class map for all 158 runtime tesforms files + 43 editor forks + adjacent modules (water/distantterrain/extradata), cross-validated against RTTI | non-form infrastructure class internals; files without `__FILE__` strings (identified as gaps: `teswaterform_editor.cpp` etc.) |

## Analysis docs (`ghidra-dump/`)

| Doc | Status | Next open questions |
|---|---|---|
| decompile-CompileTextExport-FUN_140916980 | ✅ inputs, output paths, failure modes, `TextExport.txt` contract | — |
| decompile-CompileTextExport-line-parser-FUN_14091f3d0 | ✅ delimiter behavior, StringID rules, 18-category table | — |
| localized-string-file-format | ✅ header/offset/record encodings, verified on 33 shipped files | — |
| decompile-SCOL-combined-mesh-writer | ✅ `_OC`/`_Physics` naming + NiStream payload boundary | — |
| nistream-version-globals | ✅ version-global mutation census; fixed `20.2.0.7` emission | — |
| nistream-block-save-map | ✅ SaveStream `0xd8/0xe0/0x110` dispatch, RTTI → SaveBinary catalog | — |
| ck-nif-block-format-reference | 🟡 exhaustive block-type inventory + payload patterns | remaining unresolved/helper-type block boundaries; validate against a real CK-written `_OC`/`_Physics` NIF |
| decompile-papyrus-zip-array-population | ✅ editor+0x410 zip-group array, `PapyrusSource_<group>` names | — |
| decompile-papyrus-zip-prompt-lambda-FUN_14112ae80 | ✅ COM extraction gate + clean-disable question | — |
| duplicate-ini-settings | ✅ nine duplicated descriptors, registration targets, collision semantics | — |
| game-setting-table | ✅ 5,137 bare-name `GameSetting` records under `SettingT<GameSettingCollection>` | — |
| elric-engine-ghidra-import | 🟡 persistent Ghidra project with ElricInterop + engine DLL layer | project is local-only (repo policy); coverage report exists |
| cell-worldspace-vtables/ | ✅ `FindVftable.java` RTTI→vftable resolver + raw dumps for TESForm and the 4 world-building classes | — |

## Tools

| Tool | Status | Notes / next |
|---|---|---|
| tools/validate_ckini.py | ✅ mature oracle: ini reader, batch CLI lint, `--strings` byte validation, `--compile-text` routing + cross-check, composition with anomalies, `--json`, `--composition-out`/`--composition-in`, `--verbose` | open: `--json` for the standalone `--strings` mode; exit codes stay static (runtime needs a working CK) |
| tools/validate_nif.py | ✅ NIF wire-spec parser (§4e header/types/size table/string pool/link-ids) | — |
| tools/extract_ck_nif_savebinary.py | 🟡 SaveBinary body-extraction helper for the block reference | feed the remaining `ck-nif-block-format-reference` boundaries |
| tools/pe_recon.py | ✅ PE reconnaissance helper | — |

## Samples / fixtures

| Item | Status |
|---|---|
| tools/samples/*.ini (valid/invalid/no-section/exit-gate) | ✅ oracle fixtures |
| tools/samples/compile-text/backup + primary | ✅ input-root contract fixtures (quoted roots, duplicates, reserved IDs, malformed lines) |
| tools/samples/compile-text/cross-check | ✅ MATCH/MISMATCH/WARNING fixture + `make_compiled.py` generator + committed `composition.golden.json` |
| .github/workflows/validate-ckini-golden.yml | ✅ self-hosted CI golden-file drift check (repo mirror) |

## Next open questions (prioritized)

1. **Doc 12 — finish the record set**: extract ghidra's demangled factory symbols to enumerate the complete set of records the CK can load and save (the last in-progress RE thread).
2. **NIF block reference**: resolve the remaining unresolved/helper-type block boundaries in `ck-nif-block-format-reference.md`, ideally validated against a real CK-written combined-mesh NIF.
3. **Runtime verification**: the batch exit-code matrix (doc 19) and `.scene`/TextExport live runs need the CK to actually execute — blocked by the Wine Papyrus-zip crash (doc 20). Any working launch (wine patch, Windows box) unblocks these.
4. **Standalone strings JSON**: give the `--strings` mode the same structured `--json` output the compile-text modes have, so every validator mode is machine-readable.
5. **Policy reminder**: raw Elric symbol dumps (`elric-pdb-symbols.txt`, `elric-setting-records.txt`) and the Elric Ghidra project stay local; only prose analysis (doc 18) is published.
6. **LOD-side topics** (WaterDelta=0, hide-quads, skirt depth/topology, FO4 BTR chunking, water-height grouping, VTXT quadrant rules, greedy-reducer tie-breaking) belong to the xLODGen/F4LODGEN analysis, not this workspace.
