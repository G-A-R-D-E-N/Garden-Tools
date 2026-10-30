# Radicle: plugins, records, schema and checks

This page is for developers new to the project. It explains how Radicle reads, edits and writes Fallout 4 plugin files, how the record schema turns into C++ code, and how the Problems checks work.

Related pages: Architecture, Repo-Map, Roadmap, and the Papyrus pages. This page does not repeat them.

All paths below are relative to the repo root. Source: branch `nomad/dev`.

## 1. Fallout 4 terms

| Term | Meaning |
|---|---|
| Plugin | An `.esp`, `.esm` or `.esl` file. It is one `TES4` header record, then top-level `GRUP`s, then nested groups and records (`Radicle/docs/ARCHITECTURE.md`). |
| Record | One game object (a weapon, an NPC, a quest). It has a 4-letter type such as `WEAP`, a header with flags and a FormID, and a list of subrecords. |
| Subrecord | A named chunk inside a record, such as `EDID` (editor ID) or `KWDA` (keyword list). Unknown subrecords are kept as raw bytes. |
| Group (`GRUP`) | A container of records and other groups. |
| FormID | A 32-bit ID. The high byte means something different on disk and at run time (see section 2). |
| Master | A plugin that another plugin depends on. They are listed in the `MAST` entries of the `TES4` header. |
| Light plugin | A plugin with the `SmallFile` flag (TES4 bit 9) or an `.esl` name. |

Sources: `Radicle/docs/ARCHITECTURE.md`, `Radicle/docs/FORMID_MODEL.md`, `Radicle/include/fo4/plugin/container.hpp`.

## 2. How Radicle models them

The C++ types are in `Radicle/include/fo4/plugin/container.hpp`.

| Type | What it holds |
|---|---|
| `Plugin` | `path`, `key`, the `tes4` record, a parsed `PluginHeader`, the top-level `groups`, and the original bytes. |
| `PluginHeader` | `version`, `record_count`, `next_form_id`, `author`, `description`, `masters`, `overrides`, and TES4 flags. |
| `Group` | A header and `children`. A child is a `Record` or another `Group`. |
| `Record` | `header`, `form_key`, `subrecords`, and the byte range it came from. It also has `compressed` and `undecodable` flags. |
| `Subrecord` | `signature`, `data` bytes, and an `extended_size` flag (for the `XXXX` size marker). |

### Shared source buffer

A plugin owns one immutable shared source buffer. Each record keeps only an offset and length into it (`Radicle/docs/ARCHITECTURE.md`, `Radicle/docs/PARSE_MEMORY.md`).

`PARSE_MEMORY.md` measured `Fallout4.esm` (330,776,576 bytes). Peak RSS dropped from 4,183,412 KiB to 1,921,528 KiB, and parse time from 5,138 ms to 4,257 ms. The measured machine and build are in that file.

### FormKey, not raw FormIDs

Rule from `Radicle/docs/FORMID_MODEL.md`: never treat a raw load-order-dependent FormID as the identity.

- `RawFormId` is the 32-bit value as stored.
- `FormKey` is `{ owner PluginKey, local LocalFormId }`, for example `00009A:RadiationOverhaul.esp`.
- Raw values exist only at the plugin-bytes boundary.

On disk:

- The high byte is an index into that file's `MAST` list.
- A high byte equal to the master count means "owned by this file".
- Light masters still use the `MAST` ordinal on disk, not `0xFE`.

At run time (CommonLibF4 layout, OG 1.10.163):

- Full plugin: `(compileIndex << 24) | local24`.
- Light plugin: `0xFE000000 | (smallFileCompileIndex << 12) | local12`.

Functions: `resolve_plugin_form_id`, `encode_plugin_form_id`, `resolve_runtime_form_id` (`FORMID_MODEL.md`). When a master is removed, Radicle resolves with the old master table and encodes with the new one. It never subtracts from the high byte.

### Light plugin ID range

