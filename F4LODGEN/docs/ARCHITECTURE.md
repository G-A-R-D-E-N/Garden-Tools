# Architecture

## Scope

F4LODGen is a Fallout 4-native LOD compiler, not a UI wrapper around a collection of legacy executables. The engine should understand the active plugin/asset view, create required intermediate data internally, generate final game assets, validate them, and leave enough provenance to explain every decision.

The first production target is Fallout 4 static terrain and object LOD. Runtime dynamic LOD systems are outside the v0.1 scope.

The detailed delivery order is maintained in [ROADMAP.md](ROADMAP.md). This document defines subsystem boundaries and engineering invariants rather than release scheduling.

## Process model

Use three layers:

1. `f4lod_core`: C++20 library containing deterministic game-data, geometry, texture, format, cache, calibration, and validation logic.
2. `f4lodgen`: headless CLI/worker. Every desktop operation must also be expressible here for automation and reproducible builds.
3. Desktop UI: separate frontend. Tauri 2 + React/TypeScript is the preferred direction, but the core must not depend on it.

The worker boundary is intentional. Large mesh/texture jobs can be restarted independently of the UI, progress can be streamed as structured events, and generation remains scriptable.

## Data flow

```text
Fallout 4 / mod-manager environment
            |
            v
   Virtual Game Data Resolver
   plugins + loose + BA2
            |
            v
       Record Graph
 WRLD/CELL/LAND/REFR/STAT...
            |
     +------+------+
     |             |
     v             v
 Terrain model   Object model
     |             |
     v             v
 Terrain compiler Object compiler
 BTR + DDS        BTO + DDS
     |             |
     +------+------+
            v
 validation + manifest + package
```

## 1. Virtual game-data resolver

Inputs:

- Fallout 4 `Data` directory or mod-manager-provided effective view.
- Explicit plugin/load-order file when supplied.
- Loose files.
- General and Texture BA2 archives.
- Fallout 4 INI/archive configuration needed to reproduce actual asset precedence.

Required behavior:

- Normalize paths case-insensitively using game-compatible separators.
- Resolve plugin order independently from asset/archive order.
- Record every winner and overridden candidate for diagnostics.
- Preserve source provenance for every resolved asset.
- Support an explicit input manifest so builds can be reproduced outside a mod manager.
- Hash resolved inputs for incremental invalidation.
- Never guess when archive precedence is ambiguous.

The current native BA2 layer already indexes Fallout 4 General/DX10 archives and extracts General entries. Final v0.1 precedence rules are tracked separately in the roadmap/issues.

## 2. Record graph

Convert raw plugin records into a compact immutable graph containing only information needed by LOD generation.

Initial families:

- `WRLD`: editor ID, parent/inheritance behavior, default land/water values, LOD-related world settings.
- `CELL`: grid coordinates, exterior flags, water overrides.
- `LAND`: height field, normals, landscape texture layers.
- `REFR`: base form, position, rotation, scale, state/enable metadata.
- `STAT`: model and authored LOD slots.
- `LTEX` / `TXST`: terrain texture resolution.
- Material swap/color-remap records needed by supported FO4 LOD materials.
- Additional base types only when later fake-static policy explicitly allows them.

Geometry code should never traverse raw plugin streams directly. It consumes immutable generation models built from winning records.

## 3. Preflight

Preflight is a compiler phase, not a log dump.

Errors stop generation when output would be invalid. Warnings explain the consequence and affected forms/assets.

Checks include:

- Missing NIF, BGSM/BGEM, DDS, or LODSettings dependencies.
- Broken/ambiguous archive precedence.
- Unsupported NIF blocks/material features.
- UV ranges that cannot be safely atlased.
- Stateful/enable-parent references that would be unsafe as always-visible LOD.
- Child-worldspace inheritance conflicts.
- Duplicate/conflicting LOD assignments.
- Objects without a viable authored/generated distant representation.
- Terrain holes, missing LAND coverage, or seam disagreement.
- Unsupported/calibration-mismatched BTR structures.
- Output path overlap with source data.

