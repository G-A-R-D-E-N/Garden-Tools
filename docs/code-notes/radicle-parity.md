# Radicle parity (GardenEngine #114)

## C# FormKey identity

`Radicle/bindings/csharp/Radicle.Interop/Core.cs:FormKey` compares the plugin name case-insensitively. That matches native `Radicle/src/plugin_key.cpp:operator==` and Mutagen's `ModKey.Equals`. `RecordInfo.FormKey` spells the owner the way the holding plugin's master list does. For example, `strong_PA - tAE.esp` lists `Strong_PA.esp`, so the same record came back as two distinct keys before this fix. The Fallen World MO2 parity dump listed five `strong_PA.esp` keys twice. `Radicle/bindings/csharp/Radicle.Interop.Tests/EnvironmentTests.cs:FormKeyEqualityIgnoresPluginCase` is the regression. `GetHashCode` hashes a null `Plugin` (a `default(FormKey)`) as 0: `StringComparer.OrdinalIgnoreCase.GetHashCode(null)` throws `ArgumentNullException`, so a default key in a `HashSet` or `Dictionary` threw before. Regression: `EnvironmentTests.FormKeyEqualityIgnoresPluginCase`.

## Master indices past the master table

Source: TES5Edit dev-4.1.6 `Core/wbImplementation.pas`, `PluginEditTool/Mutagen/Mutagen.Bethesda.Core/Plugins/Masters/SeparatedMasterPackage.cs`, and raw scans of the shipped masters (2026-09-27).

`Radicle/src/environment/environment.cpp:Environment::resolve_link` maps a link field whose master index is past the plugin's master table to the plugin itself. That matches xEdit's `TwbFile.FileFileIDtoLoadOrderFileID` (TES5Edit dev-4.1.6, `Core/wbImplementation.pas`) and Mutagen's `SeparatedMasterPackage.GetFormKey`. Bethesda ships such links: DLCNukaWorld.esm has 1 master, but REFRs 036900 and 049066 carry `XLYR 02006FCB` (verified 2026-09-27 by a raw subrecord scan of the shipped DLCNukaWorld.esm; the xEdit and Mutagen behavior was verified by reading those functions' source the same day). When the target does not exist, the link is dangling, the same as xEdit's `<Error: Could not be resolved>`. The strict `resolve_plugin_form_id` is still used for writing and mutation.

`Radicle/src/environment/environment.cpp:Environment::index_group` rejects the load when a record's own header form ID has such an index. The reader leaves those records without a key, and the environment previously dropped them silently. The trigger was a locally corrupted `ImprovedInstITO2Patch.esp`: two masters were deleted from its header without renumbering its FormIDs. Regressions: `Radicle/tests/test_environment.cpp:RejectsRecordWhoseMasterIndexIsPastTheMasterTable` and `Radicle/tests/test_environment_placed.cpp:LinkPastTheMasterTableResolvesToTheOwningPlugin`. On the 644-plugin Fallen World parity run, all 67,685 cells and 2,342,784 placed references now match Mutagen.

## Parity dump tool

`Radicle/tools/parity/main.cpp:run` (`radicle_parity_dump <versions|cells|world> <order.tsv> <out-dir>`, `names <order.tsv> <out-dir> <data-dir>`, `ba2 <archives.txt> <out-dir> [max-mb]`, `ba2x <archive.ba2> <entry> <out-file>`) is Radicle's side of the GardenEngine #114 parity checks. It loads a resolved MO2 order (`name<TAB>path` per line, lowest priority first) and writes sorted TSV files. The rows are shaped like GardenEngine's consumers read them:
- plugins and record version chains (GameLoader)
- merged cell children with base, transform, flags, enable parent and model (CellService)
- worldspace info, grid-keyed exterior cells and winning LAND layers (WorldspaceService, TerrainService)

Floats are written as IEEE-754 bit patterns and byte grids as FNV-1a 64 hashes, so a reference capture from Mutagen can be diffed exactly.

