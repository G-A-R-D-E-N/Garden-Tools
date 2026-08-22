# Object LOD pipeline (LODApp)

Research date: 2026-08-18

This document details the object-LOD generation flow of the console engine (`LODApp.cs`, `LODGenerator` assembly), i.e. what happens after the GUI hands over the settings file and static references (see [XLODGEN_REVERSE_ENGINEERING.md](../XLODGEN_REVERSE_ENGINEERING.md)). Output-format details (NIF/BTO headers, block layout) are in [XLODGEN_OUTPUT_FORMAT.md](XLODGEN_OUTPUT_FORMAT.md); texture atlasing is in [XLODGEN_ATLAS_TEXTURES.md](XLODGEN_ATLAS_TEXTURES.md).

## 1. Entry: `LODApp.GenerateLOD(List<StaticDesc> statics)`

The engine (in `TES5`/`SSE`/`FO3`/`FO4` object-LOD modes, `Game.Mode`) calls `GenerateLOD` with the full parsed static-reference list. Steps:

1. **Quad partitioning** — for each LOD index `i` (`level = 4 << i`: 4, 8, 16, 32), `Utils.SortMeshesIntoQuads(statics, southWestX, southWestY, i)` assigns every static to a quad via cell math:
   - Quad size `quadSize = level * 4096` game units (a quad is `level × level` cells).
   - `Cellquad(coord, southWest, level, quadSize)` buckets the static by `floor((coord - southWest) / quadSize)` in each axis (with a safety clamp into the valid range).
   - The quad's `quadLevel` is the level (4/8/16/32); its local origin is the bucket origin. `quadIndex` = `log2(level) - 2`.
   - Level filtering: FO3 only generates LOD4 (and LOD8 when `level8` is set, as `Level8.High`); non-FO4 modes skip LOD32 unless `level32` is set; `lodLevelToGenerate` restricts to a single level; `--x`/`--y` restrict to a single quad.

2. **Terrain-aware culling setup** — if `removeUnseenFaces` and a terrain data file (`TERRAINDATA=`) are present, the terrain heightmap is loaded (`TerrainData.ReadWorldData`); if `createHeightMap` (object heightmap output) is set, an all-`float.MinValue` 33×33 slot is reserved for every cell in the `Game.West..East / South..North` bounds. The heightmap is used by `RemoveUnseenTerrain` to drop object triangles that lie under terrain, and by `WriteMHDT` to emit the object heightmap file (see §6).

3. **Parallel quad processing** — quads are drained from the work list and processed with a dynamic concurrency semaphore (`Game.threads / Game.conc` slots, throttled by available physical memory vs. `Game.mem`). Each quad runs `DoLOD(quad)` on a long-running task. FO3 additionally runs `DoLOD(quad, high: true)` for the LOD8 high variant.

## 2. `DoLOD(quad)` — per-quad build

1. **Output path** (non-FO3):

```
<PathOutput>\<worldspace>.<level>.<x>.<y><season>.bto
```

FO3: `<worldspace>.Level4.X<x>.Y<y><season>.nif` (and `Level8.High` when `level8`), `Merge` mode: `<worldspace>.nif`.

2. **Season handling** — if `season` is set and the current quad is not in the season's coordinate list, the non-season output is copied instead (log `Copied LOD level ...`), skipping generation entirely.

3. **Scene construction** (`NiFile` + `NiNode`/`BSFadeNode`/`BSMultiBoundNode`):
   - Per-game header setup: SSE → userVersion2=100; FO4 → userVersion2=130; FO3 → userVersion 11, userVersion2 34, `BSMultiBoundNode` root with flags 2062/8; `Merge4` → header string "Gamebryo File Format, Version 20.0.0.5", version 335544325, userVersion 11, userVersion2 11; `Merge5` → `BSFadeNode` root.
   - Root node named `"obj"` (non-FO3, non-merge); FO3 uses the `BSMultiBoundNode` as root directly.

4. **Grass injection** — at LOD4 only, when `PATHGRASS` + `GRASSMAP` are set, `GIDFile` reads `<grassDir>\<worldspace>.4.<x>.<y><season>.gid` and appends grass placements (density-filtered via `GRASSDENSITY`, large references via `GRASSLARGEREFERENCE`) to `quad.statics`.

5. **NIF parsing** — every static's LOD model for this quad index (`staticModels[quadIndex]`) is parsed in parallel (`Parallel.For`, `Game.conc`): `ParseNif` loads the `.nif` from the BSAs, extracts `ShapeDesc`s (geometry, textures, flags, segments, bounding boxes) and applies the **atlas UV remap** (`AtlasDesc.UVAtlas`) and **alt-texture/material-swap** policies. Errors abort with `Game.logFile.Close(548)`.

