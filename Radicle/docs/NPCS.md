# NPC_ records

source:xEdit 93cc0bc5a1251936c3c7859eee3150eda12a62d7, `Core/wbDefinitionsFO4.pas` NPC_ record at line 10286. Helper definitions are in `Core/wbDefinitionsCommon.pas` and `Core/wbInterface.pas`; each is cited where it is used.

The schema lives in `schema/fallout4/fields.json` (`NPC_` entry). Scalars are generated into `include/fo4/schema/generated_scalar_fields.inc`, authoring constraints into `generated_record_constraints.inc` and the FormLink rows and lists into `generated_formlink_lists.inc`. The three repeated lists that carry a count subrecord (factions, perks, items) have typed code in `include/fo4/forms/npc.hpp` and `src/forms/npc.cpp`.

## Scalar fields

Every scalar uses `existing_only` insertion. The presentation is `identity` except on rows that carry xEdit names: `ConfigurationFlags` and `TemplateFlags` use `flags`, and `AIAggression`, `AIConfidence`, `AIMorality`, `AIMood`, `AIAssistance`, `AIAggroRadiusBehavior`, `AINoSlowApproach` and `SoundLevel` use `enum` (`wbAggressionEnum` Common:6973-6982, `wbAssistanceEnum` 7023-7031, `wbConfidenceEnum` 7249-7259, `wbMoodEnum` 7676-7689, `wbMoralityEnum` 7691-7700, `wbBoolEnum` 7240-7247, `wbSoundLevelEnum` FO4:3424-3430). Bounds are unchanged, so the names do not restrict what is written. A fixed struct with a `payload_size` is read only when the subrecord has exactly that many bytes; a shorter payload reports the field as absent. A struct with a variable-length trailing member declares `minimum_payload_size` instead, which accepts any payload at least that long and leaves the trailing bytes untouched.

