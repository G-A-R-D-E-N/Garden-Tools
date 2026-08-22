# F4LODGen roadmap

Last updated: 2026-08-18

This is the authoritative delivery plan for the standalone Fallout 4 LOD compiler. GitHub Issues track executable work; this document defines phase boundaries, ordering, release gates, and non-goals.

## Product target

F4LODGen should turn a real Fallout 4 PC environment into a deterministic LOD build without requiring users to manually chain legacy preprocessing, terrain generation, object LOD generation, texture preparation, validation, and packaging steps.

```text
Fallout 4 / mod-manager environment
            |
            v
 plugin + asset resolution
            |
            v
       record graph
 WRLD/CELL/LAND/REFR/STAT...
            |
      +-----+-----+
      |           |
      v           v
   terrain       object
   compiler      compiler
      |           |
      +-----+-----+
            v
      BTR / BTO / DDS
            |
            v
 validation + manifest + package
```

v0.1 proves the compiler with **Commonwealth terrain plus existing authored object LOD models**. Automatic creation of missing object LOD models starts in v0.2.

## CI policy

The authoritative compiler gate is **Clang CI**:

- `clang` / `clang++`
- `ubuntu-latest`
- Ninja
- Release build
- CTest regression suite
- CLI smoke test

There is **no Wine-based CI requirement** and no MSVC/Windows GitHub Actions matrix required for v0.1 compiler correctness. Fallout 4 on Windows remains the real runtime/acceptance target.

Tracking: #2

## Current implementation

Already present in the native C++20 core:

- Fallout 4 game/Data scanning, `plugins.txt`, and MO2 profile input.
- Native ESM/ESP/ESL parsing, TES4 masters, GRUP context, stable FormKeys, and winning WRLD/CELL/LAND/REFR/STAT graph.
- Full low-to-high resolved candidate retention and same-FormKey override chains for parity analysis.
- Loose asset inventory and General/DX10 BA2 indexing.
- General BA2 targeted extraction including compressed entries.
- Fallout 4 archive-order builder based on the four `[Archive]` INI lists plus plugin-associated BA2 prefixes.
- Case-insensitive first-load deduplication and loose-file override behavior.
- `terrain-plan` uses the derived archive order instead of the old single-provider restriction.
- Compressed plugin-record decoding.
- Worldspace/CELL catalog and Commonwealth detection.
- 33x33 LAND/VHGT decoding and shared-border validation.
- WRLD default land/water parsing.
- CELL `Has Water` / `XCLW` handling.
- Exact Bethesda/xEdit default-water sentinel `-2147483648.0`.
- FO4 `.lod` settings parsing and southwest-anchor LOD4/8/16/32 block planning.
- LAND source audit comparing first resolved candidate vs current winning LAND selection per CELL.
- Deduplicated terrain block mesh generation.
- xLODGen-style Quality semantics with independent LAND Z-deviation verification.
- 32,767 terrain MaxVerts ceiling and Protect Borders.
- Optimize Unseen + WaterDelta with coastline locking and underwater coarsening.
- FO4 20.2.0.7 / user 12 / Bethesda 130 NIF/BTR reader/writer foundation.
- BTR root/multibound/AABB/geometry validation.
- Half/full FO4 terrain position precision support.
- BSLightingShaderProperty and BSShaderTextureSet inspection/cloning.
- Packed BTR probing directly from General BA2 archives.
- Automatic installed Commonwealth BTR calibration framework for LOD4/8/16/32.
- Terrain compile preview diagnostics.
- Pinned `miniz` and `meshoptimizer` dependencies.

Still required for v0.1:

- confirmed green `clang-ci` on the current head
- real-profile validation of Fallout 4 archive precedence
- final proof of xLODGen's "first LAND record" selection semantics
- real installed Commonwealth BTR calibration
- any calibrated subindex/water-shape details
- terrain LOD textures
- atomic terrain output
- general static NIF/BGSM/BGEM object support
- authored object candidate/preflight model
- BTO generation
- object material/culling correctness
- object LOD textures
- end-to-end generate/cache/manifest workflow
- desktop UI
- real-game release acceptance

## v0.1 release contract

v0.1 means:

> Select a supported Fallout 4 PC environment, scan/preflight it, generate Commonwealth terrain plus existing-model object LOD, validate everything, and publish a deterministic loose-file mod directory without modifying source game or mod-manager data.

### Supported input target