`Radicle/docs/ESL_FORMIDS.md`: a light plugin's own local IDs are `0x001` to `0xFFF`. IDs below `0x800` need the xEdit rule `GetAllowHardcodedRangeUse` (HEDR at least 1.0 and at least one master). Otherwise the range is `0x800` to `0xFFF`. The old "2048 records" idea is wrong.

`OPEN_QUESTIONS.md` item 4 notes that six of nine shipped ESLs allocate below `0x800`. It also notes that `ccFSVFO4007-Halloween.esl` has 2078 own records.

## 3. Load, edit, save

### Load

Declared in `Radicle/include/fo4/plugin/reader.hpp`:

- `read_plugin(bytes, path)` and `read_plugin_file(path)`.
- `collect_records`, `collect_first_records`, `collect_groups`, `find_record`.
- `plugin_form_id_context`, `assign_record_form_keys`, `apply_master_kinds`, `stamp_master_kinds`.

Notes:

- Compressed records are inflated once while parsing. The compressed bytes stay in the source buffer (`ARCHITECTURE.md`).
- A compressed record that cannot be decoded is loaded as `undecodable` with raw bytes kept. `require_decodable` (in `container.hpp`) throws `std::invalid_argument` on any edit of it. Whole-plugin operations such as `delete_local_record` and `renumber_local_record` also refuse while any record is undecodable (`Radicle/docs/code-notes/Radicle.md`, GT-205 PR2 entry).
- Light-plugin test: `src/formid.cpp:classify_plugin_kind` is the single test. It uses the TES4 SmallFile flag or a case-insensitive `.esl` name (`docs/code-notes/Radicle.md`).

### Load order files

`Radicle/docs/LOAD_ORDER_FILES.md`. Radicle reads `plugins.txt` or `loadorder.txt`, and `Fallout4.ccc`. It then builds the loaded-file list the engine would build. The code is in `Radicle/src/loadorder/`. Headers are in `Radicle/include/fo4/loadorder/load_order_files.hpp`. The C functions are `radicle_plugin_list_read`, `radicle_ccc_read` and `radicle_load_order_assemble`. The C# wrapper is `Radicle/bindings/csharp/Radicle.Interop/LoadOrder.cs`.

Order of assembly (stage two), from the doc:

1. Official master names present.
2. `Fallout4.ccc` names present.
3. Master-flag files named in the plugin list whose masters are all present.
4. Remaining master-flag files.
5. Non-master files named in the plugin list.
6. Remaining non-master files.

A master hoist then moves masters ahead of their dependents when there is no cycle. Evidence status per game build (OG, NG, AE) is in that doc's table.

### Edit

Edit APIs live in `Radicle/include/fo4/plugin/` (`mutation.hpp`, `field_writer` code in `Radicle/src/plugin/`, `subrecord_authoring.hpp`, `subrecord_copy.hpp`, `vmad_writer.hpp`, `placement.hpp`, `maintenance.hpp`). Typed record families are in `Radicle/include/fo4/forms/` (section 5).

Scalar edits (`Radicle/docs/SCALAR_SCHEMA.md`):

- Existing bytes are decoded by storage width only.
- Range checks apply only when writing a new value. Radicle must still read and keep unusual authored data.
- Integer-backed percent fields reject fractional input.
- The generic scalar edit changes only a subrecord that already exists. It does not insert a missing one, because insertion order belongs to the record schema layer.
- Duplicate scalar subrecords and unexpected widths fail closed.

Localized strings: `Radicle/docs/LOCALIZED_STRINGS.md` covers `.STRINGS`, `.DLSTRINGS` and `.ILSTRINGS` files via `fo4/localization/strings.hpp`. TES4 bit `0x80` selects external string IDs (`OPEN_QUESTIONS.md` item 7).

### Save

Declared in `Radicle/include/fo4/plugin/writer.hpp`: `write_plugin`, `write_plugin_file`, plus helpers such as `set_plugin_light_flag`, `set_plugin_master_flag`, `set_plugin_localized_flag`, `remap_plugin_masters`, `sort_plugin_masters`, `make_empty_plugin`.

