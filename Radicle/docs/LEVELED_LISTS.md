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
| LVLI, LVLN | LVLG | Use global (FormID, GLOB) | `LeveledList::use_global`; formlink `...\UseGlobal` | 9996, 10019 |
| LVLI, LVLN | LLCT | Entry count (u8) | derived from entries; not a schema scalar | 9997, 10020; definition 3278 |
| LVLI, LVLN | LVLO | Entry, 12 bytes | `LeveledListEntry`; reference remapped by `structured_formids.inc` | 10000, 10023; `Core/wbDefinitionsCommon.pas:8791-8802` |
| LVLI, LVLN | COED | Extra data after an entry, 12 bytes | `LeveledListEntry::extra` | 10001, 10024; `Core/wbDefinitionsFO4.pas:3290-3298` |
| LVLI, LVLN | LLKC | Filter keyword chances | not implemented; authoring only | 9980-9984, 10028 |
| LVLI | LVSG | Epic loot chance (FormID, GLOB) | `LeveledList::epic_loot_chance`; formlink `LVLI\EpicLootChance` | 10029 |
| LVLI | ONAM | Override name (localized string) | `LeveledList::override_name` | 10030 |
| LVLN | MODL, MODT, MODC, MODS, MODF | Model group | not implemented; authoring only | 10006; `wbGenericModel` at `Core/wbDefinitionsFO4.pas:1030-1046` |

Entry, in order: level (u16, offset 0), two bytes (2-3), item FormID (4), count (u16, 8), chance none (u8, 10), one byte (11). The shipped byte check found non-zero values in bytes 2-3 and 11, so both are kept as read.

COED, when present: owner FormID (0, NULL allowed), union at 4, item condition float (8). The union is raw: `Core/wbDefinitionsCommon.pas:5451-5469` (`wbCOEDOwnerDecider`) makes it a global variable for an NPC_ owner and a required rank for a FACT owner. The shipped byte check found 60 COED, all with NULL owners, all in LVLI.

## Shared code

- `include/fo4/forms/leveled_list.hpp`, `src/forms/leveled_list.cpp`: decode, replace, insert, remove, set-entry and settings for LVLI, LVLN and LVSP.
- `include/fo4/forms/leveled_spell.hpp`, `src/forms/leveled_spell.cpp`: LVSP public API as a thin layer over the shared code. `LeveledSpellError` is an alias of `LeveledListError`.

## Schema coverage

- Scalars: LVLD, LVLM, LVLF for LVLI and LVLN.
- Formlinks: LVLG (both), LVSG (LVLI). Four-byte FormIDs, `payload_sizes` 4, targets GLOB, not nullable (xEdit `[GLOB]` without NULL).
- Localized: ONAM on LVLI.
- Authoring order follows xEdit: EDID, OBND, LVLD, LVLM, LVLF, LVLG, LLCT, LVLO, LLKC, then LVSG and ONAM (LVLI) or MODL, MODT, MODC, MODS, MODF (LVLN). LVLM and LLCT have minimum 0 because the shipped byte check found 65 LVLI without LVLM and 7 empty LVLI without LLCT.

## Not covered

- LLCT is not a schema scalar. The generic field writer has no derived-field guard, so a scalar write could desync LLCT from the entry count. Entry edits keep it equal.
- LVLO entry fields are not schema scalars or formlink fields. The schema addresses one subrecord payload, and LVLO repeats. The reference is already remapped by `structured_formids.inc`.
- LVLO target type is not validated: xEdit `sigBaseObjects` for LVLI (10023), `NPC_` or `LVLN` for LVLN (10000).
- LLKC and the LVLN model group are not decoded. The shipped byte check found LLKC payloads of 8, 16 and 24 bytes, so the 8-byte element in xEdit (`Core/wbDefinitionsFO4.pas:9979-9984`) does not describe every record.
- No LVLI or LVLN creator.
- LVLG and LVSG are read but not edited. Header settings do not change them.
