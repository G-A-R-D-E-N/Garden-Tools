# xLODGen terrain Quality: recovered engine behavior

Research status: 2026-08-18

This document describes the terrain simplification behavior recovered from the shipped `LODGen.exe` / `LODGenx64.exe` engine and the clean-room C++ compatibility implementation in F4LODGen. It is the reconciled single source of truth: it supersedes earlier statements of these defaults (in the `docs/reverse-engineering/` oracle copies and in [LODGEN_ENGINE.md](LODGEN_ENGINE.md)) wherever they differ — the decompiled engine source (`LODGeneratorCMD/Program.cs`, `LODGenerator/TerrainLOD.cs`, `LODGenerator.Terra/Terrain.cs`) is the ground truth here.

Related evidence:

- [XLODGEN_REVERSE_ENGINEERING.md](XLODGEN_REVERSE_ENGINEERING.md): xLODGen/xEdit Delphi-side orchestration and binary analysis.
- [LODGEN_ENGINE.md](LODGEN_ENGINE.md): decompiled .NET generation-engine contract, terrain-data format, settings, simplifier, and BTR flow.
- [reverse-engineering/XLODGEN_TERRAIN_INTERNALS.md](reverse-engineering/XLODGEN_TERRAIN_INTERNALS.md): byte-level characterization of WaterDelta, hide quads, skirts/ZSHIFT, the 32767-vs-65535 split, version-130 layout, water grouping, vertex data, and both reducers.

F4LODGen does not copy the unpublished generation engine. The recovered behavior is treated as an interoperability oracle/specification and reimplemented independently in C++.

## Reference terrain input

LODGen consumes a regular terrain heightfield built from exterior CELL data.

For each CELL, the reference terrain-data stream contains:

```text
cell X/Y
water height
LAND flags / hidden quads
base terrain height
33x33 signed-byte height deltas
trailer data
```

Important consequences:

- Exterior cells are 4096 world units wide.
- Each cell contributes 33x33 height samples.
- Sample spacing is 128 world units.
- A LOD block is a complete `(32 * LOD + 1)^2` regular grid.
- Missing terrain-data CELLs are filled from the worldspace `LANDHEIGHT` / `WATERHEIGHT` defaults instead of creating holes.

F4LODGen now carries WRLD default land/water values into `TerrainWorld` and synthesizes missing cells while building a block. Real LAND cells are inserted before fallback cells so authored shared edges remain authoritative.

## Quality coordinate space

The recovered engine scales the merged block heightfield by `1 / LOD` before simplification (`TerrainLOD.cs` `heightValues[...] / (float)quad.lodLevel`). The reducer's plane-fit error is therefore measured in **quad-local units**, and the `QUALITYLOD<n>` threshold is applied in those same units.

Therefore `Quality` is **not directly a world-unit error value**.

For the normal non-protected path:

```text
quad-local Z = world Z / LOD
Quality threshold = allowed quad-local vertical error
approx world-space error = Quality * LOD
```

Examples:

```text
LOD4  Quality 5  -> about 20 world units
LOD8  Quality 5  -> about 40 world units
LOD16 Quality 5  -> about 80 world units
LOD32 Quality 10 -> about 320 world units
```

Those examples describe the coordinate conversion, not recommended visual tolerances.

The GUI hint (verbatim from the DFM): *"Lower = Better / Larger Files, Higher = Worse / Smaller Files, -1 = No Optimization"* (LOD4 only).

### LOD4 Protect Cell Borders

LOD4 has a special path when `ProtectCellBorders` is enabled:

1. The Quality threshold is divided by 4.
2. Each 33x33 CELL heightfield is reduced independently.
3. The resulting cell meshes are stitched together using their shared source-grid vertices.

Because LOD4 itself scales Z by 1/4, `Quality / 4` in local units corresponds to approximately `Quality` world units for this protected-cell path.

This behavior is more specific than simply locking grid-line vertices in a generic simplifier.

**Net-identity correction (source-verified):** the ×4/÷4 pair cancels end to end — `Program.cs` does `if (protectCellBorders) QUALITYLOD4 *= 4f;` and `TerrainLOD.GenerateLOD` does `if (lodLevel == 4 && protectCellBorders) num3 /= 4f;`. The two multiply out, so the **user's `QUALITYLOD4` value is always the effective per-quad error threshold**. The ÷4 only matters per-cell: each cell is triangulated separately against `Quality/4` in cell-local units, i.e. the default 10 becomes 2.5 per cell (≈ 10 world units per cell).

