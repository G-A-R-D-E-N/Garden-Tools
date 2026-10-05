# Fallout 4 editing parity

Product roadmap: [#86](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/86).
Coverage authority: [#287](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/287).
Reusable infrastructure: [#43](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/43), only for demonstrated repetition or format needs.

## Scope and acceptance

Radicle is the native plugin backend for PluginTool and GardenEngine. Semantic support is classified separately as READ / WRITE / ADD / REMOVE / CREATE / REMAP / VALIDATE / OPAQUE. A decoder may land independently; it does not establish editing parity.

An editable surface must pass parse -> expose -> edit -> serialize -> reparse, including requested values, valid insertion/removal structure, master/reference remapping, rejected malformed writes, untouched fields and opaque bytes, and file save/reload. A serializer preserving an unmodified record proves preservation, not field interpretation or editing.

xEdit definitions, official plugin measurements, existing typed implementations, CK/CKRE and synthetic tests may establish stored layouts. Runtime RE is required for unresolved stored layouts, unproven safe mutation or actual runtime behavior. Evidence priority does not make executable RE a universal prerequisite. Unknown layouts remain opaque or reject mutation/remapping.

## Baseline and legend

Native baseline: main `cca4b080309053cff127b7202d388df00060388a`, inspected 2026-10-05. Open PR work is listed separately; it is not main coverage.

Definition inventory from the documentation in [TES5Edit 93cc0bc5a1251936c3c7859eee3150eda12a62d7, Core/wbDefinitionsFO4.pas](https://github.com/TES5Edit/TES5Edit/blob/93cc0bc5a1251936c3c7859eee3150eda12a62d7/Core/wbDefinitionsFO4.pas). SHA-256: `f0bacc90a7dbd62bf90ff570d1974932bd4b99488bd0da9bbcf4e01abdfb9c5e`.

Extraction excludes Pascal line/block comments while retaining string literals, then collects literal signatures passed to wbRecord, wbRefRecord and ReferenceRecord. It finds 147 unique active registration sites. It does not expand shared field helpers or evaluate conditional compilation. The older 140-signature checkpoint omitted CAMS, EYES, LVLI, PROJ, REVB, SCOL and SMBN.

- P: a partial named surface with existing regression evidence; no complete-family claim.
- I: implementation exists; per-field acceptance remains to be completed.
- U: unaudited semantic operation; neither support nor intentional exclusion is established.
- G: generic accepted-container preservation only; not semantic editing.
- --: not supplied by the particular path being assessed.
- n/a: not an operation on that value.

Opaque preservation is the generic writer contract in tests/test_writer.cpp. Record removal/copy and raw blank-record allocation are infrastructure; they do not prove semantic field removal or valid record creation. No record row is FULL.

## Record inventory

The P entries refer to the bounded surfaces below, not every xEdit member. Keyword I entries refer only to generated keyword lists. U entries are audit work, not justified permanent exclusions.

| Record | READ | WRITE | ADD | REMOVE | CREATE | REMAP | VALIDATE | OPAQUE | Evidence / pending parser |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| AACT | U | U | U | U | U | U | U | G | field audit pending; xEdit L6970 |
| ACHR | P | P | P | P | P | P | P | G | forms/placed_reference.hpp; xEdit L4483 |
| ACTI | P | I | I | I | U | U | P | G | environment / plugin headers; xEdit L5611 |
| ADDN | U | U | U | U | U | U | U | G | field audit pending; xEdit L7796 |
| AECH | U | U | U | U | U | U | U | G | field audit pending; xEdit L12206 |
| ALCH | I | I | I | I | U | U | I | G | field audit pending; xEdit L5691 |
| AMDL | U | U | U | U | U | U | U | G | field audit pending; xEdit L12246 |
| AMMO | I | I | I | I | U | U | I | G | field audit pending; xEdit L5741 |
| ANIO | U | U | U | U | U | U | U | G | field audit pending; xEdit L5777 |
| AORU | U | U | U | U | U | U | U | G | field audit pending; xEdit L12268 |
| ARMA | P | P | P | U | P | P | P | G | forms/armor.hpp; xEdit L5845 |
| ARMO | P | P | P | I | P | P | P | G | forms/armor.hpp; xEdit L5786 |
| ARTO | I | I | I | I | U | U | I | G | field audit pending; xEdit L9362 |
| ASPC | U | U | U | U | U | U | U | G | field audit pending; xEdit L7057 |
| ASTP | U | U | U | U | U | U | U | G | field audit pending; xEdit L9135 |
| AVIF | U | U | U | U | U | U | U | G | field audit pending; xEdit L7826 |
| BNDS | P | U | U | U | U | U | P | G | environment / plugin headers; xEdit L12280 |
| BOOK | I | I | I | I | U | U | I | G | field audit pending; xEdit L5891 |
| BPTD | U | U | U | U | U | U | U | G | field audit pending; xEdit L7693 |
| CAMS | U | U | U | U | U | U | U | G | field audit pending; xEdit L7880 |
| CELL | P | U | U | U | P | P | P | G | forms/cell.hpp; xEdit L5953 |
| CLAS | U | U | U | U | U | U | U | G | field audit pending; xEdit L6102 |
| CLFM | U | U | U | U | U | U | U | G | field audit pending; xEdit L9572 |
| CLMT | P | U | U | U | U | P | P | G | environment / plugin headers; PR #398; xEdit L6115 |
| CMPO | U | U | U | U | U | U | U | G | field audit pending; xEdit L12294 |
| COBJ | P | P | P | P | U | P | P | G | forms/components.hpp; xEdit L10266 |
| COLL | U | U | U | U | U | U | U | G | field audit pending; xEdit L9547 |
| CONT | I | I | I | I | U | U | I | G | field audit pending; xEdit L6176 |
| CPTH | U | U | U | U | U | U | U | G | field audit pending; xEdit L7930 |
| CSTY | U | U | U | U | U | U | U | G | field audit pending; PR #408; xEdit L6233 |
| DEBR | U | U | U | U | U | U | U | G | field audit pending; xEdit L7388 |
| DFOB | U | U | U | U | U | U | U | G | field audit pending; xEdit L12304 |
| DIAL | U | U | U | U | U | U | U | G | field audit pending; PR #406; xEdit L6309 |
| DLBR | U | U | U | U | U | U | U | G | field audit pending; xEdit L8778 |
| DLVW | U | U | U | U | U | U | U | G | field audit pending; xEdit L8819 |
| DMGT | U | U | U | U | U | U | U | G | field audit pending; xEdit L12310 |
| DOBJ | U | U | U | U | U | P | U | G | field audit pending; xEdit L8618 |
| DOOR | I | I | I | I | U | U | I | G | field audit pending; xEdit L6477 |
| DUAL | U | U | U | U | U | U | U | G | field audit pending; xEdit L9452 |
| ECZN | U | U | U | U | U | U | U | G | field audit pending; xEdit L8022 |
| EFSH | U | U | U | U | U | U | U | G | field audit pending; xEdit L6510 |
| ENCH | U | U | U | U | U | U | U | G | field audit pending; xEdit L6726 |
| EQUP | U | U | U | U | U | P | U | G | field audit pending; xEdit L8835 |
| EXPL | P | P | U | U | P | P | P | G | forms/proj_expl_hazd.hpp; xEdit L7334 |
| EYES | P | P | P | U | P | P | P | G | forms/eyes.hpp; xEdit L6752 |
| FACT | U | U | U | U | U | U | U | G | field audit pending; xEdit L6785 |
| FLOR | I | I | I | I | U | U | I | G | field audit pending; PR #404; xEdit L12167 |
| FLST | P | P | P | P | U | P | P | G | environment / plugin headers; xEdit L7574 |
| FSTP | U | U | U | U | U | U | U | G | field audit pending; PR #400; xEdit L8686 |
| FSTS | U | U | U | U | U | U | U | G | field audit pending; PR #400; xEdit L8692 |
| FURN | I | I | I | I | U | U | I | G | field audit pending; xEdit L6848 |
| GDRY | U | U | U | U | U | U | U | G | field audit pending; xEdit L12322 |
| GLOB | U | U | U | U | U | U | U | G | field audit pending; xEdit L6921 |
| GMST | U | U | U | U | U | U | U | G | field audit pending; xEdit L6936 |
| GRAS | U | U | U | U | U | U | U | G | field audit pending; PR #404; xEdit L9612 |
| HAZD | P | P | U | U | P | P | P | G | forms/proj_expl_hazd.hpp; xEdit L7180 |
| HDPT | U | U | U | U | U | U | U | G | field audit pending; xEdit L7006 |
| IDLE | U | U | U | U | U | U | U | G | field audit pending; PR #408; xEdit L9646 |
| IDLM | I | I | I | I | U | U | I | G | field audit pending; xEdit L7098 |
| IMAD | U | U | U | U | U | U | U | G | field audit pending; xEdit L7466 |
| IMGS | U | U | U | U | U | U | U | G | field audit pending; xEdit L7393 |
| INFO | P | U | U | U | U | P | P | G | forms/placed_reference.hpp; PR #406; xEdit L9672 |
| INGR | I | I | I | I | U | U | I | G | field audit pending; xEdit L9788 |
| INNR | U | U | U | U | U | U | U | G | field audit pending; xEdit L12337 |
| IPCT | U | U | U | U | U | U | U | G | field audit pending; xEdit L7978 |
| IPDS | U | U | U | U | U | P | U | G | field audit pending; xEdit L8014 |
| KEYM | I | I | I | I | U | U | I | G | field audit pending; xEdit L9823 |
| KSSM | U | U | U | U | U | U | U | G | field audit pending; xEdit L12387 |
| KYWD | U | U | U | U | U | U | U | G | field audit pending; xEdit L6947 |
| LAND | P | U | U | U | U | P | P | G | environment / plugin headers; xEdit L9846 |
| LAYR | P | U | U | U | U | U | P | G | environment / plugin headers; xEdit L12400 |
| LCRT | U | U | U | U | U | U | U | G | field audit pending; xEdit L6962 |
| LCTN | I | I | I | I | U | U | I | G | field audit pending; xEdit L8039 |
| LENS | U | U | U | U | U | U | U | G | field audit pending; xEdit L12405 |
| LGTM | U | U | U | U | U | U | U | G | field audit pending; xEdit L8629 |
| LIGH | P | I | I | I | U | U | P | G | environment / plugin headers; xEdit L9871 |
| LSCR | U | U | U | U | U | U | U | G | field audit pending; xEdit L9946 |
| LTEX | P | U | U | U | U | U | P | G | environment / plugin headers; PR #404; xEdit L9967 |
| LVLI | U | U | U | U | U | U | U | G | field audit pending; xEdit L10009 |
| LVLN | U | U | U | U | U | U | U | G | field audit pending; xEdit L9986 |
| LVSP | P | P | P | P | P | P | P | G | forms/leveled_spell.hpp; xEdit L10033 |
| MATO | U | U | U | U | U | U | U | G | field audit pending; xEdit L9375 |
| MATT | U | U | U | U | U | U | U | G | field audit pending; xEdit L7958 |
| MESG | U | U | U | U | U | U | U | G | field audit pending; xEdit L8179 |
| MGEF | I | I | I | I | U | U | I | G | field audit pending; xEdit L10052 |
| MISC | P | P | P | P | U | P | P | G | forms/components.hpp; xEdit L10212 |
| MOVT | U | U | U | U | U | U | U | G | field audit pending; xEdit L9393 |
| MSTT | I | I | I | I | U | U | I | G | field audit pending; xEdit L7067 |
| MSWP | P | U | U | U | U | U | P | G | environment / plugin headers; xEdit L12437 |
| MUSC | U | U | U | U | U | P | U | G | field audit pending; PR #400; xEdit L8665 |
| MUST | U | U | U | U | U | U | U | G | field audit pending; xEdit L8795 |
| NAVI | U | U | U | U | U | U | U | G | field audit pending; PR #402; xEdit L7240 |
| NAVM | U | U | U | U | U | P | U | G | field audit pending; PR #402; xEdit L7212 |
| NOCM | U | U | U | U | U | U | U | G | field audit pending; xEdit L12453 |
| NOTE | U | U | U | U | U | U | U | G | field audit pending; xEdit L12470 |
| NPC_ | I | I | I | I | U | U | I | G | field audit pending; xEdit L10286 |
| OMOD | U | U | U | U | U | P | U | G | field audit pending; xEdit L12501 |
| OTFT | U | U | U | U | U | U | U | G | field audit pending; xEdit L9357 |
| OVIS | U | U | U | U | U | U | U | G | field audit pending; xEdit L12546 |
| PACK | U | U | U | U | U | U | U | G | field audit pending; PR #408; xEdit L10526 |
| PARW | U | U | U | U | U | U | U | G | field audit pending; xEdit L5944 |
| PBAR | U | U | U | U | U | U | U | G | field audit pending; xEdit L5945 |
| PBEA | U | U | U | U | U | U | U | G | field audit pending; xEdit L5946 |
| PCON | U | U | U | U | U | U | U | G | field audit pending; xEdit L5947 |
| PERK | U | U | U | U | U | U | U | G | field audit pending; xEdit L7582 |
| PFLA | U | U | U | U | U | U | U | G | field audit pending; xEdit L5948 |
| PGRE | U | U | U | U | U | U | U | G | field audit pending; xEdit L5949 |
| PHZD | U | U | U | U | U | U | U | G | field audit pending; xEdit L5950 |
| PKIN | U | U | U | U | U | U | U | G | field audit pending; xEdit L12571 |
| PLYR | U | U | U | U | U | U | U | G | field audit pending; xEdit L12129 |
| PMIS | U | U | U | U | U | U | U | G | field audit pending; xEdit L5951 |
| PROJ | P | P | U | U | P | P | P | G | forms/proj_expl_hazd.hpp; xEdit L7115 |
| QUST | P | P | P | P | U | P | P | G | forms/quest.hpp; xEdit L10684 |
| RACE | P | P | P | P | P | P | P | G | forms/race.hpp; xEdit L11054 |
| REFR | P | P | P | P | P | P | P | G | forms/placed_reference.hpp; xEdit L11347 |
| REGN | U | U | U | U | U | U | U | G | field audit pending; xEdit L11870 |
| RELA | U | U | U | U | U | U | U | G | field audit pending; xEdit L8848 |
| REVB | U | U | U | U | U | U | U | G | field audit pending; xEdit L9593 |
| RFCT | U | U | U | U | U | U | U | G | field audit pending; xEdit L6163 |
| RFGP | U | U | U | U | U | U | U | G | field audit pending; xEdit L12582 |
| SCCO | U | U | U | U | U | U | U | G | field audit pending; xEdit L12593 |
| SCEN | I | I | I | I | U | U | I | G | field audit pending; PR #406; xEdit L8879 |
| SCOL | P | U | U | U | U | U | P | G | forms/static_collection.hpp; xEdit L12627 |
| SCSN | U | U | U | U | U | U | U | G | field audit pending; xEdit L12650 |
| SMBN | U | U | U | U | U | U | U | G | field audit pending; xEdit L8720 |
| SMEN | U | U | U | U | U | U | U | G | field audit pending; xEdit L8762 |
| SMQN | U | U | U | U | U | U | U | G | field audit pending; xEdit L8735 |
| SNCT | U | U | U | U | U | U | U | G | field audit pending; xEdit L9469 |
| SNDR | U | U | U | U | U | U | U | G | field audit pending; PR #400; xEdit L9406 |
| SOPM | U | U | U | U | U | U | U | G | field audit pending; xEdit L9489 |
| SOUN | U | U | U | U | U | U | U | G | field audit pending; PR #400; xEdit L11961 |
| SPEL | I | I | I | I | U | U | I | G | field audit pending; xEdit L12030 |
| SPGD | U | U | U | U | U | U | U | G | field audit pending; xEdit L6128 |
| STAG | U | U | U | U | U | U | U | G | field audit pending; xEdit L12663 |
| STAT | P | U | U | U | U | U | P | G | environment / plugin headers; xEdit L12045 |
| TACT | I | I | I | I | U | U | I | G | field audit pending; xEdit L5672 |
| TERM | I | I | I | I | U | U | I | G | field audit pending; xEdit L12671 |
| TES4 | P | U | U | U | U | P | P | G | environment / plugin headers; xEdit L12091 |
| TREE | U | U | U | U | U | U | U | G | field audit pending; PR #404; xEdit L12134 |
| TRNS | U | U | U | U | U | U | U | G | field audit pending; xEdit L12774 |
| TXST | P | U | U | U | U | U | P | G | environment / plugin headers; xEdit L6984 |
| VTYP | U | U | U | U | U | U | U | G | field audit pending; xEdit L7950 |
| WATR | P | U | U | U | U | U | P | G | environment / plugin headers; PR #396; xEdit L12788 |
| WEAP | I | I | I | I | U | U | I | G | field audit pending; xEdit L12877 |
| WRLD | P | U | U | U | U | P | P | G | environment / plugin headers; xEdit L13110 |
| WTHR | P | P | U | U | U | P | P | G | forms/weather_concept.hpp; PR #398; xEdit L13034 |
| ZOOM | U | U | U | U | U | U | U | G | field audit pending; xEdit L13180 |

## Existing bounded editing surfaces

| Surface | Implementations and regression sources | Remaining coverage |
| --- | --- | --- |
| ARMA / ARMO models | forms/armor.hpp; tests/test_armor_edit.cpp | Fields outside model bundles; remove/create parity beyond typed shape |
| EYES EDID / FULL / DATA | forms/eyes.hpp; tests/test_eyes.cpp | Localized/opaque FULL cannot be rewritten as inline text; broad deletion not proven |
| LVSP settings / entries | forms/leveled_spell.hpp; tests/test_lvsp.cpp | LVLI/LVLN share no proven typed creator; creation defaults need separate evidence |
| PROJ / EXPL / HAZD fixed values | forms/proj_expl_hazd.hpp; tests/test_scalar_embedded.cpp | Complete per-field edited round-trips, C ABI/C# mutation exposure |
| QUST stages / objectives / aliases | forms/quest.hpp; tests/test_quest_authoring.cpp, test_quest_alias_collection_authoring.cpp, test_quest_authoring_real.cpp | Full QUST creation; all fragmented/property VMAD variants; runtime fills are separate |
| RACE body / nested links | forms/race.hpp; tests/test_race_body.cpp, test_formlink_lists.cpp | Other appearance/morph members; full field-level proof |
| REFR / ACHR placed data | forms/placed_reference.hpp; tests/test_environment_writeback.cpp, test_writeback_real.cpp; managed WritebackTests.cs | Optional insertion depends on field; copied records are not arbitrary default creation |
| CELL exterior creation | forms/cell.hpp; tests/test_exterior_cell_creation.cpp | CELL field editors, interior creation and new WRLD creation |
| MISC / COBJ components | forms/components.hpp; tests/test_components.cpp | Other record members and whole-record creation |
| WTHR MNAM | forms/weather_concept.hpp; tests/test_weather_concept.cpp | Other weather fields; missing/duplicate MNAM refuses mutation |
| FLST / generated keywords | schema/formlink_lists.hpp; tests/test_formlink_lists.cpp | Each record's empty-list anchors and save/reload proof; unrelated fields |
| FormIDs / masters | schema/form_links.hpp; tests/test_formlinks.cpp, test_navmesh_formlinks.cpp, test_remap_non_formid_slots.cpp | Mixed/opaque containers require complete ownership proof; unsupported VMAD refuses remap |

These are existing test sources, not a new claim that this documentation head has run every suite. The open PR validation ledger records exact heads separately.

## Named schema fields

All 41 fields currently declared under records[].fields in schema/fallout4/fields.json are listed below. This grades the generic descriptor path only: plugin/mutation.cpp:set_record_scalar and set_record_formlink_field require existing storage. The separate typed XSCL optional insertion/removal remains credited in REFR writeback above. CREATE is record-level, not a descriptor operation.

I is deliberately weaker than complete acceptance: generated offsets and setters alone do not prove every value against corpus, every edited round-trip, or managed mutation coverage. FormLink setters validate local target types and existing master encoding; they do not supply automatic master insertion or complete external target validation.

| Field | Kind | READ | WRITE | ADD | REMOVE | CREATE | REMAP | VALIDATE | OPAQUE |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `EXPL\Damage` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `EXPL\Flags` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `EXPL\Force` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `EXPL\ImageSpaceRadius` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `EXPL\InnerRadius` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `EXPL\OuterRadius` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `EXPL\PlacedObjectFadeDelay` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `EXPL\ProjectileCount` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `EXPL\ProjectileSpread` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `EXPL\SoundLevel` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `EXPL\SpawnProjectileVectorX` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `EXPL\SpawnProjectileVectorY` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `EXPL\SpawnProjectileVectorZ` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `EXPL\StaggerMagnitude` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `EXPL\VerticalOffsetMult` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `HAZD\Flags` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `HAZD\FullEffectRadius` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `HAZD\ImageSpaceRadius` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `HAZD\Lifetime` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `HAZD\Limit` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `HAZD\Radius` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `HAZD\TaperCurve` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `HAZD\TaperWeight` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `HAZD\TargetInterval` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `PROJ\CollisionRadius` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `PROJ\ConeSpread` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `PROJ\ExplosionProximity` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `PROJ\ExplosionTimer` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `PROJ\FadeOutTime` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `PROJ\Flags` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `PROJ\Force` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `PROJ\Gravity` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `PROJ\Lifetime` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `PROJ\MuzzleFlashDuration` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `PROJ\Range` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `PROJ\RelaunchInterval` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `PROJ\SoundLevel` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `PROJ\Speed` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `PROJ\TracerFrequency` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `REFR\HealthPercent` | scalar | I | I | -- | -- | n/a | n/a | I | G |
| `REFR\Scale` | scalar | I | I | -- | -- | n/a | n/a | I | G |

## Open parser PR audit

Snapshot: 2026-10-05. Columns apply to the new named fields, not unrelated metadata or existing generic capabilities.

| PR / issue | Stored-format tranche | READ | WRITE / ADD / REMOVE / CREATE gap | REMAP / VALIDATE / OPAQUE boundary |
| --- | --- | --- | --- | --- |
| #396 / #341 | WATR visual data; effective cell/world selection | Partial C++/C/C# | WATR visual mutation and creation absent; derived effective selection is not a stored field to write | Existing raw links; unknown visual layouts preserved; runtime selection evidence separate |
| #398 / #343 | CLMT timing and moon byte; WTHR DATA | Partial C++/C/C# | CLMT timing edit/write/reparse first; WTHR MNAM already writable; other fields remain follow-ups | WLST/WTHR known links remap; raw bytes retained; weather execution separate |
| #400 / #340 | SNDR variants, SOUN, MUSC, FSTP/FSTS | Partial C++/C/C# | Variant-aware existing edits, file/link add/remove and valid creation absent | Discriminator-specific references must be proved; playback is separate |
| #402 / #339 | NAVM geometry and NAVI maps | Partial C++/C/C# | Geometry/container mutation and valid grouped creation absent | Existing FormID grammar only; opaque NVPP/island/cover data cannot be reconstructed by guessing |
| #404 / #342 | GRAS DATA, TREE CNAM, FLOR seasons/links, LTEX grass | Partial C++/C/C# | Existing value edits first, then proven optional insertion/deletion and creation | FLOR keywords exist separately; numeric bytes cannot be remapped as links; placement algorithms separate |
| #406 / #344 | DIAL, INFO responses, SCEN phases/actions | Partial C++/C/C# | Response/action editing and container add/remove/create absent | INFO copy-as-new and SCEN keyword editing already exist; action/VMAD ownership remains scoped |
| #408 / #345 | PACK headers/schedule/locations/targets, CSTY, IDLE | Partial C++/C/C# | Existing fixed data edits first; procedure/input/tree mutation and creation unproven | Discriminators distinguish IDs from numeric values; preserve opaque procedure streams |
| #409 / #15 | VMAD variable evidence | Known properties and empty type 16 | Type 6 / nonempty 16 mutation/remap remains refused; variable authoring absent | Empty type 16 remaps surrounding object references; strict traversal required; no unsafe runtime error recovery |
| #410 / #28 | Context-driven runtime light IDs | Context API | No stored-format writer prerequisite | Runtime table construction is separate from on-disk MAST ordinals |
| #411 / #29 | TES4 localization evidence | Existing stored string-ID/inline decoding | Audit flag changes and string-table edits transactionally; do not normalize unknown fields | Runtime localization naming or CK conversion is not a blanket blocker |
| #412 / #30 | Strict compressed record framing | Stored reader / existing zlib writer | Edited compressed round-trips must remain covered | Malformed-input runtime tolerance is separate; unknown/untouched bytes preserved |
| #392 / #26 | ESL compaction policy | Existing identity API | REMAP safety tranche, not broad record authoring parity | Unsupported nested rekeying remains refused; next-gen runtime ordering separate |
| #394 / #21 | BA2 layout/fixtures | Archive read/extract/write acceptance | Archive support does not count as plugin-field editing | Named installed fixtures; no embedded game data |
| #419 / #422 | CI workflow / runner tooling | Infrastructure | No semantic editing promotion | #384 managed execution still required; audit shared CI changes independently |
| #424 / #287 | Operation matrix | Documentation | Main baseline and branch capabilities stay separate; full field/helper expansion pending | No new format or runtime acceptance |
| #426 / #384 | Required managed CI | Infrastructure | Native/C ABI/managed checks required; no field promotion | Verified both runners and clean Windows teardown at its head; adoption in feature branches pending |
| #427 / #425 | Existing CLMT timing edits | Partial C++/C/C# | Existing WRITE tested; TNAM ADD/REMOVE and CLMT CREATE absent | See bounded writer ledger below; no runtime weather reproduction |

Provisional ABI versions in parallel parser branches are not a merge plan. Main is ABI 8 at this baseline. Before any approved merge, rebase in dependency order, allocate the next actual version from then-current main, regenerate C# bindings and layout probes, and rerun exact-head CI.

### Pending CLMT writer tranche

[WIP PR #427](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/pulls/427), head `cbe898279607d5e3efe2afcc53da778ec52b8042`, targets the #398 reader branch. It implements replacement of an existing unique six-byte TNAM. This is branch coverage, not a promotion of the main-baseline row above. Both native jobs passed in [run 577](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/actions/runs/577). All seven official-master-derived sky/mutation tests passed locally with zero skips/failures (9.28 seconds). Its current base does not yet include #426 required managed CI.

| CLMT/TNAM stored field | READ | WRITE | ADD | REMOVE | CREATE | REMAP | VALIDATE | OPAQUE |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Sunrise begin | P | P | -- | -- | -- | n/a | P | P |
| Sunrise end | P | P | -- | -- | -- | n/a | P | P |
| Sunset begin | P | P | -- | -- | -- | n/a | P | P |
| Sunset end | P | P | -- | -- | -- | n/a | P | P |
| Volatility | P | P | -- | -- | -- | n/a | P | P |
| Packed moon phase byte | P | P | -- | -- | -- | n/a | P | P |

`environment/base_records.cpp:set_climate_timing` and `PluginFile.cs:SetClimateTiming` have native/C ABI/managed save-reload, no-op byte preservation and malformed-layout refusal tests. `tests/test_sky_parameters.cpp` covers compressed targets, extended opaque fields and edits to official-master-derived climate records; `tests/test_clmt_formlinks.cpp` covers timing edits through WLST/master remapping. Of 800 registered local tests, 756 passed and 44 optional/corpus/platform cases skipped, with zero failures; the separate managed suite passed 75/75. The seven official cases ran separately with `FO4_DATA_DIR` set, proving requested timing values after reparse, preservation of other fields and byte-identical untouched masters. Proof: `radicle-climate-writer-425-20261005/build/writer-official.log` and `build/guard-release/writer-official.xml`. Required managed CI integration remains pending. ADD/REMOVE TNAM and valid CLMT CREATE remain separate gaps in #287.

The empty type-16 VMAD follow-up is sourced separately from the verified private [VMAD loader record](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/6916afbcbec8ab09ecc0dfd30661502d0064f4f7/RE-Work/subsystems/plugin-io-garden-tools/vmad-388-15/README.md), inspected with its build-specific address stamps. It does not establish nonempty variable mutation.

## Open issue disposition

| Issues | #287 relationship | Next proof / boundary |
| --- | --- | --- |
| #86, #287, #43 | Product / coverage / justified infrastructure | Maintain this matrix; expand schema only for proven repetition |
| #339-#345 | Parser tranches above | Keep reads independent; close stored writer gaps with operation-specific regressions |
| #395, #397, #399, #401, #403, #405, #407 | Runtime and some loader reconciliation | Reconcile new answers; stored editing does not wait for unrelated execution behavior |
| #15, #388 | Unresolved VMAD layout safety | Empty type 16 evidence can narrow refusal; other unknown variants stay opaque |
| #28, #389 | Runtime full/light index construction | Separate from stored plugin identity/remap |
| #29, #390 | Stored localization and runtime/CK semantics | Stored writes require correct representation; unresolved runtime naming is separate |
| #30, #391 | Compression container / runtime tolerance | Conservative valid writer; unsafe acceptance emulation not required |
| #21, #26 | BA2 / ESL existing proof | Review current PRs; do not retain obsolete blanket RE blockers |
| #421 | AVIF / PERK new read model | Pin fields and corpus; plan mutation gaps separately; no runtime formulas |
| #423 | QUST C ABI expansion | Reuse typed authoring; expose tested stages/objectives/aliases; runtime execution separate |
| #384 | Managed test CI | Required native/C ABI/managed checks; generation checks alone are insufficient |
| #425 | Existing CLMT timing WRITE | Tested in #427; independent ADD/REMOVE/CREATE proof remains pending |
| #32 | Localization editor consumer | Stored ID/inline classification is established; explicit sidecar ownership and transactional edits still need proof |
| #31 | Native diagnostics consumer | Expand VALIDATE coverage using proven invariants; no new Mutagen dependency |
| #35, #51 | Specialized writer/validator consumers | Precombine stamping needs generated-data/layout proof; stored dialogue-link validation is separate from runtime merge order |
| #338 | Stale roadmap audit | Reconcile product/evidence text without changing unrelated tool roadmaps |
| #346 | Shared plugin/archive architecture | No redesign prerequisite for proving current editing contracts |

The refreshed 2026-10-05 inventory contains 85 open repository issues (both API pages inspected) and 18 open PRs. The rows above cover all explicitly Radicle-tagged/titled issues, the governing product/schema/parity/CI trackers and direct writer/validator consumers. All 18 PRs appear in the parser/infrastructure ledger, including the independent runner tooling. Other PluginTool workflows and unrelated BGS/F4LODGEN issues remain outside this backend tranche. This is a scope/disposition audit; each unexamined field remains U and needs its own implementation and acceptance audit.

## Ordered follow-ups

1. Finish per-field audit for the new stored read surfaces and typed models, including generated list/string/condition descriptors. U entries remain pending, not complete or intentionally excluded.
2. Add CLMT existing timing mutation with parse/edit/serialize/reparse, negative writes, neighbor preservation and native/C ABI/C# tests. Establish optional TNAM insertion/removal and whole CLMT creation separately.
3. Promote vegetation fixed data edits, then sound variants, dialogue/scene containers and PACK fixed data; require add/remove/count/remap evidence before each promotion. NAVM geometry writes need complete container ownership and group structure proof.
4. Close #384 as a required CI check, then complete exact-head and official-corpus validation for every promoted surface.
5. Record intentionally unsupported members with specific reasons; expand #43 only where repetition warrants it. Integrate PluginTool/Garden after the backend contract is proven.

A complete field-by-field xEdit helper expansion, all typed members, all scope-specific add/remove/create tests and full managed mutation coverage are still pending. This baseline is not full FO4Edit parity and does not close #287.
