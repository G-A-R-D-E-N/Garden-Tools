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
reference/location ID reaches or exceeds it: `max(existing ANAM, max(ref/location
id)+1)`. Removals and collection-only rewrites do not recycle or decrease it.
Collection entry IDs do not contribute.

`add_quest_alias`, `remove_quest_alias`, `replace_quest_alias`, and
`reorder_quest_alias` refuse any result that would place two collection ranges
next to each other. A later decode would merge those runs into one alias.

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

A contiguous `ALCS[+ALMI]` run is one logical alias (Mutagen
`QuestCollectionAlias.Collection`). Disk cannot separate two adjacent
collection aliases, so authoring refuses consecutive collection objects.

- `ALCS`: per-entry Alias ID, u32; unique inside that collection, not versus `ALST`/`ALLS`
- `ALMI`: optional Max Initial Fill Count, u8, immediately after its `ALCS`
- no `ALED`

`SetQuestAliases` JSON: `entries: [{id, maxInitialFillCount?}, ...]`. Single-entry
sugar `id` + `maxInitialFillCount` remains. Mixing `entries` with those
top-level keys fails. Collection IDs do not allocate automatic
reference/location IDs. ANAM still grows from reference/location IDs only.

## FormID-bearing Fill Types currently modeled

All FormIDs are decoded/encoded through `PluginFormIdContext`. They are never
constructed by manipulating runtime/file-index bits directly.

### Reference alias

| Fill Type | Subrecords | Semantics |
| --- | --- | --- |
| Specific Reference | `ALFR` | non-null reference FormID (JSON cannot author `ALFR=0`) |
| Unique Actor | `ALUA` | required `NPC_` FormID |
| Location Alias Reference | `ALFA` + `ALRT` | alias i32 + required `LCRT` FormID |
| External Alias Reference | optional `ALEQ` + required `ALEA` | optional `QUST` FormID + alias i32 |
| Create Reference | `ALCO` + `ALCA` + `ALCL` | optional object FormID + alias i16 + create mode `0`/`0x8000` + level `0..3` (Easy/Medium/Hard/VeryHard; missing ALCL decodes as Easy; encode always writes ALCL) |
| Find Matching Near Alias | `ALNA` + `ALNT` | alias i32 + type `0`/`1` |
| Find Matching From Event | `ALFE` + `ALFD` | event u32 + data u32 |
| Find Matching Closest | `ALCC` | alias i32 (fill; before conditions) |

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
| Find Matching Location | `ALFE` + `ALFD` | event u32 + data u32 (before post-condition `ALCC`) |

The semantic encoder refuses an External Alias Location with neither member.

## Conditions

Reference and location aliases author a contiguous CTDA/CIS run after Fill Type
members and before extras / location `ALCC`. PluginEditTool `SetQuestAliases`
reuses the log-entry codec (`parseQuestCondition` / `serialize_ctda_run`).
Collections reject conditions. `GetEventData` stays fail-closed.

## PluginEditTool JSON fill types

`SetQuestAliases` authors every Fill Type Radicle already encodes. Nested objects
fail closed on unknown keys. `createReference.level` is `Easy`/`Medium`/`Hard`/`VeryHard`
or `0..3`. Existing `ALCL` outside that range fails coverage closed.

## Reference alias extras

`decode_alias_semantics` and `insert_alias_extras` share Mutagen WriteRecordTypes
order. Semantic fields live on `QuestAlias`; `subrecords` remain the write
authority.

| Member | Subrecords | Cardinality | Target |
| --- | --- | --- | --- |
| keywords | `KSIZ` + `KWDA` | unique packed list | `KYWD` |
| items | `COCT` + `CNTO` | unique count + one `CNTO` per item | `sigBaseObjects`, count `>= 1` on author |
| spectator/observe/guard/combat overrides | `SPOR`/`OCOR`/`GWOR`/`ECOR` | unique optional | `FLST` |
| linked aliases | one packed `ALLA` | 8-byte `{KYWD, alias i32}` rows | `KYWD` |
| display name | `ALDN` | unique optional | `MESG` |
| forced voice | `ALFV` | unique optional | `VTYP` |
| death item | `ALDI` | unique optional | `LVLI` |
| spells / factions / packages | `ALSP` / `ALFC` / `ALPC` | repeatable | `SPEL` / `FACT` / `PACK` |
| voice types | `VTCK` | unique optional | `VTYP`/`FLST`/`NPC_`/`FACT` |

Dangling extra FormIDs degrade that one semantic entry (`unresolved_links`)
instead of failing the quest. `COED` is unmodeled: if present, `items` stays
empty.

## Deliberately not modeled yet

The following remain fail-closed / raw-preserved until their full structure is
implemented and tested:

- `COED` on alias items.
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
