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
| Create Reference | `ALCO` + `ALCA` + `ALCL` | optional object form ID + alias i16 + create mode `0`/`0x8000` + level `0..4` (Easy/Medium/Hard/VeryHard/None, as xEdit FO4 `wbInteger(ALCL, 'Level', ...)`; 113 vanilla quest aliases use None; missing ALCL decodes as Easy; encode always writes ALCL) |
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
fail closed on unknown keys. `createReference.level` is `Easy`/`Medium`/`Hard`/`VeryHard`/`None`
or `0..4`. Existing `ALCL` outside that range fails coverage closed.

## Reference alias extras

`decode_alias_semantics` and `insert_alias_extras` share Mutagen WriteRecordTypes
order. Semantic fields live on `QuestAlias`; `subrecords` remain the write
authority.

| Member | Subrecords | Cardinality | Target |
| --- | --- | --- | --- |
| keywords | `KSIZ` + `KWDA` | unique packed list | `KYWD` |
| items | `COCT` + `CNTO` + optional `COED` | unique count + one `CNTO` per item; `COED` binds to the preceding `CNTO` | `sigBaseObjects`, count `>= 1` on author; `COED` is 12 bytes |
| spectator/observe/guard/combat overrides | `SPOR`/`OCOR`/`GWOR`/`ECOR` | unique optional | `FLST` |
| linked aliases | one packed `ALLA` | 8-byte `{KYWD, alias i32}` rows | `KYWD` |
| display name | `ALDN` | unique optional | `MESG` |
| forced voice | `ALFV` | unique optional | `VTYP` |
| death item | `ALDI` | unique optional | `LVLI` |
| spells / factions / packages | `ALSP` / `ALFC` / `ALPC` | repeatable | `SPEL` / `FACT` / `PACK` |
| voice types | `VTCK` | unique optional | `VTYP`/`FLST`/`NPC_`/`FACT` |

Dangling extra FormIDs degrade that one semantic entry (`unresolved_links`)
instead of failing the quest. Alias-item `COED` is bound to the preceding
`CNTO` (xEdit `wbCNTO` / Mutagen `ContainerEntry.Data`). It is 12 bytes:
owner FormID (`NPC_`/`FACT`/`NULL`), union (GLOB FormID or i32 required rank;
unused when owner is `NULL`), then float item condition. Mixed lists are
valid: only some `CNTO` entries may carry `COED`. JSON uses
`items[].extra.{owner,global,requiredRank,itemCondition}`; omit `extra` to
write `CNTO` alone. `global` requires an `NPC_` owner; `requiredRank` requires
a `FACT` owner. Encode writes `COED` immediately after that item's `CNTO`.
Decode fills the union from the owner's record type in the same plugin:
`FACT` → `requiredRank` only, `NPC_` → `global` only, `NULL`/untyped owner →
neither. Remap visits offset 4 only when that owner resolves to `NPC_` in the
plugin being remapped; `FACT` ranks are never FormIDs. Offset 4 is not in
`structured_formids.inc`. Unknown owner plus nonzero union fails closed on
strict remap. Missing or short `COED`, `COED` without a preceding `CNTO`, two
`COED` records on one item, non-finite item condition, and setting both
`global` and `requiredRank` fail closed.

## Deliberately not modeled yet

The following remain fail-closed / raw-preserved until their full structure is
implemented and tested:

- Alias VMAD extras bind Papyrus to alias IDs (FO4 object format 2 only: unused u16, alias i16, QUST FormID) and stage fragments (`Stage` u16 = QUST INDX). Format 1 alias objects fail closed on rewrite. `StageIndex` i32 is preserved; it is not treated as a QSDT count. INDX remap is identity or a unique stage-body match, never array position. Duplicate, trailing, unsupported, or unmappable bindings fail closed with disk unchanged.

Unknown members inside an existing reference/location alias are preserved in
original order by decode, and `replace_quest_alias` keeps them (see
Replacement below).

## Replacement

`replace_quest_alias` rewrites only the members the creation sets. Every other
member of the existing alias is kept and spliced back into the alias at the
position the xEdit member order gives it, so `ALED` stays last. Replacement
never targets a collection alias; only a kind change to one can produce an
`ALCS`/`ALMI` run. The Alias ID must stay the same. An empty creation field (`name`, `force_into_alias`, the fill, `conditions`, `items`, the other extras) keeps the existing member: there is no in-place clear. To remove a member, rebuild the alias with `remove_quest_alias` followed by `add_quest_alias`.

