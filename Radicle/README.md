# Radicle

Native Fallout 4 plugin SDK. The first root a plugin puts down, and a radiation pun.

C++ namespace is still `fo4`. Radicle is the native xEdit-style plugin backend replacing Mutagen for PluginTool / PluginEditTool and supplying structured game data to GardenEngine. It parses and exposes records, supports proven field and record edits, and serializes changes while preserving untouched data.

Stored layouts may be established from xEdit definitions, official plugins, existing Radicle behavior, CK/CKRE and synthetic tests. Runtime RE is required for unresolved layouts, unproven safe mutation or intended runtime behavior; it is not a blanket prerequisite for plugin editing. Unknown layouts stay opaque or refuse mutation/remapping. Mutagen remains a compatibility oracle.

[#86](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/86) governs the product roadmap; [#287](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/287) and [the parity matrix](docs/FO4_PARITY.md) govern READ / WRITE / ADD / REMOVE / CREATE / REMAP / VALIDATE / OPAQUE coverage. A reader alone does not establish editing support. [#43](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/43) supplies reusable schema infrastructure where proven gaps justify it.

## Status

Generic lossless plugin container, FormKey identity, preservation writer, master remap, schema-aware FormLink rewrite, load-order/conflict graph, stable C ABI, and the native PluginEditTool backend are implemented. FormLink coverage includes `NAME`/`KWDA`/`CTDA`/`VMAD`, discriminator-controlled package/PERK payloads, and structured slots such as `CNTO`/`LVLO`/`XTEL`.

The first machine-readable authoring schema is active under `schema/fallout4/fields.json`. A deterministic generator emits record-qualified C++ scalar descriptors and CMake checks that committed generated output is current. `REFR\\HealthPercent` / `XHLT` is the first field using this path; typed EYES and LVSP editing remain structured record models rather than scalar byte patches.

BA2 support covers `BTDX/GNRL` general resources and `BTDX/DX10` textures at versions 1, 7 and 8: list/read, stored and zlib-compressed chunks, path lookup, write/read round trips, and texture extraction that prepends a reconstructed DDS header to `.dds` entries whose payload does not already start with `DDS ` (other entries are returned as stored). PluginEditTool's native Archive panel uses the Radicle reader and rejects any non-empty GNRL or DX10 archive without a string table. Synthetic coverage is supplemented by installed-data tests for six named version/content combinations, which skip unless `FO4_DATA_DIR` is set; see `docs/BA2.md` for measured corpus scope and remaining opaque metadata.

Load order files: `plugins.txt`/`loadorder.txt` and `Fallout4.ccc` parsing, and assembly of the loaded-file list (stage order, master hoist, checked state and findings) through `fo4::loadorder`, the `radicle_plugin_list_read`, `radicle_ccc_read` and `radicle_load_order_assemble` exports, and `Radicle.Interop.LoadOrderFiles`. The OG and AE official tables are verified statically; NG 1.10.984 is verified statically against the unpacked image (same assembly as AE with the 7-name official table; GARDEN-TOOLS #520 comment 13470, RE-006). See `docs/LOAD_ORDER_FILES.md`.

Localized string-table support now covers `.STRINGS`, `.DLSTRINGS`, and `.ILSTRINGS`: strict header/offset validation, raw text-byte preservation, lookup, deduplication, and deterministic writing. Automatic resolution of 4-byte localized plugin fields remains deliberately separate until TES4 bit 7 semantics are settled or the caller supplies an explicit localization context.

Material files (`.bgsm` / `.bgem`) read and write byte-identically across the BGSM and BGEM version gates through `fo4/material/material_file.hpp`, the `radicle_material_file_*` C ABI and the `Radicle.Interop.MaterialFile` wrapper; see `docs/MATERIALS.md`.

Still incomplete: broad typed/schema-driven record authoring, safe ESL compaction, NAVM/NAVI cover, waypoint and NG layout (the stored read surface and indexed geometry edits are in `docs/NAVM_NAVI.md`), plugin-field integration with localized string tables, and the unresolved writer/reverse-engineering questions tracked in the roadmap.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Requires C++23, CMake 3.25+, zlib.

## Inspect

```bash
fo4inspect testdata/FW_HeatherSurvivalPatch.esp
fo4inspect header testdata/FW_HeatherSurvivalPatch.esp
fo4inspect masters testdata/FW_HeatherSurvivalPatch.esp
fo4inspect groups testdata/FW_HeatherSurvivalPatch.esp
fo4inspect records testdata/FW_HeatherSurvivalPatch.esp
fo4inspect record testdata/FW_HeatherSurvivalPatch.esp 01000800
fo4inspect raw testdata/FW_HeatherSurvivalPatch.esp 01000800
fo4inspect write testdata/FW_HeatherSurvivalPatch.esp /tmp/heather-copy.esp
fo4inspect formlinks testdata/FW_HeatherSurvivalPatch.esp
fo4inspect check Fallout4.esm testdata/FW_HeatherSurvivalPatch.esp
fo4inspect itm Fallout4.esm testdata/FW_HeatherSurvivalPatch.esp
fo4inspect udr testdata/FW_HeatherSurvivalPatch.esp
fo4inspect unused-masters testdata/FW_HeatherSurvivalPatch.esp
fo4inspect diff before.esp after.esp
```

JSON is the default output.

`check` loads the listed plugins as a load order (masters first) and reports problems in the last one: unresolved or wrong-type FormLinks, links to deleted records, out-of-range master indexes, schema preflight violations and circular leveled lists. `itm` lists the last plugin's identical-to-master records (nested ones before their parent) and its deleted-record counts, `udr` lists deleted placed references, `unused-masters` reports per-master reference counts, and `diff` lists added, removed and changed records.

## Canonical identity

Raw load-order FormIDs such as `0x5600009A` are never the library's identity for a reference.

They resolve to a symbolic `FormKey`:

```text
00009A:RadiationOverhaul.esp
```

On write, the destination plugin's master table reconstructs the raw on-disk FormID.

See `docs/FORMID_MODEL.md`.

## Docs

- `ROADMAP.md`
- `docs/ARCHITECTURE.md`
- `docs/RE_INVENTORY.md`
- `docs/FORMAT_NOTES.md`
- `docs/FORMID_MODEL.md`
- `docs/FORM_LINK_EVIDENCE.md`
- `docs/BA2.md`
- `docs/LOCALIZED_STRINGS.md`
- `docs/MATERIALS.md`
- `docs/TEST_STRATEGY.md`
- `docs/OPEN_QUESTIONS.md`