`Radicle/tools/parity/main.cpp:dump_world` divides `WorldspaceData::bounds_min`/`bounds_max` by 4096. Radicle keeps NAM0/NAM9 as stored (world units). xEdit (`wbWorldObjectBounds`, scale 1/4096) and Mutagen (`Worldspace.FillBinaryObjectBoundsMinCustom`) present cells, and GardenEngine's `WorldspaceService.GetWorldspaceInfo` consumes cells. The migrated consumer must apply the same division.

Result on the Fallen World profile, 644 plugins (excluding the corrupted `ImprovedInstITO2Patch.esp`), 2026-09-27:
- Every row matches Mutagen for plugins, 2,913,412 version chains, 67,685 cells, 2,342,784 placed references, 19 worldspaces, 65,911 exterior grid cells and 47,872 landscapes.
- Two chains differ: Mutagen invents DLCRobot.esm versions of quests `02058E` and `022A07` from quest-children groups that have no QUST record in that plugin. Radicle follows the bytes.

## Record names skip object-template combinations

`Radicle/src/environment/environment.cpp:decode_record_name` returns the first FULL outside an `OBTE` … `STOP` span. xEdit's `wbObjectTemplate` (source:wbDefinitionsFO4.pas at TES5Edit 9fb016884bec; used by ARMO, FURN, NPC_ and WEAP) repeats `OBTF`/`FULL`/`OBTS` per combination, and those FULLs name combinations, not the record. Taking the first FULL anywhere named 135 NPC_ and 28 ARMO/NPC_/WEAP winners after a template, for example DLCRobot.esm `001EF7`. Regression: `Radicle/tests/test_environment.cpp:NameIgnoresObjectTemplateCombinationFull`.

## Name parity

`Radicle/tools/parity/main.cpp:dump_names` writes each winner's name, resolved through `Radicle/src/environment/strings.cpp:StringTables::resolve_name` with the Data folder's Strings directory and archives, as a length and FNV-1a hash of the stored cp1252 bytes. On the 644-plugin Fallen World order (2026-09-27), all 71,091 names GardenEngine reads match Mutagen, including 570 map-marker names. Only the 7 official masters are localized there, with tables in their own BA2s. The remaining lines are Mutagen behavior:
- RFGP, MATT and MOVT expose NNAM/MNAM as `INamedGetter.Name`. That is not FULL.
- `INamedGetter.Name` is empty for KEYM and FLOR records even though `IKeyGetter.Name` holds the FULL text (for example CWServRifle.esp `00012E`). GardenEngine's base-name path would lose those names; Radicle keeps them.

## BA2 parity

`Radicle/tools/parity/main.cpp:dump_ba2` lists every entry of every archive in a list. For archives up to a size cap, it also hashes each extracted payload with `Radicle/src/archive/ba2_reader.cpp:Ba2Reader::extract`. For DDS output, `dds_payload` records the format as GardenEngine's `DdsImage.TryFormat` maps it, plus a hash of the bytes after the header.

Fallen World modlist results, 2026-09-27: 414 archives, 99 GB, covering GNRL and DX10 in versions 1, 7 and 8.
- Radicle opens all 414. Mutagen's `Archive.CreateReader` throws on 109 of them (81 GNRL v1, 26 DX10 v8, 2 GNRL v8), so GardenEngine cannot list or extract those today.
- Paths: all 832,552 entries of archives both sides open match, except three `Fallout4 - Voices.ba2` names with cp1252 accents. Mutagen decodes those to U+FFFD; Radicle keeps the stored bytes.
- Content (78,374 entries in 345 archives up to 300 MB): 70,179 are byte-identical, and 77,251 decode to the same format and pixel bytes. The rest are rebuilt-header choices:
  - Radicle writes a DX10 header with the archive's DXGI code where Mutagen writes a legacy FourCC or RGB-mask header.
  - For DXGI 88 (B8G8R8X8, 15 textures) Mutagen's header claims an alpha channel. Radicle's keeps the padding meaning.
  - Checked against Archive2 1.1.0.4 on `cw10mmsmg - Textures.ba2`: Radicle's BC7 header is byte-identical to Archive2, and Mutagen sets extra caps bits. For BC5, Archive2 writes FourCC `BC5U` where Radicle writes DXGI 83. GardenEngine reads both.