A member is written when the creation emits it, or when it belongs to a run the
creation writes: the Fill Type union of the alias kind, the `CTDA`/`CIS1`/`CIS2`
condition run, or the `COCT`/`CNTO`/`COED` item run. A run goes as a unit
because a leftover member rebinds to the new run instead of staying put: a kept
`ALEA` beside a written `ALFE`/`ALFD` would make the alias decode as two Fill
Type variants, a kept `CIS1` would rebind its string to the next `CTDA`, and a
kept `COED` would rebind its owner and rank to the next `CNTO`. The Fill Type
union is checked against the new kind as well as the existing one, so a kind
change from a location alias carrying `ALCC` to a reference creation that
writes a fill drops the `ALCC` instead of keeping it beside the fill. Kept
beside it, the decoder would refuse the alias: a Find Matching Reference fill
built from `ALNA`/`ALNT` or `ALFE`/`ALFD` reports mixed Match Type selectors,
and any other fill reports multiple Fill Type variants.

A kind change that writes no fill refuses the preserved Fill Type members
instead of carrying them over. `ALFA`, `ALEQ`, `ALEA`, `ALFE`, `ALFD` and the
location `ALCC` are Fill Type members of one kind that also have a position in
the other kind's member order, so a preserved one silently becomes a Fill Type
the caller did not ask for: a location alias's `ALCC` is Closest To Alias but a
reference Find Matching Closest fill, and a reference `ALEA` (External Alias
Reference) or `ALFE`/`ALFD` (Find Matching From Event) is a location External
Alias or Find Matching Location fill. The replacement throws `QuestError`
before the clone is committed when the kind changed, the creation writes no
fill, and a member that would be preserved is a Fill Type member of either
kind. `ALFR` and `ALFL` were already refused, because neither has a position in
the other kind's member order.

The order is the xEdit FO4 alias definition
(`Core/wbDefinitionsFO4.pas:10808-10889` reference alias, `:10898-10924`
location alias, at pin `93cc0bc5`): `ALST`/`ALLS`, `ALID`, `FNAM`, `ALFI`, the
Fill Type union, `CTDA`/`CIS1`/`CIS2`, the reference alias extras in their
WriteRecordTypes order, then `ALED`. A location alias places `ALCC` after its
condition run; a reference alias places it inside Find Matching Reference,
before the conditions.

Replacement throws `QuestError`, leaving the plugin unchanged, when a member
that has to be preserved has no position in that order (an `ALFL` inside a
reference alias, say), when the preserved members are themselves out of
order, when a kind change that writes no fill would carry over a Fill Type
member of either kind, or when the spliced alias does not decode. Order is
compared by run rather than by single signature: `CIS1` and `CIS2` rank with
the `CTDA` head of their condition run and `COED` ranks with the `CNTO` it
belongs to, so a canonical `CTDA`, `CIS1`, `CTDA` condition run and a `COCT`,
`CNTO`, `COED`, `CNTO`, `COED` item list are in order. A replacement that
changes the alias kind rewrites the alias frame for the new kind (`ALST`/`ALLS`,
or `ALCS`/`ALMI` when the new creation is a collection, plus `ALID`, `FNAM`,
`ALFI`, `ALED`) and preserves everything else under the new kind's order. On a kind change the existing `ALID` and `ALFI` are dropped when the creation leaves `name` or `force_into_alias` unset; a same-kind replacement keeps them. There is no drop opt-in for members the new kind's order does not allow: a kind change refuses them. A reference alias carrying `VTCK` replaced by a location creation throws `QuestError` (`Radicle/src/forms/quest_alias_edit.cpp:merge_alias_members`) and leaves the plugin unchanged. To change such an alias's kind, the caller must rebuild it with `remove_quest_alias` followed by `add_quest_alias`.

## Mutation safety

`add_quest_alias`, `replace_quest_alias`, and `remove_quest_alias` are
transactional. They first validate the existing alias region, clone the plugin,
change only the proven alias range, invalidate serialization caches, and commit
the clone only after successful mutation.

Current replacement requires the Alias ID to remain unchanged. Add refuses an
existing ID. Remove returns false for a missing ID and does not alter `ANAM`.