| Path | Subrecord | Offset | Storage | Payload | xEdit |
| --- | --- | --- | --- | --- | --- |
| NPC_\ConfigurationFlags | ACBS | 0 | u32 | 20 | 10299-10332 |
| NPC_\XPValueOffset | ACBS | 4 | i16 | 20 | 10333 |
| NPC_\Level | ACBS | 6 | u16 | 20 | 10334-10338 |
| NPC_\CalcMinLevel | ACBS | 8 | u16 | 20 | 10339 |
| NPC_\CalcMaxLevel | ACBS | 10 | u16 | 20 | 10340 |
| NPC_\DispositionBase | ACBS | 12 | i16 | 20 | 10341 |
| NPC_\TemplateFlags | ACBS | 14 | u16 | 20 | 10342-10343 |
| NPC_\BleedoutOverride | ACBS | 16 | u16 | 20 | 10344 |
| NPC_\CalculatedHealth | DNAM | 0 | u16 | 8 | 10411 |
| NPC_\CalculatedActionPoints | DNAM | 2 | u16 | 8 | 10412 |
| NPC_\FarAwayModelDistance | DNAM | 4 | u16 | 8 | 10413 |
| NPC_\GearedUpWeapons | DNAM | 6 | u8 | 8 | 10414 |
| NPC_\AIAggression | AIDT | 0 | u8 | 24 | 6216 |
| NPC_\AIConfidence | AIDT | 1 | u8 | 24 | 6217 |
| NPC_\AIEnergyLevel | AIDT | 2 | u8 | 24 | 6218 |
| NPC_\AIMorality | AIDT | 3 | u8 | 24 | 6219 |
| NPC_\AIMood | AIDT | 4 | u8 | 24 | 6220 |
| NPC_\AIAssistance | AIDT | 5 | u8 | 24 | 6221 |
| NPC_\AIAggroRadiusBehavior | AIDT | 6 | u8 | 24 | 6223 |
| NPC_\AIAggroUnknown | AIDT | 7 | u8 | 24 | 6224 |
| NPC_\AIWarn | AIDT | 8 | u32 | 24 | 6225 |
| NPC_\AIWarnAttack | AIDT | 12 | u32 | 24 | 6226 |
| NPC_\AIAttack | AIDT | 16 | u32 | 24 | 6227 |
| NPC_\AINoSlowApproach | AIDT | 20 | u8 | 24, from 21 bytes | 6229 |
| NPC_\AttackDamageMult, AttackChance | ATKD | 0, 4 | f32 | 44 | 4075, 4076 |
| NPC_\AttackFlags | ATKD | 12 | u32 | 44 | 4078-4111; `wbAttackData` FO4:4074 |
| NPC_\AttackAngle, StrikeAngle, AttackStagger, AttackKnockdown, AttackRecoveryTime, AttackActionPointsMult | ATKD | 16, 20, 24, 28, 32, 36 | f32 | 44 | 4112-4117 |
| NPC_\AttackStaggerOffset | ATKD | 40 | i32 | 44 | 4118 |
| NPC_\ObjectBoundsMin X/Y/Z | OBND | 0, 2, 4 | i16 | 12 | 9074-9090; wbVec3Int Common:6434-6446 |
| NPC_\ObjectBoundsMax X/Y/Z | OBND | 6, 8, 10 | i16 | 12 | 9074-9090 |
| NPC_\WeightThin, WeightMuscular, WeightFat | MWGT | 0, 4, 8 | f32 | 12 | 10426-10430 |
| NPC_\MorphRegionHead, UpperTorso, Arms, LowerTorso, Legs | MRSV | 0, 4, 8, 12, 16 | f32 | 20 | 10463-10469 |
| NPC_\TextureLightingRed, Green, Blue, Alpha | QNAM | 0, 4, 8, 12 | f32 | 16 | 10445; wbFloatRGBA Common:6899-6921 |
| NPC_\HeightMin | NAM6 | whole | f32 | 4 | 10423 |
| NPC_\HeightMax | NAM4 | whole | f32 | 4 | 10425 |
| NPC_\UnusedFloat | NAM7 | whole | f32 | 4 | 10424 |
| NPC_\FacialMorphIntensity | FMIN | whole | f32 | 4 | 10487 |
| NPC_\FaceMorphIndex | FMRI | whole | u32 | 4 | 10473 |
| NPC_\FaceMorphPosition X/Y/Z, FaceMorphRotation X/Y/Z, FaceMorphScale | FMRS | 0, 4, 8, 12, 16, 20, 24 | f32 | 28 min | 10475-10483 |
| NPC_\TintLayerDataType | TETI | 0 | u16 | 4 | 10450-10453 |
| NPC_\TintLayerIndex | TETI | 2 | u16 | 4 | 10450-10453 |
| NPC_\TintLayerValue | TEND | 0 | u8 | 1 or 7 | 10454-10458; `wbDiv(100)` |
| NPC_\TintLayerColorRed, Green, Blue | TEND | 1, 2, 3 | u8 | 7 | 10454-10461; `wbByteColors` Common:6696-6708 |
| NPC_\TintLayerTemplateColorIndex | TEND | 5 | i16 | 7 | 10454-10461 |
| NPC_\SoundLevel | NAM8 | whole | u32 | 4 | 10431; wbSoundLevelEnum FO4:3424 |
| NPC_\ActorSoundTotal | CS2H | whole | u32 | 4 | 10433; group count of NPC_\ActorSounds, plain writes refused |

Notes on the scalars:

- ACBS `Level` and `Level Mult` share offset 6. xEdit picks one with `wbACBSLevelDecider` (Common:5437-5449): when Flags bit 7 (`0x80`) is set the value is a level multiplier. The schema has no union, so the field stores the raw `u16`. xEdit displays it divided by 1000 (`wbDiv(1000, 2)`, Interface:9201-9206) and clamps it to 100..10000 (`wbACBSLevelMultAfterLoad`, Common:918-931).
- AIDT is 24 bytes when the record's form version is at least 29 (`wbFromVersion(29)`, FO4:6229-6230). The version is read through `wbFormVersionDecider` (Common:5542-5567). Radicle gates `AINoSlowApproach` on payload length (21 bytes) instead of the record version. Whether that agrees with the record version on every shipped NPC_ is not verified here; the real-data suite does not check it.
- The ACBS unused bytes at offset 18 are not modeled.
- Storage bounds are the authoring ranges. xEdit defines no tighter bound for these fields.

