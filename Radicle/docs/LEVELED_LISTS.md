# Leveled lists (LVLI, LVLN, LVSP)

The xEdit citations below were read at commit `93cc0bc5` (`Core/wbDefinitionsFO4.pas`, `Core/wbDefinitionsCommon.pas`) on 2026-10-12. A one-off byte check of the shipped `Fallout4.esm` on the same day read 2098 LVLI and 228 LVLN records; it was a scratch script, not a repo test.

Status: TODO-verify. LeveledListReal is selected by REAL_DATA_TEST_REGEX and is in no path group, so native-windows-real-data runs it on every PR when FO4 data is configured (it skips otherwise). No pass result is recorded.

Scope: typed read, entry edits and schema coverage for LVLI and LVLN. LVSP shares the entry and header code.

## Record layout

| Record | Subrecord | Field | Radicle surface | xEdit line |
|---|---|---|---|---|
| LVLI, LVLN | EDID | Editor ID | schema authoring | 9987, 10010 |
| LVLI, LVLN | OBND | Object bounds | schema authoring | 9988, 10011 |
| LVLI, LVLN | LVLD | Chance none (u8) | `LeveledList::chance_none`; scalar `LVLI\ChanceNone`, `LVLN\ChanceNone` | 9989, 10012; definition 3281 |
| LVLI, LVLN | LVLM | Max count (u8) | `LeveledList::max_count`; scalar `...\MaxCount` | 9990, 10013 |
| LVLI, LVLN | LVLF | Flags (u8) | `LeveledList::flags`; scalar `...\Flags` | 9991-9995, 10014-10018 |
| LVLI, LVLN | LVLG | Use global (form ID, GLOB) | `LeveledList::use_global`; formlink `...\UseGlobal` | 9996, 10019 |
| LVLI, LVLN | LLCT | Entry count (u8) | derived from entries; not a schema scalar | 9997, 10020; definition 3278 |
| LVLI, LVLN | LVLO | Entry, 12 bytes | `LeveledListEntry`; reference remapped by `structured_formids.inc` | 10000, 10023; `Core/wbDefinitionsCommon.pas:8791-8802` |
| LVLI, LVLN | COED | Extra data after an entry, 12 bytes | `LeveledListEntry::extra` | 10001, 10024; `Core/wbDefinitionsFO4.pas:3290-3298` |
| LVLI, LVLN | LLKC | Filter keyword chances | schema rows `FilterKeyword` (KYWD link, offset 0) and `FilterChance` (u32, offset 4), stride 8; the typed decoder does not read it | 9980-9984, 10028 |
| LVLI | LVSG | Epic loot chance (form ID, GLOB) | `LeveledList::epic_loot_chance`; formlink `LVLI\EpicLootChance` | 10029 |
| LVLI | ONAM | Override name (localized string) | `LeveledList::override_name` | 10030 |
| LVLN | MODL, MODT, MODC, MODS, MODF | Model group | not implemented; authoring only | 10006; `wbGenericModel` at `Core/wbDefinitionsFO4.pas:1030-1046` |

Entry, in order: level (u16, offset 0), two bytes (2-3), item form ID (4), count (u16, 8), chance none (u8, 10), one byte (11). The shipped byte check found non-zero values in bytes 2-3 and 11, so both are kept as read.

Chance none is zero for LVLI and LVLN records whose form version is below 69. xEdit zeroes it on load for those records (`Core/wbDefinitionsFO4.pas:2215-2250`, `wbLLEAfterLoad`, attached to LVLN at 10007 and LVLI at 10031). LVSP has no after-load (10044-10049), so its chance is read as stored. The typed decode reports 0 for LVLI and LVLN below version 69, and a typed write of such a record writes 0 for every entry. A record with no typed edit keeps its bytes.

