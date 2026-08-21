# F4LODGen

F4LODGen is a standalone modern Fallout 4 LOD compiler under active development.

The goal is a single deterministic pipeline for analyzing the active game data, generating terrain and static-object LOD, building or selecting optimized LOD assets, generating LOD textures, validating the result, and publishing a mod-ready output.

## Project plan

- [Full phase roadmap](docs/ROADMAP.md)
- [Architecture](docs/ARCHITECTURE.md)
- [LOD ecosystem research](docs/RESEARCH.md)
- [xLODGen reverse-engineering oracle](docs/XLODGEN_REVERSE_ENGINEERING.md) — recovered GUI and engine contracts: [engine internals](docs/LODGEN_ENGINE.md), [Quality/WaterDelta semantics](docs/XLODGEN_QUALITY.md), [full analysis + tools](docs/reverse-engineering/REPORT-xLODGen.md)
- v0.1 release tracker: #1
- terrain-source parity gate: #19

The current v0.1 target is **Commonwealth terrain plus existing authored object LOD models**. Automatic creation of missing object LOD models is planned for v0.2.

## Project goals

- Fallout 4 first.
- One compiler pipeline instead of a chain of unrelated executables and manual intermediate steps.
- Correct plugin/load-order and loose/archive asset resolution.
- Terrain LOD generation from WRLD/CELL/LAND data.
- Static object LOD generation with terrain/water culling.
- Automatic LOD mesh creation after the authored-LOD compiler is proven.
- Material-aware texture stitching/atlasing and correct DDS mip/compression output.
- FO4-specific handling for enable-state-sensitive references, child worldspaces, material swaps, alpha thresholds, backlighting, and fake statics.
- Incremental/resumable generation using content-addressed caches.
- Strong preflight and post-generation validation.
- Headless C++20 CLI/worker core with a separate modern desktop UI.

## Architecture

```text
Fallout 4 / mod manager view
          |
          v
  Virtual Game Data Resolver
  plugins + loose files + BA2
          |
          v
       Record Graph
 WRLD/CELL/LAND/REFR/STAT...
          |
    +-----+-------------------+
    |                         |
    v                         v
Terrain pipeline        Object pipeline
height/water            refs/models/materials
mesh/textures           select or generate LOD
    |                         |
    +------------+------------+
                 v
          LOD block builder
       BTR / BTO / textures
                 |
                 v
       validation + packaging
```

## Current implementation

The native C++20 core currently includes:

- Read-only Fallout 4 environment scanning from a game root or explicit `Data` directory.
- `plugins.txt` and MO2-profile plugin-list input.
- Native ESM/ESP/ESL record-stream parsing for Fallout 4 24-byte record headers.
- TES4 master dependency extraction and validation.
- Stable FormKey resolution from file-local master indices.
- Winning WRLD/CELL/LAND/REFR/STAT resolution.
- Nested GRUP ownership tracking for worldspace/cell context.
- Loose asset inventory.
- Fallout 4 General/DX10 BA2 indexing and General BA2 targeted extraction.
- zlib decoding for compressed plugin records and General BA2 entries.
- Worldspace EDID/exterior CELL/XCLC cataloging and Commonwealth detection.
- Full 33x33 LAND/VHGT height decoding.
- WRLD/CELL water-height resolution.
- Terrain bounds and shared-border seam validation.
- FO4 `.lod` settings parsing and LOD4/8/16/32 block planning.
- Deduplicated terrain block mesh generation.
- xLODGen-style Quality semantics with independent LAND Z-deviation verification.
- 32,767 terrain MaxVerts enforcement.
- Protect Borders support.
- Optimize Unseen + WaterDelta with underwater terrain coarsening and coastline locking.
- FO4 NIF/BTR reader and writer for the current terrain subset.
- BTR root/multibound/AABB/geometry validation.
- Half/full FO4 terrain position precision support.
- BSLightingShaderProperty and BSShaderTextureSet inspection/cloning.
- Packed BTR probing directly from General BA2 archives.
- Automatic installed Commonwealth BTR calibration framework for LOD4/8/16/32.
- Terrain compile previews with Quality/MaxVerts/WaterDelta diagnostics.

The remaining v0.1 work is tracked in issues #2 through #15 plus #19 and summarized in `docs/ROADMAP.md`.

## Current CLI/developer commands

```powershell
# Scan a normal Fallout 4 installation
f4lodgen scan --game "D:\Games\Fallout 4" --plugins "C:\Users\me\AppData\Local\Fallout4\plugins.txt"

# Scan through an MO2 profile
f4lodgen scan --game "D:\Games\Fallout 4" --mo2-profile "D:\MO2\profiles\Default"

# Machine-readable scan output
f4lodgen scan --data "D:\ResolvedData" --plugins "D:\profile\plugins.txt" --json

# Inspect one plugin
f4lodgen inspect-plugin "D:\Games\Fallout 4\Data\Fallout4.esm"

# Build a read-only Commonwealth terrain plan
f4lodgen terrain-plan --game "D:\Games\Fallout 4" --plugins "D:\profile\plugins.txt"

# Inspect a loose BTR
f4lodgen inspect-btr "D:\sample\Commonwealth.4.0.0.btr"
```

Developer calibration utilities are built from the same core and include packed-BTR and installed-Commonwealth calibration probes.

## Build

Requirements:

- CMake 3.25+
- A C++20 compiler
- Git during configure while dependencies are fetched by CMake

```powershell
cmake -S . -B build -DF4LOD_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build\Release\f4lodgen.exe --help
```

On single-config generators the executable is normally under `build/` instead.

`miniz` and `meshoptimizer` are fetched at pinned revisions. Release packaging will not require users to install them separately. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Repository layout

```text
include/f4lod/      Public core interfaces
src/core/           Plugin, archive, terrain, BTR, and pipeline code
src/cli/            Headless command-line/developer frontends
docs/               Architecture, roadmap, research notes, and the xLODGen
                    reverse-engineering oracle (docs/reverse-engineering/)
                    with its compatibility validator tools
tests/              Format and pipeline regression/smoke tests
```

## Third-party policy

Dependencies are evaluated for both technical fit and redistribution terms before adoption. GPL and non-commercial/share-alike components may be useful references or optional user-provided inputs, but should not accidentally determine the licensing of the core application. Current third-party dependencies and exact revisions are recorded in `THIRD_PARTY_NOTICES.md`.