### Template flags (ACBS offset 14)

From `wbTemplateFlags` (Common:8155-8178), FO4 names:

| Bit | Name | Bit | Name |
| --- | --- | --- | --- |
| 0 | Traits | 7 | Base Data |
| 1 | Stats | 8 | Inventory |
| 2 | Factions | 9 | Script |
| 3 | Spell List | 10 | Def Pack List |
| 4 | AI Data | 11 | Attack Data |
| 5 | AI Packages | 12 | Keywords |
| 6 | Model/Animation | 13-15 | unnamed in FO4 |

`NPC_\TemplateFlags` has the `flags` presentation with these thirteen names; bits 13 to 15 are unnamed in FO4, so the table has no entry for them. The field is still read and written as a raw `u16`.

### Configuration flags (ACBS offset 0)

`NPC_\ConfigurationFlags` has the `flags` presentation with all 32 names from FO4:10300-10331. Bit 0 `Female`, 1 `Essential`, 2 `Is CharGen Face Preset`, 3 `Respawn`, 4 `Auto-calc stats`, 5 `Unique`, 6 `Doesn't affect stealth meter`, 7 `PC Level Mult`, 8 `Has Base Sound Data`, 9 `Calc For Each Template`, 10 `Use Attack Percentage`, 11 `Protected`, 12 `No Loot`, 13 `No Rumors`, 14 `Summonable`, 15 `Disable Non-Combat Regen`, 16 `Doesn't bleed`, 17 `Very Simple Actor`, 18 `Has Bleedout Override`, 19 `Swap Gender Anims`, 20 `Simple Actor`, 21 `Unused 21`, 22 `Unused 22`, 23 `No Activation/Hellos`, 24 `Diffuse Alpha Test`, 25 `Disable Combat`, 26 `Spawns Dead`, 27 `Player Protected`, 28 `Do Not Use Load Doors`, 29 `Is Ghost`, 30 `No Bleedout Recovery`, 31 `Invulnerable`.

## Typed lists

| Subrecord | Entry | Layout | xEdit |
| --- | --- | --- | --- |
| SNAM | Faction form ID + Rank | 5 bytes: form ID u32, Rank s8 | wbFaction, Common:8527-8538 (FO4 drops the 3 unused bytes: IsFO4Plus(nil, wbUnused(3))); list at FO4:10347 |
| PRKZ | Count | u32 | FO4:10384 |
| PRKR | Perk FormID + Rank | 5 bytes: FormID u32, Rank u8 | FO4:10385-10393 (`SetCountPath(PRKZ)`) |
| COCT | Count | u32 | FO4:10397; wbCOCT FO4:3315 |
| CNTO | Item FormID + Count | 8 bytes: FormID u32, Count s32 | wbCNTO FO4:3300-3305 |
| COED | Owner, global/rank, condition | 12 bytes: Owner FormID u32, u32, float | wbCOED FO4:3290-3298 |

Decode rules:

- Each list is read in subrecord order.
- A payload of the wrong size throws `NpcError`.
- A null form ID throws, except COED owner, which decodes to no owner. xEdit's `wbFormIDCk` for SNAM ([FACT]) and PRKR ([PERK]) do not allow null. CNTO's `sigBaseObjects` includes NULL (wbDefinitionsFO4.pas:75), but Radicle's NPC_ decoder rejects a null CNTO item (src/forms/npc.cpp:247,374), so it is stricter than xEdit; COED owner allows NULL (FO4:3291).
- PRKZ must equal the number of PRKR entries when present, or decode throws. xEdit uses it as the count path (FO4:10393). COCT is not checked: decode returns the CNTO entries present, and edits rewrite COCT to the CNTO count (xEdit 93cc0bc5 Core/wbDefinitionsFO4.pas:3315-3316, COCT cpBenign, count path of the CNTO array).
- COED must immediately follow its CNTO. xEdit nests COED inside the CNTO group (FO4:3300-3314).