Rules:

- A clean plugin (its original byte range is non-empty) is written back unchanged (`Radicle/docs/HEDR_RECORD_COUNT.md`).
- A record that was not edited keeps its shared source range (`OPEN_QUESTIONS.md` item 23, default).
- When `TES4` is dirty or the structure changes, `HEDR` record count is rebuilt. The count is every non-TES4 major record plus every `GRUP`, nested ones included (`HEDR_RECORD_COUNT.md`). The oracle is xEdit's save path.
- `write_plugin` rejects duplicate, missing or wrong-width `HEDR`, and does not change the const input plugin.
- Link fields are re-encoded from `FormKey`s when masters change (`Radicle/docs/TEST_STRATEGY.md`: KWDA, NAME, CTDA, VMAD, TES4 `ONAM`).

Open question: `HEDR_RECORD_COUNT.md` says the stored count in `Fallout4.esm` (1,741,853) differs from the recursive count (1,661,657), and the reason is not established.

## 4. Schema and code generation

### The flow

```text
xEdit definitions (pinned commit 93cc0bc5...)
        |  copied by hand into
Radicle/schema/fallout4/fields.json
        |  Radicle/tools/*_codegen.py
Radicle/include/fo4/schema/generated_*.inc   (committed)
        |  included by
Radicle/src/schema/*.cpp
```

- The pinned xEdit commit is `93cc0bc5a1251936c3c7859eee3150eda12a62d7` (`Radicle/docs/SCALAR_SCHEMA.md`). Its definitions are an oracle, not vendored. Only signatures are vendored in `Radicle/schema/fallout4/xedit_signatures.txt`.
- `fields.json` has the top-level keys `version`, `records`, `keyword_records`, `target_unions` and `condition_scopes`. It has 145 `records` entries.
- Row kinds the codegen accepts: `scalar` and `string`, plus counted arrays and subrecord groups (`Radicle/tools/schema_codegen.py`).
- Scalar storage types: `u8 u16 u32 i32 f32 i8 i16`. Presentations: `identity`, `percent_0_100`, `enum`, `flags`.
- `enum` and `flags` rows carry xEdit `names`. The generator rejects duplicate names, bad bits and out-of-range values (`SCALAR_SCHEMA.md`).
- Missing-field insertion is not generated yet. Only `existing_only` exists (`schema_codegen.py`, `INSERTIONS`).

### Generators

`Radicle/CMakeLists.txt` defines a `*_check ALL` target for each generator. Each runs the script with `--check`, so a stale committed output fails the build.

| Script (`Radicle/tools/`) | Output (`Radicle/include/fo4/schema/`) |
|---|---|
| `schema_codegen.py` | `generated_scalar_fields*.inc` |
| `record_schema_codegen.py` | `generated_record_constraints*.inc` (authoring rules) |
| `lstring_schema_codegen.py` | `generated_lstring_fields.inc` |
| `formlink_schema_codegen.py` | `generated_formlink_lists*.inc` |
| `condition_scope_codegen.py` | `generated_condition_scopes.inc` |
| `condition_function_codegen.py` | `generated_condition_functions.inc` (input: `schema/fallout4/condition_functions.json`) |
| `hkx_layout_codegen.py` | Havok layout (not covered here) |
| `parity_matrix.py` | the generated table in `docs/FO4_PARITY.md` |

Large outputs are split into `_shardNN.inc` files by the generators.

### Hand-kept FormLink tables

`Radicle/schema/fallout4/README.md` says the schema holds FormLink locations, not full record layouts.

