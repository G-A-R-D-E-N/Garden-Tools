# Fallout 4 QUST alias authoring evidence

Radicle treats Fallout 4 quest aliases as a repeated discriminated container,
not as a map keyed by subrecord signature. This is required because a QUST may
contain multiple reference, location, and collection aliases, and reference /
location aliases themselves contain repeated condition/array members.

Source evidence is pinned to the Fallout 4 xEdit definitions used by this
branch (`TES5Edit/TES5Edit` commit
`93cc0bc5a1251936c3c7859eee3150eda12a62d7`).

## Alias array boundary

The QUST definition places:

1. required `ANAM` (`Next Alias ID`, u32),
2. one contiguous `Aliases` array,
3. optional QUST tail members including `NNAM`, `GNAM`, and `SNAM`.

Radicle therefore permits alias insertion only inside that single contiguous
region immediately after `ANAM`. Once the tail starts, a later `ALST`, `ALLS`,
`ALCS`, `ALED`, or collection `ALMI` is treated as malformed rather than as a
second alias array.

Alias IDs must be unique. `ANAM` grows monotonically when a newly inserted
alias ID reaches or exceeds it; removals do not recycle/decrease it.

## Alias discriminators

### Reference alias

- `ALST`: Alias ID, u32
- `ALID`: Alias Name, required zstring
- `FNAM`: Alias Flags, required u32
- `ALFI`: optional Force Into Alias, i32
- Fill Type union
- additional alias fields / arrays
- `ALED`: required empty Alias End marker

### Location alias

- `ALLS`: Alias ID, u32
- `ALID`: optional Alias Name
- `FNAM`: Alias Flags, required u32
- `ALFI`: optional Force Into Alias, i32
- Fill Type union
- additional alias fields / arrays
- `ALED`: required empty Alias End marker

### Collection alias

- `ALCS`: Alias ID, u32
- `ALMI`: optional Max Initial Fill Count, u8

The compact collection variant does not use `ALED`.

## FormID-bearing Fill Types currently modeled

All FormIDs are decoded/encoded through `PluginFormIdContext`. They are never
constructed by manipulating runtime/file-index bits directly.

### Reference alias

| Fill Type | Subrecords | Semantics |
| --- | --- | --- |
| Specific Reference | `ALFR` | nullable reference FormID |
| Unique Actor | `ALUA` | required `NPC_` FormID |
| Location Alias Reference | `ALFA` + `ALRT` | alias i32 + required `LCRT` FormID |
| External Alias Reference | optional `ALEQ` + required `ALEA` | optional `QUST` FormID + alias i32 |

`ALFA` and `ALEA` are alias IDs, not FormIDs. The generic FormLink scanner must
not remap them. `ALFR` is explicitly listed in the scanner because it is a real
FormID and otherwise master reorder would leave Specific Reference aliases in
the old master-index space.

### Location alias

| Fill Type | Subrecords | Semantics |
| --- | --- | --- |
| Specific Location | `ALFL` | required `LCTN` FormID |
| Reference Alias Location | `ALFA` + optional `KNAM` | alias i32 + optional `KYWD` FormID |
| External Alias Location | optional `ALEQ` + optional `ALEA` | optional `QUST` FormID + optional alias i32 |

The semantic encoder refuses an External Alias Location with neither member.

## Conditions

Reference and location aliases author a contiguous CTDA/CIS run after Fill Type
members and before extras / location `ALCC`. PluginEditTool `SetQuestAliases`
reuses the log-entry codec (`parseQuestCondition` / `serialize_ctda_run`).
Collections reject conditions. `GetEventData` stays fail-closed.

## Deliberately not modeled yet

The following remain fail-closed / raw-preserved until their full structure is
implemented and tested:

- PluginEditTool JSON for remaining Fill Types already encoded in Radicle:
  Create Reference, Find Matching Reference, External Alias, Location Alias
  Reference, and event/data forms.
- Remaining reference/location alias fields after the Fill Type union:
  voice type, death item, display name, and package-list overrides
  (`SPOR`/`OCOR`/`GWOR`/`ECOR`).
- Reference alias extras now modeled: keywords (`KSIZ`/`KWDA`), items
  (`COCT`/`CNTO` without `COED`), linked aliases (`ALLA`), spells (`ALSP`),
  factions (`ALFC`), and packages (`ALPC`), in Mutagen WriteRecordTypes order.
- Alias VMAD interactions beyond the existing global VMAD safety contract. PluginEditTool `SetQuestAliases` / `SetQuestStages` fail closed when QUST VMAD extras bind alias IDs or stage fragments (`fo4::quest_vmad_bindings`). Whole-array replace still does not rewrite VMAD.

Unknown members inside an existing reference/location alias are preserved in
original order. Semantic replacement intentionally emits only the fields that
Radicle has explicitly modeled; callers must not use replacement as a generic
round-trip for an alias carrying unmodeled semantics they intend to preserve.

## Mutation safety

`add_quest_alias`, `replace_quest_alias`, and `remove_quest_alias` are
transactional. They first validate the existing alias region, clone the plugin,
change only the proven alias range, invalidate serialization caches, and commit
the clone only after successful mutation.

Current replacement requires the Alias ID to remain unchanged. Add refuses an
existing ID. Remove returns false for a missing ID and does not alter `ANAM`.
