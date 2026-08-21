# 23 · CELL / Worldspace Record Model

CreationKit.exe 1.11.137.0 (64-bit). This doc maps the four world-building
record classes — `TESWorldSpace` (WRLD), `TESObjectCELL` (CELL),
`TESObjectLAND` (LAND), `TESObjectREFR` (REFR) — from RTTI through vtables to
the source files and the creation/dispatch paths. It corrects doc 12's "no
factory class" claim for these records.

Analysis method: fresh full-auto Ghidra 12.1 import of `CreationKit.exe`
(persistent project `Projects/CKGhidra`); RTTI type-descriptor → x64
`RTTICompleteObjectLocator` (4-byte-RVA `pTypeDescriptor` at COL+0xC) →
vftable (slot 0 == COL) chain resolved by `ghidra-dump/FindVftable.java`.
Raw dumps: `ghidra-dump/cell-worldspace-vtables/*.vftable.txt`.

---

## 1. RTTI + vftable inventory

| class | 4CC | enum idx | RTTI name | vftables (RVA) | override code range |
|---|---|---|---|---|---|
| `TESWorldSpace` | WRLD | 74 (0x4A) | `.?AVTESWorldSpace@@` | 3: `0x30aa510`, `0x30aa9d8`, `0x30aaac0` | `0x140e4xxxx` (`tesworldspace.cpp`) |
| `TESObjectCELL` | CELL | 63 (0x3F) | `.?AVTESObjectCELL@@` | 2: `0x309c780` (primary), `0x309cc30` (secondary) | `0x140d1xxxx` / `0x140d44xxx` (`tesobjectcell.cpp`) |
| `TESObjectLAND` | LAND | 75 (0x4B) | `.?AVTESObjectLAND@@` | 2: `0x309a5c8`, `0x309aa78` | `0x140ce/0x140d0xxxx` (`tesland.cpp`) |
| `TESObjectREFR` | REFR | 64 (0x40) | `.?AVTESObjectREFR@@` | **9** vtables | `0x140d9xxxx` + interface slots |

Every vftable begins with the COL pointer (slot 0) and a scalar-deleting
destructor (slot 1). Slots 2–22 are the inherited `TESForm` surface (identical
addresses across all four classes — see §3); class-specific overrides begin at
slot 23.

The vftable finder resolved the chain via the x64 RTTI layout:
`TypeDescriptor { pVFTable +0, spare +8, name +0x10 }`, `COL { signature +0,
offset +4, cdOffset +8, pTypeDescriptor +0xC (4-byte RVA) }` — the earlier
8-byte-pointer scan failed because x64 MSVC stores the type descriptor as an
**RVA**, not a VA.

## 2. Form factories — doc 12 correction

Doc 12 listed CELL/REFR/WRLD/LAND as "(no RTTI factory class)". That is
**wrong for CELL/WRLD/LAND**. All three have `ConcreteFormFactory` RTTI
classes; the earlier dump regex missed the `?$`-prefixed template mangling
(`.?AV?$ConcreteFormFactory@...`):

| class | factory RTTI | encoded idx | decoded |
|---|---|---|---|
| `TESObjectCELL` | `.?AV?$ConcreteFormFactory@VTESObjectCELL@@$0DP@@@` | `$0DP` | D=3, P=15 → **0x3F = 63 = CELL** |
| `TESWorldSpace` | `.?AV?$ConcreteFormFactory@VTESWorldSpace@@$0EK@@@` | `$0EK` | E=4, K=10 → **0x4A = 74 = WRLD** |
| `TESObjectLAND` | `.?AV?$ConcreteFormFactory@VTESObjectLAND@@$0EL@@@` | `$0EL` | E=4, L=11 → **0x4B = 75 = LAND** |
| `TESObjectREFR` | *no* `ConcreteFormFactory` — has **`TESObjectREFRFactory`** + **`TESObjectREFRSerializer`** RTTI (`.?AVTESObjectREFRFactory@`, `.?AVTESObjectREFRSerializer@`) | — | special-cased; see §7 |

The MSVC template-int encoding is per-nibble `0-9→'0'-'9'`, `10-15→'A'-'F'`…
empirically `A=0..P=15` by position (`$0JH`=151=AORU cross-checks from doc 12;
`$0DP`=0x3F). The decoder in doc 12 §5 had a systematic off-by-one; the factory
*registration call* (`thunk_FUN_1408bc350(idx, ...)`) is the ground truth, and
for these three it matches the formEnumString index.

Factory vftable (e.g. CELL at RVA `0x309c540`):

| slot | function | role |
|---|---|---|
| 0 | COL | RTTI |
| 1 | `thunk_FUN_140d13850` | **Create** — `*this = ConcreteFormFactory<TESObjectCELL,63>::vftable; factory_slot_clear(0x3F); ...` |
| 2 | `thunk_FUN_140d135c0` | (template ctor helper) |
| 5–7 | `FUN_1408bc290/2a0/2b0` | registry getter/setter/clear (doc 12 family) |

## 3. The TESForm base surface (slots 2–22)

Identical thunk targets in TESForm and all four classes — the inherited
virtuals (destructor chain, form identity, flag accessors, registration):