- 179 four-byte FormID subrecords are listed by hand in `include/fo4/schema/formid_subrecords.inc`. The list is not complete.
- Arrays: `KWDA`, `SPLO`, TES4 `ONAM`, `XLKR`, `APPR`.
- Structured payloads are in `structured_formids.inc` (for example `CNTO`, `LVLO`, `XTEL`).
- `CTDA` function table: `ctda_functions.inc`. Use Global (type bit 2) remaps the compare value. Run On = Reference remaps offset 24.
- Rule from `FORM_LINK_EVIDENCE.md`: Radicle rewrites a mixed payload only when a definition names the exact FormID slot. A value that only looks like a FormID is left alone.
- README rule: do not add handwritten WEAP/ARMO classes. Grow the tables instead.

### Runtime code that uses the schema

`Radicle/src/schema/` holds `scalar.cpp`, `form_links.cpp`, `formlink_lists.cpp`, `form_link_mixed.cpp`, `ctda.cpp`, `vmad.cpp`, `localized_fields.cpp`, `preflight.cpp`, and others. Public headers are in `Radicle/include/fo4/schema/`.

To change a field: edit `fields.json`, regenerate with the matching script (`--output ...` without `--check`), commit the `.inc` files. Unverified: the exact regenerate command line. The CMake targets only show the `--check` form.

## 5. Record support

How tiers are derived: `Radicle/tools/parity_matrix.py` writes a table into `Radicle/docs/FO4_PARITY.md` for 147 signatures from `xedit_signatures.txt`.

| Tier | Meaning (from `FO4_PARITY.md`) | Count |
|---|---|---|
| L0 | none of the below | 2 (`BPTD`, `TES4`) |
| L1 | keyword list only | 0 |
| L2 | typed module, no rows | 1 (`LAND`) |
| L3 | FormLink list rows only | 0 |
| L4 | has field rows, FormLink fields or authoring constraints | 144 |

The doc says no record row is "FULL". Tiers measure schema and typed-module presence. They are not a claim of full xEdit parity (`FO4_PARITY.md`, "Record inventory").

### First-class typed modules

From the "Typed module" column of `FO4_PARITY.md` (the doc says the script only checks that a file names the signature):

| Records | Module (`Radicle/src/...`) |
|---|---|
| `ACHR`, `REFR` | `forms/placed_reference*.cpp` |
| `ARMO`, `ARMA` | `forms/armor.cpp`, `forms/armor_edit.cpp` |
| `CELL` | `forms/cell.cpp`, `environment/world` |
| `COBJ`, `MISC` | `forms/components.cpp` |
| `EXPL`, `HAZD`, `PROJ` | `forms/proj_expl_hazd.cpp` |
| `EYES` | `forms/eyes.cpp` |
| `FACT` | `forms/faction.cpp` |
| `INFO` | `forms/info_copy.cpp`, `environment/dialogue` |
| `LVLI`, `LVLN`, `LVSP` | `forms/leveled_list.cpp`, `forms/leveled_spell.cpp` |
| `NPC_` | `forms/npc.cpp` |
| `QUST` | `forms/quest_*.cpp` (aliases, objectives, stages, VMAD) |
| `RACE` | `forms/race.cpp`, `forms/race_body.cpp` |
| `SCOL` | `forms/static_collection.cpp` |
| `WEAP` | `forms/weapon.cpp` |
| `WTHR` | `forms/weather.cpp`, `environment/base_records` |
| `ACTI` | `environment/base_records` |

Other headers exist in `Radicle/include/fo4/forms/` (`omod.hpp`, `perk.hpp`). `omod.cpp` and `perk.cpp` are in `Radicle/src/forms/`. They are not in the parity table's typed-module column, so their status is unverified here.

Per-family notes live in `Radicle/docs/`: `LEVELED_LISTS.md`, `NPCS.md`, `FACTIONS.md`, `QUEST_ALIASES.md`, `VMAD.md`, `WEAPONS.md`, `DIALOGUE_SCENES.md`, `NAVM_NAVI.md`, `WTHR_*.md`.

### Schema-only handling (the generic path)

Every other signature in `fields.json` has scalar, string and FormLink rows and authoring rules, but no dedicated C++ module. Examples: `AACT`, `ADDN`, `AMMO`, `ALCH`, `BOOK`, `KYWD`, `GLOB`, `GMST`, `FLST`. Edits go through the generic schema-driven API. Fields not in the schema stay opaque bytes.

