# 25 — BGSCombinedCellGeometryDB Format

Full reverse-engineering of the `bgscombinedcellgeometrydb.cpp` module — the creation kit's
per-cell combined-geometry index that maps every CELL to its `_OC.NIF` / `_Physics.NIF` references
and writes them out as `CombinedObjects.esp`.

**Module:** `shared/tesforms/world/bgscombinedcellgeometrydb.cpp`  
**Image:** `CreationKit.exe` 1.11.137.0, base `0x140000000`

---

## 1. What It Is

The BGSCombinedCellGeometryDB is a **paged BTree on-disk index** (BSTBTreeFile, from
`bssystem/bstbtreefile.h`) that persists the cell→combined-geometry mapping. The runtime
(in Fallout4.exe) reads this index to know which `_OC.NIF` files to load for each loaded
cell, without scanning the filesystem.

On disk, it takes the form of a standard Bethesda `.esp` plugin — `CombinedObjects.esp` —
with one record per cell that has combined geometry. The BTree pages are the internal
index structure used during generation; the final output is the ESP.

---

## 2. In-Memory Data Structures

### 2a. The BTree (`BSTBTreeFile<CellKey, ExtraCombinedRefs*>`)

Allocated by `PreCombinedWorld` (`FUN_140d20d90`):

```c
// local_5a0: BSTBTreeHandle<char*,ExtraCombinedRefs*>
thunk_FUN_142002200(&btreeHandle);           // init handle
thunk_FUN_142002270(&btreeHandle, 0x100);     // alloc BTree, 256 leaf capacity
```

**Key type:** 32-bit cell key (derived from cell formID, see §5).  
**Value type:** pointer to `ExtraCombinedRefs` (0x68 bytes, type tag 0xC6).

**Page faults:** BTree corruption produces the following asserts (from `bstbtreefile.h`):
- `"Corrupted arena, pHead == pCurr should mean last page."`
- `"Corrupted arena, pHead != Curr should mean more pages."`
- `"Invalid page file."`
- `"Failed to acquire page, increase PAGE_COUNT"`
- `"Invalid pin count"`
- `"Cursor going out of scope with active pages thereby leaking"`
- `"Cursor not positioned"`
- `"Circular free list"`

### 2b. The Scatter Table (ref ID → cell lookup)

Before the BTree, a **BSTScatterTable** is built for fast ref-ID lookups:

```c
// Capacity rounds up to next power-of-2, minimum 64 entries
size = 1;
while (size < 64) size *= 2;   // local_4dc grows from 1

// Alloc: size * 0x18 bytes per entry
table = heapAlloc(size * 0x18);
```

The scatter table hash function and key format match `BSTScatterTable.h`:
each entry is 24 bytes: `[key:8][value:8][next:8]`.

### 2c. ExtraCombinedRefs (0x68 bytes, type tag 0xC6)

Allocated by `PreCombinedWorld` at the start of cell processing:

| offset | size | field | notes |
|---|---:|---|---|
| +0x00 | 8 | `ExtraCombinedRefs::vftable` | RTTI: `.?AVExtraCombinedRefs@@` |
| +0x08 | 8 | BSTArrayBase (size+capacity) | ref-counted ptr to entry array |
| +0x10 | 8 | BSTArrayBase (size+capacity) | second array |
| +0x12 | 1 | **type tag = 0xC6** | FO4 extra-data type enum |
| +0x18 | 8 | BSTScatterTable data | hash table for refs |
| +0x24 | 4 | scatter table capacity | power-of-2 |
| +0x28 | 4 | scatter table size | used slots |
| +0x2C | 4 | scatter table capacity copy | mirror for rehash |
| +0x30 | 8 | BSTScatterTable entries ptr | allocated separately |
| +0x38 | 8 | linked list / next ptr | `
| +0x40 | 8 | count / cursor | active count |
| +0x48 | 8 | BSTArray of int32 | per-entry form IDs |
| ... | ... | (remaining fields) | ref-counted handles |

The two arrays at +0x08 and +0x10 are `BSTArray` growable vectors. The scatter table
maps ref formIDs to array indices for fast combined-ref lookups.

---

## 3. The Two Processing Paths

### 3a. Exterior Cells

```c
if ((cell->flags & 1) == 0) {           // exterior
    GetCellCoords(cell, &cx, &cy);
    // Gather eligible refs from cell:
    FUN_140d398d0(cell, &refList);       // collect combined-eligible REFRs
    FUN_140d39fa0(&btree, &refList);    // insert into BTree
}
```

For exterior cells, the ref collection walks all placed objects in the cell,
filtering for those with combined-geometry-eligible base forms. The BTree key
is derived from cell coordinates or formID (the `"Attempting to generate key for
cell %08x that is an exterior but has no worldspace."` error fires when the
worldspace ptr is null during key derivation).

