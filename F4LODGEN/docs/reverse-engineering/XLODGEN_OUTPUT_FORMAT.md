# Output format: BTR/NIF files

Research date: 2026-08-18

The engine's terrain and object LOD outputs are **NIF-format files** written by `NiFile.Write` (serialization in `LODGenerator.NifMain`). `.btr` files are NIF files with game-specific header markers, not a separate format. This spec is derived from the decompiled writer (`NiFile.cs`, `NiHeader.cs`, `NiTriShapeData.cs`, `NiGeometryData.cs`, `TerrainLOD.cs`, `LODApp.cs`) and is accurate for the exact bytes this tool emits.

## 1. File naming and output location

| Mode | Pattern (relative to `PATHOUTPUT`) |
|---|---|
| TES5 / SSE / FO4 terrain | `<ws>.<lod>.<x>.<y><season>.btr` — written **directly into PATHOUTPUT** |
| FO3 terrain | `<ws>.Level<lod>.X<x>.Y<y><season>.nif` |
| TES5 / SSE / FO4 object | `<ws>.<lod>.<x>.<y><season>.bto` — directly into PATHOUTPUT |
| FO3 object | `<ws>.Level4.X<x>.Y<y><season>.nif` (+ `.Level8.High.` with LOD8) |
| Merge modes | `<ws>.nif` |
| Flat terrain (FLAT* modes) | `<ws>.<lod>.<x>.<y><season>.btr` under `PATHOUTPUT\Meshes\Terrain\<ws>\` |

**Verified against a real run:** `GenerateBTR` writes `outputDir + "\" + <ws>.<lod>.<x>.<y>.btr` — the engine does **not** append `Meshes\Terrain\<ws>\`; the GUI supplies a PATHOUTPUT that already ends in `meshes\terrain\<ws>` (or `...\Objects` for object LOD). Only `GenerateFlat` (FLAT* modes) appends `Meshes\Terrain\<ws>\` itself. All little-endian. `season` is empty or `.SeasonName`.

## 2. NIF header (`NiHeader.Write`)

Byte layout, in order:

| Field | Type | Notes |
|---|---|---|
| header string | bytes + `\n` (0x0A) | default `"Gamebryo File Format, Version 20.2.0.7"`; `Merge4` uses `"Gamebryo File Format, Version 20.0.0.5"` |
| version | uint32 | 0x14020007 = 335675399 (default); 335544325 for Merge4 |
| endian type | byte | always 1 (little) |
| user version | uint32 | 12 (TES5/SSE/FO4); 11 (FO3, Merge4) |
| num blocks | uint32 | |
| user version 2 | uint32 | 83 default; **100 SSE**; **130 FO4**; **34 FO3**; 11 Merge4 |
| creator | short string | `"LODGen <version> by Ehamloptiran, Zilav and Sheson"` |
| export info 1 | short string | empty |
| export info 2 | short string | empty |
| export info 3 | short string | only when version==335675399 && userVersion2==130 (FO4) |
| num block types | uint16 | |
| block types | per type: sized string | e.g. `BSMultiBoundNode`, `BSTriShape`, `NiTriShape`, `NiTriShapeData`, `BSLightingShaderProperty`, `BSShaderTextureSet`, `BSMultiBound`, `BSMultiBoundAABB`, `NiNode`, `BSFadeNode`, `BSSegmentedTriShape`, `NiTriStrips`… |
| block type indices | numBlocks × uint16 | index into block-type table |
| block sizes | numBlocks × uint32 | only when version > 0x14020005 (335544325) |
| num strings | uint32 | |
| max string length | uint32 | recomputed at write time |
| strings | per string: sized string | |
| unknownInt2 | uint32 | **always 0 in this writer** |

String encodings:
- **Header string** (creator/export info): 1 byte `len+1`, then bytes, then 0x00.
- **Sized string** (block types, strings table): uint32 length, then bytes (no terminator).
- bool: single byte 0/1. Vector3: 3 × float32.

## 3. Body and footer

Blocks follow the header in block-index order, each written by its class (`NiObject.Write`). After the last block:

```
int32  1     (root node count)
int32  0     (root node index)
```

(`NiFile.Write` appends `Write(1); Write(0)` unconditionally.)

## 4. `.btr` markers

The BTR-ness is signaled by the **string-table contents** and **userVersion2**, added by the terrain writer (`TerrainLOD.GenerateBTR` / `GenerateFlat`):

