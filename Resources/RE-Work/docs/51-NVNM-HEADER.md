# 51 - NVNM blob header (first decode)

Probe of the NAVM payload blob flagged in doc 48, from small meshes in
Fallout4.esm. Header confirmed; triangle section needs a dedicated pass
against the CK-side reader.

## 1. Confirmed layout

| offset | type | meaning |
|---|---|---|
| +0x00 | u32 | format version = **15** (constant across samples) |
| +0x04 | u32 | checksum/hash (repeats across meshes of one group) |
| +0x08 | u32 | zero |
| +0x0C | u32 | parent/cell ID (constant within a navmesh group) |
| +0x10 | u32 | **vertexCount N** |
| +0x14 | float3 x N | vertices (X,Y,Z game units) |
| +0x14+12N | u32 | **triangleCount T** |
| after | - | triangle section - variable-length entries, not yet decoded |

Sanity checks that hold: two independent meshes give plausible vertex
coordinates (Y constant within a cell height, X/Z spread across the
cell); T scales with mesh size (4 tris / 296 B blob vs 10 / 518 B).

## 2. Section sequence (from the CK parser)

The loader wraps the blob in a PathingStreamMasterFileRead and hands it
to **`FUN_1426c5970`** (bsnavmesh.cpp), which parses in this order:

| # | section | element size | count source | element reader |
|---|---|---|---|---|
| 1 | header | dispatched | - | factory lookup (`FUN_1426c0ab0`, per-version reader objects) |
| 2 | vertices | **12 B** (float3) | obj+0x20 | `FUN_142754050` |
| 3 | triangles | **24 B fixed** | obj+0x38 | `FUN_142706c90` |
| 4 | extra counts | - | - | `FUN_140efd7b0` |
| 5 | cover/edge entries | **16 B** | runtime | `FUN_142724ef0` |
| 6 | u16 array | 2 B | - | inline |

Correction to §1: the triangle count is NOT stored immediately after
the vertex array as I first read it - all counts arrive through the
versioned header object (section 1). The 296-B sample is consistent
with T=4 fixed 24-B triangles plus tail sections.

Field-level maps for the 24-B triangle and 16-B entry need the
polymorphic version readers decompiled - flagged as the continuation
task.
