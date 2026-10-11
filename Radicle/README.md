# Radicle

Native Fallout 4 plugin SDK. The first root a plugin puts down, and a radiation pun.

C++ namespace is still `fo4`. Radicle is the native xEdit-style plugin backend replacing Mutagen for PluginTool / PluginEditTool and supplying structured game data to GardenEngine. It parses and exposes records, supports proven field and record edits, and serializes changes while preserving untouched data.

Stored layouts may be established from xEdit definitions, official plugins, existing Radicle behavior, CK/CKRE and synthetic tests. Runtime RE is required for unresolved layouts, unproven safe mutation or intended runtime behavior; it is not a blanket prerequisite for plugin editing. Unknown layouts stay opaque or refuse mutation/remapping. Mutagen remains a compatibility oracle.

[#86](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/86) governs the product roadmap; [#287](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/287) and [the parity matrix](docs/FO4_PARITY.md) govern READ / WRITE / ADD / REMOVE / CREATE / REMAP / VALIDATE / OPAQUE coverage. A reader alone does not establish editing support. [#43](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/43) supplies reusable schema infrastructure where proven gaps justify it.

## Status

Generic lossless plugin container, FormKey identity, preservation writer, master remap, schema-aware FormLink rewrite, load-order/conflict graph, stable C ABI, and the native PluginEditTool backend are implemented. FormLink coverage includes `NAME`/`KWDA`/`CTDA`/`VMAD`, discriminator-controlled package/PERK payloads, and structured slots such as `CNTO`/`LVLO`/`XTEL`.

The first machine-readable authoring schema is active under `schema/fallout4/fields.json`. A deterministic generator emits record-qualified C++ scalar descriptors and CMake checks that committed generated output is current. `REFR\\HealthPercent` / `XHLT` is the first field using this path; typed EYES and LVSP editing remain structured record models rather than scalar byte patches.

BA2 support currently covers the proven Fallout 4 version-1 `BTDX/GNRL` layout: list/read, stored and zlib-compressed chunks, extraction, path lookup, and write/read round trips. PluginEditTool's native Archive panel now uses this Radicle reader through its archive backend seam. DX10 texture archives and next-generation v7/v8 layouts remain deliberately unsupported until their fields are verified.

Localized string-table support now covers `.STRINGS`, `.DLSTRINGS`, and `.ILSTRINGS`: strict header/offset validation, raw text-byte preservation, lookup, deduplication, and deterministic writing. Automatic resolution of 4-byte localized plugin fields remains deliberately separate until TES4 bit 7 semantics are settled or the caller supplies an explicit localization context.

Still incomplete: broad typed/schema-driven record authoring, safe ESL compaction, remaining mixed-layout FormID surfaces such as NAVM/NAVI, plugin-field integration with localized string tables, and the unresolved writer/reverse-engineering questions tracked in the roadmap.

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
- `docs/TEST_STRATEGY.md`
- `docs/OPEN_QUESTIONS.md`