| Mode | Added strings | userVersion2 |
|---|---|---|
| TES5/SSE | `"chunk"`, `"land"` | 100 (SSE) |
| FO4 | `"chunk"`, `"Land"` | 130 |
| Flat (FLATSSE) | `"chunk"`, `"land"`, `"WATER"` | 100 |
| FO3 | (none) | 34 — plain `.nif` with userVersion 11 |

The terrain root is a `BSMultiBoundNode` (name index 0 in non-FO3, cull mode 1, translation `(x*4096, y*4096, 0)` in FO3) with a `BSMultiBound` → `BSMultiBoundAABB` pair (position = bbox center × lodLevel, extent = bbox extend × lodLevel). Terrain texture set:

```
Data\Textures\Terrain\<ws>\<ws>.<lod>.<x>.<y><season>.dds            (diffuse, slot 0)
..._n.dds   (TES5/SSE slot 1)     ..._msn.dds (FO4 slot 1)
FO3: Data\Textures\Landscape\LOD\<ws>\Diffuse\<ws>.N.Level<lod>.X<x>.Y<y><season>.dds
     + Normals\ sibling (6-slot set)
```

`BSLightingShaderProperty` shader type 18 (terrain); FO3 uses `BSShaderPPLightingProperty` (flags 12288/2). Per-water-height geometry groups get a `BSMultiBoundNode` child named `"WATER"` with their own AABB.

## 5. Block layouts

All blocks are written in block-index order by their class's `Write()`. Base-class fields come first, in inheritance order. **`NiObject.Write` is empty** — the `nameIndex`/`numExtraData`/`controller` fields live in `NiObjectNET` — so the `NiObject`-derived classes `NiGeometryData`, `BSMultiBound`, `BSMultiBoundAABB` and `BSShaderTextureSet` start **directly** with their own fields (no name/extra-data prefix). All layouts below were verified byte-for-byte against a real `TestWorld.4.0.0.btr` generated by the bundled LODGenx64.exe (SSE, userVersion 12, userVersion2 100).

### 5.1 `NiObjectNET` prefix

```
NiObjectNET:  nameIndex (int32), numExtraData (uint32), extraDataList (int32[]), controller (int32)
```

(For version ≤ 0x14020005 the name is a sized string instead of an int32 index.)

### 5.2 `NiAVObject`

```
NiAVObject:   flags (uint16),
              flags2 (uint16)               [only if userVersion >= 11 && userVersion2 >= 26],
              translation (Vector3), rotation (3×3 float32), scale (float32),
              numProperties (uint32) + propertyList (int32[])
                  [omitted when version > 0x14020005 && userVersion > 11],
              collisionObject (int32)
```

Verified: `flags` is **uint16** (plus a `flags2` uint16 for TES5/SSE/FO4), and `numProperties` is **not written** for userVersion 12 files.

### 5.3 `NiGeometry` (shape blocks: `NiTriShape`, `BSTriShape`, `BSSegmentedTriShape`, …)

```
NiGeometry:   data (int32 ref), skinInstance (int32 ref),
              [version > 0x14020005:]
                numMaterials (uint32), materialList (sized strings),
                materialExtraData (sized strings), activeMaterial (uint32), dirtyFlag (byte),
                bsProperties (2 × int32)     [only if userVersion == 12]
              [else:]
                hasShader (byte)
```

### 5.4 `NiTriShapeData` geometry data

`NiGeometryData : NiObject` — **no NiObjectNET prefix**; the block starts with `groupID`:

```
NiGeometryData:     groupID (uint32), numVertices (uint16), keepFlags (byte), compressFlags (byte),
                    hasVertices (bool) + numVertices × Vector3,
                    numUVSets (uint8), extraVectorFlags (uint8),
                    [skyrimMaterial (uint32) if userVersion == 12],
                    hasNormals (bool) + numVertices × Vector3,
                    [if hasNormals && extraVectorFlags & 0x10: numVertices × tangent + numVertices × bitangent (Vector3 each)],
                    center (Vector3), radius (float32),
                    hasVertexColors (bool) + numVertices × Color4 (4 × uint8),
                    [if numUVSets & 1: numVertices × (u, v) float32],
                    consistencyFlags (uint16), additionalData (int32)
NiTriBasedGeomData: numTriangles (uint16)
NiTriShapeData:     numTrianglePoints (uint32) [always 3],
                    hasTriangles (bool) + numTriangles × (v1,v2,v3 uint16),
                    numMatchGroups (uint16) [0], each group: numIndices (uint16) + uint16[]
```

