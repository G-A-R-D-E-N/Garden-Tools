# 24 — TesForms / World Source-File → Class Map

This is the file→class index for the **forms module** recovered from the `__FILE__`
strings the CK embeds for its `assert` / `BS_ASSERT` / log macros. Every path is rooted at
the CI checkout:

```
E:\BuildAgent\work\4a2aa37ca4b2ffa3\scripts\_workspace\code\
```

Raw inventory: `ghidra-dump/srcpaths.txt` (engine, 1,116 paths) and
`ghidra-dump/ck-src-files.txt` (`code\construction set\`, the CK fork).

## Module map — where the forms code actually lives

| Module | Files | What it is |
|---|---|---|
| `shared\formcomponents\` | 34 | **base form machinery** — `tesform.cpp`, `tesfullname.cpp`, `tesmodel.cpp`, the `TES*` mixins |
| `shared\tesforms\world\` | 33 | **CELL / WRLD / REFR / LAND / weather / water** — the world module |
| `shared\tesforms\character\` | 37 | NPC / race / quest / dialogue / scene |
| `shared\tesforms\gameplay\` | 41 | GMST / GLOB / sound / material / list |
| `shared\tesforms\objects\` | 47 | the `TESObject*` physical-object family |
| `construction set\tesforms\` | 43 | **editor-only forks** (`*_editor.cpp`) of the runtime forms |
| `shared\water\` | 4 | runtime water surface system (not forms) |
| `shared\distantterrain\` | 17 | LOD / terrain chunk builders (feeds the LODGen work) |
| `shared\extradata\` | 5 | `TESObjectREFR` extra-data list |
| `shared\events\` | 4 | story-event dispatch |

Key structural finding up front: **`TESForm` itself lives in `shared\formcomponents\`, not
`shared\tesforms\`.** `tesforms\` only holds the concrete form subclasses. The save/load
helpers we decompiled earlier (`FUN_1408c7a70` / `FUN_1408c7710` / `FUN_1408c7bf0`,
anchored at `tesform.cpp:0x9a4 / 0x9e0`) are therefore in *formcomponents*, which is why
the `SaveRecord` asserts anchored to `tesform.h` rather than a class `.cpp`.

---

## §1 `shared\tesforms\world\` — the world module (33 files)

| File | Class | 4CC | Notes |
|---|---|---|---|
| `tesobjectcell.cpp` | `TESObjectCELL` | CELL | main class: ctor, flags, cell-data block (doc 23 §8) |
| `tesobjectcell_reference.cpp` | `TESObjectCELL` | CELL | reference-side partial (cell ↔ REFR wiring) |
| `tesobjectrefr.cpp` | `TESObjectREFR` | REFR | main reference class |
| `tesobjectrefr_animation.cpp` | `TESObjectREFR` | REFR | animation partial |
| `tesobjectrefr_events.cpp` | `TESObjectREFR` | REFR | event partial |
| `tesobjectrefr_extradata.cpp` | `TESObjectREFR` | REFR | extra-data partial (pairs with `shared\extradata\`) |
| `tesworldspace.cpp` | `TESWorldSpace` | WRLD | |
| `tesland.cpp` | `TESLand` | LAND | |
| `teslandtexture.cpp` | `TESLandTexture` | LTEX | |
| `tesclimate.cpp` | `TESClimate` | CLMT | |
| `tesweather.cpp` / `tesweather.h` | `TESWeather` | WTHR | |
| `tesimagespace.cpp` | `TESImageSpace` | IMGS | |
| `tesimagespacemodifier.cpp` | `TESImageSpaceModifier` | IMAD | |
| `teswaterform.cpp` | `TESWaterForm` | WATR | RTTI-verified; see the editor-fork gap in §6 |
| `bgsencounterzone.cpp` | `BGSEncounterZone` | ECZN | |
| `bgslocation.cpp` | `BGSLocation` | LCTN | |
| `bgslocation_editor.cpp` | `BGSLocation` | LCTN | **editor fork lives in `shared`, not the CK dir** |
| `bgsgodrays.cpp` | `BGSGodRays` | GDRY | |
| `bgslayer.cpp` | `BGSLayer` | LAYR | |
| `bgslightingtemplate.cpp` | `BGSLightingTemplate` | LGTM | |
| `bgsreferenceeffect.cpp` | `BGSReferenceEffect` | RFCT | |
| `bgsreferencegroup.cpp` | `BGSReferenceGroup` | — | reference-group form |
| `bgsshaderparticlegeometrydata.cpp` / `.h` | `BGSShaderParticleGeometryData` | SPGD | RTTI-verified |
| `bgscombinedcellgeometrydb.cpp` | `BGSCombinedCellGeometryDB` | — | PreCombined cell-geometry DB (not a form) |
| `bgslargerefdata.cpp` | `LargeReferenceData` | — | large-reference streaming payload (not a form) |
| `bgsloadedreferencecollection.cpp` | `BGSLoadedReferenceCollection` | — | loaded-cell ref tracking (not a form) |
| `bgsworldlocation.cpp` / `.h` | `BGSWorldLocation` | — | worldspace↔cell location helper (not a form) |
| `exteriorcellloader.cpp` | *(CellLoaderTask / cell-grid machinery)* | — | streaming loader — doc 23 §9 |
| `imagespacemodifierinstance.cpp` | `ImageSpaceModifierInstance` | — | runtime IMAD instance (not a form) |
| `maxheightdb.cpp` | `MaxHeightDB` | — | LOD max-height DB (not a form) |

**Non-form members of the world module** are the ones worth flagging for the streaming /
LOD work: `exteriorcellloader.cpp` (CellLoaderTask, "Constructed %u cell grids"),
`bgscombinedcellgeometrydb.cpp` (PreCombined), `maxheightdb.cpp` (LOD height ceiling),
`bgslargerefdata.cpp` (large-ref data the CELL walk feeds).

---

## §2 `shared\tesforms\character\` (37 files)

| File | Class | Notes |
|---|---|---|
| `tesnpc.cpp` | `TESNPC` | NPC_ |
| `tesrace.cpp` | `TESRace` | RACE |
| `tesclass.cpp` | `TESClass` | CLAS |
| `tesfaction.cpp` | `TESFaction` | FACT |
| `tesquest.cpp` | `TESQuest` | QUST |
| `testopic.cpp` | `TESTopic` | DIAL |
| `testopicinfo.cpp` / `.h` | `TESTopicInfo` | INFO |
| `tesidleform.cpp` | `TESIdleForm` | IDLE |
| `teseyes.cpp` | `TESEyes` | EYES |
| `bgsperk.cpp` | `BGSPerk` | PERK |
| `bgskeyword.cpp` | `BGSKeyword` | KYWD |
| `bgsmovementtype.cpp` | `BGSMovementType` | MOVT |
| `bgsequipslot.cpp` | `BGSEquipSlot` | EQUP |
| `bgsheadpart.cpp` | `BGSHeadPart` | HDPT |
| `bgsidlemarker.cpp` | `BGSIdleMarker` | IDLM |
| `bgsnote.cpp` | `BGSNote` | NOTE |
| `bgsvoicetype.cpp` | `BGSVoiceType` | VTYP |
| `bgsrelationship.cpp` | `BGSRelationship` | RELA |
| `bgsassociationtype.cpp` | `BGSAssociationType` | ASTP |
| `bgsattractionrule.cpp` | `BGSAttractionRule` | — |
| `bgsattractionrule_editor.cpp` | `BGSAttractionRule` | **editor fork in `shared`** |
| `bgsscene.cpp` | `BGSScene` | SCEN |
| `bgsscenecollection.cpp` | `BGSSceneCollection` | SCCO |
| `bgsbodypartdata.cpp` | `BGSBodyPartData` | BPTD |
| `bgscharactertint.cpp` | `BGSCharacterTint` | tint struct, not a form |
| `bgsdialoguebranch.cpp` | `BGSDialogueBranch` | dialogue struct |
| `bgsdialogueview.cpp` | `BGSDialogueView` | dialogue UI data |
| `bgsentrypoint.cpp` | `BGSEntryPoint` | entry-point struct |
| `bgsentrypointfunction.cpp` | `BGSEntryPointFunction` | entry-point fn data |
| `bgsrefalias.cpp` | `BGSRefAlias` | quest alias data |
| `bgssceneaction.cpp` | `BGSSceneAction` | scene-action base |
| `bgssceneactionstartscene.cpp` | `BGSSceneActionStartScene` | |
| `bgssceneactiontimer.cpp` | `BGSSceneActionTimer` | |
| `bgsscenephase.cpp` | `BGSScenePhase` | scene-phase data |
| `bgstopicsubtypeunion.cpp` | `BGSTopicSubtypeUnion` | topic-info subtype union |
| `creaturesounds.cpp` | `CreatureSounds` | creature sound data struct |

---

## §3 `shared\tesforms\gameplay\` (41 files)

| File | Class | Notes |
|---|---|---|
| `tesgamesettings.cpp` | `TESGameSetting` | GMST + the ~5,100-row game-value Setting collection |
| `tesglobal.cpp` | `TESGlobal` | GLOB |
| `tesloadscreen.cpp` | `TESLoadScreen` | LSCR |
| `tesscript.cpp` | `Script` | SCPT (Papyrus VM metadata) |
| `tesshout.cpp` | `TESShout` | SHOU |
| `tessound.cpp` | `TESSound` | SOUN |
| `teswordofpower.cpp` | `TESWordOfPower` | WOOP |
| `bgslistform.cpp` | `BGSListForm` | FLST |
| `bgsmaterialobject.cpp` | `BGSMaterialObject` | MATO |
| `bgsmaterialtype.cpp` | `BGSMaterialType` | MATT |
| `bgsmaterialswap.cpp` | `BGSMaterialSwap` | MSWP |
| `bgstextureset.cpp` | `BGSTextureSet` | TXST |
| `bgsmessage.cpp` | `BGSMessage` | MESG |
| `bgsmusictype.cpp` | `BGSMusicType` | MUST |
| `bgsmusictrackformwrapper.cpp` | `BGSMusicTrackFormWrapper` | MUSC |
| `bgsreverbparameters.cpp` | `BGSReverbParameters` | REVB |
| `bgssoundcategory.cpp` | `BGSSoundCategory` | SNCT |
| `bgssoundcategorysnapshot.cpp` | `BGSSoundCategorySnapshot` | — |
| `bgssounddescriptorform.cpp` | `BGSSoundDescriptorForm` | SNDR |
| `bgssoundkeywordmapping.cpp` | `BGSSoundKeywordMapping` | — |
| `bgssoundoutput.cpp` / `.h` | `BGSSoundOutput` | SOPM |
| `bgssoundtagset.cpp` | `BGSSoundTagSet` | — |
| `bgsaudioeffectchain.cpp` | `BGSAudioEffectChain` | — |
| `bgsautoweaponsounddef.cpp` | `BGSAutoWeaponSoundDef` | — |
| `bgsacousticspace.cpp` | `BGSAcousticSpace` | ASPC |
| `bgscamerapath.cpp` | `BGSCameraPath` | CPTH |
| `bgscamerashot.cpp` | `BGSCameraShot` | CAMS |
| `bgscollisionlayer.cpp` | `BGSCollisionLayer` | COLL |
| `bgscolorform.cpp` | `BGSColorForm` | CLFM |
| `bgsdamagetype.cpp` | `BGSDamageType` | DMGT |
| `bgsdefaultobject.cpp` | `BGSDefaultObject` | DLOB |
| `bgsfootstep.cpp` | `BGSFootstep` | footstep data |
| `bgsfootstepset.cpp` | `BGSFootstepSet` | FSTS |
| `bgsimpactdata.cpp` | `BGSImpactData` | IPCT |
| `bgsimpactdataset.cpp` | `BGSImpactDataSet` | IPDS |
| `bgsinstancenamingrules.cpp` | `BGSInstanceNamingRules` | naming-rules data |
| `bgsmenuicon.cpp` | `BGSMenuIcon` | ICON helper |
| `bgstransform.cpp` | `BGSTransform` | transform data |
| `bgscomponent.cpp` | `BGSComponent` | constructible component (not a form) |
| `bgsdefaultobjectmanager.cpp` | `BGSDefaultObjectManager` | manager (not a form) |

---

## §4 `shared\tesforms\objects\` (47 files)

| File | Class | Notes |
|---|---|---|
| `tesobject.cpp` | `TESObject` | **base class**, no 4CC |
| `tesobjectacti.cpp` | `TESObjectACTI` | ACTI |
| `tesobjectanio.cpp` | `TESObjectANIO` | ANIO |
| `tesobjectarma.cpp` | `TESObjectARMA` | ARMA |
| `tesobjectarmo.cpp` | `TESObjectARMO` | ARMO |
| `tesobjectbook.cpp` | `TESObjectBOOK` | BOOK |
| `tesobjectcont.cpp` | `TESObjectCONT` | CONT |
| `tesobjectdoor.cpp` | `TESObjectDOOR` | DOOR |
| `tesobjectmisc.cpp` | `TESObjectMISC` | MISC |
| `tesobjectstat.cpp` | `TESObjectSTAT` | STAT |
| `tesobjecttree.cpp` | `TESObjectTREE` | TREE |
| `tesobjectweap.cpp` | `TESObjectWEAP` | WEAP |
| `tesobjectweapbridge.cxx` | `TESObjectWEAP` | cross-lib bridge |
| `teslight.cpp` | `TESObjectLIGH` | LIGH (RTTI-verified) |
| `tesfurniture.cpp` | `TESFurniture` | FURN |
| `tesgrass.cpp` | `TESGrass` | GRAS |
| `tesammo.cpp` | `TESAmmo` | AMMO |
| `tescombatstyle.cpp` | `TESCombatStyle` | CSTY |
| `teseffectshader.cpp` / `.h` | `TESEffectShader` | EFSH |
| `teslevcharacter.cpp` | `TESLevCharacter` | LVLN |
| `teslevitem.cpp` | `TESLevItem` | LVLI |
| `teslevspell.cpp` | `TESLevSpell` | LVSP |
| `tessoulgem.h` | `TESSoulGem` | SLGM (header only — no `.cpp` in the dump) |
| `bgsaddonnode.cpp` | `BGSAddonNode` | ADDN |
| `bgsaimmodel.cpp` | `BGSAimModel` | — |
| `bgsartobject.cpp` | `BGSArtObject` | ARTO |
| `bgsbendablespline.cpp` | `BGSBendableSpline` | — |
| `bgsconstructibleobject.cpp` | `BGSConstructibleObject` | COBJ |
| `bgsdebris.cpp` | `BGSDebris` | DEBR |
| `bgsexplosion.cpp` | `BGSExplosion` | EXPL |
| `bgshazard.cpp` | `BGSHazard` | HAZD |
| `bgslensflare.cpp` | `BGSLensFlare` | LENS |
| `bgsmovablestatic.cpp` | `BGSMovableStatic` | MSTT |
| `bgsoutfit.cpp` | `BGSOutfit` | OTFT |
| `bgspackin.cpp` | `BGSPackIn` | PKIN |
| `bgsprojectile.cpp` | `BGSProjectile` | PROJ |
| `bgsstaticcollection.cpp` | `BGSStaticCollection` | SCOL |
| `bgsterminal.cpp` / `.h` | `BGSTerminal` | TERM |
| `bgszoomdata.cpp` | `BGSZoomData` | ZOOM |
| `bgsformstreamer.cpp` | `BGSFormStreamer` | streaming helper (not a form) |
| `bgsmod_bridge.h` / `bgsmod_object.cpp` / `.h` | `BGSMod::*` | mod/attach system |
| `bgsmod_property.cpp` / `.h` | `BGSMod::Property` | |

---

## §5 `shared\formcomponents\` — base forms & mixins (34 files)

| File | Class | Notes |
|---|---|---|
| `tesform.cpp` / `.h` | `TESForm` | **the form base class** — doc 23 §10 save/load helpers live here |
| `tesfullname.cpp` / `.h` | `TESFullName` | FULL mixin |
| `tesmodel.cpp` | `TESModel` | MODL mixin |
| `tesmodeltri.cpp` | `TESModelTri` | MODT block (model data) |
| `tescontainer.cpp` | `TESContainer` | CNTO mixin |
| `tesdescription.cpp` | `TESDescription` | DESC mixin |
| `testexture.cpp` / `.h` | `TESTexture` | icon/texture mixin |
| `tesleveledlist.cpp` | `TESLeveledList` | LVLO mixin (base of the TESLev* forms) |
| `tesleveledlist_editor.cpp` | `TESLeveledList` | editor fork in `shared` |
| `tesreactionform.cpp` | `TESReactionForm` | reaction mixin |
| `tesactorbasedata.cpp` | `TESActorBaseData` | NPC/creature common data mixin |
| `tesfilecontainer.cpp` | `TESFileContainer` | file-container data |
| `bgsdestructibleobjectform.cpp` | `BGSDestructibleObjectForm` | DEST |
| `bgsopencloseform.cpp` | `BGSOpenCloseForm` | open/close mixin |
| `bgskeywordform.cpp` | `BGSKeywordForm` | keyword-holder mixin |
| `bgskeywordform_editor.cpp` | `BGSKeywordForm` | editor fork in `shared` |
| `bgstexturemodel.cpp` | `BGSTextureModel` | texture-model mixin |
| `bgsmodelmaterialswap.cpp` | `BGSModelMaterialSwap` | model material swap |
| `bgsattachparentarray.cpp` | `BGSAttachParentArray` | attach-parent data |
| `bgsbehaviorgraphmodel.cpp` | `BGSBehaviorGraphModel` | behavior-graph data |
| `bgsequiptype.cpp` | `BGSEquipType` | equip-type data |
| `bgsidlecollection.cpp` | `BGSIdleCollection` | idle-collection data |
| `bgsmenudisplayobject.cpp` | `BGSMenuDisplayObject` | menu display object |
| `bgsoverridepackcollection.cpp` | `BGSOverridePackCollection` | override-pack data |
| `bgsperkrankarray.cpp` | `BGSPerkRankArray` | perk-rank data |
| `bgsperkrankarray_editor.cpp` | `BGSPerkRankArray` | editor fork in `shared` |
| `bgsmod_container.cpp` / `bgsmod_filter.*` / `bgsmod_template.*` | `BGSMod::*` | mod system |

---

## §6 Editor forks — the `*_editor.cpp` cross-reference

The CK's editor-side classes are **not** all in one folder. They split across three trees:

1. **`construction set\tesforms\`** (43 files) — the bulk.
2. **`shared\tesforms\{character,world}\`** — `bgsattractionrule_editor.cpp`, `bgslocation_editor.cpp`.
3. **`shared\formcomponents\`** — `bgskeywordform_editor.cpp`, `bgsperkrankarray_editor.cpp`, `tesleveledlist_editor.cpp`.

### `construction set\tesforms\` fork → runtime class

| Editor fork | Runtime class | Runtime file |
|---|---|---|
| `tesobjectcell_editor.cpp` | `TESObjectCELL` | `shared\tesforms\world\tesobjectcell.cpp` |
| `tesobjectrefr_editor.cpp` | `TESObjectREFR` | `shared\tesforms\world\tesobjectrefr*.cpp` |
| `tesworldspace_editor.cpp` | `TESWorldSpace` | `shared\tesforms\world\tesworldspace.cpp` |
| `tesland_editor.cpp` | `TESLand` | `shared\tesforms\world\tesland.cpp` |
| `teslandtexture_editor.cpp` | `TESLandTexture` | `shared\tesforms\world\teslandtexture.cpp` |
| `tesimagespacemodifier_editor.cpp` | `TESImageSpaceModifier` | `shared\tesforms\world\tesimagespacemodifier.cpp` |
| `tesnpc_editor.cpp` | `TESNPC` | `shared\tesforms\character\tesnpc.cpp` |
| `tesquest_editor.cpp` | `TESQuest` | `shared\tesforms\character\tesquest.cpp` |
| `testopic_editor.cpp` | `TESTopic` | `shared\tesforms\character\testopic.cpp` |
| `testopicinfo_editor.cpp` | `TESTopicInfo` | `shared\tesforms\character\testopicinfo.cpp` |
| `tesfurniture_editor.cpp` | `TESFurniture` | `shared\tesforms\objects\tesfurniture.cpp` |
| `bgsassociationtype_editor.cpp` | `BGSAssociationType` | `shared\tesforms\character\bgsassociationtype.cpp` |
| `bgsbodypart_editor.cpp` | `BGSBodyPartData` | `shared\tesforms\character\bgsbodypartdata.cpp` |
| `bgsequipslot_editor.cpp` | `BGSEquipSlot` | `shared\tesforms\character\bgsequipslot.cpp` |
| `bgskeyword_editor.cpp` | `BGSKeyword` | `shared\tesforms\character\bgskeyword.cpp` |
| `bgsmovementtype_editor.cpp` | `BGSMovementType` | `shared\tesforms\character\bgsmovementtype.cpp` |
| `bgsnote_editor.cpp` | `BGSNote` | `shared\tesforms\character\bgsnote.cpp` |
| `bgsoutfit_editor.cpp` | `BGSOutfit` | `shared\tesforms\objects\bgsoutfit.cpp` |
| `bgsrelationship_editor.cpp` | `BGSRelationship` | `shared\tesforms\character\bgsrelationship.cpp` |
| `bgsscene_editor.cpp` | `BGSScene` | `shared\tesforms\character\bgsscene.cpp` |
| `bgsaudioeffectchain_editor.cpp` | `BGSAudioEffectChain` | `shared\tesforms\gameplay\bgsaudioeffectchain.cpp` |
| `bgsautoweaponsounddef_editor.cpp` | `BGSAutoWeaponSoundDef` | `shared\tesforms\gameplay\bgsautoweaponsounddef.cpp` |
| `bgsimpactdata_editor.cpp` | `BGSImpactData` | `shared\tesforms\gameplay\bgsimpactdata.cpp` |
| `bgsimpactdataset_editor.cpp` | `BGSImpactDataSet` | `shared\tesforms\gameplay\bgsimpactdataset.cpp` |
| `bgslensflare_editor.cpp` | `BGSLensFlare` | `shared\tesforms\objects\bgslensflare.cpp` |
| `bgslistform_editor.cpp` | `BGSListForm` | `shared\tesforms\gameplay\bgslistform.cpp` |
| `bgsmaterialobject_editor.cpp` | `BGSMaterialObject` | `shared\tesforms\gameplay\bgsmaterialobject.cpp` |
| `bgsmaterialtype_editor.cpp` | `BGSMaterialType` | `shared\tesforms\gameplay\bgsmaterialtype.cpp` |
| `bgsmusictype_editor.cpp` | `BGSMusicType` | `shared\tesforms\gameplay\bgsmusictype.cpp` |
| `bgsmusictrackformwrapper_editor.cpp` | `BGSMusicTrackFormWrapper` | `shared\tesforms\gameplay\bgsmusictrackformwrapper.cpp` |
| `bgssoundcategory_editor.cpp` | `BGSSoundCategory` | `shared\tesforms\gameplay\bgssoundcategory.cpp` |
| `bgssounddescriptor_editor.cpp` | `BGSSoundDescriptor` | *(base — no runtime file in dump)* |
| `bgssounddescriptorform_editor.cpp` | `BGSSoundDescriptorForm` | `shared\tesforms\gameplay\bgssounddescriptorform.cpp` |
| `bgssoundoutput_editor.cpp` | `BGSSoundOutput` | `shared\tesforms\gameplay\bgssoundoutput.cpp` |
| `bgsstaticcollection_editor.cpp` | `BGSStaticCollection` | `shared\tesforms\objects\bgsstaticcollection.cpp` |
| `bgstextureset_editor.cpp` | `BGSTextureSet` | `shared\tesforms\gameplay\bgstextureset.cpp` |
| `bgszoomdata_editor.cpp` | `BGSZoomData` | `shared\tesforms\objects\bgszoomdata.cpp` |

**Forks with no matching runtime file in the `__FILE__` dump** (their runtime `.cpp`
simply emits no `assert`/log with a file path, or lives in another module):

- `bgscompoundsounddef_editor.cpp` → `BGSCompoundSoundDef`
- `bgsstandardsounddef_editor.cpp` → `BGSStandardSoundDef`
- `bgslensflaresprite_editor.cpp` → `BGSLensFlareSprite`
- `bgsmusicpalettetrack_editor.cpp` → `BGSMusicPaletteTrack`
- `bgsmusicsilencetrack_editor.cpp` → `BGSMusicSilenceTrack`
- `bgsmusicsingletrack_editor.cpp` → `BGSMusicSingleTrack`

**Gap worth knowing:** `TESWaterFormEditor` is present in the RTTI
(`.?AVWaterPropertiesTab@TESWaterFormEditor@@`, etc.) but there is **no
`teswaterform_editor.cpp`** in either dump — the water-form editor class is defined in a
file that never emits a `__FILE__` string, so this list is a *lower bound* on the editor
fork surface.

---

## §7 Adjacent world-coupled modules

These are not in `tesforms\` but are load-bearing for the CELL/WRLD/LOD work.

### `shared\water\` (4 files) — runtime water surface

| File | Class | Notes |
|---|---|---|
| `teswater.cpp` | `TESWater` (runtime surface, not a form) | water geometry/system |
| `bgsautowater.cpp` | `BGSAutoWater` | auto-generated cell water planes |
| `bgswatercollisionmanager.cpp` | `BGSWaterCollisionManager` | water collision |
| `bgswateraudio.cpp` | `BGSWaterAudio` | water audio |

Directly relevant to the open "WaterDelta=0 / water node geometry" questions.

### `shared\distantterrain\` (17 files) — LOD / terrain chunk pipeline

| File | Notes |
|---|---|
| `bgsterrainmanager.cpp` / `bgsterrainupdatemanager.cpp` | terrain orchestration |
| `bgsterrainchunk.cpp` / `bgsterrainnode.*` | chunk/node tree |
| `bgsterraindataprocessing.cpp` | height/normal processing |
| `bgsterraintexturebuilder.cpp` | texture atlas baking |
| `bgsterrainobjecttextureatlasbuilder.cpp` | object texture atlas |
| `bgsterrainobjectblockbuilder.cpp` | object-block (BTO) building |
| `bgsterrainwatermeshbuilder.cpp` | **water mesh building for LOD** |
| `bgsdistantobjectblock.cpp` / `bgsdistanttreeblock.cpp` | object/tree block types |
| `bgsterrainvisibilitydata.cpp` / `_db.cpp` / `_finder.cpp` | previs visibility DB |
| `bgsterrainutils.cpp` | shared helpers |

This is the engine side that pairs with the LODGen `.btr/.bto` output format — the next
target for triangle-level parity.

### `shared\extradata\` (5 files)

| File | Notes |
|---|---|
| `extradatalist.cpp` / `extradataobjects.cpp` | extra-data list/objects (REFR-side) |
| `bgsobjectinstanceextra.cpp` | object-instance extra |
| `containeritemextra.cpp` | container item extra |
| `multiboundmarkerdata.cpp` | multibound marker data |

### `shared\events\` (4 files)

| File | Notes |
|---|---|
| `bgsstoryevent.cpp` / `bgsstoryeventmanager.cpp` | story-event manager |
| `bgsstoryeventautoregister.h` / `bgsstoryeventmember.h` | auto-registration |

---

## Observations

1. **`TESForm` is in `formcomponents`, the concrete forms are in `tesforms`.** The class
   hierarchy mirrors the directory split: base machinery (`tesform.cpp`, `tesfullname.cpp`,
   `tesmodel.cpp`, the `TESContainer`/`TESDescription`/`TESTexture` mixins) vs. concrete
   `TESObject*`/`BGS*` subclasses.
2. **Editor forks are scattered across three directories** — `construction set\tesforms\`
   (43), plus five `*_editor.cpp` files inlined next to their runtime classes in `shared\`.
   The in-shared forks (`bgslocation`, `bgsattractionrule`, `bgskeywordform`,
   `bgsperkrankarray`, `tesleveledlist`) are the ones where the editor class was kept beside
   the runtime one — usually small editor additions rather than a full fork.
3. **The world module is ~⅓ non-form infrastructure** (loader, geometry DB, max-height DB,
   large-ref data) — these are the classes the CELL/WRLD streaming contract actually runs on
   (doc 23 §9), not `TESObjectCELL` itself.
4. **`TESObjectREFR` is split across four `.cpp` files** (`tesobjectrefr.cpp` +
   `_animation` / `_events` / `_extradata`), matching the class's size; `TESObjectCELL` is
   split across two (`tesobjectcell.cpp` + `_reference`).
5. **`__FILE__` is a lower bound.** Files that never hit an `assert`/`BS_ASSERT`/log with a
   path (e.g. `teswaterform_editor.cpp`, the `BGSCompoundSoundDef` runtime) are absent from
   `srcpaths.txt` even though their classes exist in RTTI. Use RTTI (`rtti-classes.txt`) to
   fill those gaps.