Every diagnostic should identify FormKey/path, source plugin/archive, winner provenance, severity, and remediation.

## Terrain pipeline

### Canonical terrain model

For each selected worldspace, construct a canonical grid from winning `WRLD`, `CELL`, and `LAND` data.

Current invariants:

- LAND uses the stored 33x33 height field.
- Shared edge samples are validated before simplification.
- Exterior cell coordinates are retained exactly, including negative coordinates.
- `.lod` settings define the southwest anchor and enabled LOD levels.
- Water comes from CELL water semantics with WRLD default fallback.

### Block planning

Generate the normal Fallout 4 terrain hierarchy where present:

- LOD4
- LOD8
- LOD16
- LOD32

Block names/coordinates are derived from the real LODSettings southwest anchor, not assumed to start at `(0,0)`.

### Quality

F4LODGen models xLODGen-style terrain Quality as an absolute vertical error contract:

- `Quality=-1`: full source geometry, LOD4 only.
- `Quality=0..N`: requested maximum Z deviation in Fallout 4 game units.
- Output is independently resampled against original LAND samples to verify actual vertical error.
- Terrain output is capped at an xLODGen-compatible 32,767 vertices per shape.
- Protected borders are explicit vertex locks.

`meshoptimizer` is used as the reduction engine, but the externally visible contract is measured Fallout 4 terrain error, not meshoptimizer's internal metric.

### Optimize Unseen / WaterDelta

Underwater terrain optimization is a separate stage from Quality.

- `WaterDelta=-1`: disabled.
- `WaterDelta=0`: submerged-only geometry may be coarsened.
- Positive WaterDelta: shallow submerged terrain may also be pushed to at least `waterHeight - WaterDelta`.
- Vertices participating in visible/shoreline geometry are locked.
- Intentionally modified underwater geometry is excluded from visible-surface Quality verification.

### BTR writer and calibration

F4LODGen owns a native Fallout 4 NIF/BTR reader/writer for the terrain subset.

The writer must be driven by measured game semantics rather than guessed constants. The calibration subsystem can inspect real Commonwealth BTRs directly from the installed base mesh BA2 and capture:

- AV flags
- transform/scale/rotation
- position precision
- vertex descriptor bytes/stride
- UV ranges
- material/shader chain
- subindex behavior
- water child/node structure

Unsupported real-game structures fail closed until implemented.

### Terrain textures

The terrain texture compiler will consume LAND texture-layer data and resolved LTEX/TXST/DDS assets.

Requirements:

- deterministic blending/sampling
- diffuse/normal generation
- full mip chains
- explicit color-space handling
- FO4-compatible DDS formats
- independent DDS validation
- content-hash caching

## Object pipeline

### Reference collection

Collect eligible exterior static references after winning-record resolution. Each candidate carries:

- reference/base FormKeys
- world transform/scale
- worldspace/block ownership
- state/enable metadata
- authored LOD slots
- material swap/color-remap context
- provenance for every resolved asset

### Authored LOD asset selection

v0.1 resolution order:

1. Explicit authored LOD slot from the winning base record.
2. Known compatible installed authored LOD asset discovered by supported rule/naming policy.
3. Exclusion with diagnostic.

Generated LOD models become an additional source in v0.2.

### Object safety classification

Classify each reference as:

- safe static
- safe with valid enable-state semantics
- potentially stateful: exclude/warn by default
- unsupported asset/material
- explicitly forced by later user policy

Do not infer quest semantics from EditorID strings alone.

### BTO generation

For each object LOD level/block:

- transform selected authored/generated LOD geometry into block space
- batch compatible materials deterministically
- cull triangles fully hidden below generated terrain/water when enabled
- support a no-culling diagnostic mode
- preserve FO4 alpha/backlight/two-sided/material requirements
- serialize BTO using a native validated writer

## Automatic object LOD generation