**Corollary for F4LODGen:** do **not** multiply the GUI's LOD4 quality value by 4 (or divide by 4) to match xLODGen output — the reference implementation's net behavior is identity. Only the per-cell error threshold differs when ProtectCellBorders is on, which controls how aggressively each cell is simplified.

## Greedy terrain simplification

The reference engine uses a heightfield-specific greedy insertion algorithm.

Conceptually:

```text
start with constrained terrain boundary
        |
        v
triangulate selected samples
        |
        v
for each triangle:
  find unselected grid point with maximum
  abs(actual height - triangle plane height)
        |
        v
max-heap of triangle candidates
        |
        v
insert global highest-error point
        |
        v
restore local Delaunay legality
        |
        +---- repeat ----+
```

Each triangle searches the source heightfield samples it covers and stores the point with the largest absolute vertical error relative to the triangle plane.

The highest-error candidate is inserted next. Neighboring triangles are then updated and local Delaunay edge legality is restored.

### Stopping contract

The recovered `GoalMet` behavior stops refinement when either:

```text
MaxError < Quality
```

or the selected point count reaches the configured `MaxVerts` budget.

This means `MaxVerts` is not a target produced by increasing the error threshold. It is a second stopping condition.

If MaxVerts stops insertion first, the generated block can have an effective error larger than the requested Quality. F4LODGen reports that as a forced-by-limits block.

Tie-breaking and Delaunay edge-flip details (first-strictly-greater scanline tie-break, strict-`>` heap, in-circle `det > 1e-6f`, the `rand.Next() & 1` on-edge nondeterminism in `Locate`) are characterized in [reverse-engineering/XLODGEN_TERRAIN_INTERNALS.md](reverse-engineering/XLODGEN_TERRAIN_INTERNALS.md) §8/§9.

## Quality -1

`Quality=-1` is the full-geometry path and is valid for LOD4 only. In the engine it also **forces `PROTECTCELLBORDERS` off** (`if (QUALITYLOD4 == -1) protectCellBorders = false;`) and selects the pre-triangulated full-resolution cell geometry built during `TerrainData.ReadWorldData` (the `targetError[0] >= 0` guard in `GenerateQuad`).

F4LODGen preserves the complete regular LOD4 heightfield for this mode and rejects `Quality=-1` for LOD8/16/32.

Note for LOD8/16/32: `Program.cs` applies `targetError[i] = max(QUALITYLOD<i>, targetError[i])` with a constructor default of `0`, so `QUALITYLOD8/16/32 = -1` collapses to threshold 0 — simplify until error < 0, i.e. essentially lossless within the vertex budget. Only LOD4 has the pre-triangulated special case.

## MaxVerts versus NIF/BTR limits

These are separate concepts.

`MaxVerts=32767` is the terrain reducer point budget used by the reference terrain simplifier. It is a **sample-count budget for the greedy reducer** (after the 2000-vertex skirt reserve and the 512 floor), not a binary-format limit.

The FO4 BTR/NIF container itself uses larger geometry count fields and the reference writer can split output into multiple sub-geometries when necessary. Do not treat 32767 as the absolute binary-format vertex limit: `MaxVertices` caps at **65535** everywhere (`Game.MaxVertices()`, applied as `target[i] = min(MAXVERTSLOD<i>, 65535)`), `MaxTriangles` is 65535 and **unlimited for FO4**, and chunks split triangle-atomically at those caps. Splits produce multiple `BSTriShape` chunks (not `BSSubIndexTriShape` — those are LOD4 water and object LOD only).

F4LODGen currently keeps a conservative per-emitted-shape triangle safety ceiling while BTR sub-geometry parity is still being calibrated. Full detail in [reverse-engineering/XLODGEN_TERRAIN_INTERNALS.md](reverse-engineering/XLODGEN_TERRAIN_INTERNALS.md) §4/§5.

## Optimize Unseen / WaterDelta ordering

The recovered generation order is:

```text
complete regular heightfield
        |
        v
WaterDelta / Optimize Unseen height adjustment
        |
        v
greedy Quality reduction
        |
        v
terrain output / optional water processing
```

This is important. Optimize Unseen is **not** a separate generic mesh simplification pass before Quality.

