# Repo Map

A navigable map of the GARDEN-TOOLS repository: what each part does, where its code lives, how it builds and tests, and what to watch out for. Snapshot: `nomad/dev` 9d6c622c (2026-10-12, the head of PR #504). Every path, target and constant was read from the tree at that commit. Trust current code over this page where they differ, and refresh the page after large merges.

Sections: [Radicle core library](#radicle-core-library), [Radicle tests, probes, C# bindings and docs](#radicle-tests-probes-c-bindings-and-docs), [PluginEditTool](#pluginedittool), [BGS, CI and repo level](#bgs-ci-and-repo-level)

## Radicle core library
Scope: `Radicle/` minus `tests/`, `probes/`, `testdata/`, `bindings/`, `docs/` (docs only indexed at the end). Snapshot: nomad/dev 9d6c622c. Paths starting `include/`, `src/`, `tools/`, `schema/` are under `Radicle/`; others are relative to the repo root `GARDEN-TOOLS/`.

Note on game-format claims: statements about Fallout 4 formats (light plugin ID range, release builds, xEdit pins) are quoted from the repo's own docs with the doc named; not re-verified by RE for this map.

### 1. Purpose

Native Fallout 4 plugin SDK, "xEdit-style" backend. C++ namespace is still `fo4` (CMake project `fo4sdk`, `Radicle/CMakeLists.txt:3`). It parses ESM/ESP/ESL into a lossless container, exposes decoded record views, edits fields the docs call proven, and re-serialises while preserving untouched bytes. `Radicle/README.md` says Mutagen is only a compatibility oracle.

Identity rule: raw load-order FormIDs are never identity; `fo4::FormKey` (`LocalFormId` + owning `PluginKey`, text like `00009A:Plugin.esp`) is. On write the destination plugin's master table rebuilds the raw ID (`include/fo4/formid.hpp`, `docs/FORMID_MODEL.md`).

Consumers:
- PluginEditTool (PET) native C++ links the static lib `fo4` directly (`PluginEditTool/native/src/CMakeLists.txt:54`). It builds Radicle by `add_subdirectory` (`PluginEditTool/native/CMakeLists.txt:53-73`), forces `RADICLE_INSTALL OFF` and unsets `radicle_c` VERSION/SOVERSION. So typed `fo4::forms::*` writers are reached by PET in-process, not through the C ABI.
- GardenEngine and managed code use the shared lib `radicle_c` (`radicle.dll` / `libradicle.so`) through the C# wrapper `Radicle/bindings/csharp/Radicle.Interop` (see the tests and bindings section).
- CI: `.github/workflows/native-ci.yml` jobs `native-linux`, `radicle-windows-cross` (clang-cl/xwin, build-only), `plugineditool-native-windows-cross`, `native-windows-tests`, `native-windows-real-data`. Radicle's own tests run inside the PET configure (`-DRADICLE_BUILD_TESTS=ON -DRADICLE_BUILD_TOOLS=ON -DRADICLE_WARNINGS_AS_ERRORS=ON -DRADICLE_CSHARP_TESTS=ON`, native-ci.yml ~L97-110).

### 2. Layout

```
Radicle/
  CMakeLists.txt          targets fo4 (static), radicle_c (shared), tools, tests, schema --check targets
  README.md ROADMAP.md    status + fo4inspect usage; ROADMAP phases are "historical", live coverage = docs/FO4_PARITY.md
  cmake/radicleConfig.cmake.in   find_package(radicle) config (radicle::fo4, radicle::radicle_c); only if RADICLE_INSTALL
  include/fo4/            public headers; one dir per src module + 17 c_api*.h
  src/                    implementation, same module dirs; c_api*.cpp at top level
  schema/fallout4/        fields.json (generator input) + README.md
  tools/                  codegen .py, fo4inspect, parity dump, env bench, corpus scripts
  docs/                   (not mapped here) evidence docs, code-notes/Radicle.md
  bindings/ tests/ probes/ testdata/   (see the tests and bindings section)
```

`src/` module dirs:
- `src/plugin/` container I/O, writer, mutation, authoring (3.1, 3.2)
- `src/schema/` FormLink/scalar/VMAD/CTDA schema engine (3.3)
- `src/forms/` typed record models + editors (3.4)
- `src/environment/` load-order Environment + read-only decoders (3.5)
- `src/graph/`, `src/archive/` (BA2 + assets), `src/nif/`, `src/pex/`, `src/seq/`, `src/material/`, `src/localization/`, `src/loadorder/`, `src/quest/`, `src/io/`, `src/text/`, `src/binary/` (3.6)
- `src/c_api*.cpp` C ABI (4.1); `src/formid.cpp`, `src/plugin_key.cpp` identity types.
- Placeholders: `src/records/` holds only `.gitkeep`; `tools/diff/` is empty. `docs/ARCHITECTURE.md` still says BA2 is a non-goal, `src/forms` is "FormKey resolve/encode" and `schema/fallout4` is "empty until reviewed": stale, trust the code.

### 3. Key modules

#### 3.1 Container, reader, writer (`include/fo4/plugin/`, `src/plugin/`, `src/binary/`)
- `container.hpp`: `Plugin` (TES4 `Record`, top `Group`s, `PluginHeader`, immutable shared source buffer + `OriginalByteRange`), `Record`, `Subrecord`, `Group`, `MasterReference`; `require_decodable`, `is_partial_form`; `plugin_original_bytes`, `record_original_bytes`.
- `reader.hpp` / `reader.cpp`: `read_plugin(bytes, path)`, `read_plugin_file`, `collect_records`, `find_record`, `RecordIndex`, `plugin_form_id_context`, `assign_record_form_keys`, `apply_master_kinds`, `stamp_master_kinds`, `parse_tes4_header`. Guards: `k_max_record_uncompressed_bytes` = 1 GiB, `k_max_group_depth` = 16 (`reader.cpp:21-22`); zlib inflate once, XXXX large-subrecord marker preserved. `binary/cursor.hpp` is the bounds-checked LE cursor; malformed input throws `fo4::parse_error` (`error.hpp`, carries byte offset).
- `writer.hpp` / `writer.cpp`: `write_plugin` (clean plugin returns the original range unchanged; dirty groups rebuilt reusing untouched record ranges), `write_plugin_file`, `remap_plugin_masters`, `sort_plugin_masters`, `make_empty_plugin`, `apply_extension_flags`. HEDR record count rebuilt (`docs/HEDR_RECORD_COUNT.md`).
- `flags.cpp` `set_plugin_light_flag`; `master_kinds.cpp` `stamp_master_kinds_from_files`; `load_order.cpp` `build_runtime_form_id_context`; `tes4.cpp` TES4/MAST helpers; `onam.cpp` `rebuild_tes4_onam` (`docs/ONAM.md`); `placement.cpp` `record_placements`; `visit.hpp` `visit_records/visit_groups` templates.
- `io/atomic_write.hpp`: `StagedFile`, `write_file_atomic`, `write_file_pair_dependency_first` (dependency string table committed first; header says explicitly not two-file atomic). `c_api.h` documents that `radicle_plugin_write_file` stages in a sibling temp file and renames.

#### 3.2 Write operations and owners
| Operation | Function | File |
|---|---|---|
| Serialise | `write_plugin`, `write_plugin_file` | `src/plugin/writer.cpp` |
| Master remap / sort / empty plugin | `remap_plugin_masters`, `sort_plugin_masters`, `make_empty_plugin` | `src/plugin/writer.cpp` |
| FormLink rewrite on remap | `remap_form_links`, `remap_record_form_links` | `src/schema/form_links.cpp` |
| Rekey (renumber target) | `rekey_form_links` (transactional, strict) | `src/schema/form_links.cpp`; called at `mutation.cpp:257,325` |
| Light flag | `set_plugin_light_flag` | `src/plugin/flags.cpp` |
| Local FormID alloc / blank record | `reserve_local_form_key`, `create_local_record`, `set_hedr_next_form_id`, `local_form_id_range` | `src/plugin/mutation.cpp` |
| Delete record | `delete_local_record` (status `referenced` etc.), `records_removed_by_delete` | `mutation.cpp` |
| Renumber (ESL compaction primitive) | `renumber_local_record` | `mutation.cpp` |
| Copy as override | `copy_record_as_override`; nested containment `plan_nested_copy`/`insert_nested_copy` | `mutation.cpp`, `src/plugin/copy_hierarchy.cpp` |
| Bulk remove (ITM clean) | `remove_records` | `src/plugin/maintenance.cpp` |
| Scalar / string / FormLink field set | `set_record_scalar`, `set_record_string`, `set_record_formlink_field`, `record_field_count` | `src/plugin/field_writer.cpp` (+ overloads in `mutation.cpp`) |
| FormLink list edit | `insert/remove/set/clear_record_formlink_list_*`, `*_nested_*` | `src/schema/formlink_lists.cpp`, `nested_formlink_lists.cpp` |
| Conditions | `set_record_conditions` (inline in `mutation.hpp`), `set_record_scoped_conditions`, CTDA codec | `src/schema/ctda.cpp`, `condition_scopes.cpp` |
| Editor ID | `set_record_editor_id` | `mutation.cpp` |
| Add/remove subrecord, create valid record | `add_record_subrecord`, `remove_record_subrecord`, `create_record` (driven by `generated_record_constraints.inc`) | `src/plugin/subrecord_authoring.cpp` |
| VMAD property set | `set_vmad_property` | `src/plugin/vmad_writer.cpp` |
| Scene scalars / links | `set_scene_scalar`, `set_scene_formlink`, `scene_group_count` | `src/plugin/scene_writer.cpp` |
| NAVM vertex / triangle / door | `set_navmesh_vertex/triangle/door` | `src/plugin/navmesh_writer.cpp` |
| Placed REFR/ACHR edit + copy | `set_placed_reference_*`, `copy_placed_reference_as_new/_override` | `src/forms/placed_reference_edit.cpp`, `placed_reference_copy.cpp` |
| INFO copy | `copy_info_as_new` | `src/forms/info_copy.cpp` |
| Quest stages / objectives / aliases | `add/remove/write_quest_*`, `sync_quest_vmad_*` | `src/forms/quest_stages.cpp`, `quest_objectives.cpp`, `quest_alias_edit.cpp`, `quest_vmad.cpp` |
| ARMO/ARMA create + model fields | `create_armor`, `create_armor_addon`, `set_armor_*model_fields` | `src/forms/armor.cpp`, `armor_edit.cpp` |
| RACE create / body | `create_race`, `set_race_body_data` | `src/forms/race.cpp`, `race_body.cpp` |
| EYES | `create_eyes`, `set_eyes_*` | `src/forms/eyes.cpp` |
| LVSP | `create_leveled_spell`, `set_leveled_spell_settings` | `src/forms/leveled_spell*.cpp` |
| PROJ/EXPL/HAZD create | `create_projectile/explosion/hazard` | `src/forms/proj_expl_hazd.cpp` |
| Exterior CELL create | `create_exterior_cell`, `find_exterior_cell` | `src/forms/cell.cpp` |
| Components | `set_record_components` | `src/forms/components.cpp` |
| WTHR MNAM | `set_weather_mnam` | `src/forms/weather.cpp` |
| CLMT timing | `set_climate_timing` | `src/environment/base_records.cpp` |
| BA2 / strings / BGSM / PEX / SEQ write | `write_ba2_general/texture`, `write_string_table`, `write_material_file`, `PexFile::toBytes`, `SeqFile::toBytes` | module in 3.6 |

Records in `fields.json` (SNDR SOUN MUSC FSTP FSTS WATR PACK CSTY IDLE DIAL INFO SCEN GRAS TREE FLOR etc.) have no bespoke writer; they are edited through `set_record_scalar/string/formlink` driven by the schema. Per-area writer design notes: root `docs/code-notes/radicle-{sound,vegetation,water,package,dialogue,climate,navmesh,subrecord-authoring,field,vmad}-writer.md`.

ESL compaction: Radicle has only the primitives (`esl_eligibility` in `maintenance.cpp`, `local_form_id_range` / `allows_hardcoded_range` in `mutation.cpp`, `renumber_local_record`, `rekey_form_links`, `set_plugin_light_flag`). README lists "safe ESL compaction" as still incomplete. Orchestration is in PET (`PluginEditTool/native/src/plugin/RadiclePluginSupport.hpp:eslSlots` per `docs/code-notes/GARDEN-TOOLS.md`). Light-ID rules: `docs/ESL_FORMIDS.md`.

#### 3.3 Schema engine (`include/fo4/schema/`, `src/schema/`)
- `form_links.cpp`: `visit_link_offsets` (which bytes are FormIDs) feeding `collect_form_links(_detailed)`, `remap_*`, `rekey_form_links`. Helpers: `form_links_ctda.cpp` (CTDA), `form_link_mixed.cpp` (CNTO/LVLO/XTEL-style structs, packed arrays, NAVM/NAVI mixed, PERK, OBTS, placed refs), `behavior_layout.cpp` (PACK discriminated payloads), `formlink_slots.cpp`, `non_formid_slots.cpp` (corpus-measured "not a FormID" denylist).
- `scalar.cpp` / `field_address.cpp`: scalar storage types and addressing keys (`repeated`, `stride`, `array_offset`, `when`).
- `formlink_lists.cpp`, `formlink_list_read.cpp`, `nested_formlink_lists.cpp`, `nested_formlink_list_null.cpp`: list/array FormLink read and edit.
- `vmad.cpp` (700 lines): VMAD object/property/array/struct parse, `inspect_quest_vmad`; `VmadCoverage` / `records_with_incomplete_vmad` signal incomplete scans (drives `MasterUsage::complete()`).
- `ctda.cpp`, `condition_scopes.cpp`: CTDA struct/functions (`ctda_functions.inc`) and scope-aware condition read/write.
- `localized_fields.cpp`: `is_localized_field`, `read_localized_string`, `write_localized_string`.
- `preflight.cpp`: `preflight_record`, `preflight_plugin`, `preflight_authoring_record` (VALIDATE).
- Hand-maintained tables (`include/fo4/schema/`): `formid_subrecords.inc` (173 xEdit `wbFormIDCk` 4CCs per its header), `structured_formids.inc`, `ctda_functions.inc`, `non_formid_slots_manual.inc`, plus `src/schema/form_link_signatures.inc`. `schema/fallout4/README.md` is the rule sheet: grow tables, no handwritten per-record parsers for generic records.

#### 3.4 Typed record families (`include/fo4/forms/`, `src/forms/`; namespace `fo4::forms`)
Structs + decoders + creators/editors: Armor/ArmorAddon, Cell (exterior), Components, Eyes, LeveledSpell (LVSP), PlacedReference (REFR/ACHR), Proj/Expl/Hazd, Quest (aliases/objectives/stages/VMAD), Race, StaticCollection (SCOL), Weather (WTHR). `weather_concept.hpp` is a 3-line stub. Quest alias code is split across `quest_aliases_{decode,encode,extras}.cpp` + `quest_aliases_internal.hpp`.

#### 3.5 Environment (read-only decoded game data; `include/fo4/environment/`, `src/environment/`; namespace `fo4::env`)
`Environment::load(ordered_paths)` (`environment.cpp`) is an immutable, thread-safe load-order view: `plugins()`, `versions(key)`, `winner`, `find_by_editor_id`, `winners_of(sig)`, `metadata`, `resolve_link`, `worldspace_cells`, `interior_cells`, `cell_children` (merged low to high priority, deleted copies remove), `cell_landscape`. The load fails on unreadable or duplicate plugins and on a record header whose master index is past the table (`docs/code-notes/radicle-parity.md`).
Record family -> decoder (`decode_*`):
- World/cell/lighting/water: `world.cpp` (WRLD, CELL, LGTM, `resolve_water`); `base_records.cpp` (LIGH, SPLN bendable spline, LAYR, CLMT, WTHR params, STAT distant LOD / SCOL, ACTI water type, WATR, BNDS).
- Placed refs: `placed.cpp` (`decode_placed_reference`, with model path lookup for TREE/STAT/SCOL/PROJ/MSTT/LIGH/HDPT/HAZD/GRAS/FURN/FLOR/EXPL). Material swap: `material_swap.cpp` (MSWP). Scene layout: `scene_layout.cpp`.
- Terrain: `terrain.cpp` (LAND, LTEX, TXST). Vegetation: `vegetation.cpp` (GRAS, TREE, FLOR).
- Navmesh: `navmesh.cpp` (NAVM, NAVI, path lists; `docs/NAVM_NAVI.md`).
- AI/dialogue: `behavior.cpp` (PACK, CSTY, IDLE), `package_procedure.cpp` (PACK procedure tree), `dialogue.cpp` (DIAL, INFO, SCEN).
- Sound: `sound.cpp` (SNDR, SOUN, MUSC, FSTP, FSTS; `resolve_sound_files`).
- Strings: `strings.cpp` `StringTables::load(env, strings_dir, language, archives*)`, `resolve_name`.
- `quest/runtime.cpp` (`QuestRuntimeData` rows), `quest/structure.cpp` (`read_quest_structure`).

#### 3.6 Non-plugin formats, load order, graph
- BA2: `archive/ba2.cpp` (GNRL), `ba2_texture.cpp` (DX10, DDS header rebuild), `ba2_reader.cpp` (`Ba2Reader::open/find/extract`), `ba2_hash.cpp`, `assets.cpp` (`AssetResolver`, `archive_load_order`, `normalize_asset_path`). Versions 1/7/8 per README/`docs/BA2.md`.
- `localization/strings.cpp`: `.STRINGS/.DLSTRINGS/.ILSTRINGS` read/write with strict header validation.
- `loadorder/plugin_list.cpp` (`parse_plugin_list`, `parse_ccc`), `assemble.cpp` (`assemble_load_order`; official-table handling in `docs/LOAD_ORDER_FILES.md`).
- `nif/nif.cpp` + `geometry.cpp`: `read_nif`, `read_shapes`, `shape_to_global`; read-only, no C ABI wrapper and no PET caller found (only `tests/test_nif*.cpp`).
- `pex/pex.cpp` (808 lines, over the 750 limit): `fo4::pex::PexFile::fromBytes/toBytes`. `seq/seq.cpp`: `SeqFile` raw FormID array. `material/material_file.cpp`: BGSM/BGEM byte-identical round trip with typed get/set.
- `text/encoding.cpp`: Windows-1252 <-> UTF-8, folded compare (`upper_table.inc`, generated by `tools/gen_upper_table.cs`).
- `graph/`: `ConflictGraph::build` (versions, `referenced_by`, `references_from`, `conflicts`), `check_load_order` (CheckCode: invalid_master_index, unresolved_link, wrong_target_type, deleted_target, circular_leveled_list, schema_preflight), `find_leveled_list_cycles`, `diff_plugins`, `find_unreachable`.
- `plugin/maintenance.cpp`: `find_identical_to_master` (ITM), `count_deleted_records`, `esl_eligibility`; `plugin/master_usage.cpp`: `scan_master_usage`, `used_masters` (Clean Masters; PET refuses when VMAD scan incomplete).

### 4. Public surfaces

#### 4.1 C ABI
- Version: `#define RADICLE_ABI_VERSION 40u` at `Radicle/include/fo4/c_api.h:16`; returned by `radicle_abi_version()` (`src/c_api_version.cpp:4`). ABI numbers quoted in `docs/code-notes/Radicle.md` (e.g. 28, 27) are historical; only `c_api.h` is authoritative.
- 224 `RADICLE_API` functions in 17 headers (counted from the headers). `RADICLE_API` is dllexport when `RADICLE_C_BUILD` (set on `radicle_c`).
- Conventions (`c_api.h`): `radicle_status` {OK=0, INVALID_ARGUMENT=1, IO_ERROR=2, PARSE_ERROR=3, NOT_FOUND=4 (env lookups), OUT_OF_MEMORY=5, INTERNAL_ERROR=255}; `radicle_last_error()` is thread-local until the next call on that thread; caller frees results with the matching `radicle_free_*`; a `radicle_plugin` handle is not thread-safe (different handles on different threads are fine); zero-length byte output = OK with NULL/0. Text is UTF-8 at the boundary; stored Windows-1252 is converted (`c_api_env_internal.hpp:stored_text`), except VMAD string values, stored as passed (`docs/code-notes/Radicle.md` ~L252).
- Handles: `radicle_plugin` wraps one mutable `fo4::Plugin` (`src/c_api_internal.hpp`); `radicle_env` wraps an immutable `Environment` plus a mutex-guarded text cache (`src/c_api_env_internal.hpp`).
- Header -> impl map:
  - `c_api.h`: `c_api.cpp` (plugin read/write/destroy, `stamp_master_kinds`, `remap_masters`, `collect_form_keys/links`).
  - `c_api_env.h` (688 lines, near the limit, so later groups got own headers): `c_api_env.cpp` env load/query; `c_api_world.cpp` cell/worldspace; `c_api_terrain.cpp` LAND/LTEX/TXST; `c_api_placed.cpp` placed ref decode; `c_api_strings.cpp` string tables; `c_api_archive.cpp` BA2 + AssetResolver; `c_api_check.cpp` `radicle_env_check_errors`; `c_api_writeback.cpp` (`radicle_plugin_create`, `copy_placed_ref_*`, `set_placed_transform/scale`, `sort_masters`, `set_record_scalar*`); `c_api_ref_fields.cpp` `set_placed_*`; `c_api_fields.cpp` (`radicle_plugin_formlink_list_*`, `record_field_count`, `set_record_formlink/string`).
  - `c_api_records.h` (included at the end of `c_api_env.h`): `c_api_records.cpp` (`record_payload_info`, `record_copy_subrecord`, `record_resolve_form_id`, `record_topic_children`), `c_api_base_records.cpp` (LIGH, SPLN, LAYR, LGTM, CLMT, WTHR, SCOL, WATR, `set_climate_timing`), partly `c_api_world.cpp`.
  - `c_api_authoring.h`: `radicle_plugin_add_subrecord/remove_subrecord/create_record`.
  - `c_api_vmad.h`: `radicle_plugin_set_vmad_{object,string,int,float,bool}`.
  - Per-family headers: `c_api_behavior.h` (PACK/CSTY/IDLE), `c_api_dialogue.h` (DIAL/INFO/SCEN + `set_scene_*`), `c_api_navmesh.h` (NAVM/NAVI + `set_navmesh_*`), `c_api_vegetation.h`, `c_api_materials.h` (swap), `c_api_sound.h`, `c_api_quest.h` (runtime rows in `c_api_quest.cpp`, structure in `c_api_quest_structure.cpp`), `c_api_pex.h`, `c_api_seq.h`, `c_api_material_file.h`, `c_api_loadorder.h`, `c_api_maintenance.h` (ITM, deleted counts, unused masters, ESL eligibility).
- Not in the C ABI (checked by grep of `src/c_api*.cpp`): quest stage/objective/alias writers, `create_race/armor/eyes/projectile/...`, `copy_info_as_new`, `set_weather_mnam`, `set_record_components`, BA2 write, NIF. These are C++-only (PET).
- Any `c_api*.h` change requires regenerating the C# interop; ctest `radicle_csharp_interop_up_to_date` fails otherwise (section 7). ABI stubs `radicle_abi_stub` (version+1), `_v1`, `_v4` exist for managed version-guard tests (`CMakeLists.txt` ~L540-552).

#### 4.2 C++ public API
Everything under `include/fo4/` (installed via `install(DIRECTORY include/fo4 ...)` only when `RADICLE_INSTALL`). Umbrella header `include/fo4/fo4.hpp`. Namespaces: `fo4` (plugin, identity, schema glue, graph), `fo4::env`, `fo4::forms`, `fo4::schema`, `fo4::archive`, `fo4::localization`, `fo4::loadorder`, `fo4::nif`, `fo4::pex`, `fo4::seq`, `fo4::material`, `fo4::quest`, `fo4::io`, `fo4::text`, `fo4::binary`.

#### 4.3 CLI tools (built when `RADICLE_BUILD_TOOLS`)
- `fo4inspect` (`tools/inspect/main.cpp`): subcommands header, masters, groups, records, record, raw, write, formlinks, check, itm, udr, unused-masters, unreachable, diff, summary. JSON output.
- `radicle_parity_dump` (`tools/parity/main.cpp`): `versions|cells|world|names|ba2|ba2x` (GardenEngine #114 parity side, per `docs/code-notes/radicle-parity.md`); `radicle_parity_writeback` (`tools/parity/writeback.cpp`): `pick|apply|dump`; shared `tools/parity/common.hpp`.
- `radicle_env_bench` (`tools/env_bench/`): `<Data dir> [plugin ...]` load benchmark.
- Corpus scripts (manual, need a game install): `tools/corpus/{ba2_measure,form_versions,non_formid_slots,wthr_corpus_inventory,wthr_formid_fo4_only,wthr_formid_resolve}.py`.

#### 4.4 File formats
Read and written: ESM/ESP/ESL (TES4, GRUP, zlib records, XXXX), BA2 GNRL + DX10, `.STRINGS/.DLSTRINGS/.ILSTRINGS`, `.bgsm/.bgem`, `.pex`, `.seq`. Read-only: NIF, `plugins.txt`/`loadorder.txt`, `Fallout4.ccc`.

### 5. Data flow

Plugin edit (C ABI): `radicle_plugin_read_file` -> `fo4::read_plugin_file` (`reader.cpp`) -> `Plugin` -> `radicle_plugin_set_record_scalar` (`c_api_writeback.cpp:150`) -> `schema::scalar_field_descriptor` (table `generated_scalar_fields.inc`) -> `set_record_scalar` (`field_writer.cpp`) edits the subrecord and clears `original_range` -> `radicle_plugin_write_file` -> `write_plugin` (`writer.cpp`) -> atomic file stage/rename. Errors come back as `radicle_status` + `radicle_last_error()`.

Read-only game data: `radicle_env_load(paths)` -> `Environment::load` -> `radicle_env_winner` -> `radicle_*_decode` (e.g. `weather_decode`) -> `env::decode_*` (FormLinks resolved by `Environment::resolve_link`) -> flat C struct -> C# wrapper -> PET / GardenEngine. Localized text: `StringTables::load` + `resolve_name`.

Master remap: `remap_plugin_masters` rebuilds the TES4 master table and re-encodes record header IDs and schema FormLinks from FormKeys; unknown layouts stay raw or the operation refuses (fail closed).

Validation: `check_load_order` / `preflight_plugin` back `fo4inspect check` and `radicle_env_check_errors`.

### 6. Build and tests

- `Radicle/CMakeLists.txt`: C++23, CMake >= 3.25, zlib required (CI pins zlib via `.github/scripts/pinned-zlib.sh`). Options: `FO4_ENABLE_ASAN`, `FO4_ENABLE_UBSAN`, `RADICLE_BUILD_TOOLS`, `RADICLE_BUILD_TESTS`, `RADICLE_INSTALL`, `RADICLE_WARNINGS_AS_ERRORS` (these four default to `PROJECT_IS_TOP_LEVEL`), `RADICLE_CSHARP_TESTS` (OFF).
- Targets: `fo4` (static, PIC, explicit source list, zlib PRIVATE), `radicle_c` (SHARED, OUTPUT_NAME `radicle`, VERSION 0.1.0, links `fo4`), `fo4inspect`, `radicle_env_bench`, `radicle_parity_dump`, `radicle_parity_writeback`, test exe `fo4_tests` (GoogleTest v1.17.0 by FetchContent, about 200 explicit `tests/*.cpp`, none orphaned), `radicle_abi_layout_probe`, `radicle_abi_stub{,_v1,_v4}`.
- Warnings: MSVC `/W4 /permissive-` (+`/WX`), others `-Wall -Wextra -Wpedantic` (+`-Werror`) via `fo4_apply_warnings`.
- CTest: GoogleTest cases via `gtest_discover_tests` (workdir `Radicle/`, data at `testdata/`), plus `radicle_field_address_codegen`, `radicle_papyrus_dump`, `radicle_env_bench_non_ansi_data_dir`, `radicle_env_bench_unreadable_data_dir`, `radicle_parity_tools`, `radicle_csharp_interop_up_to_date`, `radicle_csharp_interop_check_ignores_line_endings`, `radicle_run_managed_tests`, and `radicle_csharp_bindings` (dotnet 8, only with `RADICLE_CSHARP_TESTS`). No ctest labels.
- Real-data tests are gated on env var `FO4_DATA_DIR` (Fallout 4 `Data` folder; skip when unset). `FO4_GAME_BUILD` overrides exe-based release detection; exact counts are asserted for one release only (`docs/TEST_STRATEGY.md`). Other corpus vars: `FO4_PEX_CORPUS`, `FO4_PEX_ARCHIVES`, `FO4_PLUGIN_CORPUS`, `FO4_SEQ_CORPUS`, `FO4_BA2_EXTRACT_ALL`; Papyrus: `FO4_PAPYRUS_{COMPILER,ASSEMBLER,IMPORTS,FLAGS}`. CI: Linux reads repo var `FO4_DATA_DIR_LINUX`; job `native-windows-real-data` runs the real-data ctest regex.
- Not run for this map. README recipe: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`, `cmake --build build -j`, `ctest --test-dir build --output-on-failure`.

### 7. Generated code and drift checks

`schema/fallout4/fields.json` (version 1; keys `records` (31 signatures), `keyword_records` (25), `target_unions`, `condition_scopes` (8)) feeds five Python generators, each `--schema ... --output ... [--check]`:
| Generator (`Radicle/tools/`) | Output (`include/fo4/schema/`) | CMake check target |
|---|---|---|
| `schema_codegen.py` | `generated_scalar_fields.inc` | `radicle_schema_check` |
| `record_schema_codegen.py` | `generated_record_constraints.inc` | `radicle_record_schema_check` |
| `lstring_schema_codegen.py` | `generated_lstring_fields.inc` | `radicle_lstring_schema_check` |
| `formlink_schema_codegen.py` | `generated_formlink_lists.inc` | `radicle_formlink_schema_check` |
| `condition_scope_codegen.py` | `generated_condition_scopes.inc` | `radicle_condition_scope_schema_check` |

`field_address_codegen.py` is a shared module for the addressing keys, tested by `tools/test_field_address_codegen.py` (ctest `radicle_field_address_codegen`). All five checks are `ALL` targets, so a normal build runs `--check` and fails on a stale committed `.inc`. `fo4` has `add_dependencies` on four of them; `radicle_lstring_schema_check` is not a dependency of `fo4` (`CMakeLists.txt` ~L108-120) but still runs in `ALL`.
Other generated or measured tables: `non_formid_slots.inc` (from `tools/corpus/non_formid_slots.py`; header names the corpus and xEdit pin; needs a game install and a `fo4inspect` built with an empty table), `formid_subrecords.inc` (from xEdit `wbFormIDCk`), `src/text/upper_table.inc` (`tools/gen_upper_table.cs`). C# interop: `Radicle/bindings/csharp/tools/generate_interop.py` reads every `c_api*.h` and emits `RadicleNative.g.cs` + `tests/abi_layout_probe.cpp`, checked by ctest.

### 8. Conventions and guards

- Fail closed: unknown layouts stay opaque or mutation/remap refuses; `CtdaNotLosslessError`, `FormLinkFieldError`, strict `rekey_form_links` throw rather than partially rewrite; `delete_local_record` returns `referenced`.
- Expected failures are status enums (`RecordCopyStatus`, `ScalarMutationStatus`, `RecordDeleteStatus`, `SubrecordAddStatus`, ...); malformed input throws `parse_error`/module errors (`Ba2Error`, `StringTableError`, `EnvironmentError`, `FormIdError`); the C layer maps both to `RADICLE_STATUS_*` (`radicle_c_detail::fail`, `fail_exception`).
- 750-line limit for hand-written files (cited in `docs/code-notes/GARDEN-TOOLS.md` and `Radicle.md`); no repo CI that enforces it. Over: `src/pex/pex.cpp` 808. Near: `src/schema/formlink_lists.cpp` 749, `include/fo4/c_api_env.h` 688 (so new C ABI groups get new headers).
- Code notes go in docs, not source: `Radicle/docs/code-notes/Radicle.md` (1103 lines, sections headed `file:symbol`) plus root `docs/code-notes/radicle-*.md` and `GARDEN-TOOLS.md`.
- xEdit parity: layouts pinned to TES5Edit commit `93cc0bc5a1251936c3c7859eee3150eda12a62d7` (`Core/wbDefinitionsFO4.pas`) per `docs/FO4_PARITY.md`, which is the record x {READ WRITE ADD REMOVE CREATE REMAP VALIDATE OPAQUE} matrix (385 lines, dated snapshot, "no record row is FULL"). RE-dependent items: GT #520 / `docs/RE_INVENTORY.md`, `docs/OPEN_QUESTIONS.md` (no guesses hardcoded).
- `ROADMAP.md` rules: do not treat `0xFExxxxxx` as on-disk ESL encoding; do not sweep every uint32 as a FormID; light kind is TES4 SmallFile flag, `.esl` extension is fallback.

### 9. Cross-component links

- PET native (`PluginEditTool/native/src/plugin/Radicle*.cpp`) uses `fo4::forms::*`, `fo4::env`, `write_file_atomic`, `esl_eligibility` directly (e.g. `RadiclePluginArmor.cpp`, `RadiclePluginCell.cpp`, `RadiclePluginComponents.cpp`, `RadiclePluginLifecycle.cpp`).
- GardenEngine and managed callers: `radicle_c` + `Radicle/bindings/csharp`; `Radicle/probes/gardenengine` (see the tests and bindings section).
- Root `cmake/xwin-clang-cl.cmake` is the Windows cross toolchain CI uses for Radicle.
- Mutagen (`PluginEditTool/Mutagen`) is the legacy oracle path; Radicle does not link it.

### 10. Gotchas

- Name mismatch: product Radicle, namespace/static lib `fo4`, shared lib `radicle`, project `fo4sdk`; ctest/exe names mix `fo4_*` and `radicle_*`.
- Every `.cpp` is listed explicitly in `Radicle/CMakeLists.txt` (`fo4`, `radicle_c`, `fo4_tests`). `radicle_c` sources are spread over four statements (`add_library` plus three `target_sources`), so a new export file is easy to miss; a missed test file is silently not run.
- ABI discipline: any `c_api*.h` edit needs a version decision, regenerated `RadicleNative.g.cs` and `abi_layout_probe.cpp`, or ctest fails. Code-note ABI numbers are stale; read `c_api.h:16`.
- Option defaults depend on being top-level: tools/tests/warnings-as-errors are ON only when Radicle is the root project. Under PET they are cache-set OFF unless CI passes them.
- Docs lag code: `ROADMAP.md` phases, README Status and `docs/FO4_PARITY.md` (baseline main cca4b080, 2026-10-05) are snapshots; `docs/ARCHITECTURE.md` layers/non-goals are stale.
- `Environment` returns borrowed `string_view`s into loaded plugins; lifetimes follow the `Environment` (use `stable_owner`); it is movable, not copyable.
- Preservation depends on `Plugin::original_range`/per-record `OriginalByteRange`: mutators clear them (`plugin.original_range.clear()` e.g. in `set_record_conditions`), so any edit rebuilds dirty groups.
- `src/records/` and `tools/diff/` are empty placeholders.

### 11. Docs index (link, do not duplicate)

Under `Radicle/docs/`: ARCHITECTURE (partly stale), FORMID_MODEL + RUNTIME_FORMIDS + ESL_FORMIDS + RUNTIME_PLUGIN_INDEX_EVIDENCE (identity), FORMAT_NOTES + HEDR_RECORD_COUNT + ONAM + PARSE_MEMORY (container/writer), FORM_LINK_EVIDENCE + SCALAR_SCHEMA + VMAD + VMAD_VARIABLE_EVIDENCE (schema), BA2 + COMPRESSION_EVIDENCE (archive), LOCALIZED_STRINGS + TES4_LOCALIZATION_EVIDENCE (strings), LOAD_ORDER_FILES, MATERIALS, NIF, PEX_PROVENANCE, SEQ_FORMAT, NAVM_NAVI, SOUND, VEGETATION, WATER, CLIMATE_WEATHER + WTHR_* (+ json inventories), DIALOGUE_SCENES, PACKAGES_COMBAT_IDLES, QUEST_ALIASES, FO4_PARITY (coverage matrix), TEST_STRATEGY, RE_INVENTORY + OPEN_QUESTIONS, FO4IDE (PET integration), `code-notes/Radicle.md`, `re-private/WTHR_VERIFIED.md`, `superpowers/specs/2026-08-19-fo4-plugin-foundation-design.md`; `ck-experiments/` is empty.

## Radicle tests, probes, C# bindings and docs
Root: `Radicle/` at nomad/dev 9d6c622c (PR 504 head). Read from files; nothing was built or run.

### 1. Purpose
- `tests/`: one gtest executable `fo4_tests` plus a handful of CTest script tests. Consumed by CI (`.github/workflows/native-ci.yml`).
- `bindings/csharp/`: `Radicle.Interop` (.NET 8), the managed wrapper that GardenEngine/PET/other C# hosts use instead of Mutagen. `Radicle.Interop.Tests` is its xunit suite.
- `probes/gardenengine/`: Godot C# scenes copied into a GardenEngine checkout to exercise Radicle there. Not built by this repo.
- `testdata/`: two small real plugins and four `.psc` oracle sources.
- `docs/`: evidence/format notes, the parity matrix, test strategy, code-notes.

### 2. Layout
```
Radicle/
  CMakeLists.txt        594 lines; tests block starts line 283 (if RADICLE_BUILD_TESTS)
  tests/                200 test_*.cpp + fixtures (*_fixture.hpp, plugin_bytes.*, temp_data_dir.hpp,
                        real_data_build.*, real_data_plugins.hpp, material_file_builder.hpp)
  tools/                codegen py scripts, corpus/ scripts, env_bench/, parity/ (+tests/test_papyrus_dump.py), inspect/
  probes/gardenengine/  6 probes: Climate, Dialogue, Navmesh, Package, Sound, Vegetation, WaterSelection (.cs + .tscn)
  testdata/             FW_HeatherSurvivalPatch.esp, SlowTime.esp, README.md, papyrus/GardenOracle{Array,Empty,Ints,State}.psc
  bindings/csharp/      Radicle.Interop, Radicle.Interop.Tests, tools/, tests/ (C++ ABI probe+stubs), packages/ (offline NuGet feed)
  docs/                 see section 7; subdirs code-notes/ (Radicle.md, 1103 lines), re-private/, superpowers/specs/, ck-experiments/ (empty)
```
Repo-root `docs/code-notes/radicle-*.md` (about 25 files, e.g. `radicle-parity.md`, `radicle-managed-ci.md`, `radicle-papyrus-census.md`) are per-topic code-notes for Radicle work; they live outside `Radicle/`.

### 3. Tests: organisation
- Single gtest target `fo4_tests`, sources listed by hand in `Radicle/CMakeLists.txt` (lines ~296-500). 200 `tests/test_*.cpp` on disk, all referenced in the CMake (checked with a loop: none unregistered). About 1444 `TEST*(` macros.
- Links `fo4 radicle_c GTest::gtest_main ZLIB::ZLIB` (CMakeLists.txt:503). GoogleTest v1.17.0 via FetchContent from a pinned URL + SHA256 (lines 287-293). Compile definition `FO4_TESTDATA_DIR="testdata"`; `gtest_discover_tests(fo4_tests DISCOVERY_TIMEOUT 120 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})` (line 514), so the cwd is `Radicle/` and `testdata/` resolves relative.
- Each gtest case becomes its own CTest test. No custom CTest `LABELS`, no per-test `TIMEOUT` properties exist in `Radicle/CMakeLists.txt` (only the discovery timeout and one `SKIP_REGULAR_EXPRESSION`). CI splits tests by test-name regex, not labels (see below).
- Non-gtest CTest tests (all `add_test`, lines 516-593):
  - `radicle_field_address_codegen` (`tools/test_field_address_codegen.py`)
  - with `RADICLE_BUILD_TOOLS`: `radicle_papyrus_dump`, `radicle_env_bench_non_ansi_data_dir`, `radicle_env_bench_unreadable_data_dir` (skips when output matches `skipped: `), `radicle_parity_tools`
  - with Python3: `radicle_csharp_interop_up_to_date`, `radicle_csharp_interop_check_ignores_line_endings`, `radicle_run_managed_tests` (self-test of the runner script)
  - with `-DRADICLE_CSHARP_TESTS=ON` (default OFF): `radicle_csharp_bindings`, which runs `run_managed_tests.py` with env `RADICLE_NATIVE_PATH`, `RADICLE_ABI_PROBE`, `RADICLE_ABI_STUB`, `_V1`, `_V4` set to the built targets.
- Schema drift guards run at build time, not as tests: `ALL` custom targets `radicle_schema_check`, `radicle_record_schema_check`, `radicle_lstring_schema_check`, `radicle_formlink_schema_check`, `radicle_condition_scope_schema_check` (CMakeLists.txt:25-65).
- CMake options (lines 17-22): `FO4_ENABLE_ASAN`, `FO4_ENABLE_UBSAN`, `RADICLE_BUILD_TOOLS`, `RADICLE_BUILD_TESTS`, `RADICLE_INSTALL`, `RADICLE_WARNINGS_AS_ERRORS` (the last four default to `PROJECT_IS_TOP_LEVEL`). Warning/sanitizer helpers: `fo4_apply_warnings`, `fo4_apply_sanitizers`.

#### Kinds of test (by filename convention)
- Unit/behavior with synthetic bytes: most `test_*.cpp`; plugins are built in memory via `tests/plugin_bytes.{hpp,cpp}` and the `*_fixture.hpp` / `*_writer_fixture.hpp` builders; `temp_data_dir.hpp` (`fo4::test::TempDataDir`) makes scratch Data dirs.
- C ABI tests: `test_c_api*.cpp` (authoring, fields, loadorder, maintenance, material_file, pex, quest, seq, version), plus `*_c_api.cpp` per feature.
- Real-data tests: `*_real.cpp` (e.g. `test_dialogue_real`, `test_ba2_real`, `test_ba2_versions_real`, `test_header_versions_real`, `test_sky_real`, `test_water_real`, `test_load_order_real`, `test_writeback_real`, `test_env_bench_real`, `test_quest_*_real`), plus `*_writer_real`, `test_condition_scopes_real`, `test_typed_families_real`, `test_actor_value_perk_real`, `test_struct_payload_formlinks_real`. Case names usually contain `RealData`/`Real`.
- Corpus tests: `test_plugin_corpus.cpp`, `test_pex_corpus.cpp`, `test_seq_corpus.cpp`, `test_nif_corpus.cpp`.
- Oracle/parity tests: `test_pex_oracle.cpp` (uses Papyrus compiler env vars below), `test_parity_dds.cpp`; plus `tools/parity` binaries run by `radicle_parity_tools`.
- Hardening/limits: `test_ba2_hardening`, `test_pex_hardening`, `test_reader_limits`, `test_text_boundary`.

#### Env vars read by tests (from `getenv` grep across `tests/`, `tools/`, `bindings/`)
| Var | Used for |
|---|---|
| `FO4_DATA_DIR` | Fallout 4 `Data` folder; ~55 call sites; real-data tests `GTEST_SKIP` when unset |
| `FO4_GAME_BUILD` | overrides release detection (e.g. `1.11.240.0`); `tests/real_data_build.cpp:83` |
| `FO4_PLUGIN_CORPUS` | dir (recursed) of plugins for `test_plugin_corpus.cpp` |
| `FO4_PEX_CORPUS`, `FO4_PEX_ARCHIVES` | loose `.pex` dirs / `.ba2` files with `Scripts/*.pex`; if both unset the data-dir archive test runs instead (`test_pex_corpus.cpp:491-552`) |
| `FO4_SEQ_CORPUS` | `.seq` corpus |
| `FO4_BA2_EXTRACT_ALL` | with `FO4_DATA_DIR`, extract every vanilla BA2 entry (`test_ba2_real.cpp:145`) |
| `FO4_PAPYRUS_COMPILER`, `_ASSEMBLER`, `_FLAGS`, `_IMPORTS` | `test_pex_oracle.cpp` |
| `RADICLE_NATIVE_PATH`, `RADICLE_ABI_PROBE`, `RADICLE_ABI_STUB(_V1/_V4)` | C# test run |
Real-data tests skip (do not fail) when `FO4_DATA_DIR` is unset or the file is missing (120 `GTEST_SKIP` sites).

#### Game build detection (OG/NG/AE)
`tests/real_data_build.{hpp,cpp}`: `enum class GameRelease { OG, NG, AE, Unknown }`; `detect_game_build(data_dir)` first honours `FO4_GAME_BUILD`, else scans `<Data>/../Fallout4.exe` backward for the file version (`parse_file_version`), then `game_build_from_version`: 1.10.163 = OG, 1.10.980/984 = NG, 1.11.x = AE, else Unknown. `exact_counts_known()` is true only for AE `1.11.240.0`; exact-count assertions (DIAL/INFO/SCEN counts, WTHR/CLMT/WATR counts, BA2 entry counts) are gated on it, other builds record via `RecordProperty("game_build", ...)` and skip only the equality. Self-tested in `test_real_data_build.cpp`. Table in `docs/TEST_STRATEGY.md` "Real-data tests and game releases".

#### CI split (not in Radicle, but governs tests)
`.github/workflows/native-ci.yml:58` defines `REAL_DATA_TEST_REGEX` (names like `*RealData.`, `QuestRuntimeReal.`, `RealPluginPreservation.`, `BehaviorReal.`, `NifCorpus.`, `VmadWriterOfficial.` ...). `native-windows-tests` runs `ctest -E <regex>` with `FO4_DATA_DIR` removed (line 504-505); `native-windows-real-data` runs `ctest --no-tests=error -R <regex>` (line 845). Linux job uses repo var `FO4_DATA_DIR_LINUX` (line 128), validates masters/BA2s exist, and unsets the var if empty. A new real-data test must match that regex or it runs (skipping) in the no-data job. Configure flags in CI: `-DRADICLE_BUILD_TESTS=ON -DRADICLE_BUILD_TOOLS=ON -DRADICLE_CSHARP_TESTS=ON -DRADICLE_WARNINGS_AS_ERRORS=ON`, built via `PluginEditTool/native` as the top-level project.

### 4. testdata
`testdata/README.md`: `FW_HeatherSurvivalPatch.esp` (2 KB, 3 masters, form version 131) and `SlowTime.esp` are small real plugins, not Bethesda masters. Rule: do not vendor `Fallout4.esm` or CC ESLs. `testdata/papyrus/GardenOracle*.psc` are sources for the PEX oracle test. Synthetic TES4/GRUP/XXXX/compressed cases come from `tests/plugin_bytes.cpp`. `SlowTime.esp` is also fed to `radicle_env_bench` and `radicle_parity_tools` tests.

### 5. probes/
`probes/gardenengine/*.cs` + `*.tscn`: Godot `Node` scripts (namespace `OpenCommonwealth`) that call `RadicleEnvironment`/GardenEngine services under real data, e.g. `WaterSelectionProbe` reads `FO4_DATA_DIR`, prints `... SKIP` and quits with exit 77 when unset. They are copied into a GardenEngine checkout (`docs/code-notes/Radicle.md` line ~48: "no CMakeLists in this repo builds it"), run with `--headless --path opencw tools/<name>.tscn` (see `docs/CLIMATE_WEATHER.md` ~131-148, prints `climate_probe PASS`). `docs/code-notes/radicle-vegetation.md:41` says the vegetation probe had not been run because GardenEngine pinned an older ABI. Not part of CI here.

### 6. C# bindings (`bindings/csharp/`)
- Projects: `Radicle.Interop/Radicle.Interop.csproj` (net8.0, nullable, unsafe, warnings as errors, no packages, no lock) and `Radicle.Interop.Tests` (xunit 2.9.3, Test.Sdk 17.11.1, runner 2.8.2, `RestoreLockedMode` with committed `packages.lock.json`). `global.json` pins SDK.
- Generated layer: `Radicle.Interop/RadicleNative.g.cs` (2365 lines, "do not edit") and `tests/abi_layout_probe.cpp` are both emitted by `tools/generate_interop.py <include dir> <cs out> <probe out>`. Input: the `HEADERS` list at top of the script (17 headers: `fo4/c_api.h`, `c_api_env`, `_records`, `_materials`, `_maintenance`, `_navmesh`, `_sound`, `_pex`, `_material_file`, `_seq`, `_quest`, `_authoring`, `_vegetation`, `_vmad`, `_behavior`, `_dialogue`, `_loadorder`). `ls include/fo4 | grep c_api` shows exactly these 17, so all are covered. A new `c_api_*.h` must be added to `HEADERS` by hand. The tokenizer raises on anything it cannot map.
- ABI version: `#define RADICLE_ABI_VERSION 40u` at `Radicle/include/fo4/c_api.h:16`; the generator regex-reads it and writes `RadicleNative.CompiledAbiVersion`.
- Drift check: CTest `radicle_csharp_interop_up_to_date` (`tools/check_generated.cmake`) regenerates into the build dir and compares with the committed `.g.cs` and probe; `_check_ignores_line_endings` (`check_generated_eol.cmake`) proves CRLF/LF differences are ignored.
- Static ctor in `RadicleNative.Init.cs`: `RadicleLibrary.Register()` (resolver). Hand-written wrappers (`Core.cs`, `PluginFile.cs`, `RadicleEnvironment.cs`, `RecordReads.cs`, `Archives.cs`, `LoadOrder.cs`, `Localization.cs`, `MaterialFile/MaterialSwaps.cs`, `Navmesh.cs`, `Pex*.cs`, `SeqFile.cs`, `QuestRuntime/QuestStructure.cs`, `Dialogue.cs`, `Behavior.cs`, `Vegetation.cs`, `WaterSelection.cs`, `SoundRecords.cs`, `SkyParameters.cs`, `PlacedReferences.cs`): `IDisposable` handles, managed records, `RadicleException`.
- ABI guard at runtime: first call loads the library through `RadicleLibrary`, calls `radicle_abi_version`, throws `RadicleAbiMismatchException` on mismatch or missing export (README "ABI check"). Not covered: a library supplied by an `AssemblyLoadContext` resolver; such hosts must call `RadicleLibrary.SetPath`.
- Tests of the guard/layout: `AbiGuardTests` (second copy of bindings in own `AssemblyLoadContext`; stubs built from `tests/abi_stub.cpp` as `radicle_abi_stub` (ABI+1), `_v1` (ABI 1, last without material swap exports), `_v4` (ABI 4, last without record payload/topic exports)); `AbiLayoutTests` compares `radicle_abi_layout_probe` `sizeof/offsetof` output with `Marshal.SizeOf/OffsetOf` for every generated struct. Other tests: ~40 files (`WritebackTests`, `VmadTests`, `QuestRuntimeTests`, `PexModelTests`, `LifetimeTests`, `OwnershipTests` ...); fixture plugins/BA2s/string tables are built in C# (`Fixtures.cs`).
- Running: `tools/run_managed_tests.py --dotnet X [--build-dir B] [--network-isolated]`: verifies `packages/manifest.json` inventory + SHA-256 of 14 `.nupkg` in `packages/`, copies projects, restores with empty caches from that offline feed only (NuGet.Config in the Tests dir), then `dotnet test`. With `--build-dir` it queries `ctest --show-only=json-v1` to find the built targets. Linux CI runs it under `unshare --user --map-root-user --net` (native-ci.yml:154) and requires external connections to fail. `test_run_managed_tests.py` tests the runner. `.gitignore` and `Directory.Build.props` are in `bindings/csharp/`.
- Locating/shipping native library: `RadicleLibrary.SetPath(fullPath)` before any call, or `RADICLE_NATIVE_PATH`, else default probing for `radicle` (`radicle.dll`/`libradicle.so`). Ship it with the Godot build. Native target is `radicle_c` (shared). Direct run: `RADICLE_NATIVE_PATH=... RADICLE_ABI_PROBE=... dotnet test Radicle/bindings/csharp/Radicle.Interop.Tests`.
- README runtime notes worth knowing: `RadicleEnvironment` parses all plugins up front (load off the main thread); `Record` handles are borrowed from the environment; reads thread-safe except `PluginFile`; PEX/SEQ names stay raw bytes (encoding above 0x7F UNPROVEN); `Native.PexValue` vs `Radicle.Interop.PexValue` name clash (CS0104).

### 7. docs/ (one line each)
Parity/process
- `FO4_PARITY.md`: editing-parity matrix per record signature (READ/WRITE/ADD/REMOVE/CREATE/REMAP/VALIDATE/OPAQUE, codes P/I/U/G), named schema fields, open-parser-PR audit, issue disposition, ordered follow-ups; tracks #86 (roadmap) and #287 (coverage authority). Record rows cite xEdit line numbers.
- `TEST_STRATEGY.md`: principles, coverage, real-data tests and OG/NG/AE release table, corpus/oracle plan, real-asset corpus env vars, sanitizers.
- `OPEN_QUESTIONS.md`: unproven items; "speculative answers must not be hardcoded".
- `ARCHITECTURE.md`, `FO4IDE.md`: SDK design and FO4IDE/PET integration path. `FORMAT_NOTES.md`: confidence-tagged record/subrecord format facts, per-release notes.
- `RE_INVENTORY.md`: inventory of local RE sources (confidence tags).
Format/feature docs
- `BA2.md` (BTDX GNRL/DX10 v1/7/8, DDS header rules), `COMPRESSION_EVIDENCE.md`, `DIALOGUE_SCENES.md`, `ESL_FORMIDS.md`, `FORMID_MODEL.md`, `FORM_LINK_EVIDENCE.md`, `HEDR_RECORD_COUNT.md`, `LOAD_ORDER_FILES.md`, `LOCALIZED_STRINGS.md`, `MATERIALS.md` (BGSM/BGEM), `NAVM_NAVI.md`, `NIF.md` (`fo4::nif` reader core; strict gate NIF 20.2.0.7/user 12/BS 130; "Parity-pending" list), `ONAM.md` (TES4 ONAM writer rule from xEdit `PrepareSave`), `PACKAGES_COMBAT_IDLES.md`, `QUEST_ALIASES.md`, `RUNTIME_FORMIDS.md`, `SCALAR_SCHEMA.md`, `SEQ_FORMAT.md`, `SOUND.md`, `VEGETATION.md`, `VMAD.md`, `WATER.md`, `CLIMATE_WEATHER.md` (also records probe run results), `PARSE_MEMORY.md` (memory measurement), `PEX_PROVENANCE.md` (where the PEX reader came from).
Evidence notes (RE-answer carriers): `COMPRESSION_EVIDENCE.md` (#30/#391), `RUNTIME_PLUGIN_INDEX_EVIDENCE.md` (#28/#389), `TES4_LOCALIZATION_EVIDENCE.md` (#29, RE-007 on #520), `VMAD_VARIABLE_EVIDENCE.md` (#15, RE-005), `WTHR_CORPUS.md` + `wthr_corpus_inventory.json` (generated by `tools/corpus/wthr_corpus_inventory.py`), `WTHR_FORMID_SLOTS.md` + `wthr_formid_slots_fo4.json`, `WTHR_SEMANTIC_PROOF.md` (#284), `re-private/WTHR_VERIFIED.md` (RE-MCP symbol table, OG 1.10.163).
- `code-notes/Radicle.md`: the per-symbol design/bug-history notes (anchored `file:symbol`); the global rule says explanatory comments go here, not in source.
- `superpowers/specs/2026-08-19-fo4-plugin-foundation-design.md`: original design spec. `ck-experiments/`: empty.
No separate "census/gate" doc under `Radicle/docs/`: census material is in `FO4_PARITY.md` (record inventory), `FORMAT_NOTES.md`/`OPEN_QUESTIONS.md`/`VEGETATION.md` (mention census), and repo-root `docs/code-notes/radicle-papyrus-census.md`, `radicle-fragment-counts.md`.

### 8. Where the parity rule and RE answers are recorded
- Pin: TES5Edit/TES5Edit commit `93cc0bc5a1251936c3c7859eee3150eda12a62d7`, `Core/wbDefinitionsFO4.pas`. Stated in `docs/FO4_PARITY.md` (baseline section; 147 unique active record registration sites, file SHA-256 given), and repeated in `docs/CLIMATE_WEATHER.md:4`, `NAVM_NAVI.md`, `SOUND.md`, `VEGETATION.md`, `BA2.md:124,143` (`Core/wbDDS.pas`), `docs/code-notes/Radicle.md` (lines 27, 177, 289, 297 as `source:xEdit ... @93cc0bc5`). `ONAM.md` uses the `dev-4.1.6` branch instead (`TwbFile.PrepareSave`); `docs/code-notes/radicle-parity.md` also cites dev-4.1.6 `wbImplementation.pas`.
- Rule text (`Radicle/README.md:7`, `FO4_PARITY.md` "Scope and acceptance"): stored layouts may come from xEdit definitions, official plugins, existing behavior, CK/CKRE and synthetic tests; runtime RE only for unresolved layouts, unproven safe mutation or runtime behavior; unknown layouts stay opaque or refuse mutation; Mutagen is a compatibility oracle; a reader alone is not editing parity; an editable surface must pass parse, expose, edit, serialize, reparse.
- RE answers: referenced from docs as RE-### entries on GARDEN-TOOLS issue #520 (e.g. RE-010 comment 13142 in `CLIMATE_WEATHER.md`; RE-005, RE-007) and as permalinks into `re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/f18bee79.../RE-Work/subsystems/plugin-io-garden-tools/...` (the `*_EVIDENCE.md` files, `CLIMATE_WEATHER.md:102`). Wiki "RE Tracking" is outside the repo (not verified here).

### 9. Cross-links
- Tests/bindings use `include/fo4/c_api*.h` (ABI), `radicle_c` and `fo4` targets, `tools/` codegen. CI builds Radicle via `PluginEditTool/native` and runs the C# job from `.github/workflows/native-ci.yml`.
- Probes depend on GardenEngine types (`OpenCommonwealth.Services.*`) and its Radicle pin (ABI must match).

### 10. Gotchas
- New `tests/test_*.cpp` must be added to `fo4_tests` in CMakeLists by hand; new real-data case names must match `REAL_DATA_TEST_REGEX` in native-ci.yml or the no-data job runs them (they skip, but the real-data job would not pick them up).
- New public header `c_api_*.h` needs adding to `HEADERS` in `generate_interop.py`, then regenerate both outputs, or `radicle_csharp_interop_up_to_date` fails.
- Bumping `RADICLE_ABI_VERSION` (now 40) changes the generated `CompiledAbiVersion`; stub libs `_v1`/`_v4` encode historical ABI boundaries.
- C# tests restore from the offline feed with locked mode; changing package refs needs the full closure update (see `packages/README.md`).
- Tests assume cwd `Radicle/` (`WORKING_DIRECTORY`), since `FO4_TESTDATA_DIR` is relative.
- Exact vanilla counts hold only for AE 1.11.240; OG/NG only record.
- Not verified: actual test pass/fail state, whether `Radicle/tools/test_field_address_codegen.py` and parity tests pass, wiki contents, how PET references the native DLL path.

## PluginEditTool
Root: `PluginEditTool/` in GARDEN-TOOLS (snapshot nomad/dev 9d6c622c). All paths below are relative to it unless prefixed `../` (repo root).

### 1. Purpose

A Fallout 4 plugin IDE for `.esp/.esm/.esl`: record browser/editor across a load order, conflict scan, masters, cell/worldspace viewer, Papyrus compile/decompile/analysis plus a node-graph Papyrus editor, NIF/material/audio/archive panels, F4SE binding tooling, and a stdio MCP server (`FO4RecordEditor.exe --mcp`) exposing the same operations to external AI clients. `README.md` states the shipped app is native C++23 and needs no Mutagen, xEdit or .NET.

Consumers: end users (MO2 launch entry, MCP clients), CI (`../.github/workflows/native-ci.yml`, `plugin-editor-ci.yml`). It consumes Radicle (`../Radicle`, sibling dir). The executable keeps the historical name `FO4RecordEditor.exe` so MO2 entries and MCP configs keep working (`native/apps/fo4ide/CMakeLists.txt` `OUTPUT_NAME`).

### 2. Layout

```
PluginEditTool/
  native/                   C++23 host (the shipping product). CMake root: native/CMakeLists.txt
    apps/fo4ide/            main.cpp (CLI modes, wiring), McpTools.cpp (MCP tool registrations)
    include/fo4ide/<mod>/   public headers per module (same names as src/)
    src/<mod>/              app, archive, assets, audio, events, f4se, graph, http, logging, material,
                            mcp, mo2, papyrus, platform, plugin, process, rpc, settings, textures, tools
    cmake/                  Dependencies (FetchContent), CompilerWarnings, Sanitizers, Utf8CodePage
    tests/                  unit/ (Catch2), integration/ (HttpServerTests), fixtures/, helpers/, native_mcp_smoke.py
  web/                      React 19 + TS + Vite frontend (web/src, built to web/dist)
  FO4RecordEditor/          LEGACY WPF app (net9.0-windows), Mutagen-based
  FO4RecordEditor.Core/     LEGACY managed library (net8.0): MutagenLoader, WriteService.*, Papyrus, Graph...
  FO4RecordEditor.Server/   LEGACY managed ASP.NET host (net9.0): served the same React UI on Linux; has --mcp
  FO4RecordEditor.Tests/    managed xunit tests (WPF-side + Core services)
  FO4RecordEditor.Core.Tests/ managed xunit tests (Papyrus, Graph, F4SE, Mutagen env, WriteService)
  Mutagen/                  vendored Mutagen subset (see section 9)
  docs/                     MCP_SETUP.md (user guide), xedit-copy-workflows.md, MODKIT21_PULLS.md, internal/ (see 8)
  tools/                    check-source-policy.py (+test), build-vanilla-pex-corpus.sh, ci/ (ghrunnerd.py, prisma runner scripts)
  packaging/linux/          build-deb.sh, .desktop, launcher script
  package.ps1, publish.bat  Windows release zip / local build+deploy
  .github/                  NOT workflows: `esl-formid.parts/00..14` and `integrity.parts/00..06` (base64-looking blobs, see section 10)
```

Workflows live at repo root `../.github/workflows/` (`native-ci.yml`, `plugin-editor-ci.yml`, `bgs-ci.yml`). There is no `PluginEditTool/.github/workflows`.

### 3. Key modules (native)

Entry: `native/apps/fo4ide/main.cpp` `main()` (line ~212). CLI flags parsed ~line 284-306: `--health`, `--headless`/`--serve`, `--browser`, `--window`, `--mcp`, `--mo2 <instance>` (flag name from the arg block at ~291), `--port`, `--web-root`. Web root defaults to `<exe dir>/web/dist` (`webRootFromArgs`); dev builds embed `FO4IDE_SOURCE_WEB_ROOT`.

Static libraries (all in `native/src/CMakeLists.txt`):
- `fo4ide_core` (INTERFACE headers, `Result<T>`/`Error{code,msg}` in `include/fo4ide/core`), `fo4ide_logging` (spdlog), `fo4ide_settings`, `fo4ide_process` (reproc++ child processes), `fo4ide_platform`, `fo4ide_window` (WebView2 on Windows, WebKitGTK on Linux), `fo4ide_events` (EventBus), `fo4ide_rpc` (`RpcDispatcher`, `Target`), `fo4ide_mcp` (`McpServer.cpp`, stdio loop), `fo4ide_http` (`src/http`, cpp-httplib).
- `fo4ide_plugin` (`src/plugin/RadiclePlugin*.cpp`, ~40 files): `RadiclePluginBackend` implements `IPluginBackend` (`include/fo4ide/plugin/IPluginBackend.hpp`) over Radicle. Files split by feature: Queries, Mutation, Masters, Lifecycle, Create, Scalar, Fields, Element, Conditions (+`ConditionCodec/Functions`), Quest* (authoring), Cell, Compact, Esl, Renumber, DeepCopy, ReferenceRemap, OverridePatch, Leveled, Seq, Compare, Check, BatchMutation, MasterMutation, ReadApi, Maintenance. `RadiclePluginSupport.hpp` has shared helpers (`support::eslSlots`, etc.).
- `fo4ide_mo2` (`src/mo2/Mo2Profile.cpp`), `fo4ide_papyrus` (Lexer/Parser/Resolver/TypeChecker/Decompiler/CodeGenerator/Binding, CK-fidelity), `fo4ide_graph` (visual-Papyrus graph: Document/Validator/Compiler/Lifter/Linearizer/ToolService), `fo4ide_f4se` (F4SE source scanner/emitter/cross-check/oracle), `fo4ide_nif` (`NifService`: drives external `niftool`), `fo4ide_audio` (external tools), `fo4ide_material` (links `fo4`), `fo4ide_textures` (DDS/BCn/PNG), `fo4ide_assets`, `fo4ide_tools` (`ToolLocator`), `fo4ide_app` (the RPC targets: `src/app/*Target.cpp`, `WikiService.cpp`; compile staging moved to Radicle, `Radicle/src/papyrus/compile_staging.cpp`: `planCompile`, `stageNamespacedScripts`, `removeCompileStaging`).
- Executable `fo4ide` links all of the above (+`fo4ide_http` when `FO4IDE_BUILD_HTTP`, defines `FO4IDE_HAS_HTTP=1`).

RPC targets (names the web layer uses, `web/src/hostbridge.ts` `HOST_OBJECTS`): `backend`, `appInterop`, `settings`, `papyrus`, `nif`, `material`, `masters`, `archive`, `audio`, `cell`, `graph`. `native/src/app/PluginBackendTarget.cpp` is the compatibility boundary mapping legacy React method names to `IPluginBackend` (per `docs/internal/ARCHITECTURE.md`). `src/app/AppInteropTarget.cpp` handles `OpenMo2Profile` / `ReloadMo2Profile`.

### 4. Public surfaces

- MCP over stdio: `--mcp`. `native/src/mcp/McpServer.cpp`; tools registered in `native/apps/fo4ide/McpTools.cpp` (via `registry.registerTool`) and `main.cpp`, dispatching to the same RPC targets. Setup guide `docs/MCP_SETUP.md`. Smoke test `native/tests/native_mcp_smoke.py`. this map did not count the tools (docs/internal/INDEX.md says 53, which describes the old managed build).
- HTTP (`native/src/http/HttpServer.cpp`): `POST /rpc`, `GET /api/health`, `GET /events` (SSE), static web root. Requests need header `X-FO4IDE-Token` (constant-time compare, `tokenMatches`); Host header restricted to 127.0.0.1/localhost. Browser takes the token from URL hash `?t=` and stores it in sessionStorage (`web/src/hostbridge.ts` `takeToken`).
- In-window: WebView2 host objects `window.chrome.webview.hostObjects.<target>.<method>(...)` -> `RpcDispatcher`. Most legacy methods return strings, many hold JSON inside the string; preserve this wire contract (ARCHITECTURE.md).
- FormKey wire format: `001234:Fallout4.esm` (6 hex local ID + owner).
- Files: reads/writes `.esp/.esm/.esl`, MO2 `ModOrganizer.ini`, `profiles/<p>/plugins.txt`, `modlist.txt`, game `Fallout4.ccc`; BA2; settings JSON.
- No C ABI of its own. It does not use Radicle's C ABI (`radicle_c`) at all; grep of `native/src native/include native/apps` for `radicle_c|radicle.h` is empty.

### 5. Data flow (native path)

React (`web/src/*`, calls through `backend.ts`/`hostbridge.ts`) -> WebView2 host object, or HTTP `POST /rpc` -> `fo4ide::rpc::RpcDispatcher` -> a `Target` in `src/app` (e.g. `PluginBackendTarget`) -> `IPluginBackend` -> `RadiclePluginBackend` -> Radicle C++ headers (`<fo4/plugin/reader.hpp>`, `writer.hpp`, `mutation.hpp`, `maintenance.hpp`, `<fo4/schema/...>`, `<fo4/graph/conflict_graph.hpp>`, `<fo4/forms/...>`, `<fo4/loadorder/...>`). Most-included Radicle headers (by `#include` count in native/): `fo4/plugin/reader.hpp` (30), `writer.hpp` (20), `mutation.hpp` (15), `fo4/signature.hpp` (13).

How PET links Radicle: `native/CMakeLists.txt` requires `../../Radicle/CMakeLists.txt` (else FATAL_ERROR), sets `RADICLE_BUILD_TOOLS/TESTS` OFF as non-forced cache vars (CI turns them ON), forces `RADICLE_INSTALL OFF`, then `add_subdirectory` -> CMake target `fo4` (static C++ lib, `Radicle/CMakeLists.txt:95`). `fo4ide_plugin`, `fo4ide_mo2`, `fo4ide_material`, `fo4ide_papyrus` link `fo4` directly. Source-level, in-process, no FFI. It also unsets VERSION/SOVERSION on `radicle_c` (ntfs3 mount cannot symlink) even though PET never links it.

Write path invariants (docs/internal/ARCHITECTURE.md "Write safety"): mutate a copy of `fo4::Plugin`, swap in live plugin only after validation and atomic write (`fo4::io::write_file_atomic`); `writeGuard()` refuses if file mtime changed after load; Game Data and official/CC plugin names are not editable (`isEditable()`); auto-created patch plugins may only be written to the MO2 overwrite directory; failed auto-created targets are rolled back only if unchanged, inside overwrite root, TES4-only; master changes remap schema-proven FormLinks, header FormIDs and FormID-bearing GRUP labels; nested REFR/ACHR have a CELL-aware copy path, other nested families fail closed. Locking: `OpLock` (Read/Write modes, per-plugin names) in RadiclePlugin*.cpp.

MO2 profile / load order (`native/src/mo2/Mo2Profile.cpp`, header `include/fo4ide/mo2/Mo2Profile.hpp`): `readInstanceInfo` reads `ModOrganizer.ini` (`selected_profile`, `gamepath`, `overwrite_directory` with `%BASE_DIR%`; falls back to `<instance>/Stock Folder/Data`); `readProfile` requires `profiles/<p>/plugins.txt` (else `Mo2PluginsMissing`), reads `modlist.txt` (`+` lines only, skips names ending `_separator`; priority = list order, high to low), indexes plugin files from game Data, then enabled mods low-to-high, then `overwrite`, and calls `fo4::loadorder::read_plugin_list`, `read_ccc`, `assemble_load_order(..., OfficialTable::ae)`; failures become `Mo2LoadOrderInvalid`. Output `ProfileSnapshot` has `loadOrder`, `plugins`, `missingPlugins`, `assetRootsLowToHigh`. Tests: `native/tests/unit/Mo2ProfileTests.cpp`, `AppInteropTargetTests.cpp`, `AppInteropRollbackTests.cpp`.

### 6. Build and tests

Native (CMake >= 3.25, C++23, Ninja): `cmake -S PluginEditTool/native -B build/native -G Ninja -DFO4IDE_BUILD_TESTS=ON`. Options: `FO4IDE_BUILD_TESTS` (ON), `FO4IDE_ENABLE_SANITIZERS` (OFF), `FO4IDE_BUILD_HTTP` (ON). Presets (`native/CMakePresets.json`): `linux-clang-debug`, `linux-clang-asan`, `linux-clang-release`, `windows-msvc-debug`, `windows-msvc-release`. FetchContent deps in `native/cmake/Dependencies.cmake`: nlohmann_json, spdlog, reproc, httplib, WebView2 SDK (Windows), Catch2 (tests); zlib pinned by 40-hex `GIT_TAG` in `native/CMakeLists.txt` (CI greps this tag and fails if not exactly one).

Tests: Catch2 via `fo4ide_add_test(name source library)` in `native/tests/CMakeLists.txt`, plus the ctest `fo4ide_native_mcp_smoke` (python). Roughly 100 files in `native/tests/unit` (RadiclePlugin*Tests, Condition*Tests, Papyrus*Tests, Graph*Tests, F4SE*Tests, Mo2ProfileTests, Mcp/Rpc/EventBus/Logger/Settings/Process/Nif/Material/Audio). `integration/HttpServerTests.cpp`. Per-test default TIMEOUT 10 s; f4se 120 s; papyrus corpus 300 s.

Real-data tests: gated on `FO4_DATA_DIR` (CI var `FO4_DATA_DIR_LINUX`) and selected by `REAL_DATA_TEST_REGEX` in `../.github/workflows/native-ci.yml:58` (names matching `*RealData.`, `QuestRuntimeReal.`, `NifCorpus.`, `VmadWriterOfficial.`, etc.). F4SE corpus sweep uses env `FO4RE_F4SE_SRC`. Windows jobs run them on runner label `fo4-data` (`native-windows-real-data`); other jobs exclude them with `-E`.

CI (`native-ci.yml`): `native-linux`, `radicle-windows-cross` (xwin clang-cl), `plugineditool-native-windows-cross` (builds target `fo4ide`), `native-windows-tests` (Release + Debug, `-DRADICLE_BUILD_TESTS=ON -DRADICLE_BUILD_TOOLS=ON -DRADICLE_CSHARP_TESTS=ON -DRADICLE_WARNINGS_AS_ERRORS=ON`), `native-windows-real-data`. Path filters include `PluginEditTool/web/**`, `PluginEditTool/FO4RecordEditor*/**`, `PluginEditTool/Mutagen/**`, `PluginEditTool/docs/**`. `plugin-editor-ci.yml` (managed tests only): jobs `windows` (restore `--locked-mode`, build+test both test projects, Release) and `linux` (Core.Tests only), dotnet 10.0.401 SDK installed (Windows also 9.0.317).

Web: `cd web && npm ci && npm run build` (`tsc -b && vite build`); `npm test` = vitest (jsdom, Testing Library); `npm run lint`. Stack: React 19, react-router-dom 7, three.js 0.180 (NIF/cell viewports), react-markdown, lucide-react, Vite 8, TS 6. `grep npm` in `../.github/workflows/*.yml` finds nothing: web tests/build are not run by the workflows.

Managed tests: `dotnet test FO4RecordEditor.Tests` and `FO4RecordEditor.Core.Tests` (CI invocations above). Core.Tests has `packages.lock.json`, `GitVersion` is disabled in CI via `DisableGitVersionTask=true` (../docs/code-notes/GARDEN-TOOLS.md:1641).

### 7. Packaging and release

- `package.ps1 [-Version -SkipWeb -SkipCkWiki -SkipAudioTools -IncludeBaseScripts]`: builds web, configures/builds native into `native/build/package-win-x64`, stages `dist/FO4RecordEditor-<ver>`, bundles optional tools (niftool from `../tools/niftool`, Texconv from a `TES5Edit-dev-4.1.6` path, audio converter, CK wiki, optionally base Papyrus scripts), adds docs + sample MCP config, zips. Hardcoded absolute developer-machine source paths for CK wiki and audio tools.
- `publish.bat`: web build + native build + deploy to a hardcoded developer-machine mod list folder (kills running exe first). Needs VS 2022 C++ tools, CMake, Python, Node.
- `packaging/linux/build-deb.sh <version>`: npm build, cmake `--target fo4ide`, stages `.deb` (binary, launcher `fo4recordeditor.sh`, `.desktop`, `web/dist`, LICENSE, THIRD_PARTY_NOTICES). Needs `dpkg-deb`, `fakeroot`.
- Requirements stated in README: WebView2 runtime + VC++ redist on Windows; no .NET. Note `docs/internal/RUNNING_ON_LINUX.md:28` still says the deb build needs `dotnet` 9 SDK (stale vs README/ARCHITECTURE).

### 8. Conventions, guards, docs

- Docs: user docs `README.md`, `docs/MCP_SETUP.md`. Contributor facts `docs/internal/ARCHITECTURE.md` (native, current, rely on this), `docs/internal/INDEX.md` (hub; STALE, still describes "built on a patched Mutagen" and 53 tools), `KNOWLEDGE.md`, `CONFLICT_ENGINE.md`, `GRAPH.md`, `PAPYRUS.md`, `XEDIT_PARITY_AUDIT_2026-08.md`, `HISTORY.md`, `MUTAGEN.md`/`MUTAGEN_FORK_COMPARISON.md`/`SPRIGGIT.md` (Mutagen/Spriggit reference, pre-native). Several internal docs mention absolute developer-machine paths from the pre-repo tree.
- Code-notes live at repo root `../docs/code-notes/` (e.g. `GARDEN-TOOLS.md`, `pet-condition-codec.md`, `radicle-managed-ci.md`), not under PluginEditTool.
- Native: warnings via `native/cmake/CompilerWarnings.cmake` (`fo4ide_enable_warnings`) on every target; sanitizers opt-in; UTF-8 code page + `utf8.manifest` on Windows. Error style: `Result<T>` = `std::expected<T, Error{code, message}>` with PascalCase codes (`PluginNotLoaded`, `PluginMissing`, `Mo2PluginsMissing`, `Mo2LoadOrderInvalid`, `CompactToEslUnavailable`, `ConditionPathUnsupported`, `QuestAuthoringCoverageIncomplete`); unverified structures fail closed with capability errors rather than guessing offsets. "Generic first": prefer schema declarations over per-record UI special cases; keep legacy React naming in `PluginBackendTarget.cpp`, not in Radicle.
- Adding an MCP tool: find owning native target/method, register schema in the registry, dispatch to the same service, state mutation semantics, add a native test (ARCHITECTURE.md "MCP").
- `tools/check-source-policy.py` (+`test_check_source_policy.py`): scans text sources (excluding `Mutagen`, `TES5Edit-dev-4.1.6`, bin/obj/dist/node_modules) for attribution-policy patterns (actor/verb regexes). this map did not find a workflow that calls it by name outside `tools/ci/ghrunnerd.py`.
- `web/src/index.css` is the source of truth for xEdit-style conflict colors.
- AI: the host embeds no provider/chat backend; `hostObjects.chat` must not return; right panel (`ChatPanel.tsx`, `NativeMcpPanel.tsx`) only hands prompts to an external MCP client.

### 9. Managed (Mutagen) path vs native (Radicle) path

State at this snapshot: two coexisting implementations of the same feature set.

| | Native (shipping) | Managed (legacy) |
|---|---|---|
| Code | `native/` | `FO4RecordEditor`, `.Core`, `.Server` |
| Plugin engine | Radicle `fo4` (C++, in-process) | Mutagen `Fallout4Mod` object graph via `MutagenLoader.cs` |
| Generic element edit | Radicle schema descriptors; unsupported paths return capability errors | .NET reflection over Mutagen objects (`ElementService.cs`) |
| UI host | `fo4ide_window` (WebView2 / WebKitGTK) + HTTP | WPF (`FO4RecordEditor`) or ASP.NET `Server` (Linux, same React bundle, same `--mcp`) |
| MO2 | `Mo2Profile.cpp` | `FO4RecordEditor.Core/Services/Mo2ProfileLoader.cs` |
| ESL compaction | `RadiclePluginCompact.cpp` `compactToEsl`, `RadiclePluginEsl.cpp` `checkEslEligibility` (uses `fo4::esl_eligibility`, `lowered_light_next_form_id`) | `WriteService.Esl.cs` `CompactToEsl`, `CheckEslEligibility`, `SetLightFlag` |
| Tests | Catch2 in `native/tests` | xunit in `*.Tests` |

Per the docs, the managed tree is "historical implementation/oracle material" kept "only while native parity and regression coverage are completed" (README.md, ARCHITECTURE.md). It is not a runtime dependency of `fo4ide`; nothing in `native/CMakeLists.txt` references it. The managed projects have no reference to Radicle (grep for "radicle" in PET `*.cs`/`*.csproj` outside `Mutagen/` is empty), so the "managed Mutagen loader" is not bridged to Radicle in PET code. Radicle has its own C# bindings under `../Radicle/bindings/csharp` (see ../docs/code-notes/radicle-managed-ci.md) but PET does not use them.

Legacy managed ESL compaction (`FO4RecordEditor.Core/Services/WriteService.Esl.cs`, 168 lines): `CompactToEsl(plugin, env)` runs inside `PluginGraphGate.Run`, opens the Mutagen mod (`EnsureOpen`), computes the ESL slot range (`EslSlots`: first/last/capacity/range), refuses duplicate FormKeys (`SharedFormKey`) and overflow beyond capacity, then assigns each out-of-range native record the lowest free ID in range, rekeys via `TryRekeyRecords(...)` (remapping all references), sets `ModHeader.Stats.NextFormID`, invalidates `MutagenLoader` index and reports "in memory until save_plugin"; the user then saves to a `.esl` path. If nothing is out of range it only lowers HEDR next ID (`LowerHedrNext`). The native version mirrors this contract (same range arithmetic, no-op lowering of nextFormID via `fo4::lowered_light_next_form_id`, write guard, `OpLock` write lock). Native tests: `RadiclePluginCompactTests.cpp`, `RadiclePluginEslTests.cpp`. `.github/esl-formid.parts/` is unrelated to the code path as far as could be told (see 10).

Vendored `Mutagen/` (top-level of PET): a trimmed copy of Mutagen-Modding/Mutagen (GPL-3.0) holding only `Mutagen.Bethesda.Kernel`, `.Core`, `.Fallout4`, plus `Directory.Build.props/targets` and `Directory.Packages.props`. Referenced only by `FO4RecordEditor.csproj:19`, `FO4RecordEditor.Core.csproj:15`, `FO4RecordEditor.Server.csproj:21` (`ProjectReference ..\Mutagen\Mutagen.Bethesda.Fallout4\...`). No upstream version/commit pin is recorded in the tree read. Patches: `docs/internal/MUTAGEN_FORK_COMPARISON.md` lists two upstream-diff commits (`a464f6828` short-backed enums for EPF3 perk flags; `7034bf2de` Next-Gen BA2 v7/v8 parsing in `Ba2Reader.cs`) and a `Directory.Build.targets` NoWarn/no-pack block; `THIRD_PARTY_NOTICES.md` says seven functional fixes (e.g. `IArchiveListingDetailsProvider.cs`) and that clean upstream is not a drop-in on Linux. Mirror noted there: gitlab.com/nomadsreach/mutagen (private; branches `plugineditool-full`, `godot-vendored`).

Retirement plan: the stated plan is "native cutover is done; keep C# only until parity/regression coverage completes", then delete the managed projects and `Mutagen/` (no step list). searched PET, `../docs` and `../.github` for "#182", "issues/182", "GT-182", "managed Mutagen loader" and "retire Mutagen": no hits, so Could not verify any issue #182 link from files. Repo-level facts that matter for retirement: CI still builds and tests the managed projects (`plugin-editor-ci.yml`), `native-ci.yml` path filters still watch `PluginEditTool/Mutagen/**` and `FO4RecordEditor*/**`, `check-source-policy.py` excludes `Mutagen`, and `docs/internal/*` plus `../docs/code-notes/GARDEN-TOOLS.md` cite Mutagen behavior as an oracle (e.g. Quest alias parsing in `Quest.cs:FillBinaryAliasParseCustom`). Also `FO4RecordEditor.Tests/MutagenLoaderCacheTests.cs`, `FO4RecordEditor.Core.Tests/EnvironmentLoadTests.cs` etc. exercise Mutagen directly.

### 10. Gotchas

- Docs contradict each other: `README.md` and `ARCHITECTURE.md` say native-only; `INDEX.md`, `MCP_SETUP.md` (e.g. line 464 "Mutagen strips it"), `RUNNING_ON_LINUX.md`, `MUTAGEN.md` still describe the managed build. Trust ARCHITECTURE.md and the CMake files.
- Many "FO4RecordEditor" names are the legacy C# projects; the native exe is target `fo4ide` renamed to `FO4RecordEditor` by `OUTPUT_NAME`.
- PET's native code uses Radicle's C++ API directly (`fo4` target), not `radicle_c`/P-Invoke. The C# bindings and the C ABI are for other consumers (GardenEngine).
- Configure needs the sibling `../Radicle` tree; building PET alone from a PET-only checkout fails with FATAL_ERROR.
- `native/CMakeLists.txt` unsets `radicle_c` shared-lib versioning because the author's build volume is ntfs3 without symlinks; harmless elsewhere.
- Test hooks: `fo4ide_plugin` is built with `FO4IDE_TEST_HOOKS` when tests are ON, so test and shipping builds differ in that target.
- `.github/esl-formid.parts/` and `.github/integrity.parts/` are split opaque blobs (first bytes look like base64 gzip `H4sI...`). this map did not decode them or find any reference to them in the repo; purpose unverified.
- Packaging scripts hardcode developer machine paths (absolute developer-machine paths).
- Web tests and build are not run by any workflow; only developers/packaging run `npm`.
- Managed projects target different frameworks (App net9.0-windows, Core net8.0, Server net9.0); CI installs dotnet 10.0.401 plus 9.0.317 on Windows.
- Mo2 `modlist.txt` parsing only keeps `+` (enabled) entries and drops separators by name suffix; a mod literally named `*_separator` would be skipped.

### Could not verify
- Issue #182 and the Mutagen retirement plan beyond the README/ARCHITECTURE statements.
- Exact upstream Mutagen version/commit.
- MCP tool count; contents/purpose of `.github/*.parts`; whether `check-source-policy.py` runs in CI.
- Whether native ESL compaction matches the managed algorithm in every edge case (read the start of both, not the full native body).

## BGS, CI and repo level
All facts below were read from files in the nomad/dev 9d6c622c tree.

### 1. BGS (BehaviourGraph Studio)

#### Purpose
Standalone desktop editor (Avalonia-style WinExe, net8.0) for Fallout 4 Havok behaviour/skeleton/animation `.hkx`, plus NIF skin/physics authoring and an AI assistant pane (`BGS/README.md`). Released via Nexus. Consumes Radicle only for BA2 reading and plugin-list ordering.

#### Layout (`BGS/`)
- `app/` - `BehaviourStudio.csproj` (AssemblyName `BehaviourGraphStudio`, WinExe, net8.0, `PublishSingleFile=true`, `<Version>1.3.0</Version>` at line 18). About 130 `.cs` files: `MainWindow.*.cs` partials (Open, Edit, Run, Layout, Search, Playback, Pose, Chain, ...), `GraphView.*`, `SkeletonView.*`, `Assistant*.cs` / `Codex*.cs` / `Claude*.cs` / `Opencode*.cs` (assistant sessions and CLI clients), `BgsMcpBridge.cs`/`BgsMcpProtocol.cs` (MCP bridge), `RigAuthoring*.cs`, `Program.cs`, `Settings.cs`, `BuildInfo.cs`, `app.manifest`, `packages.lock.json`.
- `src/` - `BehaviourGraph.Core.csproj` (net8.0, AssemblyName `BehaviourGraph.Core`, RootNamespace `OpenCommonwealth.Services`, `EnableDefaultCompileItems=false`; compiles `InputFilePolicy.cs`, `Archive/*.cs`, `Hkx/*.cs`, `Nif/*.cs`). Package: BepuPhysics 2.4.0. ProjectReference to `../../Radicle/bindings/csharp/Radicle.Interop/Radicle.Interop.csproj`. Embedded resources `Hkx/HavokClassLayouts.json`, `Hkx/HavokClassTypes.json`. `InternalsVisibleTo`: symrm, BehaviourGraphStudio, BehaviourGraph.Tests, uismoke.
  - `Archive/`: `Ba2.cs`, `GameData.cs`, `EsPlugin.cs`, `Mo2ScanData.cs`, `ScanReport.cs`, `SubgraphHash.cs`, `SubgraphIndex.cs`.
  - `Hkx/`: behaviour graph core (about 130 files): parser/readers (`HkxBinaryReader.*`, `HkxBehaviorParser.cs`, `TagfileImage.cs`, `PackfileConverter.cs`), model (`BehaviourGraphModel.cs`, `NativeGraphModel.cs`), editors (`StateEditor`, `GeneratorEditor`, `BindingEditor`, `SymbolEditor`), save pipeline (`NativeSave.*`, `DocumentSaveTransaction`, `SaveVerifier`, `GraphMutationTransaction`), project checks (`ProjectCheck/Chain/Report/Search`), Havok physics/ragdoll (`HavokPhysics*`, `RagdollSimulation.*`), skeleton (`SkeletonAuthoring/Document/Mirror/MapperReader/MapperAuthor`), animation (`AnimationEdit/Pose`, `Spline*`), assistant inspection/mutation (`Assistant*.cs`).
  - `Nif/`: `NifFile.cs`, `NifShape.cs`, `NifPhysics.cs`, `SkinEdit(.Mirror).cs`, `SkinSave.cs`, `SkinValidator.cs`, `SkinnedMesh.cs` (BGS-own managed NIF code; Radicle has its own NIF reader, `Radicle/docs/NIF.md`).
- `tools/` - `mcp/` (`BehaviourGraph.Mcp.csproj`, `McpTools.cs`, `McpPathPolicy.cs`, `Program.cs`: MCP host), `symrm/` (CLI + native regression harness: `Program.cs`, `CliEntry.cs`, `Tests.cs`, `SymrmHelp.cs`, `GameDefaults.cs`, `Png.cs`, `samples/`; doc `guide/cli/symrm.md`), `uismoke/` (UI smoke harness; files `AssistantEditorSmoke`, `AssistantLifecycleSmoke.*`, `AuditLifecycleSmoke`, `MultiChatSmoke`, `NativeAuthoringSmoke`, `RagdollLifecycleSmoke`), `tests/` (xUnit `BehaviourGraph.Tests.csproj`, ~110 test files, `fixtures/`), `ci/package.py`, `sync_hkx_readers.sh`.
- `docs/` (13 design/contract docs: `ai-mcp-integration`, `mcp-*-contract`, `assistant-*`, `havok-tagfiles`, `ragdoll-preview-engineering`, `skeleton-mapping-authoring`, `animation-set-assistant-roadmap`). `guide/` (`cli/symrm.md`, `rig-authoring.md`, `scan/{README,scan-archives,scan-clips,scan-modlist}.md`). `licenses/BEPU-LICENSE.md`, `THIRD_PARTY_NOTICES.md`, `CHANGELOG.md`, `CONTRIBUTING.md`, `README.md`, `RELEASE-NOTES.md`, `RELEASE-NOTES-NEXT.md` (draft for 1.3.0).
- Solution: `BehaviourGraphStudio.slnx` lists app, tools/mcp, tools/symrm, tools/uismoke, tools/tests (note: `BehaviourGraph.Core` is not listed; it comes in by ProjectReference).
- SDK: `global.json` pins .NET SDK `8.0.424`, `rollForward: disable`. `Directory.Build.props` turns on `RestorePackagesWithLockFile` (CI restores use `--locked-mode`, so `packages.lock.json` per project must be current).

#### How BGS uses Radicle.Interop
- `src/Archive/Ba2.cs`: `using Radicle.Interop; using Radicle.Interop.Native;`. `Ba2.Open` calls `Ba2Archive.Open(path)`, rejects non-`Ba2Kind.General` (DX10 texture archives refused), requires `HasNameTable` when count > 0, reads `archive.Entry(i)`, normalises `\` to `/`, refuses rooted paths, `:` in names, entries > 4 GiB. `Read(entry)` -> `_archive.Extract(index)`. `RadicleException` is wrapped into `InvalidDataException`. `Ba2.IsNativeLoadFailure` (DllNotFoundException, BadImageFormatException, EntryPointNotFoundException, TypeInitializationException) is turned into "the Radicle native library could not be loaded: ...". Test: `tools/tests/Ba2RadicleTests.cs`, `ArchivePathRegressionTests.cs`.
- `src/Archive/GameData.cs` (~line 533, `OrderByPlugins`): orders archives by `LoadOrderFiles.ReadPluginList(pluginsPath)`; on `RadicleException`, `FormatException`, `OverflowException` or native load failure it falls back to the unordered archive list. Test: `GameDataPluginOrderTests.cs`.
- Failure mode when the native lib is absent: project still builds (warning only); BA2 reads fail at runtime.

#### How the Radicle native library gets into BGS builds/packages
- `BGS/Directory.Build.props`: `RadicleNativeDir` default `$(MSBuildThisFileDirectory)../build/radicle-bgs/native` (repo-root `build/`); `RadicleNativeFileName` = `radicle.dll` on Windows, `libradicle.so` elsewhere.
- `BGS/Directory.Build.targets` (applies to every BGS project): if the file exists, adds a `None` item linked into the output with `CopyToOutputDirectory=PreserveNewest`, `CopyToPublishDirectory=PreserveNewest`, `ExcludeFromSingleFile=true` (so it sits next to the single-file exe). Otherwise target `WarnMissingRadicleNative` (BeforeBuild) emits a warning.
- CI builds it first in `bgs-ci.yml` (cmake target `radicle_c`, `-DRADICLE_BUILD_TESTS=OFF -DRADICLE_BUILD_TOOLS=OFF -DRADICLE_WARNINGS_AS_ERRORS=ON`, into `build/radicle-bgs`, copied to `build/radicle-bgs/native/`). Manual recipe: `BGS/README.md` "Native Radicle library" (lines ~97-113); override with `-p:RadicleNativeDir=<folder>`.
- `BGS/tools/ci/package.py publish-check` (53 lines): args `directory --commit <40-hex> --runtime win-x64|linux-x64 [--archive zip]`; requires the package dir beneath BGS; fails with "Package is missing the native Radicle library" if `radicle.dll`/`libradicle.so` is absent; runs the published exe with `--version` and checks `Studio <csproj Version> `, `build <commit[:12]>` and the runtime in the output; deletes `*.pdb`; writes `build-info.json`; optionally writes a zip plus `.sha256`.

### 2. CI (`.github/workflows/`)

Runner labels (see `ci/runners/README.md`): `[self-hosted, Linux, X64, garden]` (WSL Ubuntu, 2 slots each), `[self-hosted, Windows, X64]`, `[fo4-data]` (user's PC with Fallout 4 Data; only the real-data job runs there). All on one 12-thread Windows host; `CMAKE_BUILD_PARALLEL_LEVEL=4`. Hosted runners deliberately avoided (free minutes exhausted). Actions are pinned by SHA; upload-artifact is `code.forgejo.org/forgejo/upload-artifact@...v5`. Concurrency: per-ref group, `cancel-in-progress: true`, but main runs use `github.sha` in the group so they always finish.

#### native-ci.yml (852 lines)
Triggers: `push` to `main` only (paths-ignore: `BGS/**`, `PluginEditTool/web/**`, `PluginEditTool/FO4RecordEditor*/**`, `PluginEditTool/Mutagen/**`, `PluginEditTool/docs/**`, `PluginEditTool/images/**`, `docs/**`, `**/*.md`); `pull_request` (any branch, any base, no path filter); `workflow_dispatch` with input `lane` = `full` (default) | `radicle-cross`. Work-branch pushes intentionally do not run (avoids double slots with the PR run). Env `REAL_DATA_TEST_REGEX` (line ~50) selects the real-data ctest names (`*RealData.`, `NifCorpus.`, `BehaviorReal.`, `DialogueReal.`, `NavmeshReal.` ...).
Jobs:
1. `native-linux` "Native (Linux, Debug)" - garden, 60 min, skipped when lane=radicle-cross. Configures `PluginEditTool/native` (Ninja, Debug, `FO4IDE_BUILD_TESTS=ON`, `RADICLE_BUILD_TESTS/TOOLS/CSHARP_TESTS=ON`, `RADICLE_WARNINGS_AS_ERRORS=ON`, ccache launcher; 3 configure retries). Restores a RID consumer for `Radicle.Interop` on linux-x64 (setup-dotnet from `Radicle/bindings/csharp/global.json`). Checks `FO4_DATA_DIR_LINUX` repo var (fails if set but missing ESMs/BA2; if unset, real-data tests skip). `ctest --parallel 4`, then `unshare --user --map-root-user --net python3 Radicle/bindings/csharp/tools/run_managed_tests.py --build-dir build/native --network-isolated`. Artifact on failure `native-linux-failure` (3 days).
2. `radicle-windows-cross` "Radicle (Windows cross, clang-cl/xwin, build-only)" - garden, 45 min, `if event_name != 'pull_request'` (main push and dispatch, incl. radicle-cross lane). Builds pinned static zlib then Radicle with `cmake/xwin-clang-cl.cmake`, tests OFF (compile+link only; Wine retired). Artifact `radicle-windows-x64` (exe/dll/lib + build-info.txt), 7 days.
3. `plugineditool-native-windows-cross` - garden, 45 min, skipped on PRs and on lane=radicle-cross. Cross-builds `build/fo4ide-win/bin/FO4RecordEditor.exe` (tests OFF, pinned zlib fallback as package.ps1). Artifact `fo4recordeditor-windows-x64`, 7 days.
4. `native-windows-tests` "Native tests (Windows, MSVC)" - `[self-hosted, Windows, X64]`, 90 min, cmd shell, skipped on radicle-cross. Release Ninja build (`FO4IDE_BUILD_TESTS`, `FO4IDE_BUILD_HTTP`, `RADICLE_*` tests/tools/csharp, zlib from `ZLIB_SRC`), `ctest -E $REAL_DATA_TEST_REGEX` (FO4_DATA_DIR removed). Artifact `radicle-msvc-release-tests` (fo4_tests.exe, radicle.dll, build-info.txt), 7 days, `continue-on-error`. A second Debug configure/build/ctest (`/Z7`) runs only when `event_name != 'pull_request'`. Ends with `dotnet build-server shutdown`.
5. `native-windows-real-data` "Native real-data tests" - `[fo4-data]`, 90 min. Prints Fallout4.exe version and Steam buildid; step `fo4` validates `FO4_DATA_DIR` and sets `has_data`; every later step is gated on `has_data == 'true'` (skip by design with a `::notice::` if unset). Builds only `fo4_tests` and `fo4ide_test_sound_semantic_real`; `ctest -R $REAL_DATA_TEST_REGEX --no-tests=error` (+ `-E $CTEST_EXCLUDE_REGEX` set earlier ~line 690).

#### bgs-ci.yml (193 lines)
Triggers: `push` main with paths `BGS/**`, `Radicle/**`, the workflow file; `pull_request` with the same paths; `workflow_dispatch`. Complements native-ci (which ignores `BGS/**`).
Job `verify` "BGS build and test (<rid>)", 45 min, `fail-fast: false`, matrix: `linux-x64` on `[self-hosted, Linux, X64, garden]` (bash, python3) and `win-x64` on `[self-hosted, Windows, X64]` (cmd, python). Working dir `BGS`. Steps: checkout; setup-dotnet 8.0.424 (tool-cache install dir); build native Radicle (`radicle_c`) per platform (Windows: vswhere + vcvars64, caches pinned zlib from the GIT_TAG in `PluginEditTool/native/CMakeLists.txt` under `garden-src`, builds `zlibstatic`, passes `ZLIB_*`); `dotnet restore --locked-mode` for Core, app, symrm, tests, uismoke, MCP; build MCP host and app (Release); `symrm test`; xUnit `BehaviourGraph.Tests`; uismoke (default, `--assistant`, `--multichat`, `--layout-smoke`, `--rig-authoring`); `dotnet publish` self-contained per RID with `-p:BuildCommit=<sha>` to `publish-check`; `python tools/ci/package.py publish-check`. No artifact upload (package is verified, not stored). No real-game-data dependency.

#### plugin-editor-ci.yml (136 lines)
Triggers: `push` main with paths `PluginEditTool/FO4RecordEditor*/**`, `PluginEditTool/Mutagen/**`, `PluginEditTool/README.md`, `PluginEditTool/docs/MCP_SETUP.md`, the workflow; `pull_request` (no path filter); dispatch. Env `DisableGitVersionTask=true`.
- `windows` "Managed tests (Windows)" `[self-hosted, Windows, X64]`, 60 min, cmd: SDKs 10.0.401 + 9.0.317, restore `--locked-mode`, build and test `FO4RecordEditor.Tests` and `FO4RecordEditor.Core.Tests` (TEMP set to `runner.temp` because the home folder is a reparse point), trx results; failure artifact `plugin-editor-windows-failure` (3 days). Uses the full-URL checkout to avoid the shared action-cache `shallow.lock` race with native-ci.
- `linux` "Core managed tests (Linux)" garden, 45 min: SDK 10.0.401, only `FO4RecordEditor.Core.Tests`; failure artifact `plugin-editor-linux-failure` (3 days).

#### ci/runners and cmake
- `ci/runners/README.md`: runner inventory table (labels, slots, env), the 2026-10-04 CI audit (ccache `CCACHE_BASEDIR`, PRs 413-418, rule "never locate files from `__FILE__` with ccache base_dir"), and how to re-run jobs without the web UI.
- `ci/runners/clear-stale-git-locks.ps1`: removes `shallow.lock` older than 5 min under the runner's act cache. `alias-profile-path.ps1`: rewrites PATH entries under the real profile to the runner home alias. Both installed host-side.
- `cmake/xwin-clang-cl.cmake`: Linux-to-Windows x64 cross toolchain (clang-cl, lld-link, llvm-lib/rc/mt, target `x86_64-pc-windows-msvc`); `XWIN_ROOT` default `/opt/xwin`; fatal error if any CRT/SDK subdirectory is missing. Used by the two cross jobs.
- Local CI guard (README): `ci-guard.mjs` in agent hooks, rules: no manual dispatch on PR branches, batch pushes, build/test locally before pushing C++/CMake changes (`node ~/.codex/hooks/ci-guard.mjs --test build/radicle build/fo4ide`).

### 3. Repo-level

#### Top-level tree
`BGS/`, `PluginEditTool/`, `Radicle/`, `ci/`, `cmake/`, `docs/`, `.github/workflows/`, `README.md`, ignored `build/`.

#### README.md (31 lines)
Tool table (Radicle, PluginEditTool, `Resources/RE-Work`), Radicle roadmap link, and the "CI guard" section. Gotchas: `Resources/RE-Work/` does not exist in this tree (dead link); the table has no BGS row; the roadmap link points to a github.com G-A-R-D-E-N org, while CI/Forgejo is git.nomadicinteractive.dev.

#### docs/
- `docs/code-notes/GARDEN-TOOLS.md` (1960 lines) - main code-notes ledger, entries anchored `path:symbol` (Radicle schema, PET native, CI, F4LODGEN removal at line 1944).
- `docs/code-notes/BGS.md` (339) - BGS notes (e.g. headless-scan JSON contract, `src/Archive/ScanReport.cs`, issue #192).
- `174c-pending-lifecycle-audit.md` - matrix and call-path audit of #174C pending-lifecycle gaps.
- `info-copy.md` - INFO record copy via `form_links.cpp:visit_link_offsets`.
- `pet-condition-codec.md`, `radicle-condition-json.md` - typed CTDA condition codec / JSON round-trip (`ConditionCodec.cpp`).
- `radicle-avif-perk.md` - AVIF/PERK evidence boundary and sources.
- `radicle-climate-writer.md`, `radicle-dialogue-writer.md` (DIAL/INFO/SCEN), `radicle-sound-writer.md` (SNDR/SOUN/MUSC/FSTP/FSTS), `radicle-water-writer.md`, `radicle-vegetation-writer.md`, `radicle-vegetation.md`, `radicle-navmesh-writer.md`, `radicle-package-writer.md` (package/combat style/idle), `radicle-vmad-writer.md`, `radicle-vmad-array-elements.md`, `radicle-vmad-opaque-word-20261008.md` - per-record-family writer/reader notes.
- `radicle-field-writer.md` (W0a) and `radicle-subrecord-authoring.md` (W0b) - field/subrecord authoring design; anchored to `c_api.h:RADICLE_ABI_VERSION` and `fields.json`.
- `radicle-copy-cell-children.md` (GT-200), `radicle-ctda-contiguous-run.md`, `radicle-quest-structure.md` (#423), `radicle-fragment-counts.md` and `radicle-papyrus-census.md` (Papyrus/VMAD measurements), `radicle-parity.md` (GardenEngine #114 parity), `radicle-semantic-editor.md` (shared field addressing), `radicle-managed-ci.md` (managed binding CI).
- `docs/audits/bgs/2026-09-12/` - BGS audit: `README.md`, `report.md`, `evidence/{rerun,results,scan-overrides,scan-unreadable}.txt`, `repro/{Audit.csproj,Program.cs,.gitignore}` (reproducer project).
- `docs/evidence/bgs-ui/` - `README.md` (BGS UI layout root-cause note) plus before/after PNGs for animation, chain, command, playback.

#### Branch model hints
- Workflows only push-trigger on `main`; PRs run on any base. Memory/org policy: GT PRs target `nomad/dev`, `main` is nomad-only. Snapshot is the merge of PR 547 into nomad/dev. Local worktree branches seen: `chore/remove-f4lodgen`, `ci/forgejo-artifacts`, `ci/windows-tests`, `feat/359-xedit-parity`, `feat/radicle-nif-reader` and others (not authoritative for the model).
- main/nomad/dev guard is not encoded in-repo; `grep nomad/dev BGS/CONTRIBUTING.md` found nothing.

#### F4LODGEN / f4lod references (outside build dirs; case-insensitive)
- `docs/code-notes/GARDEN-TOOLS.md:1944` (heading "F4LODGEN removed") and `:1946` (note: retired 2026-10-11, last commit containing it `06db28942fddd8d6e1f1470d3ef31bc28d57c1a3`, recover with `git show 06db28942...:F4LODGEN/<path>`; says README row and native-ci `paths-ignore` entry were removed - both verified gone).
- `Radicle/docs/code-notes/Radicle.md:862` (read_nif note mentions F4LODGEN).
- `Radicle/docs/FO4_PARITY.md:375` (inventory row mention).
- `Radicle/docs/NIF.md`: 31 lines cite `F4LODGEN@06db2894:<lines>` as the NIF reader provenance (lines 11-12, 24, 26-27, 36, 38, 41-42, 44, 47, 50, 53, 56-57, 69, 72-74, 78, 85, 87-88, 90, 96, 200, 207, 230, 233, 237, 244). Intentional: citations pinned to that commit "because F4LODGEN is being removed from main".
- No hits in workflows, CMake, source, README, BGS. No F4LODGEN directory exists.

### Gotchas
- BGS builds without the Radicle DLL (warning only) but BA2 reads and plugin-order then fail or degrade; the `package.py` check is the hard gate in CI.
- `RadicleNativeDir` default is relative to `BGS/` (`../build/radicle-bgs/native`), so a build elsewhere must override it.
- bgs-ci `win-x64` job runs `cmd` as default shell; the Radicle step overrides to powershell.
- `native-ci` ignores `BGS/**` and `docs/**` on main pushes, but PR runs have no path filter, so BGS-only PRs still run all of native-ci plus bgs-ci.
- Windows MSVC Debug pass and both cross lanes do not run on PRs; real-data job needs the `fo4-data` runner and silently skips (notice) if `FO4_DATA_DIR` unset.
- Runner paths in `ci/runners/README.md` are host paths, not contributor paths; `FO4_DATA_DIR_LINUX` is a repo variable, `FO4_DATA_DIR_WINDOWS` is intentionally unused.