- Fallout 4 Windows PC.
- Current PC data as the primary release target.
- One supported downgraded PC installation used by legacy modlists as an acceptance target.
- Steam primary tested path; compatible GOG/manual Data paths supported.
- MO2 primary mod-manager workflow.
- Normal/deployed Data directories.
- Vortex deployment treated as a deployed Data view, not native Vortex database integration.

### v0.1 scope limits

- Commonwealth is the release-gated worldspace.
- Existing authored object LOD models only.
- Missing authored LOD is reported/excluded.
- Unsafe/stateful representations fail closed.
- Loose-file output is required; BA2 output comes later.

## Phase overview

| Phase | Scope | Status | Issue(s) |
|---|---|---|---|
| 0 | Clang CI + exact asset precedence | In progress | #2, #3 |
| 1 | Canonical WRLD/CELL/LAND/water semantics | In progress, parity gate open | #19 |
| 2 | Real Commonwealth BTR calibration + terrain parity | In progress | #4, #5 |
| 3 | Terrain textures + atomic terrain publication | Not complete | #6, #7 |
| 4 | Static object asset/candidate foundation | Not started | #8, #9 |
| 5 | BTO + terrain/material correctness | Not started | #10, #11 |
| 6 | Object LOD texture compiler | Not started | #12 |
| 7 | End-to-end CLI/worker/cache/manifest | Not started | #13 |
| 8 | Minimal standalone desktop UI | Not started | #14 |
| 9 | v0.1 real-game acceptance/release | Not started | #15 |
| v0.2 | Automatic missing object LOD generation | Planned | #16 |
| v0.3 | Patch writer/fake statics/enable-state | Planned | #17 |
| v0.4-v1.0 | DLC/custom worlds, BA2, hardening | Planned | #18 |

## Phase 0: foundation gate

Tracking: #2, #3

### Clang build/test gate

- `clang-ci` configures with Clang/clang++ and Ninja.
- Release build succeeds.
- Every regression/smoke test is registered in CTest.
- `ctest --output-on-failure` passes.
- Quality, Optimize Unseen, water, archive-order, record-chain, BTR, BA2, and calibration tests run in CI.
- Dependencies remain pinned and reproducible.
- No Wine dependency exists in the workflow.

### Exact asset precedence

The core now implements the xEdit/FO4-style ordering model:

1. `[Archive] sResourceIndexFileList`
2. `[Archive] sResourceStartUpArchiveList`
3. `[Archive] sResourceArchiveList`
4. `[Archive] sResourceArchiveList2`
5. BA2 archives associated with active plugin basenames, in plugin order
6. loose files override archive assets

`Fallout4Custom.ini` values override matching archive keys from the primary INI. Duplicate archives retain their first loaded position. Windows-style paths are normalized explicitly so Clang CI tests the same semantics as the Windows runtime.

Remaining Phase-0 acceptance:

- validate this order on a real clean Fallout 4 install
- validate MO2 effective Data priority
- validate deployed Data priority
- confirm General/Texture BA2 ordering before the texture pipeline depends on it
- record winner and losing-candidate provenance

Release rule: never guess an ambiguous asset winner.

## Phase 1: canonical terrain source semantics

Tracking: #19

The code has a working WRLD/CELL/LAND terrain model, but the exact selection semantics must be proven against xEdit/xLODGen.

Primary-source behavior already established:

- terrain height generation uses height data from the **first LAND record for a cell**
- default land height comes from WRLD as fallback
- terrain texture layers, vertex colors, and normals use the same first LAND record
- water height comes from CELL
- WRLD default water height is the fallback
- WRLD LOD-water height is not used by xLODGen
- CELL default water is represented by the exact `-2147483648.0` sentinel or an absent explicit height

F4LODGen now retains:

- every resolved LOD-critical record in deterministic low-to-high plugin/file order
- same-FormKey override chains
- current final winners

`terrain-plan` performs a LAND source audit per selected worldspace and reports:

- landscape cells
- cells with multiple LAND candidates
- cells where first resolved LAND candidate differs from the current winner
- cells with multiple distinct winning LAND FormKeys

Generation **still uses the current winner** until #19 proves what "first LAND" means in xEdit's loaded-record ordering. No policy change will be made from wording alone.

Remaining Phase-1 acceptance:

- run the audit on clean Commonwealth
- run it on a landscape-overhaul profile
- compare xLODGen terrain output/source behavior for controlled LAND overrides
- settle the selection rule
- verify missing LAND/default-land behavior at exterior coverage edges
- verify negative coordinates and `.lod` southwest-anchor alignment