`FO4_PARITY.md` also keeps a hand-kept status table with letters READ, WRITE, ADD, REMOVE, CREATE, REMAP, VALIDATE and OPAQUE. It is a 2026-10-05 snapshot. Many rows are `U` (unaudited), which the doc says does not mean unsupported.

### VMAD (script data on records)

`Radicle/docs/VMAD.md` summary:

- Read: script names, flags and known scalar values, array and struct counts, quest fragment and alias views.
- Write: existing object, string, int, float and bool values.
- Add and remove: leading scripts and properties, and fragment bindings on QUST, PACK, INFO, SCEN, PERK, TERM (details in the doc).
- Opaque: variable type 6, non-empty type 16, nested struct members, and unknown versions or tails.
- Remap: complete grammar only; unsupported properties fail closed.
- VMAD string values are UTF-8 bytes. Script and property names are Windows-1252 (`docs/code-notes/Radicle.md`).

### Quest aliases

`QUEST_ALIASES.md`: aliases are a repeated, discriminated container, not a map keyed by signature. A QUST can hold many reference, location and collection aliases.

## 6. Checks (the Problems list)

### Where they run

| Layer | File |
|---|---|
| Core checks | `Radicle/src/graph/check_errors.cpp`, `Radicle/src/graph/check_errors_records.cpp` |
| Public header | `Radicle/include/fo4/graph/check_errors.hpp` |
| C ABI | `Radicle/src/c_api_check.cpp`, `radicle_env_check_errors` in `Radicle/include/fo4/c_api_env.h` |
| Preflight (schema validation) | `Radicle/src/schema/preflight.cpp`, `Radicle/include/fo4/schema/preflight.hpp` |
| PluginEditTool Problems drawer | `PluginEditTool/native/src/plugin/RadiclePluginAnalysis.cpp:getProblems` |

Entry point: `check_load_order(load_order, CheckOptions)`. `CheckOptions` has a `plugins` filter and a `limit`. A `CheckFinding` has `code`, `severity`, `plugin`, `record`, `subrecord`, `offset`, `target`, `message` and `detail`.

Per selected plugin, `check_load_order` runs in this order: link checks, dialogue link checks, schema preflight, record-level checks, condition checks. After the loop it runs the injected-record check and the leveled-list cycle check.

### Stable codes

`check_code_name` (`Radicle/src/graph/check_errors.cpp`) returns these strings:

| Code | Severity | What it detects |
|---|---|---|
| `invalid_master_index` | warning | A form ID whose master index does not resolve to any plugin. |
| `unresolved_link` | error, or warning if the slot is not proven by the schema | A link target not in any loaded plugin. Low IDs of `Fallout4.esm` under the hardcoded range are skipped. A small exemption list covers 7 game-master slots (`RFGP RNAM`, `REGN WNAM`, `REFR XRFG/XMSP/XLKR`, `ACHR XLCN`, `SCCO SNAM`), and `DLBR SNAM`. |
| `deleted_target` | warning | The link target is deleted in its winning version. |
| `wrong_target_type` | error | The target's record type is not one the slot allows. |
| `invalid_condition` | error | A `CTDA` with an unknown function index or an unknown Run On value. |
| `schema_preflight` | error or warning | Schema validation failures. The preflight code goes in `detail` (below). |
| `branch_starting_topic_not_dial` | error | A `DLBR` `SNAM` target is not a loaded `DIAL`. |
| `topic_children_not_dial` | warning | An `INFO` topic-children label is not a loaded `DIAL`. |
| `light_object_id_overflow` | error | A light plugin's own record has an ObjectID above the 12-bit mask. |
| `internal_form_id_mismatch` | warning | A record header master index is past the master count. |
| `deleted_record_content` | error | A record marked deleted still has content (placed references may keep a base). |
| `partial_form_content` | error | A Partial Form record has content beyond what is allowed. Partial form applies to `CELL`, `DIAL`, `QUST`, `WRLD` only (`container.hpp`). |
| `injected_record` | warning | A record sits in plugin B with a FormKey owned by plugin A, and A has no such record. |
| `circular_leveled_list` | error | A cycle in leveled lists (`LVLI`, `LVLN`, `LVSP`). The message is the chain `A -> B -> A`. |