COED's third field is `global_or_rank`, stored raw. xEdit reads it as a Global FormID or as a required rank s32 depending on the owner: `wbCOEDOwnerDecider` (Common:5451-5469) selects rank for NPC_ or FACT owners. Radicle keeps the raw `u32`.

Edit rules:

- Add, remove at index, and set at index for each list. Remove and set on an out-of-range index throw. Add, remove and set return `false` when the stored bytes would not change.
- The list's own subrecords are replaced as one run. A run with other subrecords between its entries is refused.
- Removing the last entry drops PRKZ or COCT with the run.
- A missing run is inserted after the last subrecord that xEdit orders before it. The order is `k_order` in `src/forms/npc.cpp`. A record with no earlier subrecord (no ACBS) is refused.

### Link rows

| Path | Subrecord | Targets | xEdit |
| --- | --- | --- | --- |
| NPC_\Spell | SPLO | SPEL, LVSP | FO4:10372; wbSPLO FO4:3284; member of the `NPC_\Spells` group |
| NPC_\AttackSpell | ATKD | SPEL | FO4:4077, nullable |
| NPC_\ActorSoundKeyword | CS2K | KYWD | FO4:10434; wbActorSounds Common:8399-8414 |
| NPC_\ActorSoundSound | CS2D | SNDR | FO4:10434 |
| NPC_\TemplateActorTraits, Stats, Factions, SpellList, AIData, AIPackages, ModelAnimation, BaseData, Inventory, Script, DefaultPackageList, AttackData, Keywords | TPTA | BMMO, LVLN, NPC_ | FO4:10353; wbNPCTemplateActorEntry Common:8274, nullable |

`NPC_\AttackSpell` is a single 4-byte link at offset 8 inside the 44-byte ATKD payload, and the thirteen `NPC_\TemplateActor*` rows are the thirteen 4-byte links of the 52-byte TPTA payload at offsets 0 to 48, in xEdit member order (`wbNPCTemplateActorEntry` order: Traits, Stats, Factions, Spell List, AI Data, AI Packages, Model/Animation, Base Data, Inventory, Script, Def Package List, Attack Data, Keywords). All fourteen are `nullable: true` and `insertion: existing_only`.

`AttackSpell` is `repeated: true` with `payload_sizes: [44]`, because xEdit wraps ATKD in `wbAttackData` (`wxRStructSK([1], 'Attack', [ATKD, ATKE])`) and the NPC_ member is `wbRArrayS('Attacks', wbAttackData)` (FO4:10377), so a record may carry several ATKD subrecords. The ten ATKD scalars above are `repeated: true` with `payload_size: 44` for the same reason: index 0 is the first attack, index 1 the second, and an index past the last ATKD reports `subrecord_missing`. Because the payload must be exactly 44 bytes, a truncated ATKD exposes no member at all.

Each link of a repeated FormLink row is read and written by index through the generic FormLink field path, and a write leaves the other links byte-identical. Adding and removing links is not offered by that path; the repeated links that own a list row are `NPC_\Packages` and `NPC_\HeadParts` below.

`NPC_\Spell` is the same read and indexed write, but it is the member link of the `NPC_\Spells` subrecord group (`count_subrecord` `SPCT`, one required 4-byte `SPLO` member), so `insert_record_subrecord_group` and `remove_record_subrecord_group` add and remove a spell and rewrite `SPCT` with the run. The row carries no `payload_sizes` of its own; the member descriptor holds the 4-byte rule. `NPC_\SpellCount` is the group's whole-payload u32 count row, and a plain scalar write to it is refused, as for `CONT\ItemTotal`. `RACE\ActorEffects` still rows the same xEdit list shape with no count.