This is a v0.2 feature after the authored-LOD compiler is proven.

A generated LOD is not `simplify(mesh, 10%)`.

### Safety classifier

Reject or require manual approval for unsupported animation, skin, effects, shaders, or state behavior.

### Geometry preparation

- stable local transform
- protect material boundaries
- protect UV seams
- preserve silhouette-critical edges
- preserve required topology
- remove hidden/tiny components only when policy permits
- generate required normals/tangents

### Simplification

Use meshoptimizer with appearance/error targets and triangle targets only as upper bounds.

### Validation

Record:

- triangle/vertex reduction
- bounding-box and centroid drift
- simplification error
- material/submesh changes
- UV changes
- fixed-view silhouette comparison

Unsafe results fall back to authored LOD or exclusion.

## LOD texture pipeline

The texture compiler replaces the external texture-preparation step for supported v0.1 object LOD.

Capabilities:

- load DDS from loose files and Texture/General BA2 sources
- read alpha/backlight/two-sided/material settings
- stitch compatible textures
- atlas compatible UV/material groups only
- keep tiled/incompatible UV materials on dedicated textures
- generate full mip chains
- output explicit FO4 BC formats/color spaces
- cache decoded/intermediate images by content hash

DirectXTex remains the preferred Windows DDS/compression backend candidate, subject to the dependency review recorded in research/third-party notices.

## Patch generation

Plugin mutation is not part of v0.1.

A later patch writer may add:

- generated LOD slot assignments
- fake statics
- valid enable-state flags/policies
- material helper records

Generated plugins are always new output artifacts and never in-place edits of source plugins.

## Cache and reproducibility

Every generation job writes a versioned manifest containing:

- tool version
- plugin list/order/hashes
- resolved asset hashes/provenance
- selected worldspaces/bounds
- Quality/WaterDelta/material settings
- calibration profile
- dependency versions
- per-stage cache keys
- final output hashes

Cache boundaries include:

- parsed plugin records
- record graph
- resolved NIF/material dependency graphs
- terrain blocks
- decoded texture sources
- generated object LOD meshes
- texture products
- final BTR/BTO blocks

Changing one object should not force unrelated terrain regeneration.

## Concurrency and failure handling

Use a bounded memory-aware worker pool. Do not equate `hardware_concurrency()` with unconstrained job count.

Each block/model task should be retryable. Write to temporary files and atomically publish validated output so cancellation/crashes never leave a half-valid mod directory.

## Validation

Post-generation validation reopens produced data through independent readers whenever possible.

Checks include:

- required files exist
- BTR/BTO/NIF structures parse successfully
- root/block references are valid
- bounds/coordinates match requested coverage
- neighboring terrain borders match
- no NaN/inf geometry
- texture dimensions/mips/formats are legal
- every referenced material/texture resolves
- object bounds remain within configured drift tolerance
- asset paths obey engine normalization rules
- output hashes match the manifest

## Desktop UX

The desktop app exposes the compiler model instead of recreating legacy dialogs.

Minimum v0.1 pages:

1. **Project**: game/mod-manager source, output, detected load order.
2. **Preflight**: actionable errors/warnings with source ownership.
3. **Worldspace**: Commonwealth for v0.1, later arbitrary supported worldspaces.
4. **Quality**: opinionated preset plus advanced Quality/WaterDelta controls.
5. **Generate**: DAG/progress view with CPU/RAM/cache statistics and cancellation.
6. **Validate**: final report and output/package summary.

The CLI/worker is the product API. The desktop UI is one client of it.

## Release phases

See [ROADMAP.md](ROADMAP.md) and issues #1 through #18 for the authoritative delivery plan. In short:

- v0.1: Commonwealth terrain + existing authored object LOD, textures, validation, standalone app.
- v0.2: automatic safe missing object LOD generation.
- v0.3: patch writer, fake statics, enable-state support.
- v0.4-v1.0: DLC/custom worldspaces, BA2 packaging, mod-manager integration, and production hardening.
