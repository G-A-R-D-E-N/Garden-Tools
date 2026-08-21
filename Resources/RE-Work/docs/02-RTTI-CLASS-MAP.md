# CreationKit.exe — RTTI Class Map

Full MSVC RTTI type names are embedded (`.rdata`), so class identity survives. 4,438 unique
`.?AV…`/`.?AU…` names extracted from the binary — full list in
[`ghidra-dump/rtti-classes.txt`](ghidra-dump/rtti-classes.txt). This doc is the curated map.

## Embedded third-party SDKs (from RTTI + strings)

| SDK | Classes | Notes |
|---|---|---|
| Autodesk FBX SDK | 170 (`fbxsdk_Iex_2_5` …) | FBX import/export; `fbxsdk` namespace families: `fbxsdk_` all modules |
| Alembic | 31 | Alembic geometry import |
| Havok | ~60 (`hkbInternal`, `hclStorageSetupMeshSection`, `hkcdSimdTree`, `hknpShapeViewerImpl`, `hkgpMesh`, `hkXmlParser`) | animation + cloth + physics |
| NVIDIA Flex | `NVFlex` (4) | particle/fluid sim |
| ANTLR / Wintertree | (strings) | script parser + spelling checker |
| Json | 14 (`Json::…`) | JSON (bethesda.net / config) |

## Engine core (shared with the game runtime)

- **BSResource** (24): `CacheDrive`, `CacheDriveSystemUtility`, `ICacheDriveOp`, pathing streams (`MasterFilePathingStreamGetSize`, `MasterFilePathingStreamWriteToBuffer`) — the next-gen Masterfile-aware resource layer
- **BSPlatform** (17), **BSScript** (37) / **CSScript** (48): Papyrus VM + script system
- **BSSystemFileStreamer**, **BSTextureStreamer**, **BSTextureArray**, **BSStaticTriShapeDB**, **BSNodeVisualization**, **BShkbHkxDB** (behavior DB)
- **BSPerforce** (5): the CK's built-in VCS (Perforce-flavored)
- **bnet** (7): bethesda.net client

## Record database — `TES*` (126 classes)

Form/record layer, full FO4 record set. Highlights:

- Base: `TESForm`, `TESObject`, `TESBoundObject`, `TESBoundAnimObject`, `TESObjectREFR` (references), `TESWorldSpace`, `TESCell`, `TESRegion*` (region data incl. `TESRegionDataLandscape` + `…Editor`), `TESNPC`, `TESRace` (in BGS), `TESWeather`/`TESClimate`, `TESWaterForm` + `TESWaterNormals`/`Reflections`, `TESEffectShader`, `TESImageSpace*`, `TESObjectARMO/ARMA/WEAP/ACTI/ANIO/DOOR`, `TESLev*`, `TESFaction`, `TESClass`, `TESCombatStyle`, `TESKey`, `TESFlora`, `TESGrass`, `TESGlobal`, `TESIdleForm`, `TESLoadScreen`, `TESShout`, `TESWordOfPower`, `TESSoulGem`, `TESSound`, `TESTopic`/`TESTopicInfo`, `TESQuest`, `TESPackage` (custom package data family: `TESAmbushPackageData`, `TESUseWeaponPackageData`, …)
- Trait mixins: `TESFullName`, `TESDescription`, `TESIcon`, `TESModel`, `TESModelRDT`, `TESModelTri`, `TESValueForm`, `TESWeightForm`, `TESSpellList`, `TESMagicCasterForm`, `TESMagicTargetForm`, `TESHealthForm`, `TESEnchantableForm`, `TESBipedModelForm`, `TESAttackDamageForm`, `TESCondition`
- Editor-only: `TESRenderControl`, `TESWaterFormEditor`, `TESFormIDListView`, `TESCellUseList`, `TESChildCell`

Full list: `ghidra-dump/tes-classes.txt` (also embedded below in `ghidra-dump/rtti-classes.txt`).

## Render window (editor surface)

```
BGSRenderWindow
├── BGSRenderWindowCamera
├── BGSRenderWindowBorder
├── BGSRenderWindowGizmo / TranslationGizmo / RotationGizmo / ScaleGizmo / PrimitiveGizmo
└── Edit modules (each with Factory + EditModuleManager):
    ├── BGSRenderWindowEditModule
    ├── BGSRenderWindowLandscapeEditModule
    ├── BGSRenderWindowNavMeshEditModule
    ├── BGSRenderWindowPrimitiveEditModule
    └── BGSRenderWindowReferenceEditModule
```
Plus `BGSRenderWindowUtils` (`SceneGraphConnectPointAddFunctor`), `RenderWindowPrefsDlg`, and the
lighting/material stack (`BSLightingShaderMaterialLandscape`, `BSLightingShaderMaterialLODLandscape`).

## Dialog / UI classes

`BGSErrorDlg`, `BGSThreadedProgressDlg`, `DialogExtraLinkedDlg`, `VersionControlDlg`,
`MainDialog`, `LayoutManager` (`BGSObjectWindowUtils`), `WindowLayout` (5), `WaterPropertiesTab` …
The CK's window framework is MFC-free custom (`Dialog`, `WindowLayout` namespaces).

## Notes for the naming pass

- RTTI names give near-free class naming for Ghidra: an `RTTI_ClassNames.java` pass can label
  every `__type_info` and (via vftable xrefs) every vtable.
- `TESWaterFormEditor` and `TESRegionDataLandscapeEditor` are editor-only forks — the CK splits
  editor behavior from runtime form data in the same class family.
- The `Masterfile*` optimizer classes confirm the next-gen "Master Files" pipeline lives in the CK:
  `MasterfileOptimizer`, `DLCMasterfileOptimizer`, `MasterfileDelocalizer`, `MasterfileTagifier`,
  `MasterfileTextExporter`.
