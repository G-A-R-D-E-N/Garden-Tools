# CreationKit.exe — Internal Settings Key Database

The CK embeds 1,629 unique colon-bearing `bKey:Section` string literals plus **25 bare
`SettingT<INISettingCollection>` records** whose reader section defaults to `[MAIN]`. These are
the settings the editor reads/writes (the `CreationKit.ini` `[Section]`/key pairs map onto them).
The raw colon-bearing list is [`ghidra-dump/ck-settings-keys.txt`](ghidra-dump/ck-settings-keys.txt);
`tools/validate_ckini.py` augments it with the 25 byte-verified bare records.

## The naming convention

- Keys are `b` (bool), `i` (int), `u` (uint), `f` (float), `s` (string) prefixed: `bAllowUnsafeSave:General`, `fLodDistance:LOD`, `uDistantObjectExtraHideDelayMS:LOD`.
- The **`Section` after the colon is the ini section** — `bGenerateGrassDataFiles:Grass` lives under `[Grass]` in the ini.
- Value parsing is `GetPrivateProfileStringA`-family (ini text), matching the runtime.

### Bare `[MAIN]` INI records

The 25 no-section records are still INI settings, not GameSetting names. The reader's
`GetSection` helper supplies the literal `MAIN`, so for example:

```ini
[MAIN]
fMaxBoneSpeedBeforeDefaultStiffness=50.0
sControlsRemapFile=Controls\\ControlMap_Custom.txt
```

The full record addresses are tracked in the validator's
`NO_SECTION_DESCRIPTORS` schema. Fixture coverage is in
`tools/samples/no-section-creationkit.ini` and
`tools/samples/invalid-no-section-creationkit.ini`.

## Separate runtime GameSetting registry

The bare game-value names (`sYes`, `fAI...`, `fActor...`, and thousands of other
`s`/`f`/`i`/`u`/`b` names) are **not** CreationKit.ini keys. They belong to the
separate `SettingT<GameSettingCollection>` registry under vftable `0x142f97230`.
A byte census finds 5,137 bare-name records plus the 7 colon-bearing UI records
that share the specialization. Do not merge these names into the 1,629-key INI
literal dump or the validator's 1,654-record schema; the full RTTI/layout/census and representative
record map is in [`ghidra-dump/game-setting-table.md`](ghidra-dump/game-setting-table.md).

## Section inventory (key counts)

| Section | Keys | Section | Keys |
|---|---|---|---|
| General | 424 | Pathfinding | 95 |
| Display | 104 | Animation | 60 |
| BudgetCaps | 51 | NavMeshGeneration | 48 |
| Landscape | 48 | Audio | 47 |
| Flowchart | 41 | Workshop | 38 |
| HAVOK / Havok | 38 | Water | 34 |
| Papyrus | 34 | Visibility | 30 |
| VATS | 30 | Bethesda | 28 |
| PreviewMovement | 24 | Archive | 21 |
| Weather | 20 | LOD | 20 |
| NVHBAO | 19 | TerrainManager | 17 |
| Grass | 17 | Pathfind | 15 |
| Trees | 13 | FaceGen | 12 |
| Cloth | 12 | Decals | 12 |
| MESSAGES | 11 | Localization | 10 |
| BackgroundLoad | 9 | Navigation | 8 |
| … | | | |

## LOD-relevant keys (cross-ref to the lodgen work)

```ini
[LOD]
bFadePortals=          bMeshLODRenderAllLevels=   bUseTextureUpgrade=
fDistanceMultiplier=   fFadeInThreshold=          fFadeInTime=
fFadeOutThreshold=     fFadeOutTime=              fLODBoundRadiusMult=
fLodDistance=          fLODFadeOutMultActors=     fLODFadeOutMultItems=
fLODFadeOutMultObjects=fLODFadeOutMultSkyCell=    fLODFadeOutPercent=
fLODMultTrees=         fPortalFadeMultiplier=     fTalkingDistance=
iFadeNodeMinNearDistance=  uDistantObjectExtraHideDelayMS=

[TerrainManager]
bDisplayCloudLOD=      bEnableCrossfading=        bKeepLowDetailTerrain=
bShowLODInEditor=      bUseNewTerrainSystem=      fBlockLevel0Distance=
fBlockLevel1Distance=  fBlockLevel2Distance=      fBlockMaximumDistance=
fCameraAboveMaxHeightThreshold=  fFadeSeconds=    fLODLandSpecPower=
fLODLandSpecScale=     fSplitDistanceMult=        fTreeLoadDistance=
iTerrainTextureBuildMemCleanupInterval=  uDistantTreeBlockCacheSizePerCell=

[Grass]
bAllowCreateGrass=     bAllowLoadGrass=           bDrawShaderGrass=
bGenerateGrassDataFiles=  bGrassPointLighting=    fGrassDefaultStartFadeDistance=
fGrassFadeRange=       fGrassMaxStartFadeDistance=fGrassMinStartFadeDistance=
fGrassStartFadeDistance=  fGrassWindMagnitudeMax= fGrassWindMagnitudeMin=
fTexturePctThreshold=  fWaveOffsetRange=          iGrassCellRadius=
iMaxGrassTypesPerTexure=(sic)  iMinGrassSize=
```

`bUseNewTerrainSystem:TerrainManager` is the FO4 terrain-switch flag; `bGenerateGrassDataFiles` is
the grass-LOD-data generation toggle — both pair with the runtime LOD/terrain pipeline the
lodgen analysis documented.

## Precombined / PreVis keys

```ini
[General]
bUseCombinedObjects=           bGeneratePreCombinedGeometryPreVis=
bForcePreCombinedObject32bitExport=  bOnlyLoadPreVis3D=
fPreVisCameraBufferNear=       fPreVisCameraZShift=
[Display]
bCombinedObjectsAvailableForPicking=
```
Plus the data paths: `Data\Meshes\PreCombined\%08X_*.NIF`, `Data\PreVis.esp`, `CombinedObjects.esp`. The direct batch consumers, descriptor VAs, and runtime value references are cross-indexed in [22-BATCH-SETTING-DEPENDENCIES.md](22-BATCH-SETTING-DEPENDENCIES.md).

## Uses

- Writing `CreationKit.ini` by hand: every key here is a candidate the editor actually reads.
- The validator-style oracle idea from the lodgen work applies directly: cross-check any ini
  against this embedded database (a `bKey:Section` literal lookup).
- Ghidra xref on any key string lands on the reader code — the settings system is one function
  family (`GetSetting`/`SetSetting`).
