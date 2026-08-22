# LOD ecosystem research baseline

Research date: 2026-08-17

This document records the behavior and constraints F4LODGen should preserve or improve. It is a design reference, not a claim that existing projects are obsolete or interchangeable.

## Sources reviewed

- Nexus Mods Fallout 4 article 4162: LOD Generation Instructions
- STEP DynDOLOD / xLODGen support forum
- DynDOLOD classic Nexus page
- DynDOLOD 3 Alpha Nexus page and linked documentation
- FOLIP - New LODs Nexus documentation
- TES5Edit/xEdit source, especially LOD and archive tooling
- fo76utils / libfo76utils
- Microsoft DirectXTex
- meshoptimizer
- Mutagen and nifly for architecture/license comparison

## What the current Fallout 4 workflow actually is

The current Fallout 4 instructions still form a multi-tool pipeline:

1. Install LOD resources such as FOLIP and FO4LODGen Resources.
2. Run FOLIP/xEdit preprocessing when required to create LOD assignments and a patch.
3. Run TexGen in Fallout 4 mode to create stitched object LOD textures.
4. Run xLODGen/FO4LODGen for terrain and object LOD.
5. Keep terrain generation ahead of object generation when terrain geometry is used to cull hidden object triangles.
6. Optionally package the result into BA2 archives.

That workflow is functional, but it makes correctness depend on the user maintaining several intermediate states and knowing which tool owns each problem.

## Fallout 4-specific behaviors worth preserving

The current FO4 guidance exposes several engine-specific controls that should become first-class compiler policies:

- Source alpha threshold when building BTO geometry.
- Backlight power for vegetation/other suitable distant materials.
- Optional vertex-color suppression for problematic inherited worldspaces/interior-window behavior.
- Terrain optimization below water.
- Fake statics for eligible non-STAT base types.
- Force-LOD8-style fallback when an object has a nearer LOD model but lacks the next slot.
- Child worldspace/inheritance handling.
- Enable-state-sensitive distant references.

These should not be hidden as unexplained checkboxes. Preflight should state why a policy is applied and which records it affects.

## Terrain LOD

xLODGen documentation describes terrain LOD as distant ground plus large bodies of water. Generation is based primarily on LAND height data, with world/cell defaults contributing water and fallback values.

The critical coupling is that generated terrain meshes are also useful during object generation to remove object triangles hidden underneath terrain or water. This is why a modern pipeline should treat terrain as a dependency of object optimization, even though terrain can otherwise be generated independently.

Known quality concerns to test explicitly:

- Shared cell/block borders.
- Outer worldspace cells.
- Water height/default inheritance.
- Normal generation consistency.
- Texture mip/format behavior.
- Culling false positives where object geometry intersects terrain.

## Static object LOD

Existing LOD generators build combined distant meshes from references to static objects. They typically select pre-authored optimized LOD models, fall back according to configured rules, transform geometry into LOD blocks, merge compatible material groups, and emit BTO files.

A major limitation documented by DynDOLOD is that it does not automatically create general-purpose reduced LOD models from arbitrary full-detail models. Appropriate LOD assets still need to exist or a full model must be used as a costly fallback.

That gap is the strongest reason to build F4LODGen as more than a replacement frontend.

## Automatic model generation opportunity

For Fallout 4, an automatic model path should be conservative:

1. Parse the full NIF and material graph.
2. Classify whether the model can safely become static distant geometry.
3. Split/protect material and UV boundaries.
4. Simplify with an appearance/error metric.
5. Produce multiple target levels.
6. Rewrite only the subset of NIF/material data required by object LOD.
7. Render/measure the result against the source from fixed viewpoints.
8. Reject bad reductions automatically.

`meshoptimizer` is attractive because it is MIT licensed and currently provides attribute-aware simplification, border/vertex locking, topology-preserving and permissive modes, absolute/relative error control, and LOD-chain guidance.

Triangle percentage alone should never be the quality metric. A thin radio tower and a concrete wall can tolerate very different reductions at the same triangle count.

## Texture generation

TexGen demonstrates two useful approaches:

- Stitch LOD textures from source textures used by LOD models.
- Render texture/billboard representations from full models where appropriate.

For the FO4 object pipeline, the MVP should prioritize deterministic stitched/atlased texture generation for static object LOD. Automatic billboard generation is a separate feature and should not block correct BTO/BTR output.

Current Fallout 4 community guidance uses different compression choices for object diffuse, object normal/specular, terrain diffuse, and terrain normal outputs. F4LODGen should expose these as named compatibility presets while recording the exact pixel format and encoder settings in the build manifest.

DirectXTex is the preferred candidate for DDS decode/resize/mips/BC compression on Windows because it is MIT licensed and maintained by Microsoft.

## Load order and asset order are separate problems

The tool must not assume that the winning plugin record owns the winning NIF/material/texture. Fallout 4 data can come from loose files and multiple BA2 archives with precedence distinct from plugin override order.

The internal model should therefore have two resolvers:

- Record resolver: winning plugin records and FormID/master relationships.
- Asset resolver: winning normalized path among loose/archive sources.

Diagnostics should be able to answer both "which plugin caused this reference?" and "which mod/archive supplied the mesh that was actually used?"

## FOLIP lessons

FOLIP has significantly expanded Fallout 4's pool of usable distant models and its current tooling performs useful preprocessing work such as LOD assignment, naming/rule handling, material variants, fake statics, and child-worldspace logic.