```
slot 2  FUN_140339c10      slot 13 FUN_1408c12b0
slot 3  FUN_1408bca90      slot 14 FUN_14032dd40
slot 4  FUN_1408bcb80      slot 15 FUN_1408c2b90
slot 5  FUN_14032dba0      slot 16 FUN_1408c2ca0
slot 6  FUN_14032dbc0      slot 17 FUN_1408c2ce0
slot 7  FUN_1408bf520      slot 18 FUN_1408c2fa0
slot 8  FUN_1408bf760      slot 19 FUN_1408c3400
slot 9  FUN_1408c0ce0      slot 20 FUN_14039c060
slot 10 FUN_14032dc40      slot 21 FUN_14032dd90
slot 11 FUN_1408c0e90      slot 22 FUN_1408bca80
slot 12 FUN_14032dd30
```

Slot 23 onwards is where each class diverges (the `0x140d/0x140e/0x140cf/0x140ce`
ranges) — those are `LoadRecord`/`SaveRecord`-family and cell/land/worldspace
specific virtuals. REFR additionally has ~8 interface vtables (see §7).

## 4. Source mapping (confirmed by `__FILE__` strings)

Runtime classes (embedded engine), `code\shared\tesforms\world\`:

| source | class |
|---|---|
| `tesobjectcell.cpp` | TESObjectCELL core |
| `tesobjectcell_reference.cpp` | cell → reference association |
| `tesland.cpp` | TESObjectLAND (heightfield) |
| `teslandtexture.cpp` | TESLandTexture |
| `tesworldspace.cpp` | TESWorldSpace |
| `tesobjectrefr.cpp` + `_animation` / `_events` / `_extradata` | TESObjectREFR |
| `exteriorcellloader.cpp` | exterior cell streaming |
| `bgsloadedreferencecollection.cpp`, `bgslargerefdata.cpp`, `maxheightdb.cpp` | cell-adjacent DBs |

Editor forks (CK-specific), `code\construction set\tesforms\`: `tesobjectcell_editor.cpp`,
`tesobjectrefr_editor.cpp`, `tesworldspace_editor.cpp`, `tesland_editor.cpp`,
`teslandtexture_editor.cpp`. The editor adds UI/serialization behavior on the
same runtime classes (no separate `*_editor` RTTI classes exist).

Shared I/O: `code\shared\fileio\tesdatahandler.cpp` (the record container),
`tesfilecontainer.cpp` (file reader — doc 12).

## 5. Creation dispatch

`TESDataHandler::CreateForm(type)` = `FUN_140865b10` (`tesdatahandler.cpp`,
assert line 0x1bc4):

```c
form = factory_getter(type & 0xFF)            // FUN_1408bc2f0 (doc 12 §3)
if (form)  return (*form->vftable[1])(form);  // factory Create virtual
// else log "TESDataHandler trying to create TESForm for unknown type '%s'."
//      (4CC looked up in formEnumString via PTR_DAT_143b9a548[type*3])
```

`TESDataHandler::NewCell(worldspace, x, y)` = `FUN_14085ae30`
(`tesdatahandler.cpp`, assert 0x62a): bounds-checks coords
(`x+0x6E <= 0xDC` and same for y — i.e. |coord| ≤ 110 …), constructs the cell
via `thunk_FUN_140d13940` (the `TESObjectCELL` ctor reached from the factory
thunk chain), sets cell flag bit 1 (exterior), calls the coordinate setter
`thunk_FUN_140d197c0(cell, x, y)`, then inserts into the worldspace's cell map
via `thunk_FUN_140e4d180(worldspace, cell)`.

Errors surfaced: `"Invalid X coord passed to TESDataHandler::NewCell"`,
`"Invalid Y coord ..."`, `"Attempting to generate key for cell %08x that is an
exterior but has no worldspace."`, `"nullptr returned cell, we should always at
least create a cell on demand"`.

## 6. GRUP / group structure (world file layout)

The group-name formatter `FUN_140676cd0` (xrefs the group-label strings)
recovers the full group-type enum used by the file reader:

| type | label |
|---|---|
| 1 | `Group: Worldspace (%08X) Children` |
| 2 | `Group: Interior Cells Block %u` |
| 3 | `Group: Interior Cells Sub-block %u` |
| 4 | `Group: Exterior Cells Block (%i, %i)` |
| 5 | `Group: Exterior Cells Sub-block (%i, %i)` |
| 6 | `Group: Cell (%08X) Children` |
| 7 | `Group: Topic (%08X) Children` |
| 8 | `Group: Cell (%08X) Persistent Children` |
| 9 | `Group: Cell (%08X) Temporary Children` |
| 10 | `Group: Quest (%08X) Children` |
| other | `Group: UNKNOWN TYPE` (also `Group: Type %s` for record-type groups) |

This is the standard TES4 GRUP hierarchy: WRLD → (Interior/Exterior Cell
Blocks → Sub-blocks) → CELL → (Persistent/Temporary Children + LAND/NAVM/REFR
children). The reader emits `"Form Group at offset %I64u has invalid group
type."` for bad group headers.

