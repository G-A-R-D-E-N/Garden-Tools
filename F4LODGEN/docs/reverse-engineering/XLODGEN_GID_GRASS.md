# Grass LOD input: the binary .gid placement file

Research date: 2026-08-18

This document specifies the binary grass-placement file format consumed by the engine's object-LOD pipeline (`LODGenerator/GIDFile.cs`, decompiled C#; the format is the game's own grass cache, so a writer must match it byte-for-byte). It is a companion to [XLODGEN_OBJECT_LOD.md](XLODGEN_OBJECT_LOD.md) §"Grass LOD (`GIDFile`)".

## 1. File discovery and naming

For each quad (LOD4 only, `x..x+level`, `y..y+level`), the engine looks for one grass file per cell:

```
<worldspace>x<X>y<Y>[<season>].<ext>     in <PATHGRASS>\  (loose)
grass\<worldspace>x<X>y<Y>[<season>].<ext>  inside BSAs / BA2s
```

- `<ext>` = `Game.gid` = **`.cgid`** by default (FO4); the `GRASSGID=` settings key sets `Game.gid = "." + value` (e.g. `.gid` for earlier games).
- `<X>`/`<Y>` formatting: positive coords are zero-padded to **4 digits** (`5` → `0005`), negative coords to 3 with the sign (`-5` → `-05`), e.g. `Commonwealthx0000y0000.cgid`, `...x-05y-01.cgid`.
- A season-suffixed file is preferred; the non-season variant is the fallback (both are tried loose-first, then BSA).
- The cell is recovered from the filename via `x([0-9\-]+)y` / `y([0-9\-]+)\.` regexes.

## 2. File layout

All values little-endian. The reader is a strict sequential `BinaryReader`; any deviation aborts with exit code 655 (`Error processing grass data ... Unknown size` for bad placement sizes).

```
uint32  grassTypeCount
```

### 2.1 Per grass type (grassTypeCount ×)

