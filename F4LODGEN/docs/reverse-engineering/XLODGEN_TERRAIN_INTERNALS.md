# Terrain-LOD engine internals: water delta, hide quads, skirts/ZSHIFT, BTR chunking, water nodes, vertex data, reducers

Research date: 2026-08-18. Companion to `XLODGEN_OUTPUT_FORMAT.md` (byte layout),
[`XLODGEN_QUALITY.md`](../XLODGEN_QUALITY.md) (defaults), `XLODGEN_SETTINGS_WRITER.md` (GUI writer), and
[`LODGEN_ENGINE.md`](../LODGEN_ENGINE.md). Everything below is read from the decompiled engine source
(xLODGen's `LODGenerator*` projects). The shipped `xLODGenx64.exe` was decompiled
with ILSpy (`/tmp/lodgen-il/out/LODGenerator/TerrainLOD.cs`) and the IL checked
where a C# cast could hide truncation — see §1 for the integer-division catch.
These are the exact behaviors a native (C++) port must reproduce for
byte/geometry parity.

Pipeline recap (TerrainLOD.cs):
`ReadWorldData()` (load `LODGen_Terrain_<ws>.bin`) →
per quad `GenerateQuad()` (heightfield + greedy `Terrain` + WaterDelta + border pins)
→ `CreateGeometry()` (vertex reorder + UVs) →
`GenerateTerrain()` (65535-vertex chunk split) →
`GenerateBTR()` (NIF/BTR scene: land chunks + skirts + water nodes + bounds).

---

## 1. WaterDelta exact behavior (`TerrainLOD.cs`, `GenerateQuad`)

Settings: `WATERDELTALOD4/8/16/32` → `int[] waterDelta` (default **-1 = disabled**).
The whole block is skipped when `waterDelta[lodIndex] < 0`.

```csharp
if (waterDelta[quad.lodIndex] >= 0)
{
    float num15 = waterDelta[quad.lodIndex] / quad.lodLevel;  // per-level delta —
                                                              // TRUNCATING int div
    float num16 = num15 / 100f;                               // 1% of it
    // per heightfield sample (x,y):
    if (heightValues[x,y] >= waterheightValues[x,y])          // ABOVE water
        heightValues[x,y] += num16;                           // raised by delta/100
    else                                                      // BELOW water
    {
        allBelow = true; work();                              // 3x3 diagonal check
        num18 = waterheightValues[x,y] - num15;
        if (heightValues[x,y] > num18) heightValues[x,y] = num18;  // clamp DOWN
        if (allBelow && !(protectCellBorders && lodLevel==1 && border sample))
            list.Add(new Index2(x,y));                        // "unseen" set
    }
}
terrain.ScriptedPreInsertion(list, 0);                        // state 0 → Ignored;
                                                              // runs BEFORE the
                                                              // border-pin passes
```

- **Above-water samples are raised by `delta/100`** (1% of the per-level delta —
  the `if (h < h + num16)` guard is trivially true; it is not a clamp). The raise
  is a no-op when `num15 == 0` (`height + 0 < height` is false).
- **Below-water samples are clamped DOWN to `waterHeight − delta/lodLevel`**
  (never raised). The clamp fires only when the sample is *above* that target,
  i.e. when it sits within `delta/lodLevel` of the water plane; deeper samples
  keep their true height.
- **`waterDelta / lodLevel` is truncating integer division** — `waterDelta` and
  `quad.lodLevel` are both `int`, and the IL is `div` + `conv.r4` with no float
  cast first. So `num15` is an exact integer, and **small deltas silently
  vanish**: `WaterDelta=3` at LOD4 (÷4), `7` at LOD8, `15` at LOD16 and `31` at
  LOD32 all truncate to 0, making the height-modification block a no-op while
  the setting is still "on" (the underwater-vertex passes below still run).
- **`allBelow`** is computed by `work()`: scans only the **4 diagonal** neighbors
  (`num30 != x && num29 != y` — orthogonals are skipped); false if any diagonal
  neighbor is at/above water.
- The collected underwater samples are fed to `ScriptedPreInsertion(list, 0)` =
  `PointState.Ignored` — **the greedy reducer never spends a point on them**
  (and the diagonal check decides whether a below-water sample is "interior
  underwater" and gets ignored, or near the surface and stays eligible). This
  pass runs *before* the border-pin passes below, and a later state-1 pass
  upgrades an `Ignored` point back to `Used` (see border pins).
- **`WaterDelta=0` is NOT a no-op.** The whole block runs (`0 >= 0`), but with
  `num15 = 0`: above-water samples are untouched and the below-water clamp
  target is exactly `waterHeight` — a below-water sample is `< waterHeight` by
  definition, so **the clamp never fires and heights are never flattened**;
  underwater samples keep their true height. What *does* change is the vertex
  handling: interior underwater samples are marked `Ignored` (no vertices spent
  below water) and, at `lodLevel > 1`, underwater coarse-grid samples are forced
  `Used` for seam matching (see border pins). So **0 is "optimize unseen with
  zero height modification"** — `-1` is the true off switch. Truncated-to-zero
  deltas behave identically to 0 (see above).
- Errors: OOM → log "Set Optimize Unseen for LOD level N to off or use x64
  version" + exit **640**; other exceptions → "Error optimizing unseen triangles
  for LOD level N" + exit **646**.

### Border pins (same function)

After the delta block the greedy mesh is pre-seeded (all via `ScriptedPreInsertion`):

- `lodLevel == 1` (protectCellBorders / UNDER per-cell path): vertices of the
  previous-level geometry inside this cell → **Used** (forced); border samples at
  x==0 or y==0 (per cell, cols/rows 1..31) not covered by old geometry → **Ignored**.
- `lodLevel > 1`: the loop walks the coarse grid (every `lodLevel*2` step) and
  forces **`Used`** every sample on the quad outline (x==0||x==32||y==0||y==32)
  **plus, when `waterDelta >= 0` — including at `WaterDelta=0` and at deltas
  truncated to 0 — every coarse-grid sample whose height is below water**, even
  in the interior. This state-1 pass runs *after* the waterDelta Ignored pass
  and upgrades its `Ignored` marks back to `Used` (guard admits `Ignored`;
  state 1 → `Select()` + `Used`). Net effect: underwater coarse-grid samples are
  kept as forced vertices (seam matching at LOD resolution), while underwater
  fine-grid samples (not on the `lodLevel*2` lattice) stay `Ignored` and the
  greedy reducer spends nothing on them.

## 2. Hide quads (`HIDEQUADS`, `landFlags`, `IgnoreHide`)

The engine has no triangle-strip "hidden" representation — hiding is done through
**landFlags**, a per-cell int32 read from the `LODGen_Terrain_<ws>.bin` record
(x, y, waterHeight, landFlags, …). Bits 0–3 are the four quadrant-visible flags:

| bit | quadrant (cell-local 33×33 sample grid, `max/2` = 16) |
|---|---|
| 1 (0x1) | x < 16 && y < 16 (SW) |
| 2 (0x2) | x >= 16 && y < 16 (SE) |
| 4 (0x4) | x < 16 && y >= 16 (NW) |
| 8 (0x8) | x >= 16 && y >= 16 (NE) |

`InQuadrant(x, y, 32, landFlags)` returns true iff the sample lies in a flagged
quadrant; `landFlags == 0` → never hidden.

Read path (`ReadWorldData`):
```csharp
if (!hideQuads || IgnoreHide.Contains(x + "," + y))
    terrainDesc.landFlags = 0;      // HIDEQUADS=true + not in IgnoreHide → unhide all
if (ignoreWater || waterHeight > 16777216f) waterHeight = float.MinValue;
```
So `HIDEQUADS=true` **zeroes** the flags (nothing hidden) unless the cell is in
`IgnoreHide` (the engine's `IGNOREHIDE` key, a comma-space-separated "x,y" list)
— i.e. the GUI-side default is "show everything"; the quadrants become visible
only through the GUI's hide-quad UI, which writes the landFlags in the bin and
`IgnoreHide` cells get their flags preserved.

Where hidden quadrants actually affect geometry — two sites:

1. `ReadWorldData` (UNDER mode only): samples in a hidden quadrant get
   `vector.z -= 131072f` (sunk 131072 world units) and are excluded from the
   cell bbox.
2. `GenerateQuad` at `lodLevel == 1` (protectCellBorders or UNDER per-cell):
   `heightValues[x,y] = height - 131072f` for samples in hidden quadrants
   (131072 = 128×1024 — the standard "below-world" sentinel; a vertex 131072
   under the terrain is effectively invisible, never culled by the engine).

Triangles are therefore **never removed** for hidden quads in the BTR path; the
hidden region is sunk below the world. (`RemoveHiddenTerrain` does exist but only
runs for UNDER mode / TERRAIN lodLevel ≤ 4 and culls triangles with any vertex
`z < minTerrainZ/lodLevel` — the underside path, not the hide-quad path.)

## 3. Skirts and ZSHIFT

### 3.1 The per-level depth (`AddSkirts`, BTR path)

```csharp
float num = skirts;
if (num < 0f) num /= quad.lodLevel;              // absolute-z mode
else if (num > 0f) num -= (quad.lodIndex - 1) * 63;   // relative mode, decay
```
For the new-style `Skirts=256`: **LOD4=256, LOD8=193, LOD16=130, LOD32=67**
(256 − (lodIndex−1)·63). For `Skirts=-14098` (old-style): num = −14098/lodLevel,
and vertices are placed at **absolute z = num** (not `z − num`). `0` = off.

Topology (per boundary edge): for every triangle edge whose **both endpoints lie
on the same quad boundary** (x==0, x==4096, y==0, or y==4096 in quad-local space):
- two new vertices at `(x, y, z − num)` (relative mode) or `(x, y, num)`
  (absolute mode), appended to the vertex list; UVs copied from the edge endpoints;
- two new triangles forming a downward quad:
  - winding A: `(v2, v1, c)`, `(v2, c, c+1)` when the edge runs
    (x increasing along y=0) or (x decreasing along y=4096) or
    (y decreasing along x=0) or (y increasing along x=4096);
  - winding B (mirrored) otherwise.
- The geometry bbox is *not* grown for FO3 (`!IsMode("TerrainFO3")` guard).

Skirt vertices are added **after** the reducer runs, so they count against the
budget via the reserve (below).

### 3.2 The 2000-vertex reserve (`GenerateLOD`)

```csharp
if (skirts != 0f) { target[num] -= 2000L; }
if (target[num] < 512) { target[num] = 512L; }
```
With skirts on, the reducer budget is reduced by 2000 (floor 512) — the reserve
for the added skirt vertices.

### 3.3 ZSHIFT (`ZSHIFTLOD4..32` → `float[] zShift`)

Pure **node translation**: every terrain shape (BSTriShape/NiTriShape) and water
shape gets `translation.z = zShift[lodIndex]` (the land node also has the FO4
mid-height added, see 4.3). Nothing else changes (no vertex z edit). Defaults 0.

`REMOVEUNSEENZSHIFT` (`float num2`, default 0) is a different, object-LOD-side
value: `LODApp` applies `num2 / quadLevel` as a z-bias when comparing object
triangles against the terrain heightmap for unseen-face removal.

### 3.4 UNDER-mode skirt variants

`GenerateUndersideNIF` uses the neighbor-aware variants instead:
- LOD4: `AddSkirts3` — depth `num = skirts` (no decay), drops to
  `waterHeight/lodLevel` (positive skirts) or `landHeight` (negative/absolute);
  only edges lying on a boundary grid line (`j = 0 … 4096` step `4096/lodLevel`).
- LOD8/16/32: `AddSkirts2` — no drop (edge stays at terrain z), but only on
  quad sides that face a **missing neighbor quad**; skips vertices with summed
  incident angle > 181°; the two triangles are emitted with reversed winding
  when both endpoints are underwater.

## 4. FO4 terrain BTR geometry: the 32767 budget vs the 65535 chunk split

Two different caps are involved:

1. **The reducer point budget** (`MAXVERTSLOD*`, default 32767): the greedy
   `Terrain`/`GreedySubdivision` stops inserting points when
   `PointCount > target[lodIndex]` (after the 2000 skirt reserve and the 512
   floor). Points are grid samples of the heightfield, so this is a *sample
   count*, not triangles.
2. **The shape chunk cap**: `Game.MaxVertices()` = **65535** (ushort index
   limit) and `Game.MaxTriangles()` = **65535** for all non-FO4 modes,
   **0xFFFFFFFF (unlimited)** for FO4. In `GenerateTerrain`, after the greedy
   mesh is built, if `triangles.Count > MaxTriangles || vertices.Count >
   MaxVertices`, the mesh is split into successive `Geometry` chunks:

```csharp
// new chunk; per-triangle: remap vertices through a fresh Dictionary → indices restart at 0
for each triangle k:
    add triangle to geometry2 (local indices)
    if (k+1 == count || geometry2.triangles.Count+1 > MaxTriangles
                     || geometry2.vertices.Count+3 > MaxVertices)
        list.Add(geometry2);  // flush; start a new chunk with a fresh remap
```

- The split is **triangle-atomic** (never mid-triangle) and only on the vertex
  cap for FO4 (MaxTriangles is unlimited there).
- With the default 32767 budget the reducer output is far below 65535, so one
  chunk is the norm; multiple chunks appear only when (a) the user sets
  `MAXVERTSLOD*` above 65535, (b) skirts (or the old-geometry pinning) push a
  near-cap chunk over, or (c) `QualityLOD4=-1` (full-res path, no reducer).
- **Each chunk becomes its own `BSTriShape` (FO4/SSE) / `NiTriShape` (TES5/FO3)
  child of the root `BSMultiBoundNode` — the chunks are NOT
  `BSSubIndexTriShape`.** `BSSubIndexTriShape` in this engine appears only for
  LOD4 water and for object LOD (see §5, §6). The subindex mechanism that does
  apply to land is the per-shape `numSegments` list carried in every
  `BSTriShape`/`BSSubIndexTriShape` (written from the water/segment tables).

## 5. Land chunk / subindex layout for Bethesda version 130 (FO4)

`GenerateBTR` per quad:

- Header markers (NiFile string table + user versions):
  - FO4: `AddString("chunk")`, `AddString("Land")`, `SetUserVersion2(130)`.
  - SSE: `AddString("chunk")`, `AddString("land")`, `SetUserVersion2(100)`.
  - FO3: `SetUserVersion(11)`, `SetUserVersion2(34)`, no "chunk"/"land" strings.
- Block order: `BSMultiBoundNode` ("chunk", cullMode 1) →
  per land chunk `BSTriShape`/`NiTriShape` (translation (0,0,zShift), scale
  lodLevel; FO3: translation (x·4096, y·4096, zShift), no scale, flags 2062/8) →
  optional `BSMultiBoundNode` ("WATER", cullMode 1) →
  per water height its shape (see §6) → `BSMultiBound`+`BSMultiBoundAABB`
  bounds (land from chunk[0] bbox; water from `bbWater`, both × lodLevel).
- Land texture set: `Data\Textures\Terrain\<ws>\<ws>.<lod>.<x>.<y><season>.dds`
  (diffuse) + `_msn.dds` (FO4) / `_n.dds` (SSE/TES5); FO3 uses
  `Data\Textures\Landscape\LOD\<ws>\Diffuse\...` with 6 textures (normals via a
  `Diffuse`→`Normals` regex replace).
- Shader: `BSLightingShaderProperty` shaderType **18** (FO4 terrain),
  flags1 `2151682048` (0x80401000), flags2 3; UNDER mode ORs 0x200 into flags1
  and 0x10 into flags2; glossiness 1.0 for TES5/SSE else 0.
- The `Land`/`land`/`chunk` strings are what the game's loader keys on, and
  userVersion2 130 = FO4 (see `XLODGEN_OUTPUT_FORMAT.md` §2/§4 for the exact
  header bytes).

### 5.1 FO4 vertex flags / data (`BSTriShape.Write`, "VTXT" layout)

Per-vertex component flags (`vertexFlags` ushort):

| flag | data | storage |
|---|---|---|
| 0x10 | position + bitangent.x | **4× int16 half** (FO4, no 0x4000); **4× float32** when userVersion2==100 (SSE) or 0x4000 set |
| 0x20 | UV (u,v) | 2× int16 half |
| 0x80 | normal + bitangent.y | 4× byte (`b/255*2−1`) |
| 0x100 | tangent + bitangent.z | 4× byte |
| 0x200 | vertex colors | 4× byte /255 |
| 0x400 | skinning | 4× int16 + 4× byte |
| 0x1000 | eye data | int32 |

- `Utils.FloatToShort` is a **half-float (float16) conversion with round-to-nearest**
  (`(bits & 0x7FFFFFFF) + 0x1000`, overflow at 65536.0f) — positions/UVs are
  quantized to half precision unless SSE or the 0x4000 flag.
- `vertexSize` (byte) = floatSize(4 or 2) + 1 per extra component set;
  `floatSize` = 4 for SSE/0x4000 else 2.
- The 4th position float / normal w / tangent w are packed into a per-vertex
  **bitangent** `(x,y,z)` vector (never written separately for terrain).
- **Terrain chunks zero `vertexFlag3/4/5/8`** (`if (Game.IsMode("terrain"))` —
  substring match, true for all Terrain* modes).
- `dataSize = vertices × (floatSize*4 + 4·(UV?1:0) + 4·(norm?1:0) + 4·(tan?1:0)
  + 4·(col?1:0) + 12·(skin?1:0) + 4·(eye?1:0)) + triangles × 6`.
  The FO4 `ToBSSubIndexTriShape` pre-computes `vertices*20 + triangles*6`
  (position+UV only) before the per-flag loop corrects it.
- The **0x4000 (16384) flag** is set when the FO4 land chunk's height span
  `bbox.pz2 − pz1 > 131008` — forcing float32 positions (full precision for
  extreme-height worldspaces).
- FO4: `numTriangles` written as **uint32**, `numVertices` as ushort; SSE: both
  ushort.
- The 4th-position float in the read path is the bitangent.x; the FO4 `ShiftZ`
  compression (`Geometry.ShiftZ`) centers the chunk's z:
  `mid = pz1 + span/2; vertices.z -= mid; return mid * lodLevel` — the returned
  world-space mid-height is added to the node translation and `center.z = 0`.

## 6. Water node geometry (`GenerateWater`)

`GenerateWater(quad, list[0], out bbWater, out waterTriangles)` — note it
triangulates against **chunk 0** of the land mesh. Returns
`Dictionary<float, Geometry>` keyed by water height.

Grouping keys:
- **FO3**: key = 1 if cell waterHeight == global waterHeight else 0 (two groups).
- **Non-FO3, LOD4**: key = the cell's **exact water height** → one shape per
  unique height (the "grouping by unique water height"; `OrderBy(key)` ascending
  when writing).
- **Non-FO3, LOD8/16/32**: key = **0 for every cell** → all water is merged into
  a single shape per quad at higher LODs.
- A cell is skipped when waterHeight is NaN, `waterHeight <= landHeight` (bbox
  pz1), or `waterHeight >= 16777216`.

Per cell: 4 corner vertices at z = `waterHeight/lodLevel` (FO3: raw
waterHeight, quad size `num = 4096`; others `num = 4096/lodLevel`) + 2 triangles
`(c, c+1, c+2)`, `(c+1, c+3, c+2)`; UV = raw x/y; `waterTriangles += 2`;
`bbWater` grown. At LOD4 the cell is registered in a **segment table**:
`segments[cellX_local*lodLevel + cellY_local] = { startTriangle*3, 2 }`
(segment id = x-local·lodLevel + y-local, wrap-around mod lodLevel); each water
geometry pre-seeds all 16 `SegmentDesc`s.

Shape selection per group (`GenerateBTR`):
- **LOD4 water, FO4/SSE**: `BSSubIndexTriShape` via
  `ToBSSubIndexTriShape(false, false, radius:true, lodLevel, (0,0,zShift))`;
  `SetSegments(segments, lodLevel)` fills **lodLevel² subindices** and trims
  trailing empty ones; FO4 adds a `BSEffectShaderProperty` (flags1 0x80000000,
  flags2 1, clamp 65283, emissive white, softFalloff 100, envScale 1).
- **LOD4 water, TES5/FO3**: `BSSegmentedTriShape` with the same segment table;
  FO3 pads to ≥ 3 segments.
- **LOD8/16/32 water, FO4/SSE**: plain `BSTriShape`; **TES5/FO3**: `NiTriShape`.
- The special-water path (`SPECIALWATER`, FO4) builds per-cell quads **clipped
  against the land mesh**: edges crossing the water plane are intersected
  (`t = (w − z0)/(z1 − z0)`), triangles whose centroid is below the land are
  dropped (`GetTriangleHeight`), then merged into the key-0 group.

`BSSITSSegment` (BSSubIndexTriShape): `triangleOffset, triangleCount,
unknownHash=0xFFFFFFFF, numSegments=0, subSegment=-1` — written per subindex.

## 7. The mesh reducers

### 7.1 Greedy subdivision (`LODGenerator.Terra` + `.Greedy`)

- `Map` = the heightfield (size = `32*lodLevel+1`); `Eval` returns the raw
  sample (no interpolation).
- `GreedySubdivision` seeds the 4 corners (PointCount = 4) and the two initial
  triangles; every face is a `TrackedTriangle` in a **binary max-heap** keyed by
  `Importance`.
- `ScanTriangle(tri)`: fits a plane through the triangle's 3 corner heights;
  rasterizes the triangle by rows (points sorted by **y only**); for each sample
  not Used/Ignored computes `importance = |height[x,y] − plane[x,y]|`
  (vertical error); `Candidate.Consider` keeps the **first strictly-greater**
  sample (ties → the earliest in scanline order: y ascending, then x ascending).
  If max importance < 0.0001 the triangle's heap entry is killed
  (token −47) and candidate set to (−69,−69).
- `GreedyInsert()`: extract the heap root → `Select(x,y,tri)` → mark Used,
  PointCount++, `Insert` (point-location + fan + edge flips, §7.2).
- Loop (`Terrain.Triangulate`): `while (!GoalMet() && GreedyInsert())` where
  `GoalMet()` = `MaxError() < ErrorThreshold` (heap root importance) **or**
  `PointCount > PointCountLimit`. `ErrorThreshold` = QUALITYLOD value (divided by
  4 for LOD4 with ProtectCellBorders); `PointCountLimit` = the budgeted
  MaxVerts.
- **Tie-breaking in the heap**: strict `>` comparisons only — equal-importance
  nodes are never swapped, so ordering is stable by heap/token position; the
  heap root among equals is the one that got there first. `Update` re-heaps on
  importance change.
- Output: `GenerateOutput` walks faces and emits vertices with z snapped to the
  **original heightfield** (`heightMap.Eval`), counter-clockwise winding enforced
  (`IsCounterClockwise` → swap two indices), then `CreateGeometry` **re-sorts
  vertices by `y*4096 + x`** (y-major, ascending) and re-indexes triangles —
  a deterministic vertex order independent of insertion order.
- The `PointStates` matrix drives everything: `Unused` → eligible,
  `Used` → inserted (never re-inserted), `Ignored` → never inserted (the
  WaterDelta/border machinery, §1/§2).

### 7.2 Delaunay insertion / edge-flip ordering (`Subdivision`, quad-edge)

- `Insert(point, tri)`: `Locate` (walk from the hint triangle's anchor), `Spoke`
  (fan-connect the new point), `Optimize(point, edge.Sym)`.
- `Optimize` walks the ring of edges around the new point: for each interior
  edge with `ShouldSwap` true → `Swap` (edge flip) and **retry the same edge
  without advancing** (cascading flips); then every adjacent face is re-scanned.
- `ShouldSwap(point, edge) = IsInCircle(edge.Orig, oPrev.Dest, edge.Dest, point)`
  — the classic **in-circle test**, `det > 1e-6f` (strict epsilon; not `>= 0`).
  Orientations elsewhere use `TriArea` promoted to **double** for the
  comparison; the in-circle test itself is all-float.
- `Locate` walk (Guibas–Stolfi style): when the point lies exactly **on the
  current edge** (`num2 == 0 && num3 == 0`, outside the boundary special-case),
  the next step is chosen by `(rand.Next() & 1) > 0` — a **random pick between
  the two sides** (`Random` is a .NET `new Random()` seeded from the clock).
  **Consequence for parity:** on-edge greedy insertions (common on the integer
  sample grid — e.g. a candidate at the exact midpoint of a grid-aligned edge)
  produce a *non-deterministic* triangulation. Replicating xLODGen exactly is
  therefore only possible if the C++ port reproduces the .NET `Random` sequence
  or avoids the ambiguous walk (e.g. by preferring one side); triangle-for-
  triangle parity is otherwise best-effort.

### 7.3 The standalone `DelaunayTriangulator` (water/object hulls)

Used only by `Geometry.CreateTriangles` (called from `GenerateWater`'s
special-water path with `hull: true`, and from object-LOD flat/shadow paths).
- Points are sorted **counter-clockwise around their centroid**
  (`Vertex.SortCounterClockwise`), deduped via a SortedDictionary.
- `Triangulator.Triangulation`: points pre-sorted by **squared distance from
  points[0]**; seeds the initial triangle from the smallest circumcircle; then
  incremental insertion with `FlipTriangle` (in-circle flips) bounded by
  `num2 < 1000` then `num2 < 2000` passes — a **flip iteration cap**, so
  pathological/degenerate sets can stop before full Delaunay.
- With `hull: false` (object paths) the point set must lie entirely on its
  convex hull or the call returns false (no geometry).
- Triangle emission order = the flip-settled triad list order; winding is then
  fixed against the UV direction (`front` flag) in `CreateTriangles`.

---

## Quick reference: where each setting lands

| setting | consumed at | effect |
|---|---|---|
| `WATERDELTALOD4..32` | `GenerateQuad` | above-water +delta/100; below-water clamp to water−delta; interior underwater samples Ignored; border underwater Used; ≥0 only, −1 off |
| `HIDEQUADS` | `ReadWorldData` | zeroes `landFlags` unless cell in `IGNOREHIDE`; hidden quadrants sunk −131072 at lodLevel 1 |
| `IGNOREHIDE` | `ReadWorldData` | comma-space "x,y" list preserving landFlags |
| `SKIRTS` | `GenerateLOD`/`GenerateBTR`/`GenerateUndersideNIF` | 2000-vertex reserve, 512 floor; `AddSkirts` depth 256/193/130/67 (or absolute `skirts/lodLevel`); UNDER uses AddSkirts2/3 |
| `ZSHIFTLOD4..32` | `GenerateBTR` | node translation.z only |
| `REMOVEUNSEENZSHIFT` | `LODApp` | object-LOD heightmap z-bias (`/quadLevel`) |
| `MAXVERTSLOD4..32` | `Terrain` | greedy point budget (after reserve/floor) |
| `QUALITYLOD4..32` | `Terrain` | error threshold (`÷4` at LOD4 with ProtectCellBorders); `-1` = full-res, no reducer |
| `PROTECTCELLBORDERS` | `GenerateTerrain`/`GenerateQuad` | LOD4 per-cell generation + border pinning + error ÷4 |
| `SPECIALWATER` | `GenerateWater` | land-clipped water quads into key-0 group |
| `IGNOREWATER` | `ReadWorldData`/`GenerateWater` | waterHeight = float.MinValue; no water shapes |

Evidence: `LODGenerator/TerrainLOD.cs` (GenerateQuad 313–537, ReadWorldData
182–262, AddSkirts 581–637, AddSkirts2 638–707, AddSkirts3 708–776,
GenerateTerrain 784–837, GenerateWater 839–1090, GenerateBTR 1339–1690,
GenerateLOD 1705–1908), `LODGenerator/Geometry.cs` (CreateGeometry 274,
ToBSSubIndexTriShape 217, ShiftZ 2176, CreateTriangles 1538),
`LODGenerator.NifMain/BSTriShape.cs` (Write/vertex data 233–448, GetDataSize
450–488), `LODGenerator.NifMain/BSSubIndexTriShape.cs` (SetSegments 160–181),
`LODGenerator.Terra/Terrain.cs`, `LODGenerator.Terra.Greedy/GreedySubdivision.cs`,
`LODGenerator.Terra/Subdivision.cs` (Locate/Spoke/Optimize/Swap 42–341),
`LODGenerator.Terra.Memory/Heap.cs`, `DelaunayTriangulator/Triangulator.cs`,
`LODGenerator.Common/Game.cs` (MaxVertices/MaxTriangles 48–60),
`LODGeneratorCMD/Program.cs` (key parsing).
