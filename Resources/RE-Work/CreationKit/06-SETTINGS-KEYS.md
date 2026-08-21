# CreationKit.exe — Internal Settings Key Database

The CK embeds its **complete settings database** as `bKey:Section` string literals — 1,629 unique
keys. These are the settings the editor reads/writes (the `CreationKit.ini` `[Section]`/key pairs
map onto them). Full list: [`ghidra-dump/ck-settings-keys.txt`](ghidra-dump/ck-settings-keys.txt).

## The naming convention

- Keys are `b` (bool), `i` (int), `u` (uint), `f` (float), `s` (string) prefixed: `bAllowUnsafeSave:General`, `fLodDistance:LOD`, `uDistantObjectExtraHideDelayMS:LOD`.
- The **`Section` after the colon is the ini section** — `bGenerateGrassDataFiles:Grass` lives under `[Grass]` in the ini.
- Value parsing is `GetPrivateProfileStringA`-family (ini text), matching the runtime.

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
Plus the data paths: `Data\Meshes\PreCombined\%08X_*.NIF`, `Data\PreVis.esp`, `CombinedObjects.esp`.

## Uses

- Writing `CreationKit.ini` by hand: every key here is a candidate the editor actually reads.
- The validator-style oracle idea from the lodgen work applies directly: cross-check any ini
  against this embedded database (a `bKey:Section` literal lookup).
- Ghidra xref on any key string lands on the reader code — the settings system is one function
  family (`GetSetting`/`SetSetting`).