- Known divergence, not fixed: `Radicle/src/archive/ba2_texture.cpp:extract_ba2_texture_entry` rebuilds a header only for `.dds` names, by design (pinned by `Radicle/tests/test_ba2_texture.cpp:ExtractConcatenatesNonTextureChunks`). Archive2 and Mutagen rebuild one for any DX10 entry. The only affected entry is `HaxRPG7 - Textures.ba2` `textures/weapons/rpg-7/body_d.dds_backup`, which GardenEngine never requests.

## Writeback parity

`Radicle/tools/parity/writeback.cpp:apply` repeats GardenEngine's `ReferenceWriteback.Commit` through the C ABI, the path the migrated consumer will call. It creates `OpenCommonwealth.esp`, copies each placed reference as an override, and sets the edited fields: transform, scale, EditorID, record flags, and, for REFR, comments, location, ownership, enable parent, linked refs, teleport, primitive and activate-parent delay. `pick` chooses Fallout4.esm references that carry each optional field, and `dump` reloads a saved plugin over the masters and prints the decoded overrides.

A Mutagen capture of the same edits, written with GardenEngine's logic, decodes identically on all 10 references (2 ACHR, 8 REFR) from the 2026-09-27 run. One difference remains: a teleport rotation component stored as -0.0. Radicle leaves it untouched; Mutagen rewrites it as +0.0. Mutagen also reads Radicle's plugin back as the winning override. The edit uses stored XPRM bounds + 1 on the Radicle side and + 2 on the Mutagen side, because Mutagen doubles XPRM bounds on read (`Radicle/include/fo4/environment/placed.hpp:Primitive`).

## Parity tool arguments, paths and errors

`Radicle/tools/parity/common.hpp:guarded_main` is the entry point of both tools: `wmain` on Windows, so arguments arrive as UTF-16 paths, and a catch that prints `error: ` and returns 1. `read_order` builds each path from the order file's bytes as UTF-8 (`utf8_path`), and `Radicle/tools/parity/writeback.cpp:apply` passes paths to the C ABI through `fo4::io::utf8`, which `Radicle/src/c_api.cpp:utf8_path` expects. Before, the tools used `char** argv` and `std::string` paths, which Windows decodes in the ANSI code page, and `apply` handed `path::string()` to the C ABI. With a Data folder named `parity-Žluť-ж`, `apply` aborted with 0xC0000409: `ж` has no ANSI form, so `string()` threw and nothing caught it. Malformed edits (`FormKey::parse`) and `ba2x` with three arguments (null `argv[4]` into `ofstream`) also aborted. Regression: `Radicle/tools/parity/parity_tools.cmake` (ctest `radicle_parity_tools`), which failed before the fix.

`Radicle/tools/parity/common.hpp:dds_payload` returns `-` when a DDS is shorter than its header. A 128 to 147-byte entry with a DX10 FourCC used to take `subspan(148)` past the end and hash out-of-bounds memory. Regression: `Radicle/tests/test_parity_dds.cpp:Dx10HeaderLongerThanTheFileIsNotAPayload`, which crashed with 0xC0000005 before the fix.

`Radicle/tools/parity/writeback.cpp:read_edits` and `Radicle/tools/parity/main.cpp:dump_ba2` strip a trailing CR, the same as `read_order`. `dump_ba2` used to keep it, so every archive in a CRLF list failed to open with the CR in its path; `radicle_parity_tools` checks the `ba2_index.tsv` name column.

## Parity report integration

`Radicle/docs/FO4_PARITY.md:Baseline and legend` and `Radicle/docs/FO4_PARITY.md:Historical native writer branch delivery (2026-10-07)` preserve the 2026-10-05 inventory and 2026-10-07 delivery evidence as historical snapshots. The documentation changes add no stored-format behavior or public exports beyond main `c50ad4d5405e1571113019654b10e3d4a05ca0f1`, so this PR retains that main's ABI 11. Exact-head validation is recorded on PR #424; historical green runs do not establish readiness of subsequently updated branches or resolve #428's LTEX ownership conflict. Full field/helper parity and issue #287 remain open.