### 3b. Interior Cells

```c
if ((cell->flags & 1) != 0) {           // interior
    // Separate BTree for interiors:
    thunk_FUN_142002200(&interiorBTreeHandle);
    thunk_FUN_142002270(&interiorBTreeHandle, 0x80);  // 128 leaf capacity
    
    FUN_140d395c0(cell, &refList);       // interior cell ref collection
    FUN_140d3a0f0(&interiorBTree, &refList);  // insert
}
```

Interior cells use a smaller BTree (128 vs 256 leaves) and collect only persistent
references (no grid-walk needed).

---

## 4. Combined Geometry Output Files

### 4a. `_OC.NIF` — Per-Entry Geometry

From `FUN_140d4f160` (path builder) and `FUN_14048a890` (static-collection routine):

```
Data\Meshes\PreCombined\<Group>\<OwnerID>_<EntryID>_OC.NIF
```

- `<Group>` — optional collection group name from the associated static collection
- `<OwnerID>` — owning form ID masked to 24 bits (`formID & 0x00FFFFFF`)
- `<EntryID>` — the 32-bit combined-entry ID

The NIF contains `BSCombinedTriShape` blocks (RTTI: `.?AVBSCombinedTriShape@@`),
which are the FO4 runtime's native combined-geometry shape. The wire format is
standard Gamebryo 20.2.0.7 with FO4 extensions (userVersion=12, BSHeader).

### 4b. `_Physics.NIF` — Per-Cell Havok Physics

```
Data\Meshes\PreCombined\<Group>\<OwnerID>_Physics.NIF
```

One physics companion per cell/owner. Contains `bhkNiTriStripsShape` blocks
from the Havok physics world (`hknpPhysicsSystemData` vftable at `0x143c27308`).
The physics pipeline:

1. Allocate `hknpPhysicsSystemData` (0x78 bytes)
2. Init with `0xffff0001` flag mask, 6 body slots with `0x80000000` capacity
3. Populate bodies from combined-cell collision shapes
4. If capacity exhausted: `thunk_FUN_141c6c0a0` grows arrays by 0x60/8 bytes
5. Serialize through NiStream save chain → `_Physics.NIF`

### 4c. Static Collection Meshes

```
Data\Meshes\PreCombined\SCOL\<Group>\CM<ID>.NIF
```

Format string at `0x143075570`: `SCOL\\%s\\CM%08X.NIF`. These are the
static-collection (`SCOL`) combined meshes — separate from the cell `_OC` family.

### 4d. Non-Combined Objects

The setting `bForcePreCombinedObject32bitExport:General` controls whether
32-bit form IDs are forced in the export. `bUseCombinedObjects:General` gates
runtime use of the combined object system.

---

## 5. Cell Key Derivation

The BTree key for exterior cells is derived from the cell formID (the `%08x` in
the error string). The function that generates keys:

```
Attempting to generate key for cell %08x that is an exterior but has no worldspace.
```

This fires in the key generator when an exterior cell has a null worldspace pointer.
The key is likely `(cellFormID & 0x00FFFFFF) | (worldspaceIndex << 24)` — a packed
32-bit key that uniquely identifies cells across worldspaces. Interior cells use
their raw formID as the key (they have no worldspace).

---

## 6. `CombinedObjects.esp` Output Format

The DB is serialized as `Data\CombinedObjects.esp` — a standard Bethesda plugin
containing:

| record type | purpose |
|---|---|
| TES4 | standard plugin header; master `Fallout4.esm` |
| CELL (exterior) | persistent-cell records with XCLC coords, `ExtraCombinedRefs` extra data |
| CELL (interior) | interior cell records with `ExtraCombinedRefs` extra data |
| REFR | persistent combined-object references (the individual `_OC.NIF` owners) |

The `ExtraCombinedRefs` extra data block (type `0xC6`) is the critical piece —
it contains the per-cell list of combined-ref formIDs. Each REFR has an attached
NIF path that the runtime resolves to the matching `_OC.NIF` file.

### Wire Format for `ExtraCombinedRefs` subrecord:

In the ESP, each CELL record gets an `XCRF` subrecord (or similar) containing
the array of combined-ref formIDs. The exact 4CC and structure are:

```
XCRF [count:u32] [refFormID:u32]...  (count entries)
```

Or possibly:

```
XCRO [count:u32] [refFormID:u32] [meshPath:zstring]...  (count entries)
```

This needs verification against a real `CombinedObjects.esp` sample.

---

## 7. Full Pipeline Summary

```
-GeneratePreCombined:F4.esm clean all 1 1
  │
  ├─► PreCombinedWorld (0xd20d90)
  │     ├─► Create BTree (256 leaves)
  │     ├─► Create ScatterTable (min 64 entries)
  │     ├─► For each cell:
  │     │     ├─► FUN_140d398d0 / 0xd395c0 — gather eligible REFRs
  │     │     ├─► FUN_140d39fa0 / 0xd3a0f0 — insert into BTree
  │     │     ├─► Alloc ExtraCombinedRefs (0x68 bytes, tag 0xC6)
  │     │     ├─► For each combined ref entry:
  │     │     │     └─► Write <ID>_<entryID>_OC.NIF + <ID>_Physics.NIF
  │     │     └─► Attach ExtraCombinedRefs to CELL via FUN_140d38ac0
  │     └─► FUN_140d3ba80 — flush BTree to disk
  │
  └─► Save CombinedObjects.esp
        ├─► TES4 header
        ├─► CELL records with XCRF extra data
        └─► REFR records for combined objects
```

---

## 8. Relevant Settings

| setting | section | effect |
|---|---|---|
| `bGeneratePreCombinedGeometryPreVis` | General | pipeline enable gate |
| `bForcePreCombinedObject32bitExport` | General | 32-bit formID forcing |
| `bUseCombinedObjects` | General | runtime combined-object loading |
| `bThrowProcessCompleteExceptions` | General | batch exit-code mapping |

---

## 9. Open Questions

1. **XCRF subrecord 4CC** — confirmed 4CC and exact wire format of the
   `ExtraCombinedRefs` subrecord in `CombinedObjects.esp`. Verify against a
   real output file via byte-parse.

2. **Cell key encoding** — confirm the packed key format
   `(formID & 0xFFFFFF) | (worldspaceIndex << 24)` vs. raw formID, and how
   interior cells avoid worldspace-index collisions.

3. **BSTBTreeFile page size** — the BTree uses 256-leaf capacity; confirm
   the page size, fanout, and on-disk block layout from `bstbtreefile.h`.

4. **BSCombinedTriShape binary format** — the exact NIF block layout for
   this class (vertex format, index buffer, material binding, and how it
   differs from `BSSubIndexTriShape`).

5. **Per-entry ID assignment** — how the `_<EntryID>` component is assigned.
   Is it the ref formID, a sequential counter, or a hash?

6. **`ExtraCombinedRefs` cleanup** — the `FUN_140d38ac0` attach function
   and `FUN_1403dbf30` free function; confirm they operate on the BTree
   cursor vs. direct cell attachment.

---

## 10. Cross-References

- **Doc 09** (`09-PRECOMBINED-PREVIS.md`) — PreCombined CLI grammar, dispatcher, process runner
- **Doc 14** (`14-SCENE-FORMAT.md`) — PreVis `.scene` binary format
- **Doc 23** (`23-CELL-WORLDSPACE.md`) — CELL/WRLD/REFR class layout, LoadRecord/SaveRecord slots
- **Doc 18** (`18-ELRIC-PDB.md`) — NiStream save chain, `BSCombinedTriShape` class layout
- **`ghidra-dump/decompile-SCOL-combined-mesh-writer.md`** — SCOL mesh naming, NiStream path
- **`ghidra-dump/decompile-PreCombinedWorld-0xd20d90.log`** — full PreCombinedWorld decompile
- **`ghidra-dump/decompile-PreVisPreCombine-Execute-0x48f920.log`** — Execute state machine

---

## 11. Decompile Artifacts

| artifact | location |
|---|---|
| PreCombinedWorld | `ghidra-dump/decompile-PreCombinedWorld-0xd20d90.log` (RVA 0xd20d90) |
| PreVisPreCombine Execute | `ghidra-dump/decompile-PreVisPreCombine-Execute-0x48f920.log` (RVA 0x48f920) |
| Per-cell builder (interior) | `FUN_140d395c0` (RVA 0xd395c0) |
| Per-cell builder (exterior) | `FUN_140d39fa0` (RVA 0xd39fa0) |
| ExtraCombinedRefs alloc | `FUN_140d38ac0` (RVA 0xd38ac0) |
| BTree flush | `FUN_140d3ba80` (RVA 0xd3ba80) |
| SCOL path builder | `ghidra-dump/decompile-SCOL-combined-mesh-writer.md` |