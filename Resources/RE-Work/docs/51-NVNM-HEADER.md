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

## 2a. Empirical validation (7,894 blobs)

* Version field is **always 15** in shipped masters.
* Header `[version][hash][0][packed cell coords][vertexCount]` +
  float3 vertex array confirmed across all sampled blobs (u32[3] values
  like 246000/492200/828000 are packed cell coordinates, not formIDs -
  corrects §1).
* The section AFTER vertices is **bit-packed**, not byte-aligned:
  no fixed byte stride fits the post-vertex region across blobs. The
  loader wraps the payload in `PathingStreamMasterFileRead`/
  `BSPathingStreamRead`, whose read methods are bit-level. All element
  layouts in §3 are therefore LOGICAL (post-unpacking) structures;
  decoding the bit-stream packing itself requires the stream class's
  reader internals - flagged as the remaining task.

## 2b. Section sequence (from the CK parser)

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

## 2c. Stream slot semantics (PathingStreamMasterFileRead vtable)

* slot +0x30 (`FUN_140f358d0`): consumes **4 bytes**, stores low 16 bits
* slot +0x20 (`FUN_140f35970`): consumes **8 bytes**
* slot +0x08 (`FUN_140f35a30` region): 4-byte float path

Therefore the triangle reader's six +0x30 calls plus one +0x20 call
consume about **32 on-disk bytes per triangle** for version 15 - not
the 16-byte stride assumed in the first empirical pass (which
over-counted T by fitting windows coincidentally). Re-deriving the
post-vertex offsets with a 32-B stride is the immediate next step;
the bit-packed hypothesis from §2a is superseded by these plain
fixed-width reads.

## 3. Element readers (decoded)

* **Vertex** (`FUN_142754050`): three f32 reads - X, Y, Z.
* **Triangle** (`FUN_142706c90`, in-memory 0x18 B):
  | offset | type | meaning |
  |---|---|---|
  | +0x00 | u16 x3 | vertex indices (0xFFFF = no connection) |
  | +0x06 | u16 x3 | edge/adjacency indices (same sentinel) |
  | +0x0C | u32 | version > 0x39 only: extra field (default constant otherwise) |
  | +0x10 | u32 | flags word |
  | +0x14 | u8 | derived from flags for version <= 0x39: bit4->0x01, bit6->0x02, bit9->0x04, bit12->0x08 |

  On-disk per-tri size is version-dependent (the <=0x39 path serializes
  12 B of indices + 4 B flags; the +0x0C field exists only on newer
  writes).
* **16-B cover/edge entry** (`FUN_142724ef0`): an 8-byte key block via
  `FUN_142724720` plus a u16 counter at +0x08 - structure confirmed,
  key semantics deferred.

## 4. On-disk triangle record SOLVED: 21 bytes, stride validated 60/60

Brute-force alignment search over 60 blobs gives a unique perfect fit:
triangles start immediately after the vertex array as `[u32 T]` then
**T x 21 bytes**:

| offset | type | meaning |
|---|---|---|
| +0x00 | u16 x3 | vertex indices |
| +0x06 | u16 x3 | neighbour-triangle refs per edge (0xFFFF = border) |
| +0x0C | f32 | edge metric - real lengths inland, **FLT_MAX sentinel on perimeter tris** |
| +0x10 | u16 | additional triangle ref (cover/link candidate) |
| +0x12 | u8 | constant 0x08 |
| +0x13 | u8 | flags (0..3 observed) |
| +0x14 | u8 | pad |

Semantic names for +0x10/+0x13 are inferred from value distributions,
not yet reader-confirmed. The earlier stream-slot width analysis
(section 2c) describes the in-memory read API; the shipped files are
plain little-endian 21-B records - no bit unpacking needed (supersedes
§2a's packing hypothesis).