F4LODGen has removed its old meshoptimizer underwater pre-compaction. The regular heightfield remains intact for the greedy reducer.

Exact engine semantics (`TerrainLOD.GenerateQuad`):

```text
delta = WATERDELTALOD<n> / lodLevel        (percent of quad size, per level)
nudge = delta / 100
heights at/above water:  height += nudge    (z-fight avoidance)
heights below water:     height = min(height, waterHeight - delta)
```

`/` is **truncating integer division** (both operands `int`; IL `div` + `conv.r4`),
so deltas smaller than the LOD level truncate to 0 (`WaterDelta=3` at LOD4,
`7` at LOD8, `15` at LOD16, `31` at LOD32) and behave exactly like
`WaterDelta=0` for heights. So terrain under water is pulled down to
`waterHeight - delta` and merged into large flat underwater chunks (only when
the delta survives truncation). `-1` = disabled.

Current implemented behavior:

- `WaterDelta < 0`: feature disabled.
- positive WaterDelta: shallow submerged height samples are lowered relative to their resolved water plane before greedy reduction.
- underwater-triangle statistics are retained for diagnostics.

`WaterDelta=0` is now characterized (no longer open): it is *not* "off" — but with `delta=0` the below-water clamp target is `waterHeight` itself, and since a below-water sample is already below it the clamp **never fires: heights are left untouched, not flattened**. What 0 *does* do is the underwater vertex handling: interior underwater samples are marked `Ignored` (the greedy reducer never spends a point on them), while underwater coarse-grid samples become `Used` (forced vertices, seam matching at LOD resolution); above-water samples are raised by `delta/100` (no-op at 0). The interactions with hide-quads, `REMOVEUNSEENFACES`, ZSHIFT, and special water are documented in [reverse-engineering/XLODGEN_TERRAIN_INTERNALS.md](reverse-engineering/XLODGEN_TERRAIN_INTERNALS.md) §1/§2/§3/§6.

## Defaults — engine side (Program.cs)

| Setting | Default | Notes |
|---|---|---|
| `QUALITYLOD4/8/16/32` | **10 / 15 / 20 / 25** | `float[4] {10f, 15f, 20f, 25f}` |
| `MAXVERTSLOD4/8/16/32` | **32767** | `long[4] {32767 ×4}`; capped at 65535 when applied |
| `WATERDELTALOD4/8/16/32` | **-1** (disabled) | `int[4] {-1 ×4}` |
| `ZSHIFTLOD4/8/16/32` | **0** | `float[4]` zeros |
| `SKIRTS` | **256** | skirt length; 2000 vertices reserved, target never below 512 |
| `PROTECTCELLBORDERS` | **true** | |
| `DEFAULTALPHATHRESHOLD` | **128** | |

The engine-side `TerrainLOD` constructor defaults `targetError = {-1, 0, 0, 0}` and `target = {65535 ×4}`, but `Program.cs` **always** overwrites them via `targetError[i] = max(QUALITYLOD<i>, targetError[i])` / `target[i] = min(MAXVERTSLOD<i>, target[i])` — so the constructor values are only reachable when the keys are absent. The GUI always writes explicit values, so the effective defaults in normal operation are the Program.cs ones above.

## Defaults — GUI side (frmLODGen)

The Quality combos (`cmbTerrainQualityLOD4/8/16/32`) and MaxVerts edits (`edTerrainMaxVertsLOD4/8/16/32`) are owner-drawn and populated **at runtime** (the DFM carries no `Items`), so their initial values are set in Delphi code, not in the form resource. The engine defaults (10/15/20/25, 32767, -1) match the tool's published defaults and are what the GUI writes when the user has not changed them — the two sides are consistent by construction. The only GUI-side constants recoverable from the binary are in the dialog's option handling (`FUN_0141c4c0`): atlas width/height 4096, texture size 512.

## F4LODGen clean-room implementation

Production xLODGen-style terrain Quality now uses `terrain_greedy.cpp` rather than meshoptimizer.

Implemented characteristics:

- complete rectangular heightfield requirement
- quad-local Z scaling
- constrained outer block boundary
- per-triangle maximum vertical-error candidate search
- deterministic max-heap insertion order
- point insertion and triangle subdivision
- local Delaunay edge legalization
- Quality stopping threshold
- MaxVerts stopping budget
- LOD4 per-CELL `Quality/4` path with stitched shared vertices
- independent source/output counts and error reporting