6. **Shape merging** — `MergeShapes(quad, shapes)` (when `mergeShapes`, i.e. not `DONTMERGESHAPES`) groups compatible `ShapeDesc`s by material/texture key into combined `Geometry` buffers, splitting into multiple `ShapeDesc`/`SegmentDesc` groups whenever vertex/triangle limits would be exceeded (`Game.MaxVertices()` = 65535, `Game.MaxTriangles()` = 65535, unlimited for FO4) — `geometry.Optimize()` is attempted first. Per-shape attributes appended: positions (FO3 scaled by `quadLevel`), UVs, face normals (computed if absent), tangents/bitangents (only when `generateTangents[i]`, and only for LOD4/HD/pass-thru shapes when the optimizer is on), vertex colors (white-filled if `generateVertexColors[i]` but source has none), and remapped triangles. Segment bookkeeping records `startTriangle`/`numTriangles` per original shape id so each shape stays addressable.

7. **Node creation** — game-specific:
   - `CreateLODNodesFO4` / `CreateLODNodesSSE` / `CreateLODNodes` (TES5-style) / `CreateLODNodesFO3` (LOD4/Level8 with `NiTriShape` + segment data) / `CreateMergeNodes`/`CreateMerge4Nodes`/`CreateMergeSSENodes` (single-file merge modes).
   - Shapes are attached with per-shape transforms (position × `quadLevel`, rotation from the static's orientation, scale), and `BSMultiBoundAABB` bounds are grown per shape. If the resulting root has no children the quad is skipped (no output file).
   - `CreateHeightMap` (FO4): each shape's position+radius contributes to the per-cell 33×33 height array (`mhdt`, key = `0x100000000 | (x << 16) | y`) used for the heightmap output.

8. **Write** — `niFile.Write(path)` (NIF serialization, see [XLODGEN_OUTPUT_FORMAT.md](XLODGEN_OUTPUT_FORMAT.md)); the quad logs `Finished LOD level <lvl> coord [x, y] [totalTri/reducedTri]`.

## 3. Reference resolution details (`ParseNif`)

- The static's `staticModels[quadIndex]` path is loaded via `BSAArchive` (loose files first). The mesh's textures are read from the NIF's `BSLightingShaderProperty`/`BSShaderTextureSet` (`Textures[0..]`: diffuse, normal, glow, specular, ...).
- **Atlas remap**: if the shape is not named `*noatlas*` and its texture key exists in `AtlasList`, `AtlasDesc.UVAtlas` remaps all UVs into the atlas rect (details in XLODGEN_ATLAS_TEXTURES.md); diffuse/normal/glow/specular texture slots are swapped to the atlas textures.
- **Alt textures**: `ALTTEXTURES=` file lists substitute texture keys; when a shape's diffuse matches an alt entry, the whole texture set is swapped to the alt's set (with the `\lod\`-path guard: only swap when the original is a LOD texture and the alt is not).
- **Material swaps** (FO4): `materialName` in the static descriptor selects a `.bgsm`; its diffuse feeds the texture key, and material-swap source/target lists re-point textures.
- **HD textures**: shapes whose key resolves to an `HDTexture` entry keep their original (full-res) textures instead of the atlas.
- **Pass-through shapes**: when `REMOVEUNSEENFACES` is off and the shape is pass-through, only the texture swap is applied (no UV remap).

## 4. Culling (`RemoveUnseenTerrain`)

When `RemoveUnseenFaces=True` (the GUI always writes it for object LOD):
- The loaded terrain heightmap is consulted per cell; triangles whose vertices are entirely below the terrain surface (plus `REMOVEUNSEENZSHIFT` offset, and `removeUnderwaterFaces` when water is not ignored) are removed from the geometry. `REMOVEUNSEENGRASS` additionally removes grass shapes placed under terrain.

## 5. Grass LOD (`GIDFile`)

- Input: `Meshes\Terrain\<worldspace>\Grass\<worldspace>.4.<x>.<y><season>.gid` (binary placement list: grid cell, density, positions of grass references per cell).
- Output: `StaticDesc`s referencing the LOD4 grass model with density `GRASSDENSITY` (0..1, default 1) applied probabilistically; `GRASSLARGEREFERENCE` keeps large grass references; brightness/color adjustments (`GRASSBRIGHTNESSTOP/BOTTOM`, `CROWNBRIGHTNESS`, `TRUNKBRIGHTNESS`, `FLATTRUNKBRIGHTNESS`) are applied as vertex colors on the grass shapes.

## 6. `WriteMHDT()` — object heightmap output

Written after all quads when `createHeightMap` is on and the run is LOD4-only over the whole worldspace:

```
<PathOutput>\<worldspace>.objects.heightmap.dat
```

Format:

```
int32    cellCount
repeat per cell:
  int16   cellX (high word of key)
  int16   cellY (low word of key)
  33 × 33 × float32  heights (row-major, 1089 values; float.MinValue = no data)
```

This is the file the game's object placement (and other tools) use to know where object LOD geometry sits.

## 7. Relevance to F4LODGen

- The `StaticDesc` line contract and quad bucketing (`level * 4096`, `Cellquad`) define the object-pipeline partitioning; a compatible implementation can feed the same records without the intermediate settings file.
- The merge budget (65535 vertices/triangles, FO4 unlimited triangles) and segment bookkeeping are the practical constraints a replacement must honor for BTO validity.
- `objects.heightmap.dat` is an observable side artifact only when heightmap output is requested — optional for a reimplementation.
