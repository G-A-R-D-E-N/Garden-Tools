# CreationKit.ini — Key Inventory

Source: `.../steamapps/common/Fallout 4 1946160/CreationKit.ini` (shipped default, 2026-05-11 build of the Steam CK 1.11.137).

## `[General]`

| Key | Default | Notes |
|---|---|---|
| `bCullingBatch` | 0 | batch culling toggle |
| `bUseMyGamesDirectory` | 0 | 0 → paths are relative to the game folder; 1 → `Documents\My Games\Fallout4` |
| `bUseVersionControl` | 0 | Perforce-style check-out/check-in (CK's internal VCS) |
| `SLocalSavePath` | `Saves\` | relative to game folder |
| `SLocalMasterPath` | `Data\` | plugin/asset root |
| `SLocalBackupPath` | `Data\CheckInBackup` | VCS check-in backup |
| `uiFaceGenMaxEGTDataSize` | 67108864 (64 MB) | FaceGen geometry data cap |
| `uiFaceGenMaxEGMDataSize` | 67108864 (64 MB) | FaceGen morph data cap |
| `bUseEyeEnvMapping` | 1 | eye environment mapping |
| `bFixFaceNormals` | 0 | auto-fix face normals on load |
| `bUseFaceGenHeads` | 1 | FaceGen head generation |
| `bFaceMipMaps` | 1 | FaceGen mipmaps |
| `bExternalLODDataFiles` | 1 | LOD data as external files (not packed) |
| `bOnlyActiveFileForRefFix` | 1 | ref-fix operates on active plugin only |
| `bForceCheckOutOnRefFix` | 0 | VCS check-out forced during ref-fix |
| `bFixPersistenceOnRefInit` | 0 | repair persistence flags on init |
| `bFixBadLocRefsOnInit` | 0 | repair bad location refs on init |
| `iCheckCellRegionsOnInit` | 0 | cell-region consistency check level |
| `bCheckDoorCollisionOnInit` | 0 | door collision check on load |
| `bWarnOnGameSettingLoad` | 0 | warn when GameSetting records load |
| `bFaceGenTexturing` | 1 | FaceGen texture baking |
| `bCheckHairOnInit` | 0 | hair consistency check |
| `bKeepHeadOnRaceChange` | 0 | keep head when race changes |
| `bCreateShaderPackage` | 0 | build shader packages (PS4/XB1 era) |
| `uGridDistantTreeRangeCity` | 4 | distant-tree grid range (city) |
| `uGridDistantCountCity` | 4 | distant grid count (city) |
| `uGridDistantTreeRange` | 15 | distant-tree grid range |
| `uGridDistantCount` | 25 | distant grid count |
| `uGridsToLoad` | 5 | cell grid radius (editor loads 5×5) |
| `bAnimationUseBlendFromPose` | 0 | animation blending |
| `fAnimationDefaultBlend` | 0.2 | |
| `fAnimationMult` | 1.0 | |
| `iCheckOutOnBoundChange` | 2 | VCS check-out behavior when a form's bounds change |
| `bFixAIPackagesOnLoad` | 0 | repair AI packages on load |
| `bKeepPluginWhenMerging` | 0 | keep source plugin when merging |
| `bLoadTheUserPlugin` | 0 | auto-load user plugin |
| `bCreate Maps Enable` | 0 | (note the space — legacy key) |
| `bDisableDuplicateReferenceCheck` | 1 | |
| `bCheckForMultiFileForms` | 1 | multi-file form consistency |
| `bCheckForRefCellChanges` | 0 | |
| `bTintMipMaps` | 0 | |
| `uExterior Cell Buffer` | 36 | exterior cell buffer (note space) |
| `uInterior Cell Buffer` | 3 | |
| `bPreemptivelyUnloadCells` | 0 | |
| `SFaceTestSoundName` | `Data\Sound\Voice\Test\CharGen QA1.wav` | chargen QA voice test |
| `bClearPersistenceOnVCSort` | 0 | |
| `bAllowNonGroupedVersionControl` | 0 | |
| `iFullSaveCellThreshhold` | 20 | (sic — engine spelling) full-save cell count |
| `iNPCCheckOutCount` | 500 | VCS NPC check-out batch |
| `iLandCheckOutCount` | 1024 | VCS landscape check-out batch |
| `fPathGridAutoConnectRadius` | 512.0 | auto-connect path grid radius |
| `SFile Path` | (empty) | legacy file dialog path |
| `bLight Radius` | 0 | (space) light radius display |
| `bMaximize Editor` | 0 | (space) maximize on launch |
| `bPreview` | 0 | preview mode |
| `bAllowUnsafeSave` | 0 | allow saves that bypass safety checks |
| `bAllowMultipleEditors` | 0 | multiple editor instances |
| `bEditorHotLoading` | 1 | hot-reload edited assets (PC) |
| `bEditorHotLoading360` | 1 | legacy X360 hot-loading |
| `bEditorHotLoadingPS3` | 0 | legacy PS3 hot-loading |
| `bDisableComputeShaderOnCombinations` | 1 | |
| `sMaterialEditorPage` | `Tools\MaterialEditor\htdocs\index.html` | material editor is an HTML app in `Tools/MaterialEditor/` |

## `[Bethesda.net]`

| Key | Default | Notes |
|---|---|---|
| `bEnablePlatformSelection` | 1 | platform picker on login |
| `bEnableXB1` | 1 | legacy Xbox One publishing |
| `bEnablePS4` | 1 | legacy PS4 publishing |

## `[Imagespace]`

| Key | Default | Notes |
|---|---|---|
| `bMBEnable` | 0 | motion blur |

## `[Display]`

| Key | Default | Notes |
|---|---|---|
| `bDeferredCommands` | 0 | deferred command queue |
| `bDynamicObjectQueryManager` | 0 | |
| `bMultiThreadedAccumulation` | 0 | render thread accumulation |
| `bMultiThreadedRenderingUNP` | 0 | |
| `iTexMipMapMinimum` | 0 | min mip level |
| `fDecalLifetime` | 10.0 | decal persistence |
| `bUsePreCulledObjects` | 0 | |

## `[Papyrus]`

| Key | Default | Notes |
|---|---|---|
| `sScriptSourceFolder` | `.\Data\Scripts\Source\User` | user script source root |
| `sAdditionalImports` | `$(source);.\Data\Scripts\Source\Base` | compiler import path (note `$(source)` variable) |
| `sAutoFillNamespaceDefault1..5` | `MyMod01`…`MyMod05` | auto-fill script header namespaces |

## Observations

- Several keys carry the engine's legacy spelling/format (`iFullSaveCellThreshhold`, `uExterior Cell Buffer`, `bLight Radius`, `bCreate Maps Enable`) — the parser is `GetPrivateProfileStringA`-family, values are matched verbatim.
- The editor's default config is **console-era**: X360/PS3 hot-loading keys, `bCreateShaderPackage` (shader packaging for console), `[Bethesda.net]` platform flags, `sMaterialEditorPage` pointing at a local HTML app.
- `bAllowUnsafeSave=0` + `bAllowMultipleEditors=0` are the safety rails; `bAllowUnsafeSave` is the interesting one for mod tooling (bypasses the save safety checks).
- Cross-reference with the binary is planned (verify each key is actually read via `GetPrivateProfileStringA` xrefs once the Ghidra project finishes).