The set of codes was verified from the `CheckCode` enum and `check_code_name`. The severities and conditions come from `check_errors.cpp` and `check_errors_records.cpp`.

### Preflight detail codes

Seen in `Radicle/src/schema/preflight.cpp` (this is a search result, not a full audit):

- Scalar storage: `duplicate_scalar_subrecord`, `invalid_scalar_storage`, `scalar_value_out_of_range`.
- Names: `scalar_enum_value_unnamed`, `scalar_flag_bits_unnamed`.
- FormLink lists: `invalid_formlink_list_storage`, `invalid_nested_formlink_list_storage`, `invalid_nested_formlink_terminator_storage`, `unterminated_nested_formlink_list`.
- Climate: `climate_sun_window_full_day`, `climate_sun_window_never_opens`.
- Authoring (`preflight_authoring_record`): `record_missing`, `authoring_schema_missing`, `record_placement_invalid`, `subrecord_order_invalid`, `required_subrecord_missing`, `subrecord_cardinality_invalid`, `subrecord_count_mismatch`.

Per `preflight.hpp`: a readable legacy value outside the write limit is a warning, so inspection stays lossless. Malformed or duplicate storage is an error. The authoring preflight is separate from the read preflight, so old files are not rejected for a different authoring shape.

### The Problems drawer (PluginEditTool)

`getProblems` in `PluginEditTool/native/src/plugin/RadiclePluginAnalysis.cpp` takes a FormKey and returns JSON rows. Every row has `Code`, `Detail`, `Severity` and `Description`.

| Source | Code | Detail | Severity | Description |
|---|---|---|---|---|
| Graph references from the winning record to a FormKey with no winner | `unresolved_link` | empty | `critical` | `Missing <SIG> FormLink target <key>` |
| `preflight_record` on that record | `schema_preflight` | preflight code | `critical` (error) or `warning` | `<path>: <message>` |

Notes:

- The drawer maps core severity `error` to the string `critical`. Core uses `error` and `warning` (`CheckSeverity`).
- `getProblems` is per record. It uses only these two sources, not the full `check_load_order` set. This is from reading the function, not from the PET UI.
- It returns `RecordNotFound` if no loaded plugin has the FormKey.
- Another file, `PluginEditTool/native/src/plugin/RadiclePluginCheck.cpp`, also uses `check_code_name`. Unverified: what it exposes.
- Unverified: issue #31 text itself. The row shape was verified in code.

Tests: `Radicle/tests/test_check_errors.cpp`, `Radicle/tests/test_check_errors_conditions.cpp`, `Radicle/tests/test_preflight.cpp`. The `fo4inspect` CLI (`Radicle/tools/inspect/main.cpp`) also calls `check_code_name`.

## 7. Parity testing

Policy: `Radicle/docs/TEST_STRATEGY.md` and `Radicle/docs/FO4_PARITY.md`.

- Mutagen and FO4IDE results are oracles. When they disagree with the game or Creation Kit, the difference is documented.
- xEdit (pinned commit) is the oracle for layouts and rebuild rules, such as the `HEDR` count and light ID range.
- Acceptance for an editable surface is: parse, expose, edit, serialize, reparse. Check edited values, valid add and remove, remap, rejected bad writes, untouched fields, and save and reload. A byte-identical round trip proves preservation only.
- Unknown layouts stay opaque or refuse mutation.
- Runtime RE is needed only for unresolved stored layouts, unproven safe mutation, or runtime behavior. Per the global rules, RE work goes to the RE machine as a request, not done here.

Test data:

- Committed real plugins: `Radicle/testdata/FW_HeatherSurvivalPatch.esp` and `SlowTime.esp`. Both are written back byte-identical (`TEST_STRATEGY.md`).
- Tests whose names end in `_real.cpp` read the game install through `FO4_DATA_DIR` and skip otherwise (77 files match `*real*` in `Radicle/tests`). Release detection reads `Fallout4.exe` (OG 1.10.163, NG 1.10.980 or 1.10.984, AE 1.11.x). `FO4_GAME_BUILD` overrides it.
- Counts for OG and NG are recorded, not asserted (`TEST_STRATEGY.md`).
- Python generator tests are registered in `Radicle/CMakeLists.txt` (for example `radicle_field_address_codegen`, `radicle_parity_matrix_unit`).
- `Radicle/tools/parity/` is a C++ tool and test dir (`main.cpp`, `writeback.cpp`, `tests/test_papyrus_dump.py`). Unverified: what the whole tool compares against.

Status warning: some docs mark their own real-data status as unproven. For example `LEVELED_LISTS.md` says "TODO-verify ... No pass result is recorded". Check CI before relying on a claim.

## 8. Known gaps

From `Radicle/docs/OPEN_QUESTIONS.md` unless noted.

| Area | Gap |
|---|---|
| ESL IDs | Own-record header encoding is confirmed on one sample (`ccBGSFO4096-AS_Enclave.esl`). Repeat on more ESLs before calling it universal. |
| Masters | Behavior with more than 254 masters is open (item 6). |
| Runtime index | Live runtime behavior is not tested in game. Radicle keeps caller order and safe caps. `RUNTIME_PLUGIN_INDEX_EVIDENCE.md`. |
| HEDR | Why `Fallout4.esm` stores a different count than the recursive one. `HEDR_RECORD_COUNT.md`. |
| Groups | Group type integers 0 to 10 are not confirmed against CK or runtime (item 11). Persistent vs temporary REFR grouping on save is open (item 21). |
| Chunks | Engine `GetChunkData` truncates oversize chunks. Radicle fails hard (item 10b). |
| Localization | `.STRINGS` layout for next-gen changes (item 15). CK table emission and language ownership (item 16). |
| VMAD | Variable authoring, non-empty type 16, type 6, third-party fixtures. Tracked as RE-005 on #520 (item 17, `VMAD_VARIABLE_EVIDENCE.md`). |
| CTDA | The library cannot read SmallFile from a master known only by its `MAST` name (item 18). |
| PERK | Add Activate Choice (item 19). |
| NAVM and NAVI | Reads are shipped. Creation and counted-structure edits are refused. Several flag meanings are inferred (items 20a and 20b, #401). |
| Dialogue | Action-specific scene payloads and VMAD fragments are opaque. Runtime response selection needs RE #405 (item 20c). |
| BA2 | Layouts and versions not in the measured install (item 22). |
| Builds | OG 1.10.163 and NG 1.10.984 are measured but not build-verified. `HeaderVersionsReal` and the BA2 checks have not run there (item 24). |
| Writer policy | Whether an unmodified rewrite should match CK or the original bytes. Default keeps the source range (item 23). |
| Parity | `FO4_PARITY.md` says full FO4Edit parity is not established and #287 is not closed. Many hand-kept status cells are `U`. |
| Insertion | Scalar edits never insert a missing subrecord (`SCALAR_SCHEMA.md`). |
| Docs | `FO4_PARITY.md` sections other than the generated table are snapshots from 2026-10-05 and may be stale. |

## 9. Dependencies and guards

- Do not hand-edit generated `.inc` files. The `--check` targets fail when they differ from generator output (`Radicle/CMakeLists.txt`).
- Keep files under the 750-line source limit (see `docs/code-notes/GARDEN-TOOLS.md`); split by responsibility and add new translation units to `Radicle/CMakeLists.txt`.
- Put new tests in the existing `Radicle/tests` suite (`AGENTS.md`).