`NPC_\ActorSoundKeyword` and `NPC_\ActorSoundSound` are the two links of xEdit's `wbActorSounds` pair: one CS2K keyword and one CS2D sound per sound group. The CS2H count is `NPC_\ActorSoundTotal` and CS2K and CS2D are the members of the counted group `NPC_\ActorSounds` (optional keyword, then required sound), so a sound can be added and removed with its keyword and the count follows. xEdit's CS2E end marker is empty and is not created by an insert, so adding the first sound to an NPC_ with no CS2H is refused.

### Link lists

| Path | Subrecord | Targets | xEdit |
| --- | --- | --- | --- |
| NPC_\Packages | PKID | PACK | FO4:10400-10402; wbRArray |
| NPC_\HeadParts | PNAM | HDPT | FO4:10417; wbRArrayS |
| NPC_\AttachParentSlots | APPR | KYWD | FO4:10404; wbAPPR FO4:5275 |

`NPC_\Packages` and `NPC_\HeadParts` are `per_subrecord` lists over one 4-byte subrecord per element, `nullable: false` and `mutation: existing_neighbors`, so an element can be inserted, set, moved, removed and cleared by index. xEdit gives neither a count subrecord: `wbRArray('Packages', wbFormIDCk(PKID, 'Package', [PACK]))` (FO4:10400-10402) and `wbRArrayS('Head Parts', wbFormIDCk(PNAM, 'Head Part', [HDPT]))` (:10417) repeat the subrecord itself, so no counter has to be kept in sync. A record that is not four bytes reports `FormLinkListError` from `matching_positions`, the same guard the SPLO field rows apply through `payload_sizes`. Measured against Fallout4.esm (both top-level `NPC_` groups, compressed records inflated): 3015 NPC_ records, PKID present in 1120 of them (1 to 10 each, 1872 subrecords) and PNAM in 1602 (1 to 72 each, 17271 subrecords), every PKID, PNAM and SPLO payload exactly four bytes.

The two lists carry an explicit `anchor_predecessors` list in reverse xEdit order, so the first element of an absent list lands after the last present subrecord xEdit orders before it: after AIDT or whichever predecessor appears last for PKID, and after DNAM or whichever predecessor appears last for PNAM. Each list is the complete xEdit-order predecessor set, including all five members of the attack run (`ATKD`, `ATKE`, `ATKW`, `ATKS`, `ATKT`; `wbAttackData`, FO4:4073-4123), which the NPC_ authoring block lists as `ATKD` alone. NPC_ declares only list rows, so the sibling-derived anchors the generator would otherwise use are empty. A record with none of those predecessors fails closed with `insertion_boundary_unavailable` rather than appending out of order; every real NPC_ carries RNAM, which both lists list as a predecessor.

`NPC_\AttachParentSlots` is a typed list in the same shape as `RACE\AttachParentSlots`: a `packed_array` over the APPR subrecord, targets KYWD, `nullable: false` and `mutation: appendable`, so elements can be appended, set and removed. Its first element is anchored against the declared predecessors (`KWDA` and the rest of the subrecords xEdit orders before APPR, FO4:10404) and it appends at the end of the record when none of them is present.

`NPC_\ActorSoundsFinalize` is the whole 1-byte CS2F payload (xEdit `wbByteArray(CS2F, 'Finalize', 1)`, FO4:10436), `repeated: true`; Fallout4.esm and the DLC masters carry exactly one CS2F per NPC_ with sounds.

## Not covered