## 7. TESObjectREFR — the special case

REFR has **no** `ConcreteFormFactory`; instead RTTI shows
`TESObjectREFRFactory` and `TESObjectREFRSerializer`. Its 9 vtables reflect the
reference's multiple interfaces. RTTI also shows the reference-typed
functors: `.?AUIDecalRefFunctor@TESObjectCELL@@`,
`.?AVIAliasFunctor@TESObjectREFRDef@@`,
`.?AVILinkedReferenceFunctor@TESObjectREFRDef@@`,
`.?AVHandleOutfitItemsFunctor@TESObjectREFR@@`.

The reference is split across four source files (`tesobjectrefr.cpp`,
`_animation`, `_events`, `_extradata`), and its serialization is handled by
the dedicated `TESObjectREFRSerializer` rather than the generic
`ConcreteFormFactory` Create path.

## 8. Cell-object layout hints (from decompiled overrides)

`FUN_140d13f00` (`tesobjectcell.cpp` assert 0x49f — cell teardown/clear) reads:

| offset | meaning |
|---|---|
| +0x10 | form flags (`>>14 & 1` = has-multibound?; `>>3 & 1`; `>>1 & 1` = …) |
| +0x50 | byte flag (`& 1` = persistent-data loaded?); the `*cell` flags field |
| +0x58 | per-cell resource handle |
| +0x60 | cell ref-array pointer (released via `FUN_140f8d1f0`) |
| +0x150 | multibound / linked collection (cleared via `thunk_FUN_1408c0210`) |
| +0x16C | int reset to 0 |

`FUN_140d44f00` (ref-registration / InitItem path) walks the cell's ref array
at `+0x10` (`count at param_1[0x12]` → 0x48) and IncRef/DecRefs each via
`BSHandleRefObject` (`bshandlerefobject.inl`), then registers with the data
handler (`thunk_FUN_14087d2d0`), and calls `thunk_FUN_140d55ca0` (probably
cell → worldspace association). Cell coord setter is `thunk_FUN_140d197c0`.

## 9. Known record strings (load/save surface)

CELL: `XCLC` (coords), `XCLW`, `XCLL` (lighting), `XCLR`, `XCLP`, `XCLS`,
`XCMO`, `XCWT` (water), `XCCM`, `XCGD`, `XCIM`, `XCET`, `XCHG`, `XCNT`,
`XCPD`, `XCRI`, `XCVL`, `XCVR`, `XCWC`, `XCZA`, `XCZC`, `XCZR`, `XWEM`,
`XWCU`, `XWCN`, `XLOD` (LOD settings), `XDCR`, `XEMI`, `XENC`, `XEPA`,
`XEPH`, `XEQC`, `XESP` (enable parent), `XEZC`, `XEZN`, `XLCN`, `XLRM`,
`XMRK` (map marker), `XNDP`, `XOWN` (ownership), `XPLK`, `XPPA`, `XPRD`,
`XPTL`, `XRTM`, `XSCL`, `XSED`, `XSOL`, `XSPC`, `XTRI`, `XWPN`.

WRLD: `WNAM`, `CNAM` (climate), `SNAM` (sound), `LNAM`, `MNAM`, `DNAM`,
`RNAM` (parent), `OFST`, `ONAM` (LOD data), `NAM0`–`NAM9` (map offsets),
`MHDT` (max height data), `XLOD`.

LAND: `DATA` (heightfield 33×33), `VNML` (vertex normals), `VCLR` (vertex
colors), `VHGT` (height data), `VTEX` (texture index array), `VTXT` (texture
variations), `ATXT`/`BTXT`/`XTXT` (layer textures), `LNAM`, `MNAM`, `NAM0`.

(All verified present as ASCII strings in the binary; the subrecord compare
constants themselves are inline immediates in `LoadRecord`, not string xrefs.)

## 10. Open questions

- Exact `LoadRecord`/`SaveRecord` slot indices per class (the virtuals at
  slots 23+ need per-slot naming via decompile + source-string anchors).
- Full `TESObjectCELL` member layout (only the offsets above are pinned).
- `TESObjectREFRSerializer`'s record path vs the generic factory path.
- The `exteriorcellloader.cpp` streaming contract (cell-grid construction:
  `"Constructed %u cell grids in %.2f ms"`, `"Improperly sized cell node
  array."`).
- Whether any CK feature writes WRLD/CELL records with non-standard subrecords
  (editor-only `EditorWarnings.txt` paths noted in §9 of doc 03).

## Artifacts

- `ghidra-dump/cell-worldspace-vtables/{TESForm,TESObjectCELL,TESWorldSpace,TESObjectLAND,TESObjectREFR}.vftable.txt`
- `ghidra-dump/FindVftable.java` (RTTI → vftable resolver),
  `FindStringXrefs.java`, `FindCallers.java` (also mirrored to `scripts/`)
- `ghidra-dump/decompile-GRUP-reader-FUN_140676cd0.txt`
- `ghidra-dump/decompile-TESDataHandler-CreateForm-FUN_140865b10.txt`
- `ghidra-dump/decompile-TESObjectCELL-LoadRecord-candidate.txt`