F4LODGen should learn from those requirements, but FOLIP assets/rules have their own redistribution terms. The safe design is:

- Treat an installed FOLIP setup as a supported input source.
- Reimplement general record/asset analysis behavior from documented game semantics.
- Do not copy or bundle FOLIP assets/rule datasets without explicit license review and permission/compatibility.

Eventually F4LODGen can remove the need for a separate preprocessing script by generating its own minimal patch, but that should follow a correct reader/generator implementation rather than lead it.

## xEdit/xLODGen lessons

xEdit's LOD code is valuable as an implementation and format-behavior reference. It is MPL-2.0, which is substantially easier to coexist with than GPL code but still requires care when copying source files.

The preferred approach is to implement clean subsystem boundaries and use independent/permissive readers where available, then compare behavior and generated outputs against xEdit/xLODGen as a compatibility oracle.

BSArch in the xEdit tree can pack Fallout 4 General and DDS BA2 archives and is also MPL-2.0. Archive writing can therefore be implemented later either from format behavior/specification or as a clearly isolated MPL-derived component if the project license strategy permits it.

## Permissive foundation candidates

### fo76utils / libfo76utils

License: MIT.

Useful existing code includes:

- `ESMFile`: Creation Engine plugin reader with explicit Fallout 4 support and FormID/plugin mapping.
- `BA2File`: archive/loose-file resolver supporting Fallout 4 BA2 and texture archives.
- DDS decoding utilities including BC6/BC7 support.
- Worldspace/landscape rendering tools in the parent project.
- Fallout 4-oriented NIF/material/world rendering code in the repository that should be audited before reuse.

This is currently the best starting point for avoiding a years-long detour into basic Bethesda file parsing.

### DirectXTex

License: MIT.

Candidate responsibilities:

- DDS loading/writing.
- Resize and format conversion.
- Mipmap generation.
- BC1/BC5/BC7 compression.
- Optional GPU-assisted texture conversion paths where appropriate.

### meshoptimizer

License: MIT.

Candidate responsibilities:

- Vertex/index optimization.
- Attribute-aware mesh simplification.
- Controlled seam/border preservation.
- LOD-chain error metrics.

## Components not chosen as core dependencies

### Mutagen

Mutagen is a mature typed .NET library for Bethesda plugins and has excellent load-order/winning-override abstractions. Its GPL-3.0 license makes it a poor default dependency if F4LODGen intends to keep a permissively licensed core. It remains useful as an architectural reference and independent test oracle.

### nifly

nifly has strong Fallout 4 NIF read/write capability but is GPL-3.0. The same licensing concern applies. Prefer a permissive implementation for the core unless the project intentionally chooses GPL compatibility.

## UI direction

The generator core should stay native/headless. A Tauri 2 desktop shell is a reasonable modern UI layer because Tauri is MIT/Apache-2.0 and allows a React/TypeScript frontend without forcing the expensive generation engine into web technology.

The UI should communicate with a worker using structured messages/events, not parse stdout intended for humans.

Example event shape:

```json
{
  "type": "task.progress",
  "stage": "generate-object-lod",
  "worldspace": "Commonwealth",
  "task": "block:-4,8:lod8",
  "completed": 137,
  "total": 412,
  "memoryBytes": 1842032640,
  "cache": "miss"
}
```

## Design principles derived from the research

1. Correctness before feature count.
2. Fallout 4 semantics before cross-game abstraction.
3. One reproducible DAG instead of manual tool chaining.
4. Automatic LOD creation must be safety-checked, not blind decimation.
5. Treat terrain as an object-optimization dependency.
6. Treat plugin and asset precedence separately.
7. Cache immutable intermediate artifacts aggressively.
8. Every generated artifact should have provenance.
9. Every warning should name the affected record/asset and remediation.
10. The CLI is the product API; the desktop UI is one client of it.

## Compatibility test strategy

Build a small controlled Fallout 4 fixture set with cases for:

- Single exterior LAND cell.
- Adjacent LAND cells sharing a border.
- Missing outer cell.
- Water override.
- Authored `_lod_0/_lod_1/_lod_2` static.
- Static with only one LOD level.
- Static with no authored LOD but safe simplifiable geometry.
- Tiled/out-of-range UV model.
- Alpha-tested vegetation/material.
- Backlit material.
- Material swap and color remap.
- Stateful/enable-parent reference.
- Child worldspace.
- Loose NIF overriding archive NIF.
- Texture archive overriding older archive content.

For each fixture, compare the semantic result and, where practical, rendered output against known-good xLODGen/FOLIP/TexGen behavior. Binary byte-for-byte equivalence is not required unless the format demands it; visual and engine-semantic equivalence is the target.

## Reference links

- https://www.nexusmods.com/fallout4/articles/4162
- https://stepmodifications.org/forum/forum/223-dyndolod-xlodgen/
- https://www.nexusmods.com/skyrimspecialedition/mods/32382
- https://www.nexusmods.com/skyrimspecialedition/mods/68518
- https://github.com/TES5Edit/TES5Edit
- https://github.com/fo76utils/fo76utils
- https://github.com/microsoft/DirectXTex
- https://github.com/zeux/meshoptimizer
- https://github.com/Mutagen-Modding/Mutagen
- https://github.com/ousnius/nifly