| xEdit | Field | Reason |
| --- | --- | --- |
| FO4:10298-10346 | ACBS unused bytes (offset 18) | Padding. |
| FO4:10347, 10377 | Attacks (ATKD and the rest of `wbAttackData`) | The eleven ATKD members are rowed above, indexed per attack instance. ATKE (`:4120`, `wbString`) has no row: Radicle has no null-terminated string storage. Factions (10347) are covered above. |
| FO4:10348-10369 | INAM, VTCK, TPLT, LTPT, LTPC, TPTA | Form IDs, not scalars. TPTA is rowed as thirteen links in the Link rows table. |
| FO4:10370-10383 | RNAM, WNAM, ANAM, ATKR, SPOR, OCOR, GWOR, ECOR, FCPL, RCLR | FormIDs, not scalars. |
| FO4:10371 | SPCT count | Rowed as `NPC_\SpellCount`, the count of the `NPC_\Spells` group. A generic write would desync it from SPLO, so `write_scalar` refuses it and only insert and remove write it. |
| FO4:10394-10396, 10399, 10401 | PRPS, FTYP, NTRM, AIDT's sibling PKID list | Properties (xEdit `wbPRPS`, `Core/wbDefinitionsFO4.pas:10394` at pin 93cc0bc5), an array of 8-byte `wbObjectProperty` entries (`:5266-5272`). Modeled by `NPC_\PropertyActorValue` (link to AVIF, nullable, offset 0, stride 8) and `NPC_\PropertyValue` (f32, offset 4, stride 8) in `Radicle/schema/fallout4/fields.json`, both existing_only. FTYP and NTRM are form IDs; PKID is a repeated form ID list, rowed as `NPC_\Packages`. |
| FO4:10403-10404 | KWDA/KSIZ keywords | Covered by the keyword FormLink tables. APPR is rowed as `NPC_\AttachParentSlots`. |
| FO4:10405 | Object template OBTE/OBTF/OBTS/STOP | `Core/wbDefinitionsFO4.pas:5520-5529` (pin 93cc0bc5): OBTE is the combination count, OBTF and STOP are empty markers, and each OBTS combination is a `wbStruct` whose keyword, include and property arrays carry their own counts, so its payload length varies. Walking the NPC_ groups of Fallout4.esm measured OBTE 4 bytes, OBTF 0, STOP 0 and OBTS lengths from 18 to 186 bytes, confirming the OBTS payload is not a fixed width. |
| FO4:10406-10408 | CNAM, FULL, SHRT | CNAM is a FormID. FULL and SHRT are localized strings; NPC_ is not in `localized_subrecords`, so localization is unchanged. |
| FO4:10409 | DATA | Empty marker subrecord; authoring only. |
| FO4:10417-10421 | PNAM head parts, HCLF, BCLF, ZNAM, GNAM | HCLF, BCLF, ZNAM and GNAM are form IDs. PNAM is a repeated form ID list, rowed as `NPC_\HeadParts`. |
| FO4:10422 | NAM5 | Unknown bytes of no fixed width. |
| FO4:10435 | CS2E | Empty end marker; not rowed and not created by a group insert. |
| FO4:10438-10444 | CSCR, PFRN, DOFT, SOFT, DPLT, CRIF, FTST | FormIDs. |
| FO4:10446-10447 | MSDK, MSDV | Morph arrays of variable length. |
| FO4:10459 | TEND offset 4 | Unused byte of `wbByteColors`. |
| FO4:10460 | FMRS from offset 28 | Unbounded trailing `wbByteArray('Unknown')` after the seven modeled values, which stop at offset 28, so the rows declare `minimum_payload_size` 28 and no `payload_size`. |
| FO4:10488 | ATTX | Localized string. |

## Authoring constraints

`authoring.subrecords` lists every top-level subrecord in xEdit order. `minimum` 1 mirrors the required flag on the xEdit member (for example OBND, ACBS, AIDT, RNAM, CNAM, DATA, NAM5, NAM6, NAM8). NAM7 is the exception: it has minimum 0, because no vanilla NPC_ record carries it. Defaults are given for the three fixed structs with a required flag: OBND (12 bytes), ACBS (20 bytes) and AIDT (24 bytes), all zero.

Repeated subrecords (SNAM, PRKR, CNTO, COED, SPLO, FULL, and others) have a maximum of 65535 and are not `uncounted`. The generic add and remove path refuses them, so the typed list API owns them.
