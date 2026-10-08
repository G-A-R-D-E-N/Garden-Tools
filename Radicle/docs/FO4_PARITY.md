# Fallout 4 editing parity

Product roadmap: [#86](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/86).
Coverage authority: [#287](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/287).
Reusable infrastructure: [#43](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/43), only for demonstrated repetition or format needs.

## Scope and acceptance

Radicle is the native plugin backend for PluginTool and GardenEngine. Semantic support is classified separately as READ / WRITE / ADD / REMOVE / CREATE / REMAP / VALIDATE / OPAQUE. A decoder may land independently; it does not establish editing parity.

An editable surface must pass parse -> expose -> edit -> serialize -> reparse, including requested values, valid insertion/removal structure, master/reference remapping, rejected malformed writes, untouched fields and opaque bytes, and file save/reload. A serializer preserving an unmodified record proves preservation, not field interpretation or editing.

xEdit definitions, official plugin measurements, existing typed implementations, CK/CKRE and synthetic tests may establish stored layouts. Runtime RE is required for unresolved stored layouts, unproven safe mutation or actual runtime behavior. Evidence priority does not make executable RE a universal prerequisite. Unknown layouts remain opaque or reject mutation/remapping.

## Baseline and legend

Historical inventory baseline: main `cca4b080309053cff127b7202d388df00060388a`, inspected 2026-10-05. The record and named-field matrices below retain that snapshot; they are not a fresh inventory of current main. PR #424 now includes main `c50ad4d5405e1571113019654b10e3d4a05ca0f1` (ABI 11) through a merge without rewriting history. Current validation belongs to the exact documentation head reported on the PR, separately from the historical branch ledgers below.

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

All 41 fields declared under records[].fields in schema/fallout4/fields.json at the historical inventory baseline are listed below. This grades the generic descriptor path only: plugin/mutation.cpp:set_record_scalar and set_record_formlink_field require existing storage. The separate typed XSCL optional insertion/removal remains credited in REFR writeback above. CREATE is record-level, not a descriptor operation.

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
| #426 / #384 | Required managed CI | Infrastructure | Native/C ABI/managed checks required; no field promotion | Verified both runners and clean Windows teardown at its head; adopted into feature branches (ledger below); not yet adopted: #392, #419 |
| #427 / #425 | Existing CLMT timing edits | Partial C++/C/C# | Existing WRITE tested; TNAM ADD/REMOVE and CLMT CREATE absent | See bounded writer ledger below; no runtime weather reproduction |
| #428 / #342 | Existing GRAS/TREE/FLOR fixed fields | Partial C++/C/C# | Existing WRITE tested; ADD/REMOVE/CREATE, PFIG/SNAM link writes and LTEX GNAM list edits absent | See vegetation writer ledger below |

### Managed CI adoption ledger

On 2026-10-05, #426 was merged into each branch below. Local MSVC Release full ctest with the managed suite passed before each push, and exact-head CI passed both native jobs including managed CTest. The reruns marked * replaced load-induced PluginEditTool host test timeouts (588) and a hung Windows `nlohmann_json` FetchContent clone (597) without code changes.

| PR | Head | CI run | Local registered / failed | Managed |
| --- | --- | --- | --- | --- |
| #408 | `cc99bee6` | [587](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/actions/runs/587) | 796 / 0 | 74/74 |
| #406 | `af38be4b` | [588](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/actions/runs/588)* | 795 / 0 | 69/69 |
| #404 | `890158cc` | [589](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/actions/runs/589) | 794 / 0 | 72/72 |
| #402 | `06f0478f` | [590](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/actions/runs/590) | 795 / 0 | 69/69 |
| #400 | `bfe2e23a` | [591](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/actions/runs/591) | 796 / 0 | 70/70 |
| #398 | `b5c9c7bb` | [592](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/actions/runs/592) | 796 / 0 | 70/70 |
| #427 | `40ec4a5f` | [593](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/actions/runs/593) | 800 / 0 | 75/75 |
| #396 | `25ea41cd` | [594](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/actions/runs/594) | 800 / 0 | 66/66 |
| #409 | `56c44508` | [595](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/actions/runs/595) | 789 / 0 | 68/68 |
| #410 | `de899a0b` | [596](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/actions/runs/596) | 787 / 0 | 65/65 |
| #411 | `15e53952` | [597](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/actions/runs/597)* | 787 / 0 | 65/65 |
| #412 | `f45ca86c` | [598](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/actions/runs/598) | 789 / 0 | 65/65 |
| #394 | `0780437b` | [599](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/actions/runs/599) | 791 / 0 | 65/65 |

#410, #411, #412 and #394 were based on the older main `d71b097d`. Their merges also bring main `cca4b080`; code-note conflicts were resolved as unions with no wording changes. #408 also braces one conditional test assertion that GCC rejected in run 586. #392 changes PluginEditTool host code that the local Radicle build does not cover, and #419 is itself a CI-infrastructure PR, so neither is adopted yet.

The official-corpus run for the package reader was at its pre-merge head `557718ef` (785 of 796 passed, 11 opt-in corpus skips, 0 failed, including the seven official-master package cases).

Provisional ABI versions in parallel parser branches are not a merge plan. Main was ABI 8 at this historical baseline. Before any approved merge, merge current main in dependency order without rewriting published history, allocate the next actual version from then-current main, regenerate C# bindings and layout probes, and rerun exact-head CI.

### Historical native writer branch delivery (2026-10-07)

This replaced the earlier pending CLMT/vegetation writer snapshots on 2026-10-07. Coverage below belongs only to the named historical PR heads and CI runs; it does not establish current-head readiness. READ and WRITE refer to the supported stored fields in each tranche, not every xEdit field. Each operation preserves unedited/unknown data or refuses unsupported mutation. Public writer paths have native, C ABI and managed save/reparse proof at those heads. The current #428 integration is blocked on overlapping LTEX Grass/Grasses FormLink ownership; its previous green run does not resolve that decision. Current audit results and consumer/runtime gaps are recorded on each PR.

| Tranche | READ | WRITE | ADD | REMOVE | CREATE | REMAP | OPAQUE / refusal boundary |
|---|---|---|---|---|---|---|---|
| #428 GRAS/TREE/FLOR/LTEX | Typed reader models | Known scalar, harvest-link and grass-list fields | Proven defaults; LTEX grass-list insert | Safe optional subrecords/list elements | GRAS/TREE/LTEX; FLOR only non-localized | Known links/lists | Model/destruction groups, VMAD, unsupported shapes and unedited bytes |
| #427 CLMT/WTHR | Typed modern/legacy variants | Known timing, textures, colours, speeds, parameters and links | Safe defaulted subrecords | Safe optional subrecords | CLMT; WTHR refused | Known links | Legacy table variants without write descriptors, pads, coupled counts/version tables |
| #396 WATR | Typed water fields | Registered scalars, strings and links | Proven defaults | Safe optional subrecords | WATR required defaults | Known links | DATA/GNAM/pads; prior nonstandard 189-199-byte DNAM reader/writer silt gap remains documented |
| #400 SNDR/SOUN/MUSC/FSTP/FSTS | Typed sound families | Known fields, discriminated BNAM, existing rate/footstep entries | Safe defaults and uncounted sounds | Safe optional/uncounted subrecords | SNDR/MUSC/FSTP; SOUN/FSTS refused | Known links | Kind/count/marker structure, unresolved defaults, unknown rate-group order |
| #406 DIAL/INFO/SCEN | Typed dialogue/scene models | Fixed fields, complete INFO responses, typed scene phase/actor/action headers | Safe DIAL/sparse INFO defaults; scene groups refused | Safe optional fields; grouped members refused | Refused: required quest/topic containment | Known links | Derived counts, sparse response members, action payloads, unsupported scoped layouts |
| #408 PACK/CSTY/IDLE | Corrected signed/union reader models | Known fixed/variant fields; stable scoped package fields | Safe known/defaulted layouts; package structural edits refused | Safe optional fields; required/coupled fields refused | CSTY/IDLE; PACK refused | Known variants; unknown package unions refuse | Counters/markers, CSMD, optional absent tails and unknown data |
| #402 NAVM/NAVI | Typed geometry and info views | Existing vertex/triangle/door entries; fixed info prefix/links | Refused | Refused | Refused | Checked existing mixed-link grammar | Traversal bytes, unnamed flags, cover/grid/island/waypoint tails; no derived navigation rebuild |
| #409 VMAD | Leading scripts/scalars, array/struct counts; existing quest views | Known leading property types 1-5 and existing array elements 11-15 | Refused for script/property/array structure | Refused for script/property/array structure | Refused for VMAD structures | Complete grammar, including empty type 16 | Type 6/nonempty 16, recursive struct/fragment/alias-script authoring, future versions/formats and unknown tails |
| #432 W0 | Registered descriptors/counts | Shared scalar/string/link setters | Proven schema defaults in known order | Cardinality/coupled-safe subrecords | Complete required defaults at supported top level only | Reuses existing remapper | Localized text without sidecar ownership, coupled structures and unregistered/unknown layouts |

The shared #432 follow-up carries the byte-identical dialogue quest/topic placement taxonomy, raw-value link comparison and actual localized-flag preflight, plus sound's explicit `when.match_absent` handling. Generic creation refuses non-top-level placement even with explicit rules. Missing-discriminator matching does not accept malformed or duplicate discriminator data. The shared infrastructure itself adds no record registrations or public C ABI exports.

| PR | Historical writer head | Historical provisional ABI | Historical exact-head CI |
|---|---|---:|---|
| [#428](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/pulls/428) | `634f4ee8293eae3a912d05b98e3b13fd67d1ceba` | 17 | [run 609](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/actions/runs/609) success |
| [#427](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/pulls/427) | `21edf3e502152aad7112b3b30ee6cda63f482d3e` | 18 | [run 610](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/actions/runs/610) success |
| [#396](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/pulls/396) | `5112c3a3f88024bda9afc242b24da71fb759c9f0` | 19 | [run 611](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/actions/runs/611) success |
| [#400](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/pulls/400) | `697d62216e54fa7e25dc39af7398dfe8c843d662` | 20 | [run 613](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/actions/runs/613) success |
| [#406](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/pulls/406) | `e1c99ccb3d8087daf71b02016f6029b69528f2a5` | 21 | [run 614](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/actions/runs/614) success |
| [#408](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/pulls/408) | `3c53c8630cf23636232acc6823cb6a23771ba154` | 22 | [run 615](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/actions/runs/615) success |
| [#402](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/pulls/402) | `a867493c41365feddc5f3d92274566acda4da292` | 23 | [run 616](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/actions/runs/616) success |
| [#409](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/pulls/409) | `b2bc9579855152e17419560a0447744e6f6b8579` | 24 | [run 617](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/actions/runs/617) success |
| [#432](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/pulls/432) | `0e9f30ac5a7578056a19aa6f2c13a582c72ed996` | 10 | [run 618](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/actions/runs/618) success |

Historical local final validation for those tranches (registered CTest checks include skips and the managed-suite entry):

| PR | Registered checks, zero failures | Official passes / skips | Managed passes | Proof in the single task worktree |
|---|---:|---:|---:|---|
| #406 | 884 | 872 / 12 | 81 | `build/evidence/dialogue-full-review-fixes.log`, `dialogue-official-review-fixes.log`, `dialogue-push-guard.log` |
| #408 | 874 | 863 / 11 | 86 | `build/evidence/packages-final-full.log`, `packages-final-official-full.log`, `packages-push-guard.log` |
| #402 | 865 | 854 / 11 | 81 | `build/evidence/navmesh-final-full.log`, `navmesh-final-official-full.log`, `navmesh-push-guard.log` |
| #409 | 857 | 846 / 11 | 80 | `build/evidence/vmad-final-full.log`, `vmad-official-full.log`, `vmad-push-guard.log` |
| #432 | 843 | 832 / 11 | 75 | `build/evidence/w0-port-full.log`, `w0-port-official-full.log`, `w0-port-push-guard.log` |

Official data was read-only through `FO4_DATA_DIR`. The tranche tests inspect official order/grammar and cap mutation work, then save/reparse and compare all untouched body records. The last four full MSVC warning-as-error builds and full suites passed at the historical heads above; independent read-only reviews were completed and findings applied. The registered check count is not a runtime-acceptance count. Local corpus proof and runner CI are separate evidence. Existing optional third-party/archive/compiler corpora remain skipped where unavailable.

`build/evidence/native-backend-progress.json` retains the historical branch heads, validation, relative proof paths and integration decision. Evidence logs are local ignored artifacts from the original task checkout. CI links and committed regression tests provide the remote reproducible checks; current PR descriptions report subsequent validation.

Before any future approved integration, renumber provisional ABIs in dependency merge order against then-current main, regenerate bindings/probes, resolve schema unions and placement-enum extensions, then rerun exact-head CI. No merge is authorized by this delivery. The VMAD variable-layout follow-up still relies on the existing verified private evidence recorded on #409 in `Radicle/docs/VMAD_VARIABLE_EVIDENCE.md`; no nonempty variable or runtime-binding behavior is inferred.

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
| #425 | CLMT stored editing | Current #427 branch proves known timing and schema-safe ADD/REMOVE/CREATE; pending integration |
| #342 | Vegetation stored editing | Current #428 branch proves known fields/links, LTEX list edits and schema-safe authoring; pending integration |
| #32 | Localization editor consumer | Stored ID/inline classification is established; explicit sidecar ownership and transactional edits still need proof |
| #31 | Native diagnostics consumer | Expand VALIDATE coverage using proven invariants; no new Mutagen dependency |
| #35, #51 | Specialized writer/validator consumers | Precombine stamping needs generated-data/layout proof; stored dialogue-link validation is separate from runtime merge order |
| #338 | Stale roadmap audit | Reconcile product/evidence text without changing unrelated tool roadmaps |
| #346 | Shared plugin/archive architecture | No redesign prerequisite for proving current editing contracts |

The refreshed 2026-10-05 inventory contains 85 open repository issues (both API pages inspected) and 19 open PRs. The rows above cover all explicitly Radicle-tagged/titled issues, the governing product/schema/parity/CI trackers and direct writer/validator consumers. All 19 PRs appear in the parser/infrastructure ledger, including the independent runner tooling. Other PluginTool workflows and unrelated BGS/F4LODGEN issues remain outside this backend tranche. This is a scope/disposition audit; each unexamined field remains U and needs its own implementation and acceptance audit.

## Ordered follow-ups

1. Reconcile the provisional ABI series and schema unions only when integration is authorized; merge current main without rewriting published history, regenerate bindings/layout probes and rerun exact-head validation.
2. Continue field-by-field xEdit/helper audits outside the named writer scopes. Unexamined U entries remain pending.
3. Keep variable VMAD layouts, navigation derivation and scope-specific grouped authoring refused until their ownership and safe mutation are proven.
4. Complete independent runtime/consumer acceptance in the owning projects; stored edit/reparse and CI do not establish game execution behavior.
5. Integrate PluginTool/Garden consumers after their required backend contracts and relevant runtime evidence are established.

A complete field-by-field xEdit helper expansion, all typed members and all grouped add/remove/create operations remain pending. These branch deliveries do not establish full FO4Edit parity or close #287.