`meshoptimizer` remains in the project for legacy experiments and future object-LOD simplification. It no longer defines the normal terrain Quality algorithm.

## Coordinated LOD profile

LOD4/8/16/32 are treated as one profile, not four unrelated builds.

Current explicit v0.1 profile:

| Level | Quality | Protect cell borders | MaxVerts | WaterDelta |
|---|---:|---|---:|---:|
| LOD4 | 0 | Yes | 32767 | -1 |
| LOD8 | 5 | No | 32767 | 0 |
| LOD16 | 5 | No | 32767 | 0 |
| LOD32 | 10 | No | 32767 | 550 |

These are F4LODGen defaults, not engine-internal fallback values.

The reverse-engineered LODGen engine itself contains fallback Quality values `{10, 15, 20, 25}` and WaterDelta `-1`, but the xLODGen GUI normally writes explicit per-level settings. F4LODGen therefore treats its own profile as explicit configuration rather than adopting those fallback constants blindly.

The profile validator checks all four levels together and reports unusual cross-level combinations as warnings while reserving hard errors for invalid format/algorithm settings.

## Reporting

For each block, F4LODGen should distinguish:

- requested Quality
- effective error in quad-local units
- effective error converted back to world units
- source/output point and triangle counts
- greedy insertion count
- whether MaxVerts stopped refinement before Quality was met
- WaterDelta activity and shifted sample count

Example:

```text
LOD32 block -64,-32
Quality: 10
Max error: 8.7 quad units / 278.4 world units
MaxVerts forced: no
```

## Reconciliation summary (analysis doc → source truth)

| Claim | Verdict |
|---|---|
| Quality defaults {10, 15, 20, 25} per LOD4/8/16/32 | ✅ confirmed (Program.cs `array8`) |
| MaxVerts default 32767 | ✅ confirmed (`array9`) |
| WaterDelta default -1 | ✅ confirmed (`array10`) |
| Quality = max Z deviation (lower = better) | ✅ confirmed (`GoalMet`/`MaxError`, GUI hint) |
| MaxVerts = vertex budget, capped by NIF limits | ✅ confirmed (`min(..., 65535)`, GUI hint) |
| QUALITYLOD4=-1 = no optimization / full-res cells | ✅ confirmed (guard `targetError[0] >= 0`; forces ProtectCellBorders off) |
| ProtectCellBorders ×4/÷4 | ✅ confirmed — net identity; per-cell threshold ¼ of GUI value |
| Skirts default 256 | ✅ confirmed (engine default) |

**Writer-side note:** the GUI terrain settings writer (`xLODGen.exe` @ 0x143b218, see [XLODGEN_SETTINGS_WRITER.md](reverse-engineering/XLODGEN_SETTINGS_WRITER.md) §4/§4a) emits `Skirts=256` for new-style modes (mode ≥ 5: TES5, Enderal, FO4, SSE, TES5VR, EnderalSE, FO4VR, FO76, Starfield — the `wbGameMode` ordering, resolved in §4a) but the literal `Skirts=-14098` for old-style modes (mode < 5: TES3/TES4/TES4R/FO3/FNV, negative = absolute-z skirt ÷ lodLevel). Defaults 10/15/20/25 for Quality per LOD level come from the frmLODGen per-level properties `TerrainQualityLOD<n>`; the writer's literal fallback is `10`.

## Remaining parity work

The characterization items below are now **documented** in [reverse-engineering/XLODGEN_TERRAIN_INTERNALS.md](reverse-engineering/XLODGEN_TERRAIN_INTERNALS.md) (WaterDelta=0, hide quads, skirts/ZSHIFT, BTR sub-geometry, water grouping, vertex data, greedy tie-breaking, Delaunay ordering). What remains is implementation parity and calibration:

- Run the greedy reducer against real xLODGen Commonwealth outputs and compare point/triangle sets (triangle-for-triangle parity requires pinning the `.NET` PRNG side-choice in `Locate` or reproducing it).
- Confirm BTR sub-geometry splitting at the real FO4 container limits against live engine output.
- Benchmark LOD16/LOD32 generation performance and memory use.
- Verify water geometry parity against live engine output (per-unique-height grouping, segment-table ids).

## Evidence policy

Behavior recovered from `LODGen.exe` is used only to understand interoperable file/algorithm semantics. No Bethesda assets or proprietary LODGen executable payloads are committed to this repository.