COED, when present: owner form ID (0, NULL allowed), union at 4, item condition float (8). The union is raw: `Core/wbDefinitionsCommon.pas:5451-5469` (`wbCOEDOwnerDecider`) makes it a global variable for an NPC_ owner and a required rank for a FACT owner. The shipped byte check found 60 COED, all with NULL owners, all in LVLI.

## Shared code

- `include/fo4/forms/leveled_list.hpp`, `src/forms/leveled_list.cpp`: decode, replace, insert, remove, set-entry and settings for LVLI, LVLN and LVSP.
- `include/fo4/forms/leveled_spell.hpp`, `src/forms/leveled_spell.cpp`: LVSP public API as a thin layer over the shared code. `LeveledSpellError` is an alias of `LeveledListError`.

## Schema coverage

- Scalars: LVLD, LVLM, LVLF for LVLI and LVLN.
- Formlinks: LVLG (both), LVSG (LVLI). Four-byte form IDs, `payload_sizes` 4, targets GLOB, not nullable (xEdit `[GLOB]` without NULL).
- Localized: ONAM on LVLI.
- Authoring order follows xEdit: EDID, OBND, LVLD, LVLM, LVLF, LVLG, LLCT, LVLO, LLKC, then LVSG and ONAM (LVLI) or MODL, MODT, MODC, MODS, MODF (LVLN). LVLM and LLCT have minimum 0 because the shipped byte check found 65 LVLI without LVLM and 7 empty LVLI without LLCT.

## Not covered

- LLCT is not a schema scalar. The generic field writer has no derived-field guard, so a scalar write could desync LLCT from the entry count. Entry edits keep it equal.
- LVLO entry fields are not schema scalars or formlink fields. The schema addresses one subrecord payload, and LVLO repeats. The reference is already remapped by `structured_formids.inc`.
- LVLO target type is checked only for a local entry whose item is new to the list, on insert or set. Foreign entries and entries already present are not checked. Allowed types follow xEdit `sigBaseObjects` for LVLI (10023), `NPC_` or `LVLN` for LVLN (10000). See `validate_entry_target` in `Radicle/docs/code-notes/Radicle.md`.
- The LVLN model group is not decoded or written. LLKC has schema rows for LVLI and LVLN (`LVLI\FilterKeyword` and `LVLN\FilterKeyword` link KYWD at offset 0; `LVLI\FilterChance` and `LVLN\FilterChance` are u32 scalars at offset 4; both stride 8, existing entries only), but the typed leveled-list decoder does not read or write LLKC, and it is kept as an opaque subrecord on replace. Remap reads it as repeated 8-byte entries for LVLI and LVLN (form ID at 0, the 4-byte chance is not checked), so a payload of 8, 16 or 24 bytes is one, two or three entries. xEdit defines LLKC as an uncounted array of that 8-byte struct (`Core/wbDefinitionsFO4.pas:9979-9984`). See `leveled_list_formid_offsets` in `Radicle/docs/code-notes/Radicle.md`.
- LVLI and LVLN creation writes a minimal record only: EDID (optional), OBND, LVLD, LVLM, LVLF and LLCT, with no entries, LVLG, LVSG, ONAM or model group. Entries are added later through insert or set. See `create_leveled_list` in `Radicle/docs/code-notes/Radicle.md`.
- LVLG and LVSG are read but not edited. Header settings do not change them.
- Typed writes store LVLO entries sorted by level, then by the raw reference form ID, then by count, the key xEdit compares for the entry struct (`Core/wbDefinitionsCommon.pas:8788-8802`, `Core/wbImplementation.pas:2151-2175`). The sort is stable. Insert and set take an index into the list as decoded before the call; the written list is then sorted, so the entry may end up at another index. See `sorted_entries` in `Radicle/docs/code-notes/Radicle.md`.
- An LVSP entry cannot carry COED. A write that includes one is refused, because xEdit's LVSP entry is LVLO only (`Core/wbDefinitionsFO4.pas:10044-10049`).
