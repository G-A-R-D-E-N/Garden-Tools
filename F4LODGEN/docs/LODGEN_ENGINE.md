# LODGen.exe (the .NET engine) internals

Research date: 2026-08-18

This document records the internals of the console engine shipped as `Edit Scripts\LODGen.exe` / `LODGenx64.exe` next to xLODGen. The Delphi GUI described in [XLODGEN_REVERSE_ENGINEERING.md](XLODGEN_REVERSE_ENGINEERING.md) exports game data and spawns this process; this engine does the actual mesh/texture generation. Companion docs: [XLODGEN_OBJECT_LOD.md](reverse-engineering/XLODGEN_OBJECT_LOD.md) (object pipeline), [XLODGEN_OUTPUT_FORMAT.md](reverse-engineering/XLODGEN_OUTPUT_FORMAT.md) (BTR/NIF bytes), [XLODGEN_ATLAS_TEXTURES.md](reverse-engineering/XLODGEN_ATLAS_TEXTURES.md) (atlas/texture flow), [XLODGEN_QUALITY.md](XLODGEN_QUALITY.md) (Quality/MaxVerts reconciliation). It is a **Mono/.NET assembly** (C#, compiled as `LODGenx64`), fully decompiled with ilspycmd 11 into readable C#. It is reference material for the compatibility-oracle approach — F4LODGen targets equivalent observable behavior.

## What it is

- **LODGeneratorCMD** (entry assembly): thin command-line front end.
- **LODGenerator** core: object LOD (`LODApp`), terrain LOD (`TerrainLOD`), terrain data reader (`TerrainData`), NIF writing, atlas/flat texture handling, BSA archive reading.
- **LODGenerator.Common**: `Game` (mode/global state), `CmdArgs`, `LogFile`, math/geometry types, `BSAArchive`.
- **LODGenerator.NifMain**: NIF block classes (BSMultiBoundNode, BSTriShape, NiTriShapeData, BSLightingShaderProperty, ...).
- **LODGenerator.Terra + Terra.Greedy/Geometry/Maps/Memory**: the terrain mesh simplifier (greedy heightfield triangulation).
- **DelaunayTriangulator**: the underlying Delaunay mesh code.

Namespaces and class names confirm the source layout (e.g. `TerrainLOD.cs`, `TerrainData.cs` in the LODGen repository).

## CLI and input file

The process is invoked as:

```
LODGenx64.exe [--switch value ...] [inputfile]
```

- `--logfile <path>` — log destination (default `LODGen_log.txt` in the current directory; written via the bundled `LogFile` with exit-code-suffixed closes).
- `--inputfile <path>` — the settings/reference file; the first positional argument is used as the fallback.
- Boolean switches: `--gamemode <mode>`, `--dontMergeShapes`, `--removeUnseenFaces`, `--ignoreWater`, `--ignoreMaterial`, `--usehdlod`, `--globalScale <float>`, `--verbose`, `--LODLevel <int>`, `--x <int>`, `--y <int>`.
- Duplicate switches abort with exit code 665; malformed ones exit 666.

The input file has two parts:

1. **Header:** `KEY=VALUE` lines (`=` split on the first occurrence; empty values skipped). This is the settings contract written by the Delphi GUI (see the `.dlodsettings` keys in XLODGEN_REVERSE_ENGINEERING.md).
2. **Body:** one **TAB-separated static reference descriptor per line**, parsed in parallel into `StaticDesc`. Columns (documented from the parser):

```
0  refID (hex string)         1  refFlags (hex)            2  x          3  y          4  z
5  rotX   6  rotY   7  rotZ   8  scale                     9  staticName (LOD model path)
10 staticFlags (hex)          11 materialName              12 staticFullModel
13..16 LOD4/8/16/32 model paths (13..15 if no 4th slot; LEVEL32 mode may copy slot 3 to 4)
17 enableParent (hex)         18 material-swap sources (csv)  19 material-swap targets (csv)
20..26 optional transform: translation XYZ, rotation XYZ (deg), scale
```

Cells are 4096 game units; `cellX = floor(x / 4096)`. Grass is detected by material name containing "grass" with a numeric density in `staticName`.

## Game modes

`Game.Mode` is set from the `GameMode=` key and matched case-insensitively as a substring. Mode aliases:

| Mode key | Internal mode |
|---|---|
| `TES5`, `SSE`, `FO3`, `FO4` | object LOD generation (`LODApp.GenerateLOD`) |
| `TERRAINTES5`, `TERRAINSSE`, `TERRAINFO3`, `TERRAINFO4` (+ VR/Enderal aliases) | terrain LOD meshes (`TerrainLOD.GenerateLOD`) |
| `UNDERTERRAIN*` | terrain underside meshes (`GenerateUnderside`) |
| `FLATTES5`, `FLATSSE` | flat terrain LOD (`TerrainLOD.GenerateFlat`) |
| `TEXTURES` | build the texture list (`LODApp.TexturesList`) |
| `TEXTURESLIST` | emit a textures-list file |
| `HYBRID` | batch NIF processing from an input folder |
| `REVERSEATLAS` | reverse atlas mapping |
| `BTR2RAW` | convert a BTR to raw height data (`BTR2RAW`; see [BTR2RAW mode](#btr2raw-mode-btr-to-raw-heightmap)) |
| `BTR2TEST` | terrain data self-test (`TerrainData.GenerateTerrainDesc`) |

## Full settings key list (from the engine parser)

Defaults are shown where meaningful; per-LOD keys use indices 4/8/16/32.

```
WORLDSPACE=            CELLSW=<x y>        MAXLEVEL=32          SEASON=
PATHDATA=              PATHOUTPUT=         PATHINPUT=           PATHGRASS=
GRASSGID=              GRASSMAP=           TEXTURESLISTFILE=    TEXTUREATLASMAP=
FLATNIF= / FLATTEXTURES=  ALTTEXTURES=     TERRAINDATA=
RESOURCE=<bsa path>    GAMEMODE=
GLOBALSCALE=1          SCALEXY=1           LANDHEIGHT=0         WATERHEIGHT=0
SKIRTS=256             DEFAULTALPHATHRESHOLD=128
QUALITYLOD4..32        MAXVERTSLOD4..32    WATERDELTALOD4..32    ZSHIFTLOD4..32
PROTECTCELLBORDERS=    HIDEQUADS=          IGNOREHIDE=          LODCOORDS4..32=
IGNOREWATER=           SPECIALWATER=       CREATEHEIGHTMAP=
LEVEL32=               LEVEL8=             EXPERIMENTAL=        REMOVEBLOCKS=
THREADS=               THREADSPLIT=        RESERVEMEM=          VERBOSE=
MINX= MINY= MAXX= MAXY=  LODLEVEL=         ATLASTOLERANCE=0.2
DONTGENERATEVERTEXCOLORS=  GENERATEVERTEXCOLORS(+LOD4/8/16/32)=  MERGEVERTEXCOLORS(+LOD)=
DONTGENERATETANGENTS=  GENERATETANGENTS(+LOD)=  FIXTANGENTS(+LOD)=  DONTFIXTANGENTS=
DONTMERGESHAPES=       DONTGROUP=          REMOVEUNSEENFACES=    REMOVEUNSEENZSHIFT=0
REMOVEUNSEENGRASS=     USEHDFLAG=          USEOPTIMIZER=         IGNOREMATERIAL=
ALLDOUBLESIDED=        ALPHADOUBLESIDED=   USEALPHATHRESHOLD=    USEBACKLIGHTPOWER=
USEDECALFLAG=          FLATLODLEVELLODFLAG=  IGNORETRANSLATION=
ISHDMESHMASK= NOTHDMESHMASK= PASSTHRUMESHMASK= ISHDTEXTUREMASK= TEXTUREDIFFUSEHD=
NOTHDTEXTUREMASK= DOUBLESIDEDMESHMASK= DOUBLESIDEDTEXTUREMASK=
NOTLODFLAGMESHMASK(+LOD4/8/16/32)= NOTTANGENTSMESHMASK(+LOD4/8/16/32)=
GRASSBRIGHTNESSTOPR/G/B= GRASSBRIGHTNESSBOTTOMR/G/B= COMPLEXGRASSBRIGHTNESSTOP/BOTTOM R/G/B=
CROWNBRIGHTNESSR/G/B= TRUNKBRIGHTNESSR/G/B= FLATTRUNKBRIGHTNESSR/G/B=
GRASSLARGEREFERENCE= IGNOREGRASSPLACEMENTBRIGHTNESS= IGNORECOMPLEXGRASSPLACEMENTBRIGHTNESS=
GRASSDENSITY= (percent, clamped 0..1)  GRASSMODE=
```

Engine-internal defaults relevant to terrain: `QUALITYLOD*` default **{10, 15, 20, 25}**; `MAXVERTSLOD*` default **32767**; `WATERDELTA*` default **-1** (disabled); `SKIRTS` 256 (2000 vertices reserved for skirts, target never below 512); `PROTECTCELLBORDERS` default true. Note: the GUI always writes explicit values, so these are only fallbacks.

## Terrain mesh generation flow

### 1. Terrain data file (`TERRAINDATA=`)

Binary, one record per CELL:

```
int32  cellX, cellY
float  waterHeight            (>16777216 treated as "no water")
int32  landFlags              (hide-quad flags)
float  baseHeight             (×8 = starting Z)
1089 × int8  height deltas    (33×33 grid, ×8 units; per-row delta encoding:
                               first sample of a row advances baseHeight, rest accumulate)
int16 + int8  trailer
```

Cell size 4096 units, vertex spacing 128 units (33×33 samples per cell). Missing cells fall back to `LANDHEIGHT`/`WATERHEIGHT` defaults.

### 2. Quad building (`TerrainLOD.GenerateLOD` → `GenerateQuad`)

- Iterates LOD levels 32 → 4 (`level = 4 << index`), skipping levels above `MAXLEVEL`; optional single-level (`LODLEVEL=` / `--LODLevel`) and single-quad (`--x`/`--y`) modes.
- Merges the per-cell 33×33 height grids into a `(32*level+1)²` quad grid, scaling heights by `1/level` into quad-local units.
- `WATERDELTA` ≥ 0 performs the "Optimize Unseen" water adjustment on the heightfield.
- `QUALITYLOD* == -1` (LOD4 only) switches to the pre-triangulated full-resolution cell geometry (built during `ReadWorldData`).

### 3. Simplification (`LODGenerator.Terra`)

This is the algorithm behind the `Quality`/`MaxVerts` contract that [XLODGEN_QUALITY.md](XLODGEN_QUALITY.md) models:

- `Terrain(errorThreshold, pointCountLimit, heightValues)` wraps a `GreedySubdivision` mesh.
- The mesh starts as one quad (4 corner points) over the heightfield and refines by **greedy insertion**: every triangle computes a best-candidate point (the uninserted grid sample with the largest absolute deviation `|height - plane|` from the triangle's plane) and stores it in a max-heap keyed by that error.
- `GreedyInsert()` repeatedly takes the highest-error triangle and inserts its candidate point (Delaunay-style subdivision); neighbors re-scan for new candidates.
- Stopping rule (`GoalMet`): stop when `MaxError() < errorThreshold` **or** point count exceeds `PointCountLimit`. So `Quality` is the maximum permitted vertical (Z) deviation in quad-local units, and `MaxVerts` is the hard budget — exactly the semantics documented in XLODGEN_QUALITY.md.
- With `PROTECTCELLBORDERS` on LOD4, the quality threshold is divided by 4 and each cell quad is generated and stitched separately (`GenerateTerrain`/`AddScaled`) so shared cell borders stay dense; borders at block edges are always kept via mesh constraints.
- Output is triangulated (`GenerateOutput`) into vertices + indices with UVs (`u = x/4096`, `v = 1 - y/4096`).

### 4. BTR/NIF emission (`GenerateBTR`)

- Splits the quad into sub-geometries when vertex/triangle limits are exceeded: `MaxVertices` = 65535 everywhere; `MaxTriangles` = 65535, unlimited for FO4.
- Builds the NIF scene: `BSMultiBoundNode` root, `BSMultiBoundAABB` bounds, terrain `BSShaderTextureSet` (`Data\Textures\Terrain\<worldspace>\<worldspace>.<lod>.<x>.<y><season>.dds`, `_n.dds` normals for TES5/SSE, `_msn.dds` for FO4), `BSLightingShaderProperty` (shader type 18), per-level geometry (`BSSegmentedTriShape` at LOD4, `NiTriShape` above; `BSTriShape` for SSE/FO4), optional water geometry (`WATER` node, per-water-height quads), skirts, and `ZSHIFT`.
- Per-game NIF version quirks: SSE userVersion2=100, FO4 userVersion2=130 + `Land` chunk string, FO3 userVersion=11/34 + `.nif` files instead of `.btr`.
- Output file: `<worldspace>.<lod>.<x>.<y><season>.btr` (TES5/SSE/FO4), `<worldspace>.Level<lod>.X<x>.Y<y><season>.nif` (FO3).

### 5. Water and culling

- Water planes are generated per unique water height; water triangles are kept for the quad, and `REMOVEUNSEENFACES` + non-ignored water removes terrain faces below water (`RemoveUnseenTerrain`) after an optional Z-shift.
- `UNDERTERRAIN*` modes generate underside geometry (terrain flipped and lowered, with neighbor-aware `LODCOORDS`/`IGNOREHIDE` handling) and emit separate `Under ...` meshes.

## BTR2RAW mode (btr to raw heightmap)

`GameMode=BTR2RAW` converts the worldspace's LOD4 terrain `.btr` quads into a single flat `.raw` heightmap. It is the only mode whose `TERRAINDATA=` value is an **output base name, not an input file** (the terrain-mode readers consume the 1112-byte `.bin`; see [section 1](#1-terrain-data-file-terraindata)).

### Dispatch (Program.cs:978-985)

```csharp
if (Game.IsMode("btr2raw"))
{
    text11 = AppDomain.CurrentDomain.BaseDirectory + text11;
    Game.logFile.WriteLog("Generating BIN from BTR " + text2);
    BTR2RAW bTR2RAW = new BTR2RAW(gameDir, text2, text11, southWestX, southWestY);
    bTR2RAW.Generate();
    Game.logFile.Close(0);
    return 0;
}
```

- Runs after the required-key checks but before every side-file reader (atlas/flat/terrain-bin — none apply here).
- `text11` is the `TERRAINDATA=` value **prepended with the exe's BaseDir via literal string concat** — a relative value resolves to `Edit Scripts\`, and an *absolute* value (including a `Z:\` drive path) gets mangled into `Edit Scripts\Z:\...` and dies with an unhandled `DirectoryNotFoundException` (verified live: the log shows the mangled path and the process crashes). There is no `Path.Combine`/`IsPathRooted` handling here.
- Always closes with exit code 0 on the normal path.

### Size derivation (`BTR2RAW.Generate`)

With `quadLevel = 4` hardcoded in the constructor:

```csharp
int cellSize = 4096 / quadLevel;   // 1024
int samples  = 128 / quadLevel;    // 32
int lengthX  = 1 + Math.Abs(southWestX) * samples * 2;   // 1 + |southWestX| * 64
int lengthY  = 1 + Math.Abs(southWestY) * samples * 2;   // 1 + |southWestY| * 64
```

- `southWestX/Y` come from `CELLSW=<x y>`. Log line: `Size <lengthX> x <lengthY>` (e.g. `CELLSW=-4 -4` → `Size 257 x 257`).
- The `×2` follows from the loop span: quad columns iterate `i` over the full `[southWestX, |southWestX|)` range — `2·|southWestX|` cells wide, i.e. `2·|southWestX|/quadLevel` columns. Each column maps to `quadLevel·32 = 128` array nodes plus a shared boundary node with its neighbor, so `lengthX = 1 + (2·|southWestX|/quadLevel)·(quadLevel·32) = 1 + |southWestX|·64`. The destination grid therefore spans `2·|southWestX|` cells per axis at a nominal 128-unit node pitch; only the LOD4 quads themselves carry data.

### Heightmap layout

- Backing store is `float[lengthX, lengthY]`, **zero-initialized** — any node never touched by a quad stays `0.0f`.
- Quad iteration (inside `Parallel.For` over `yy` in `[southWestY / quadLevel, |southWestY| / quadLevel)`, then `i` in `[southWestX, |southWestX|)` step `quadLevel`): for each loaded quad, the loops `j,k ∈ {0, 1024, 2048, 3072}` × `l,m ∈ [0, 1024]` step 32 sweep the quad's `[0, 4096]²` space at 32-unit pitch — 129×129 samples. Each sample writes:

```csharp
heighData[lengthX / 2 + i * 32 + num4 / samples,
          lengthY / 2 + num3 * 32 + num5 / samples] = triangleHeight * quadLevel;
```

  where `num4 = k + l`, `num5 = j + m`, `num3 = yy * quadLevel`, and `triangleHeight` is the barycentrically interpolated z of the first triangle containing the (x,y) sample (2D point-in-triangle test); samples not inside any triangle fall back to a ±1-unit probe then `float.MinValue`.
- Consecutive quads land adjacent in the array: quad `(i, ·)` spans nodes `lengthX/2 + i·32` … `lengthX/2 + i·32 + 128`; quad `(i+quadLevel, ·)` starts at `lengthX/2 + (i+quadLevel)·32 = lengthX/2 + i·32 + 128`, so the 129th node is the shared boundary written by both quads (guarded by a lock — whichever thread writes last wins).
- **Output filename** (log: `Filename: <text>`): `binFilename + "_" + lengthY + "x" + lengthY + ".raw"` — note **`lengthY` is used for *both* axes in the name**, so with `|southWestX| ≠ |southWestY|` the filename misreports the X dimension.
- **File bytes**: `BinaryWriter` over `new FileStream(text, FileMode.Create)` (truncate/create; a missing parent directory is the unhandled crash above). Traversal is x-major (`for x { for y { Write((int)heighData[x, y]) } }`), each value a **little-endian int32** via `(int)` cast — C# float→int **truncates toward zero**, no rounding. `float.MinValue` casts to `-2147483648`. Total size `lengthX · lengthY · 4` bytes (verified live: `257²` → 264,196 bytes).

### Input quads (`LoadTerrainQuad`)

- Reads `meshes\terrain\<ws>\<ws>.4.<x>.<y>.btr` (per disk `gameDir + path`, else `BSAArchive.FileExists` over the `RESOURCE=`-loaded archives). The `Game.Mode == "fo3"` branch (`meshes\landscape\lod\...nif`) is **dead code** — modes are uppercased and `"fo3"` compares case-sensitively.
- The file must be a LOD4 terrain BTR: block 0 `BSMultiBoundNode`, child 0 `BSTriShape` (SSE/FO4) or other `NiTriBasedGeom`. Anything else (e.g. an object-LOD `.bto` with a `BSSubIndexTriShape` root) throws an unhandled `InvalidCastException` (verified live).
- **Boundary-triangle filter**: triangles whose three vertices all sit on `x == 0`, `x == 4096`, `y == 0`, or `y == 4096` in the quad's local space are removed (the skirt/boundary faces). Each surviving triangle's 3 vertices are appended unshared (`Triangle(count, count+1, count+2)`), with a precomputed plane for the point-in-triangle test.
- Missing quads are **silent** (verbose-only `terrain file not found`, no exit code) — the region stays 0.0f. With a non-negative `CELLSW` the quad range is empty and `Parallel.For` runs zero iterations → an all-zeros `.raw`.
- `removeUnderwaterFaces`/`removeUnseenZShift` are constructor fields but **never read** by `Generate()`.

## Object LOD flow

`LODApp.GenerateLOD(statics)` consumes the parsed `StaticDesc` list, resolves each reference's LOD model chain, merges compatible material groups into combined NIFs, applies tangents/vertex-color/alpha/backlight policies, performs terrain-based culling where configured, and writes object LOD BTO files; `WriteMHDT()` emits the multi-bound height-data table. Texture stitching/atlasing is handled by the same process using `TEXTUREATLASMAP`/`FLATTEXTURES` inputs (atlas UV rects, tolerance-based packing, HD texture sets, alpha thresholds). Full walkthrough: [XLODGEN_OBJECT_LOD.md](reverse-engineering/XLODGEN_OBJECT_LOD.md); file formats: [XLODGEN_OUTPUT_FORMAT.md](reverse-engineering/XLODGEN_OUTPUT_FORMAT.md); atlas details: [XLODGEN_ATLAS_TEXTURES.md](reverse-engineering/XLODGEN_ATLAS_TEXTURES.md).

## Relevance to F4LODGen

- The `Quality`/`MaxVerts`/`ProtectCellBorders` semantics implemented in `LODGenerator.Terra` are the compatibility target for F4LODGen's terrain reducer (XLODGEN_QUALITY.md); the greedy insertion algorithm is a concrete reference for what "Quality = Z deviation" means in implementation terms.
- The `GameMode` alias table and the `.btr`/NIF naming and shader-set conventions are the compatibility contract for output placement (`Data\Textures\Terrain\...`).
- The static-reference line format is the reference implementation's data contract for object LOD inputs — useful for F4LODGen's own object-pipeline record extraction.
- The terrain data binary format (33×33 sbyte heightfield per cell, 128-unit spacing, delta encoding) is the engine's expected terrain input; F4LODGen's record resolver can feed an equivalent internal structure without needing the intermediate file.

## Reproducing the decompilation

```
dotnet tool install --tool-path /tmp/dotnet-tools ilspycmd
/tmp/dotnet-tools/ilspycmd -p -o /tmp/lodgen-src "Edit Scripts/LODGenx64.exe"   # point at the LODGenx64.exe of an xLODGen install
```

(Set `DOTNET_ROOT` if the apphost cannot find the runtime.) The x86 `LODGen.exe` decompiles to the same sources.