## Phase 2: terrain mesh/BTR parity

Tracking: #4, #5

### Real Commonwealth calibration

Run the installed-game calibrator against base-game Commonwealth BTRs at LOD4/8/16/32 and confirm:

- block/shape types
- AV flags
- transform/rotation/scale
- half/full precision
- vertex descriptor bytes/stride
- UV range
- shader/material chain
- subindex table semantics
- water child/node structure

Do not commit Bethesda assets. Commit only independently created fixtures or legal semantic expectations.

### Quality

Current contract:

- `Quality=-1`: full source geometry, LOD4 only.
- `Quality=0..N`: requested maximum absolute LAND Z deviation in FO4 game units.
- final mesh is independently resampled against original LAND.
- MaxVerts = 32767.
- LOD4 Protect Borders enabled by default.

Current compatibility defaults:

```text
LOD4   Quality 0
LOD8   Quality 5
LOD16  Quality 5
LOD32  Quality 10
```

### Optimize Unseen / WaterDelta

Current defaults:

```text
LOD4   WaterDelta -1
LOD8   WaterDelta 0
LOD16  WaterDelta 0
LOD32  WaterDelta 550
```

Release checks:

- positive WaterDelta only lowers shallow submerged terrain
- visible/coastline geometry is locked
- submerged-only interior can be coarsened
- visible-surface Quality verification excludes intentionally changed underwater geometry

### Writer completion

Implement whatever real calibration proves necessary, including subindex segmentation, water nodes, exact transforms/precision, material refs, UV convention, and path naming. Unsupported calibrated features stop generation.

## Phase 3: terrain textures and safe publication

Tracking: #6, #7

Add the minimum terrain texture graph:

- LAND texture layers
- LTEX
- TXST
- winning DDS paths
- vertex color/normal inputs used by the blend

Generate diffuse/normal terrain DDS with deterministic blending, full mips, explicit color-space handling, known-good FO4 BC formats, independent validation, and source/encoder provenance.

Then add game-facing publication:

- output path cannot overlap source Data/mod roots
- temporary files first
- validate before atomic publish
- `manifest.json`, `report.json`, human report
- input/output hashes
- no source mutation

Phase-3 exit: validated standalone **terrain-only** Commonwealth output.

## Phase 4: existing authored object LOD foundation

Tracking: #8, #9

Support the FO4 static NIF/material subset needed by authored object LOD, including geometry, transforms, BSLightingShaderProperty, BGSM/BGEM, textures, alpha/two-sided/backlight values, and required material swaps/remaps. Fail closed on animation, skin, effects, or unsupported shader behavior.

Build deterministic REFR/STAT candidates with world ownership, transforms, authored LOD paths, state/enable metadata, and asset provenance.

v0.1 classes:

- safe static
- safe with known enable-state behavior
- potentially stateful: exclude/warn
- unsupported
- no authored LOD: exclude/report

## Phase 5: BTO and FO4 material correctness

Tracking: #10, #11

- deterministic object block placement
- transform authored LOD meshes into block space
- batch compatible materials
- native validated BTO writer/reader
- terrain and separate water-plane culling
- no-culling diagnostic mode
- source alpha threshold
- source backlight power
- two-sided behavior
- vertex-color policy

## Phase 6: object LOD texture compiler

Tracking: #12

- deterministic stitching
- compatible atlasing only
- tiled/out-of-range UV fallback to dedicated texture
- alpha-aware handling
- full mips
- explicit FO4 BC formats/color space
- cached decoded/intermediate images
- validate every BTO texture reference

Phase-6 exit: complete Commonwealth terrain + authored object LOD assets.

## Phase 7: one compiler workflow

Tracking: #13

```text
scan -> resolve -> graph -> preflight
     -> terrain geometry -> terrain textures
     -> object candidates -> BTO -> object textures
     -> validate -> publish
```

Add structured progress events, cancellation, content-addressed cache, incremental invalidation, resume, versioned manifest, and output hashes.

## Phase 8: standalone desktop app

Tracking: #14

Preferred frontend: Tauri 2 + React/TypeScript, with the C++ worker/core authoritative.

Minimum pages:

1. Project
2. Preflight
3. Worldspace
4. Quality
5. Generate
6. Validate

The UI contains no generation logic.

## Phase 9: v0.1 acceptance/release

Tracking: #15

Real-game matrix:

- clean current Fallout 4
- supported downgraded Fallout 4
- MO2
- deployed/normal Data
- 400+ plugin heavy profile

Commonwealth smoke route:

- Sanctuary
- Concord
- Boston skyline
- Diamond City exterior
- waterfront
- Glowing Sea

Hard gates:

- `clang-ci` green
- no source mutation
- no missing terrain blocks
- no obvious cracks/terrain drops
- no purple terrain/object LOD
- no exploding/misplaced geometry
- no known stateful object baked incorrectly
- deterministic unchanged rebuild
- no unbounded memory growth
- standalone Windows package
- notices/license docs complete

## Post-v0.1

### v0.2: automatic missing object LOD generation
Tracking: #16

```text
full NIF
 -> safety classifier
 -> protect material/UV/silhouette boundaries
 -> meshoptimizer simplification
 -> multiple LOD levels
 -> bounds/error/silhouette validation
 -> accept or reject
```

Never use triangle percentage as the only quality metric.

### v0.3: patch writer, fake statics, enable-state
Tracking: #17

Add only after the read-only compiler is proven: deterministic FO4 plugin writing, generated LOD slots, fake statics, valid enable-state policies, stateful-reference handling, and child-worldspace patch support. Generated plugins are new outputs, never in-place source edits.

### v0.4 -> v1.0
Tracking: #18

v0.4: Far Harbor, Nuka-World, arbitrary exterior worldspaces, parent/child inheritance.

v0.5: native BA2 output, archive-winner verification, optional MO2 create/install workflow, cache/storage UX, Vortex integration only if deterministic and safe.

v1.0: stable worker/event schema, stable versioned manifest, crash-safe resume, memory-aware worker pool, broad regression corpus, large-modlist performance targets, Fallout 4 update compatibility policy, public-release troubleshooting quality.

## Dependency chain

```text
#2 clang-ci
 |
 +--> #3 archive precedence
 |
 +--> #19 terrain source semantics
        |
        +--> #4 real BTR calibration
               |
               +--> #5 terrain BTR parity
                      |
                      +--> #6 terrain textures
                      |      |
                      |      +--> #7 terrain output
                      |
#3 ------------------> #8 object asset reader
                         |
                         +--> #9 candidate graph
                                |
                                +--> #10 BTO
                                       |
                                       +--> #11 culling/materials
                                              |
                                              +--> #12 object textures
                                                     |
#7 --------------------------------------------------+
                                                     v
                                                   #13 generate/cache/manifest
                                                     |
                                                     +--> #14 desktop UI
                                                     |
                                                     +--> #15 v0.1 release
                                                            |
                                                            +--> #16 v0.2
                                                                   |
                                                                   +--> #17 v0.3
                                                                          |
                                                                          +--> #18 v0.4-v1.0
```

## Issue index

- #1 v0.1 release tracker
- #2 Clang CI build/test gate
- #3 Fallout 4 archive precedence
- #19 canonical terrain input semantics parity gate
- #4 real Commonwealth BTR calibration
- #5 terrain BTR parity
- #6 terrain LOD texture compiler
- #7 atomic validated terrain output
- #8 FO4 NIF/BGSM/BGEM object asset foundation
- #9 authored object candidate graph/preflight
- #10 BTO writer
- #11 terrain/water culling and FO4 object material policy
- #12 object texture stitch/atlas
- #13 end-to-end generate/cache/manifest
- #14 desktop UI
- #15 v0.1 acceptance/release
- #16 v0.2 automatic missing object LOD models
- #17 v0.3 patch/fake statics/enable-state
- #18 v0.4-v1.0 expansion/hardening

## Explicit v0.1 non-goals

- Dynamic/runtime LOD.
- Fallout 4 VR.
- Automatic missing object LOD generation.
- Fake statics.
- Plugin patch writing.
- BA2 output requirement.
- Native Vortex database integration.
- Far Harbor/Nuka-World/custom-worldspace release guarantees.
- Linux end-user release requirement.

## Engineering rules

1. Correctness before feature count.
2. Fallout 4 semantics before cross-game abstraction.
3. Plugin precedence and asset precedence are separate systems.
4. Fail closed on unsupported binary/material/state behavior.
5. Terrain precedes object LOD when culling depends on it.
6. Every output has provenance and a reproducible manifest.
7. Every warning names the affected record/asset and remediation.
8. Source game/mod-manager data is read-only.
9. Generated files are validated before publication.
10. The CLI/worker is the product API; the desktop app is a client of it.
