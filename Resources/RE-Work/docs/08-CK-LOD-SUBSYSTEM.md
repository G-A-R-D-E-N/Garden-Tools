# The CK's Built-In LOD Generation Subsystem

The editor **ships its own LOD pipeline** (unlike the external xLODGen). Everything below is
recovered from the 1.11.137 binary's string surface — the LOD UI source file is
`code\construction set\misc\bgsworldlodsettingsdialog.cpp` and the distant-LOD mesh dialog is
`bgsdistantlodmeshdialog.cpp`.

## LOD settings files

```
Data\LODSettings
Data\LODSettings\%s.LOD          ← per-world LOD settings file
LODSettings\%s.LOD
LODSettings\%s.*                  ← glob used when scanning for settings artifacts
```

- Per-worldspace settings are stored as `Data\LODSettings\<Worldspace>.LOD`.
- `LODSettings\%s.*` implies sibling artifacts (e.g. generated textures) share the stem.
- This is the CK analogue of xLODGen's per-world `.dlodsettings` — same role, different format.

## Build flow (string-level contract)

1. Entry: `Building LOD for %s...` (worldspace name)
2. Object usage scan: `Analying LOD object usage...` (sic — engine typo)
3. Atlas packing: `Building object texture atlas...` / `Creating atlas...`
   - Failure: `Atlas layout failed. This probably means the LOD for this world is using too much
     texture space (max total size is %u x %u).`
   - Size parity checks: `LOD diffuse texture and normal texture on geometry '%s' are not the
     same size (but should be)...` (also specular variant); `LOD diffuse texture '%s' missing`
4. Terrain/water:
   - `Building LOD water for cell %i, %i. Water height is %f. Min land height is %f.`
   - water LOD type form: `LOD Water Type (%08X) '%s', ` and the failure
     `Unable to find lod water type (%08X) on owner worldspace (%08X).`
5. Object placement rules:
   - `Object LOD only supported for statics.  %s (%08X) is not a static.`
   - `LOD Land Static '%s' (%08X) has 0 bound size. References to this object may not correctly
     be added to all cells they overlap.`
   - `LOD build error: Geometry '%s' does not have a lighting shader property.`
6. Trees: `Data\Textures\Terrain\%s\Trees\%sTreeLOD.DDS`; tree-type validation against a
   TreeTypes file (`LOD: Invalid LOD tree type %u found. TreeTypes file may be out of date or corrupt.`)
7. Parallelism: `Generating LOD tiles using %d threads`
8. Threading/context: `Building %u chunks at level %u.`, `Connecting LOD tiles`,
   `Cell %i, %i: %u / %u LOD nodes are visible from this cell.`
9. Easter egg: `I see you are building LOD for SanctuaryHillsWorld. Do you want to use Commonwealth extents?`
   — a hardcoded special case for the default worldspace.

## Combined LOD (precombined / previs adjacency)

- `Data\Meshes\PreCombined\%08X_*.NIF`, `Data\Meshes\PreCombined\%s\…`
- `CombinedObjects.esp` / `Data\CombinedObjects.esp`, `Data\PreVis.esp`
- Import: `Importing combined LOD takes a few minutes and may require as much as 2gb of available
  memory. You also need to have writable LOD textures and combined textures. Are you sure?`
- Export: `Exporting combined LOD takes a few minutes and may require as much as 2gb of available
  memory and 5gb of hard disk space. Are you sure?`
- Batch CLI: `-GeneratePreCombined:<ESMFilename> [clean,filtered] [all,main,ints,other] [currentset(1-totalsetcount)] [totalsetcount]` (same for `-GeneratePreVisData:`)
- Error: `ERROR: too many precombined meshes in this cell to index properly in the precomputed visibilty data.` (sic)
- PreVis refs live in an extra-data slot: `ExtraPreVisRefs`; process class `PreVisPreCombineDataProcess`
- Console-help string: `Hides all objects previs would not use as an occluder. 1-hide, 0-show, 2-partial`

## LOD-related settings keys

```ini
[LOD]       bFadePortals bMeshLODRenderAllLevels bUseTextureUpgrade fDistanceMultiplier
            fFadeInThreshold fFadeInTime fFadeOutThreshold fFadeOutTime fLODBoundRadiusMult
            fLodDistance fLODFadeOutMultActors/Items/Objects/SkyCell fLODFadeOutPercent
            fLODMultTrees fPortalFadeMultiplier fTalkingDistance iFadeNodeMinNearDistance
            uDistantObjectExtraHideDelayMS
[DistantLOD] bUpdateDistantRefDataOnLoad
[TerrainManager] bDisplayCloudLOD bEnableCrossfading bKeepLowDetailTerrain bShowLODInEditor
            bUseNewTerrainSystem fBlockLevel0/1/2Distance fBlockMaximumDistance
            fCameraAboveMaxHeightThreshold fFadeSeconds fLODLandSpecPower fLODLandSpecScale
            fSplitDistanceMult fTreeLoadDistance iTerrainTextureBuildMemCleanupInterval
            uDistantTreeBlockCacheSizePerCell
[Water]     bReflectLODLand bReflectLODObjects bReflectLODTrees
[Display]   fInteriorMeshLODLevel1FadeDist fInteriorMeshLODLevel2FadeDist fMeshLODFadeBoundDefault
            fMeshLODFadePercentDefault fMeshLODFadeScalar fMeshLODFadeTime fMeshLODLevel1FadeDist
            fMeshLODLevel1FadeTreeDistance fMeshLODLevel2FadeDist fMeshLODLevel2FadeTreeDistance
[General]   bAllowMeshLODDebug
```

## Relevance to the xLODGen work

- The CK's "Building LOD water for cell %i, %i. Water height is %f. Min land height is %f." is
  the same water-vs-land decision xLODGen's terrain writer makes — the CK is the reference oracle
  for that logic.
- `Data\LODSettings\%s.LOD` is the CK-native per-world LOD config — the format to reverse next
  (byte-parse against a real file, like the `.dlodsettings` work).
- `Object LOD only supported for statics` mirrors xLODGen's static-only object LOD rule.
- The precombined/previs batch CLI is the CK's headless pipeline — the `-GeneratePreCombined` /
  `-GeneratePreVisData` contract deserves its own deep dive (see 05-BATCH-CLI.md).
