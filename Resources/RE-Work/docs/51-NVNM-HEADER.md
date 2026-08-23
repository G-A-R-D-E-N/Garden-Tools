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

## 2. Triangle section observations

Raw hex shows recurring 3x u16 vertex-index triplets followed by a
repeating tail (`ff ff .. 7f 7f 00 00 08`), indicating per-tri edge/
flag payloads of non-fixed width. Blind decoding stalls here - next
step is locating the CK's NVNM consumer (via the loader at
FUN_140efe470 or the 'NVNM' 4CC xref) and reading the parse loop.
