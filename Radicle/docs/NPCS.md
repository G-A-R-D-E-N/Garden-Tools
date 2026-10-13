# NPC_ records

source:xEdit 93cc0bc5a1251936c3c7859eee3150eda12a62d7, `Core/wbDefinitionsFO4.pas` NPC_ record at line 10286. Helper definitions are in `Core/wbDefinitionsCommon.pas` and `Core/wbInterface.pas`; each is cited where it is used.

The schema lives in `schema/fallout4/fields.json` (`NPC_` entry). Scalars are generated into `include/fo4/schema/generated_scalar_fields.inc` and authoring constraints into `generated_record_constraints.inc`. The three repeated lists have typed code in `include/fo4/forms/npc.hpp` and `src/forms/npc.cpp`.

## Scalar fields

Every scalar uses `existing_only` insertion. The presentation is `identity` except on rows that carry xEdit names: `ConfigurationFlags` and `TemplateFlags` use `flags`, and `AIAggression`, `AIConfidence`, `AIMorality`, `AIMood`, `AIAssistance`, `AIAggroRadiusBehavior`, `AINoSlowApproach` and `SoundLevel` use `enum` (`wbAggressionEnum` Common:6973-6982, `wbAssistanceEnum` 7023-7031, `wbConfidenceEnum` 7249-7259, `wbMoodEnum` 7676-7689, `wbMoralityEnum` 7691-7700, `wbBoolEnum` 7240-7247, `wbSoundLevelEnum` FO4:3424-3430). Bounds are unchanged, so the names do not restrict what is written. A fixed struct with a `payload_size` is read only when the subrecord has exactly that many bytes; a shorter payload reports the field as absent.

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
| NPC_\ObjectBoundsMin X/Y/Z | OBND | 0, 2, 4 | i16 | 12 | 9074-9090; wbVec3Int Common:6434-6446 |
| NPC_\ObjectBoundsMax X/Y/Z | OBND | 6, 8, 10 | i16 | 12 | 9074-9090 |
| NPC_\WeightThin, WeightMuscular, WeightFat | MWGT | 0, 4, 8 | f32 | 12 | 10426-10430 |
| NPC_\MorphRegionHead, UpperTorso, Arms, LowerTorso, Legs | MRSV | 0, 4, 8, 12, 16 | f32 | 20 | 10463-10469 |
| NPC_\TextureLightingRed, Green, Blue, Alpha | QNAM | 0, 4, 8, 12 | f32 | 16 | 10445; wbFloatRGBA Common:6899-6921 |
| NPC_\HeightMin | NAM6 | whole | f32 | 4 | 10423 |
| NPC_\HeightMax | NAM4 | whole | f32 | 4 | 10425 |
| NPC_\UnusedFloat | NAM7 | whole | f32 | 4 | 10424 |
| NPC_\FacialMorphIntensity | FMIN | whole | f32 | 4 | 10487 |
| NPC_\SoundLevel | NAM8 | whole | u32 | 4 | 10431; wbSoundLevelEnum FO4:3424 |

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
| PRKR | Perk form ID + Rank | 5 bytes: form ID u32, Rank u8 | FO4:10385-10393 (`SetCountPath(PRKZ)`) |
| COCT | Count | u32 | FO4:10397; wbCOCT FO4:3315 |
| CNTO | Item form ID + Count | 8 bytes: form ID u32, Count s32 | wbCNTO FO4:3300-3305 |
| COED | Owner, global/rank, condition | 12 bytes: Owner form ID u32, u32, float | wbCOED FO4:3290-3298 |

Decode rules:

- Each list is read in subrecord order.
- A payload of the wrong size throws `NpcError`.
- A null form ID throws, except COED owner, which decodes to no owner. xEdit's `wbFormIDCk` for SNAM ([FACT]), PRKR ([PERK]) and CNTO (`sigBaseObjects`, NULL excluded) does not allow null; COED owner allows NULL (FO4:3291).
- PRKZ must equal the number of PRKR entries when present, or decode throws. xEdit uses it as the count path (FO4:10393). COCT is not checked: decode returns the CNTO entries present, and edits rewrite COCT to the CNTO count (xEdit 93cc0bc5 Core/wbDefinitionsFO4.pas:3315-3316, COCT cpBenign, count path of the CNTO array).
- COED must immediately follow its CNTO. xEdit nests COED inside the CNTO group (FO4:3300-3314).