| Field | Type | Notes |
|---|---|---|
| model | sized string | `uint32 len` + bytes; prefixed `Meshes\` by the loader (NULs stripped) |
| wave | float32 | wind-wave parameter |
| formID | uint32 | masked on load: top byte `0xFE` → `& 0xFFF` (compressed ref), else `& 0xFFFFFF` |
| slope | bool (1 byte) | |
| scale | bool (1 byte) | non-uniform scale flag |
| light | bool (1 byte) | |
| instanceCount | uint32 | |

### 2.2 Per instance (instanceCount ×)

| Field | Type | Notes |
|---|---|---|
| center | 3 × float32 | absolute world units, within the cell (reader logs `center ... outside cell` if not) |
| extend | 3 × float32 | bounding box half-extents |
| unknown | uint32 | reserved, skipped by the reader |
| placementCount | uint32 | |
| placementSize | uint32 | **14 or 16** — the number of int16 fields per placement; anything else is fatal (655) |

### 2.3 Per placement (placementCount ×)

The placement is a fixed struct of 14 (legacy) or 16 (FO4) **int16** fields, each a **half float** (see §3):

| Disk order | Field | Loaded as |
|---|---|---|
| 1–3 | `pos.x`, `pos.y`, `pos.z` | half → float + **block origin** on X/Y (see §4) |
| 4 | `color` | half → float (0..1 vertex-color scalar) |
| 5–7 | `rotx.x`, `rotx.y`, `rotx.z` | half → float |
| 8 | (see §2.4) | half → float → becomes **rotz.y** |
| 9–11 | `roty.x`, `roty.y`, `roty.z` | half → float |
| 12 | (see §2.4) | half → float → becomes **rotz.z** |
| 13 | (see §2.4) | half → float → becomes **rotz.x** |
| 14 | `scale` | half → float |
| 15–16 | *unused* | only when `placementSize == 16`; read and discarded |

Sizes: 14 fields = **28 bytes**, 16 fields = **32 bytes**.

### 2.4 Rotation matrix assembly

The 9 rotation halves are stored interleaved with the scale field. The engine assembles the placement transform as a `Matrix33` with rows:

```
row1 = (rotx.x, rotx.y, rotx.z)        # disk 5–7
row2 = (roty.x, roty.y, roty.z)        # disk 9–11
row3 = (rotz.x, rotz.y, rotz.z)        # = (disk 13, disk 8, disk 12)
```

So on disk the Z basis vector is written as `rotz.y, rotz.z, rotz.x` split around `roty` and `scale` — a writer must reproduce this exact interleaving, not a contiguous 3×3 matrix. (The naming follows the reference implementation; `rotz.y` = disk field 8, `rotz.z` = disk field 12, `rotz.x` = disk field 13.)

## 3. Half-float conversion (`Utils.ShortToFloat`)

Standard **IEEE 754 binary16 → float32**:

- sign bit (0x8000) → float sign;
- exponent (0x7C00): all-ones → float exponent 0xFF (inf/nan); normal → exponent − 15 + 127 (bias shift 0x1C000 → `+114688` applied in int-space); zero → subnormal path (mantissa normalized until bit 0x400 set, exponent decremented);
- mantissa (0x3FF) → float mantissa.

`FloatToShort` is the inverse (round-to-nearest with `+4096` tie handling) — used by the GUI's grass exporter, so both directions exist in the codebase.

## 4. Coordinate spaces

- `center`/`extend` (instance): **absolute** game units; the reader cross-checks `center` against the cell bounds `[cell*4096, cell*4096+4096]` and logs `center ... outside cell` (verbose).
- `pos` (placement): stored **relative to the 12×12-cell block origin**: `blockOrigin = (int)(cell / 12) * 12 * 4096` per axis, added back at load. The reader also verbose-warns `pos ... outside cell`.
- Cells are 4096 units; the placement Z is not block-relative.

## 5. Conversion to statics (`GIDFile.ToStatics`)

Each placement becomes a `StaticDesc` for the object-LOD pipeline:

- **Grass map** (`GRASSMAP=`, TAB-separated, 2 columns): `key → LOD model path`, where `key = <model filename without ext>_<formID X8 hex>.DDS` (uppercased, e.g. `GRASSBLUE_00001234.DDS`). Missing keys log `Grass mapping not found` and skip the grass type; the map file is resolved relative to the engine dir if not rooted.
- **Density** (`GRASSDENSITY=`, 0..1, clamped ≥ 0): the engine keeps `floor(placementCount * density)` placements by randomly removing extras (`num = count * density; while (num < count) remove random`). `density <= 0` → no grass at all.
- **Large references** (`GRASSLARGEREFERENCE=`): materialName `-LargeRef` on the static.
- **Flags**: `refFlags = (slope << 2) | (scale << 1) | light`.
- **Transform**: position = placement pos (absolute, block origin added); `scale = placement.scale + 1`, then the type flag applies: if `scale` flag set → `scaleZ = 1` (X/Y scaled, Z flat), else → `scale = 1` (Z scaled from `scaleZ = placement.scale + 1`, X/Y flat). Rotation = the assembled Matrix33.
- `staticName = "Grass"`, `isBillboard = true`, `isGrass = true`, `alphaThreshold = 128`, `staticFullModel` = the type's model, LOD0 model = the grass-map target; `refID` = the cell coordinate string.

## 6. Relevance to F4LODGen

- This is the one remaining binary input of the object pipeline whose bytes were unspecified; the spec above is write-compatible with the reference reader (half-float encoding, block-relative positions, 14/16-field placement records, rotation interleave).
- A compatible implementation can either (a) consume existing `.cgid` files produced by the game/GUI, or (b) emit them — both need this exact layout.
- Exit-code contract: 655 on unknown placement sizes / parse failures, 640 on OOM (x64 hint), 500/501/502 on file access failures.

## 7. Source

`LODGenerator/GIDFile.cs` (decompiled from `Edit Scripts/LODGenx64.exe`); helpers in `LODGenerator.Common/Utils.cs` (`ShortToFloat`, `ReadSizedString`, `ReadBool`); `Game.gid` in `LODGenerator.Common/Game.cs`.
