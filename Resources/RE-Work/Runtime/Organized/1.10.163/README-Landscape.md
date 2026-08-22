# Cells and exterior worldspace / landscape -- catalogued

RE pass on Fallout 4's cell and exterior terrain system: `TESObjectCELL`, the interior/exterior
split, `TESWorldSpace`, and `TESObjectLAND`'s on-disk VHGT heightmap format decoded and confirmed
against the running engine. Built from CommonLibF4 header cross-referencing plus Ghidra decompiles
against 1.10.163 (OG). Written for a Godot-port reference (the same audience as
`Player-Controller-Notes`), not for any one plugin's implementation details.

## Architecture

Three pieces:

1. **`TESObjectCELL`** is the same class for both interior and exterior cells -- there is no
   separate "exterior cell" C++ type. `IsInterior()`/`IsExterior()` just read one flag bit
   (`Flag::kInterior`). A `CELL_DATA` union (`cellData`) holds either `INTERIOR_DATA*` or
   `EXTERIOR_DATA*` depending on that flag. Every cell optionally has a `TESObjectLAND* cellLand`
   pointer (null for interiors), and a `worldSpace` back-pointer when it belongs to one.

2. **`TESWorldSpace`** owns the grid of cells (`BSTHashMap<int32_t, TESObjectCELL*> cellMap`, keyed
   by a packed grid coordinate) plus per-worldspace defaults: `defaultLandHeight` (fallback height
   for a grid cell with no LAND data), `defaultWaterHeight`, `minimumCoords`/`maximumCoords` (the
   worldspace's overall grid bounds), and the LOD/map data blocks.

3. **`TESObjectLAND`** is the per-cell terrain record (LAND). Its on-disk `VHGT` subrecord decodes
   into a `LoadedLandData` at runtime: a 34x34-looking-but-actually-4x-quadrant-of-17x17 layout
   (`heights[1156]` = 4 quadrants x 289 = 17x17 each) used for the engine's own LOD/rendering split,
   built from the plugin's real 33x33 vertex grid. The quadrant split is a runtime-only
   reorganization -- the on-disk format is a single 33x33 grid per cell, matching what Mutagen
   already models (`Landscape.VertexHeightMap`, `.VertexColors`, `.VertexNormals`, all confirmed
   33x33 via `Landscape_Generated.cs`'s own `*FixedSize` constants).

## Class reference

Real CommonLibF4 headers, confirmed struct layout:

| Class | Size | Header |
|---|---|---|
| `TESObjectCELL` | `0xF0` | `RE/T/TESObjectCELL.h` |
| `EXTERIOR_DATA` | `0x28` | `RE/E/EXTERIOR_DATA.h` |
| `TESObjectLAND` | `0x48` | `RE/T/TESObjectLAND.h` |
| `TESObjectLAND::LoadedLandData` | `0x6578` | `RE/T/TESObjectLAND.h` |
| `OBJ_LAND` | `0x4` | `RE/O/OBJ_LAND.h` (flags only -- the engine does not keep a raw-record mirror at runtime, only the decoded `LoadedLandData`) |
| `TESWorldSpace` | `0x2D0` | `RE/T/TESWorldSpace.h` |
| `CHAR_NORM` | `0x3` | `RE/C/CHAR_NORM.h` (`char x,y,z` -- plain signed bytes) |

### `EXTERIOR_DATA` (`0x28` bytes)

```
std::int32_t cellX;         // 00  grid coordinate, NOT a world position
std::int32_t cellY;         // 04
std::uint8_t* maxHeightData;// 08
BGSTerrainVisibilityData* lodVisData; // 10
float maxHeight;            // 18
float worldX;               // 1C  precomputed cellX * 4096
float worldY;                // 20  precomputed cellY * 4096
REX::TEnumSet<LandHideFlag> landHideFlags; // 24
```

### `TESObjectCELL` -- fields relevant to exterior/terrain

```
CELL_DATA cellData;          // 0x50  union { EXTERIOR_DATA* exterior; INTERIOR_DATA* interior; }
TESObjectLAND* cellLand;     // 0x58  null for interior cells
float waterHeight;           // 0x60
union { TESWorldSpace* worldSpace; std::uint32_t tempDataOffset; }; // 0xC8
```

`IsInterior()`/`IsExterior()` read `cellFlags.all(Flag::kInterior)` (bit 0 of a 16-bit set) --
that is the *entire* interior/exterior distinction at the type level; nothing else about the class
differs structurally between the two.

## Confirmed engine constants (decompile-verified, not community lore)

These three numbers are the ones a heightmap importer gets silently wrong if guessed instead of
checked -- verified here against the running 1.10.163 binary specifically:

- **Cell size = 4096 game units.** `TESObjectLAND::GetWorldCellX` decompiles to
  `(float)(cellX << 0xc)` -- a plain `cellX * 4096`. Quad pitch follows: 4096 / 32 quads = **128
  units per quad**, giving the standard 33x33-vertex-per-cell grid (32 quads across).
- **VHGT chunk is exactly `0x448` bytes.** `TESObjectLAND::LoadVerticesIntoArrays`'s VHGT branch
  reads `TESFile::GetChunkData(file, buf, 0x448)` -- 4 (float `Offset`) + 1089 (33x33 signed-byte
  deltas) + 3 (unused padding) = 1096 = `0x448`. Matches Mutagen's `LandscapeVertexHeightMap`
  exactly (`Offset` float + 33x33 `HeightMap` + `P3UInt8 Unknown`).
- **Height finalize step is `(int)accumulated << 3`** -- i.e. **the accumulated height is
  multiplied by 8** to get the game-unit height. Confirmed directly in the decompiled loop that
  populates `LoadedLandData::heights[]` from the raw deltas, not carried over from
  community-modding-tool convention (xEdit/UESP document the same "×8" factor, but this pass
  verified it independently against this exact engine build rather than trusting that agreement).
- **Row-boundary modulus is 33** (`0x21`): the accumulation loop resets its running sum at every
  33rd element, matching a 33-wide row. Consistent with (not independently re-derived
  instruction-by-instruction past this point, since the standard row/column delta-accumulation
  algorithm is unambiguous public Bethesda-format knowledge going back to Oblivion, and no
  competing decode order has ever been documented anywhere): row 0 seeds directly from the record's
  `Offset`; every other row's first column inherits its starting value from the row above (same
  column), then each subsequent column in that row accumulates from the previous column in the
  *same* row.

Height decode, in full:

```
height[x, 0] = Offset for x == 0, else height[x-1, 0] + delta[0, x]
height[0, y] = height[0, y-1] + delta[y, 0]           (y > 0)
height[x, y] = height[x-1, y] + delta[x, y]            (y > 0, x > 0)
worldHeightUnits = (int)height[x, y] * 8
```

## Exterior reference positions are world-absolute -- confirmed, not assumed

`TESObjectREFR::GetPosition()` (`RE/T/TESObjectREFR.h:379`) is `return data.location;` --
unconditional, no interior/exterior branch, no cell-grid offset ever added. Cross-referenced
against `GetWorldCellX`/`GetCellX`, which *derive* the grid coordinate from a world value (`>>
12`), never the reverse: a cell's grid X/Y is purely a spatial-partition index for streaming and
lookup, never a coordinate origin that a reference's position is relative to.

**Practical consequence for any importer:** a placed reference's raw position field is already
usable as-is, in the exact same coordinate space, whether its parent cell is interior or exterior.
No cell-origin subtraction/addition is needed to place it correctly relative to the terrain --
only the terrain's own vertices need the grid-to-world conversion above (`cellX * 4096 + col *
128`), since LAND data is stored as a local 33x33 grid per cell rather than as absolute-position
vertices.

## Vertex normals and colors

`CHAR_NORM` (`char x, y, z`) confirms on-disk VNML data is **signed bytes**, not an unsigned
0-255 range: the VNML load branch in `LoadVerticesIntoArrays` is a raw 3-byte copy straight from
the file into the `CHAR_NORM` array, no rescale at load time, so whatever the file's byte values
mean is exactly what the engine's signed-char interpretation gives it. Decode as `sbyte / 127.0f`
per axis (the standard Bethesda compressed-normal convention elsewhere in the engine). VCLR
(vertex color) is a plain unsigned RGB triple, same raw-copy pattern, no rescale needed beyond
`byte / 255.0f`.

Both VNML and VCLR are confirmed **33x33**, one value per height-grid vertex (not the runtime's
4x17x17 quadrant split) -- via Mutagen's own `VertexNormalsFixedSize`/`VertexColorsFixedSize`
constants, cross-checked against the same `0xcc3`-byte (33*33*3 = 3267) chunk-size read in the
VNML/VCLR branches of `LoadVerticesIntoArrays`.

## Texture layers (BTXT / ATXT / VTXT)

Decoded. A cell's LAND record carries a flat list of layers; each layer header is
`{ FormLink<LTEX> Texture, Quadrant quadrant (1 byte), uint8 unused, uint16 layerNumber }`.
`BTXT` is a base layer (covers its whole quadrant at full opacity); `ATXT` is an alpha layer and is
always followed by a `VTXT` opacity payload. Mutagen models this as `BaseLayer` vs `AlphaLayer`
(the latter overriding the header's record type BTXT -> ATXT), which matches the engine's own
branch structure.

Quadrants are `BottomLeft=0, BottomRight=1, TopLeft=2, TopRight=3`, each a **17x17** vertex block.
The four overlap on their shared edges to tile the 33x33 grid, so quadrant origins in grid space are
`(0,0)`, `(16,0)`, `(0,16)`, `(16,16)` -- not `(0,0)/(17,0)/...`. Getting this wrong shifts every
odd quadrant by one vertex.

`VTXT` is an array of 8-byte entries: `uint16 position`, 2 unused bytes, `float opacity`.
`position` indexes within that layer's own quadrant as `qy * 17 + qx`, **not** into the 33x33 cell
grid. Entries are sparse -- only vertices the layer actually touches are listed, so an absent
vertex means zero opacity for that layer, not "inherit".

The runtime's `quadTextureArray[4][12]` (quadrant*12 + layer index) caps a quadrant at 12 layers.
The blend is a straight weighted sum normalised per vertex; there is no documented priority or
painting order beyond `layerNumber`.

**Resolving a layer to an actual image:** the header's FormLink points at an `LTEX`
(`LandscapeTexture`), whose `TextureSet` FormLink points at a `TXST` -- the diffuse path lives on
the TXST, not on the LTEX. LTEX additionally carries `MaterialType`, `HavokFriction`/
`HavokRestitution` and a `Grasses` list, which is where terrain footstep sounds and grass
placement would come from if either is implemented later.

## Not yet closed out
- **`Workshop::Update`-style full per-frame terrain interaction** (grass, snow deformation, etc.)
  not investigated -- this pass covers the static heightmap/color/normal data only.
- **NAVM (navigation mesh) records** are a separate system, not touched here.