COED's third field is `global_or_rank`, stored raw. xEdit reads it as a Global form ID or as a required rank s32 depending on the owner: `wbCOEDOwnerDecider` (Common:5451-5469) selects rank for NPC_ or FACT owners. Radicle keeps the raw `u32`.

Edit rules:

- Add, remove at index, and set at index for each list. Remove and set on an out-of-range index throw. Add, remove and set return `false` when the stored bytes would not change.
- The list's own subrecords are replaced as one run. A run with other subrecords between its entries is refused.
- Removing the last entry keeps PRKZ or COCT at 0. xEdit sets the count element to 0 and keeps it (`Core/wbImplementation.pas:20774-20797`, `TwbSubRecordArray.BeforeActualRemove`).
- A local CNTO item must be a base object (`is_sig_base_object`, the xEdit `sigBaseObjects` list at `Core/wbDefinitionsFO4.pas:74`), and a local COED owner must be NPC_ or FACT (`Core/wbDefinitionsFO4.pas:3291`). Foreign keys are not checked.
- A missing run is inserted after the last subrecord that xEdit orders before it. The order is `k_order` in `src/forms/npc.cpp`. A record with no earlier subrecord (no ACBS) is refused.

## Not covered

| xEdit | Field | Reason |
| --- | --- | --- |
| FO4:10298-10346 | ACBS unused bytes (offset 18) | Padding. |
| FO4:10347, 10377 | Attacks (ATKD and the rest of `wbAttackData`) | Repeated struct. Only the ATKD payload is in authoring; the list is not modeled. Factions (10347) are covered above. |
| FO4:10348-10369 | INAM, VTCK, TPLT, LTPT, LTPC, TPTA | form IDs, not scalars. TPTA is a thirteen-form ID struct. Covered by the FormLink tables where they exist. |
| FO4:10370-10383 | RNAM, WNAM, ANAM, ATKR, SPOR, OCOR, GWOR, ECOR, FCPL, RCLR | form IDs, not scalars. |
| FO4:10371, 10372 | SPCT count, SPLO list | Count with no typed owner; a generic write would desync it from SPLO. |
| FO4:10394-10396, 10399, 10401 | PRPS, FTYP, NTRM, AIDT's sibling PKID list | PRPS is an object property list; FTYP and NTRM are form IDs; PKID is a repeated form ID list. |
| FO4:10403-10404 | KWDA/KSIZ keywords, APPR | Covered by the keyword FormLink tables. |
| FO4:10405 | Object template OBTE/OBTF/OBTS/STOP | Combination struct; not modeled. |
| FO4:10406-10408 | CNAM, FULL, SHRT | CNAM is a form ID. FULL and SHRT are localized strings; NPC_ is not in `localized_subrecords`, so localization is unchanged. |
| FO4:10409 | DATA | Empty marker subrecord; authoring only. |
| FO4:10417-10421 | PNAM head parts, HCLF, BCLF, ZNAM, GNAM | form IDs or a repeated form ID list. |
| FO4:10422 | NAM5 | Unknown bytes of no fixed width. |
| FO4:10432-10436 | CS2H count, CS2K/CS2D sound pairs, CS2E, CS2F | Count and list members are not modeled; CS2H has no typed owner, as with SPCT. |
| FO4:10438-10444 | CSCR, PFRN, DOFT, SOFT, DPLT, CRIF, FTST | form IDs. |
| FO4:10446-10447 | MSDK, MSDV | Morph arrays of variable length. |
| FO4:10448-10462 | TETI, TEND face tint layers | Repeated struct list; not modeled. |
| FO4:10471-10486 | FMRI, FMRS face morphs | Repeated struct list; not modeled. |
| FO4:10488 | ATTX | Localized string. |

## Authoring constraints

`authoring.subrecords` lists every top-level subrecord in xEdit order. `minimum` 1 mirrors the required flag on the xEdit member (for example OBND, ACBS, AIDT, RNAM, CNAM, DATA, NAM5, NAM6, NAM7, NAM8). Defaults are given for the three fixed structs with a required flag: OBND (12 bytes), ACBS (20 bytes) and AIDT (24 bytes), all zero.

Repeated subrecords (SNAM, PRKR, CNTO, COED, SPLO, FULL, and others) have a maximum of 65535 and are not `uncounted`. The generic add and remove path refuses them, so the typed list API owns them.