Verified corrections to earlier guesses: `numUVSets`/`extraVectorFlags` are **single bytes** (not uint32); `numTrianglePoints` is **uint32** (not uint16); and the UVs are **one flat array** gated by `numUVSets & 1` — there are no per-set `hasUV` flags.

### 5.5 Other block types

```
NiNode:              NiAVObject + numChildren (uint32) + children (int32[]),
                     + numEffects (uint32) + effects (int32[])   [omitted when userVersion2 >= 130]
BSMultiBoundNode:    NiNode fields + multiBound (int32 ref),
                     cullMode (uint32)   [only if userVersion >= 12]
BSMultiBound:        data (int32 ref)                       // : NiObject, no prefix
BSMultiBoundAABB:    position (Vector3), extent (Vector3)   // : BSMultiBoundData : NiObject, no prefix
BSShaderTextureSet:  numTextures (uint32) + sized strings   // : NiObject, no prefix
```

`BSLightingShaderProperty` writes **`shaderType` (uint32) first**, then the base chain:

```
shaderType (uint32),
NiObjectNET prefix,
flags1 (uint32), flags2 (uint32),
uvOffset (2 × float32), uvScale (2 × float32), textureSet (int32 ref),
emissive (Color3), emissiveMultiple (float32),
[rootMaterial (int32) if version == 0x14020007 && userVersion2 == 130 (FO4)],
clampMode (uint32), alpha (float32), refractionStrength (float32),
glossiness (float32), specularColor (Color3), specularStrength (float32),
[FO4: 10 × float32 extras; else: lightingEffect1/2 (2 × float32)],
shader-type tail: envMapScale (1), skinTintColor (5), hairTintColor (6),
                  maxPasses + scale (7); none for terrain type 18
```

### 5.6 Terrain shape variants

- Main terrain shapes at **all** LOD levels (4/8/16/32) are plain `NiTriShape` + `NiTriShapeData`.
- `BSSegmentedTriShape` is **water-only** (LOD4): after the `NiGeometry` fields it adds `numSegments (uint32)` + segments `{ startTriangle (uint32), numTriangles (uint32) }`; FO3 pads to ≥ 3 segments.
- FO4/SSE object LOD uses `BSTriShape` (vertex data inline in the shape block rather than a separate `NiTriShapeData`).
- `NiTriShapeData.ToNiTriShapeData(generateVertexColors, optimize)` drives the hasX flags; optimization removes unused vertices and re-indexes triangles.

## 6. Terrain quad specifics (`GenerateBTR`)

- Geometry is split into multiple shapes when limits are hit: `MaxVertices` = 65535 always, `MaxTriangles` = 65535 (unlimited FO4).
- Non-FO3 shapes carry `translation (0,0,zShift[lod])` and `scale = lodLevel`; FO3 shapes carry `translation (x*4096, y*4096, zShift)` and no scale.
- Water: one shape per unique water height at LOD4 (`BSSegmentedTriShape`), merged with `WATERDELTA` handling; an empty FO3 water shape is padded with a degenerate (0,0,0) vertex/triangle.
- Skirts: 256-unit skirts reserve 2000 vertices; the triangle budget is never pushed below 512 target.
- UVs: `u = x/4096`, `v = 1 - y/4096` (quad-local).

## 7. Object LOD quad specifics (`DoLOD`)

- Root: `NiNode` (TES5/SSE/FO4) named `"obj"`, `BSFadeNode` (Merge5), `BSMultiBoundNode` (FO3, flags 2062/8).
- One `BSTriShape`/`NiTriShape` per merged shape group; `BSMultiBound`/`BSMultiBoundAABB` bounds grown from all shapes (position × lodLevel). Shapes store per-static transforms (position × lodLevel, rotation matrix, scale).
- Grass shapes get vertex colors for brightness adjustments; FO4 shader property carries the `"Land"` string and userVersion2=130 header marker.

## 8. Compatibility notes for F4LODGen

- A BTO/BTR writer only needs: the header above, `BSMultiBoundNode` + `BSMultiBoundAABB` bounds, `BSShaderTextureSet`/`BSLightingShaderProperty` (type 18 for terrain), and `NiTriShapeData`-layout geometry (or `BSTriShape` for FO4). The string-table markers (`chunk`/`Land`, userVersion2 100/130) are what the game's mesh loader keys on.
- Vertex/triangle budgets (65535; FO4 triangles unlimited) are hard engine constraints mirrored in the writer's shape-splitting logic.
- The 1/0 root footer is required.
