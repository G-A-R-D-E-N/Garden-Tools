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
| `TESObjectREFR` | *no* `ConcreteFormFactory` — has **`TESObjectREFRFactory`** + **`TESObjectREFRSerializer`** RTTI (full names carry the anonymous-namespace hash: `.?AVTESObjectREFRFactory@?A0xd5936d08@@`, `.?AVTESObjectREFRSerializer@?A0xd5936d08@@`) | — | special-cased; see §7 |

The MSVC template-int encoding is per-nibble `0-9→'0'-'9'`, `10-15→'A'-'F'`…
empirically `A=0..P=15` by position (`$0JH`=151=AORU cross-checks from doc 12;
`$0DP`=0x3F). The decoder in doc 12 §5 had a systematic off-by-one; the factory
*registration call* (`thunk_FUN_1408bc350(idx, ...)`) is the ground truth, and
for these three it matches the formEnumString index.

Factory vftable (CELL at RVA `0x309c540`; REFR's hand-written analog at
`0x30a0a88` — identical slot shape). Slot numbering follows the finder's
convention (slot 0 = COL, slot 1 = vftable+8, …), and `CreateForm` dispatches
through `vftable+8` **= slot 2**:

| slot | function | role |
|---|---|---|
| 0 | COL | RTTI |
| 1 | `thunk_FUN_140d13850` (CELL) / `FUN_140d64b80` (REFR) | **factory scalar-deleting dtor / ctor** — `*this = <Factory>::vftable; factory_slot_clear(type); IFormFactory::vftable; delete-if-flag`. The `factory_slot_clear` call is the un-registration hook; not the form Create. |
| 2 | `thunk_FUN_140d135c0` (CELL) / `FUN_140d64ae0` (REFR) | **Create** — the virtual `CreateForm` actually calls: `alloc(0x170 CELL / 0x130 REFR) + in-place ctor` (`FUN_140d64f50` for REFR: the full multi-inheritance vftable chain) |
| 3 | `FUN_140d64b60` (REFR) | GetFormTypeName → `"Reference"` |
| 4 | `FUN_140d64b70` (REFR) | GetFormType → `0x40` |
| 5–8 | `FUN_1408bc290/2a0/2b0/2c0` | IFormFactory interface slots (return constants — see §7) |

Earlier versions of this table labeled slot 1 "Create" and slot 2 "template
ctor helper" — that was a one-slot shift. `CreateForm` calls `vftable[+8]`
(slot 2); slot 1 is the factory's own scalar-deleting ctor/dtor (it clears
the factory registry slot for its type — the un-registration hook).

The factory registry itself is a flat 0x9F-entry pointer array at
`DAT_143d49cd0` (lazily zeroed, guard byte `DAT_143d49cc8`), with the classic
triplet:

| function | role |
|---|---|
| `FUN_1408bc2f0(type)` | **getter** — returns `reg[type]` (the pointer `CreateForm` dereferences) |
| `FUN_1408bc350(type, factory)` | **setter** — `reg[type] = factory` (the registration call) |
| `FUN_1408bc3b0(type)` | **clear** — `reg[type] = 0` (called by the factory dtor) |

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

REFR has **no** `ConcreteFormFactory`; instead RTTI shows two hand-written
anonymous-namespace classes, `TESObjectREFRFactory` and
`TESObjectREFRSerializer` (both mangled with the same TU hash
`@?A0xd5936d08@@`, so they live in one source file — almost certainly
`tesobjectrefr_editor.cpp`, the construction-set side of the reference). Its
9 vtables reflect the reference's multiple interfaces. RTTI also shows the
reference-typed functors: `.?AUIDecalRefFunctor@TESObjectCELL@@`,
`.?AVIAliasFunctor@TESObjectREFRDef@@`,
`.?AVILinkedReferenceFunctor@TESObjectREFRDef@@`,
`.?AVHandleOutfitItemsFunctor@TESObjectREFR@@`.

The reference is split across four source files (`tesobjectrefr.cpp`,
`_animation`, `_events`, `_extradata`). Unlike CELL/WRLD/LAND, its creation
and serialization are handled by **two separate dedicated classes** instead
of the single `ConcreteFormFactory<T,N>` template.

### 7a. TESObjectREFRFactory — vftable `0x30a0a88` (RTTI `0x3c95a20`)

Slot shape is identical to the template `ConcreteFormFactory` (see §2), but
hand-written:

| slot | function | role |
|---|---|---|
| 0 | COL | RTTI |
| 1 | `FUN_140d64b80` | ctor/dtor — sets `_anon_D5936D08::TESObjectREFRFactory::vftable`, clears registry slot `0x40` |
| 2 | `FUN_140d64ae0` | **Create** — `alloc(0x130, 0x10, 1)` + `FUN_140d64f50` in-place ctor |
| 3 | `FUN_140d64b60` | `GetFormTypeName` → `"Reference"` |
| 4 | `FUN_140d64b70` | `GetFormType` → `0x40` (64 = REFR) |
| 5–8 | `FUN_1408bc290/2a0/2b0/2c0` | IFormFactory interface slots |

`FUN_140d64f50` is the full in-place `TESObjectREFR` constructor: it lays
down the whole multiple-inheritance vftable chain (`TESChildCell` →
`NiRefObject`/`BSHandleRefObject` → `BSTEventSink<...>` ×3 →
`IKeywordFormBase` → `ActorValueOwner` → `TESObjectREFR::vftable` for every
subobject), so the factory's Create is the *complete* object builder — not a
stub.

### 7b. TESObjectREFRSerializer — vftable `0x30a0940` (RTTI `0x3c95a68`)

This is the key difference from the factory flow. `FUN_140d64a80` (serializer
ctor) sets `JSONSerializerBase<_anon_D5936D08::TESObjectREFRSerializer,
TESObjectREFR, 64>::vftable`, registers form type `0x40` in the **JSON
serializer registry** (`FUN_140396a50(0x40)` → `FUN_140399700` singleton,
hash map at `DAT_143d1b0d0`, entry size 0x18), then sets the `IJSONSerializer`
base (`FUN_1403969e0`).

| slot | function | role |
|---|---|---|
| 1 | `FUN_140d64a80` | ctor — JSONSerializerBase vftable + register `0x40` |
| 2 | `FUN_140d649d0` → `FUN_140d82720` | **JSON read** (record → object): big body reading `pBaseObject`, resolving the base object via `vftable[0x1d0]`, form-type byte at `+0x26` |
| 3 | `FUN_140d649f0` → `FUN_140d85830` | **JSON write** (object → record), alloca-heavy |
| 4 | `FUN_140d64a70` → `FUN_140d87d90` | **JSON schema builder**: registers ~150 named properties with human-readable labels + descriptions + JSON keys (`pBaseObject`, `pMultiboundRef`, `pSpawnContainer`, `pAcousticParent`, …) |

`FUN_140d87d90`'s property list is the REFR JSON surface: `Pos X/Y/Z`,
`Rot X/Y/Z`, `Scale`, `Bound X/Y/Z` (half-extents), `Base Object`,
`Encounter Zone`, `Owner Faction`, `Owner Actor`, `Locked` / `Lock Level`,
`Enable State Parent`, `Linked Reference`, `Linked Door`, `Multibound Ref`,
`Spawn Container`, `Acoustic Parent`, `Radiation`, `Health`, `Ammo Count`,
light/emittance fields, physics (Linear/Angular Velocity), etc.

### 7c. Two paths, not one

- **Binary record load** (`.esm`/`.esp` via `TESFile`): `CreateForm(type)`
  → `factory_getter(0x40)` → **factory** vftable slot 2 (`FUN_140d64ae0`)
  → alloc + full ctor. This is the same dispatch CELL/WRLD/LAND use — the
  REFR factory just replaces the template.
- **JSON record serialization**: looked up through the *serializer* registry
  (a separate hash map keyed by form type, populated by
  `FUN_140396a50`), dispatched through `IJSONSerializer` virtuals. REFR is
  one of **10** JSON-serializer classes in the binary (`TESObjectWEAP`,
  `Explosion`, `Projectile`, `BGSKeyword`, `BGSAimModel`, `BGSMod_Object`,
  `CameraShot`, `SoundCat`, `SoundDesc`, `TESObjectREFR`), all in anonymous
  namespaces — the JSON system is a parallel, opt-in record surface, not the
  file-format loader.

So the practical answer: REFR records are **created** through the same
factory-registry path as every other form (CreateForm → vftable slot 2), and
the `TESObjectREFRSerializer` is a *separate* JSON serialization surface used
for the JSON record format — the reason REFR gets the hand-written pair is
that its editor needs both, and the template's fixed name/type accessors
aren't enough for the JSON schema.

## 8. TESObjectCELL member layout (constructor + setters)

Recovered from the constructor `FUN_140d13940` (`tesobjectcell.cpp`) plus the
NewCell helper chain. `param_1` is an 8-byte slot pointer, so `param_1[N]`
reads as `+8N`; byte/word fields are named at their absolute offset.

### 8.1 Constructor (`FUN_140d13940`)

| offset | size | init | meaning |
|---|---:|---:|---|
| +0x00 | 8 | `TESObjectCELL::vftable` | primary vftable (`0x309c780`) |
| +0x28 | 8 | `TESObjectCELL::vftable` | **second vftable** (`0x309cc30`, secondary base) |
| +0x30 | 16 | ctor `thunk_FUN_140914c20` | sub-object (`TESFullName` slot written first at +0x28, then overwritten) |
| +0x40 | 4 | 0 | |
| +0x44 | 4 | 0 | |
| +0x48 | 4 | 0 | |
| +0x4C | 4 | 0 | |
| +0x50 | 2 | 0 | **cell flags** (bit0 interior, bit1 exterior) |
| +0x54 | 1 | 0 | |
| +0x58 | 8 | 0x20-byte alloc | cell-data **handle** (refcounted: `*ptr + 1` under lock) |
| +0x60 | 8 | 0 → alloc in `FUN_140d18510` | **cell-data pointer** (block type per +0x50 bit0) |
| +0x80 | — | ctor `thunk_FUN_141ffc960` | ref array container |
| +0x90 | — | ctor `thunk_FUN_141ffc4f0` | secondary container |
| +0x98 | 8 | 0 | |
| +0xB8 | 8 | `&DAT_143bd5c24` | static marker |
| +0xC0 | 4 | 0 | |
| +0xD0 | 8 | 0 | |
| +0xF0 | 8 | `&DAT_143b99df8` | static marker |
| +0x108 | 8 | 0 | |
| +0x128 | 8 | `&DAT_143bd5c38` | static marker |
| +0x130 | 4 | 0 | |
| +0x140 | 8 | 0 | |
| +0x148 | 8 | 0 | |
| +0x150 | 8 | 0 | multibound / linked collection (cleared in teardown) |
| +0x158 | 8 | 0 | |
| +0x160 | 8 | 0 | |
| +0x168 | 4 | 0xFFFFFFFF (-1) | |
| +0x16C | 4 | 0 | |
| +0x26 | 1 | **0x3F = 63** | **form-type byte = CELL** (matches `formEnumString` index) |
| +0x10 | 4 | (from TESForm base) | form flags (`>>14&1`, `>>3&1`, `>>1&1` read in teardown) |

Notable: the form-type byte at **+0x26** is set to 0x3F (63 = CELL) right in
this constructor — the same value `TESDataHandler::CreateForm` passes to the
factory getter, closing the loop between the enum table and the object.

### 8.2 The NewCell helper chain

`FUN_14085ae30` calls, in order:

1. **ctor** `thunk_FUN_140d13940` (above).
2. **`FUN_140d1d2e0(cell, 0)`** — interior-flag setter: writes `cell+0x50`
   word bit0 = (param != 0). NewCell passes 0, so a fresh cell is **exterior**.
3. **`*(ushort*)(cell+0x50) |= 2`** — sets exterior bit1 directly.
4. **`FUN_140d18510(cell)`** — cell-data allocator:
   - if `+0x50 & 1` (interior): allocates a **0x20-byte** block, zeroed.
   - else (exterior): allocates a **0x88-byte** block via
     `thunk_FUN_140911970` with float scale/identity seeds at +0x1C, +0x24,
     +0x4C, +0x70, +0x74, +0x78, +0x7C (all `0x3f800000` = 1.0f) and
     `*(u64*)(+0x58) = 0x79F`.
   - stores the new block at **cell+0x60**, after releasing any previous one.
5. **`FUN_140d197c0(cell, x, y)`** — coordinate setter: only when exterior
   (`+0x50 & 1 == 0`), writes `*(int*)(cell+0x60) = x`, `*(int*)(+0x64) = y`
   (cell-data block offsets 0/4). Interior cells carry coords in their record
   instead.
6. **`FUN_140e4d180(worldspace, cell)`** — worldspace cell-map insert (see §8.3).

`FUN_1408bfb80` (called with the cell + an editor-ID string) is the generic
`TESForm` EDID setter (`tesform.cpp` assert 0x1078) with DUPLICATE%03i
collision handling — not cell-specific.

### 8.3 Worldspace cell-map insert (`FUN_140e4d180`, `tesworldspace.cpp`)

- Guard: `cell != 0 && (cell+0x50 & 1) == 0` (exterior only).
- Looks up the cell in the worldspace's cell map at **worldspace+0x70** via
  `thunk_FUN_140e539e0` (coord key).
- Not present: reads the cell's world position (`thunk_FUN_140d19770`), grows
  the worldspace bounds **+0x140…+0x14C** (minX, minY, maxX, maxY floats), and
  adds the cell. Duplicate → log `"Cell (%08X) %s already exists at coord
  (%i, %i)."`.
- If the cell is interior and worldspace+0xA0 is empty: stores it there
  (single interior-cell slot).

`FUN_140d44f00` (ref-registration / InitItem path) walks the cell's ref array
(`count at cell+0x48`) and IncRef/DecRefs each ref via `BSHandleRefObject`
(`bshandlerefobject.inl`), then registers with the data handler
(`thunk_FUN_14087d2d0`), and calls `thunk_FUN_140d55ca0` (probably cell →
worldspace association).

## 9. Exterior cell loading — the `exteriorcellloader.cpp` streaming contract

Source: `shared\tesforms\world\exteriorcellloader.cpp` (confirmed `__FILE__`
string). Three layers cooperate: a **task queue** (`CellLoaderTask`), the
**cell-grid manager** that owns the loaded cells, and the **file walk**
(`BGSConstructFormsInFileMap::ConstructForms`) that walks the WRLD GRUP tree
and queues blocks/sub-blocks.

### 9a. CellLoaderTask — queue + run

Task object layout (from `FUN_140ccf770` / `FUN_140cd11d0` / `FUN_140ccf610`):

| offset | field | role |
|---|---|---|
| `+0x48` | interior cell ptr | the cell when loading an interior (else 0) |
| `+0x50`/`+0x54` | int32 x, y | exterior cell grid coords |
| `+0x58` | uint state | 3 = queued → 4 = running → 5 = done (PostProcess) |
| `+0x5D` | bool | interior flag (0 = exterior) |
| `+0x60` | worldspace ptr | owner worldspace (exterior coords keyed to it) |

`FUN_140ccf770` — **task name virtual**: exterior →
`"CellLoaderTask for cell ( %i, %i )"`; interior →
`"CellLoaderTask for interior cell '%s' (%08X)"` (name via `vftable+0x238`,
formID at `+0x14`); missing interior cell → `"... interior cell NONE
(canceled)"`.

`FUN_140cd11d0` — **LoadCellData** (assert line 0x279):

```
if interior flag == 0 (exterior):
    if task.cell == 0: task.cell = GetCellFromCellCoord(worldspace, x, y)   # FUN_1405b9850
else if interior flag != 0 and task.cell == 0:
    assert "Found queued interior cell load without interior cell."       # canceled task
if task.cell == 0:
    task.cell = TESDataHandler::NewCell(worldspace, 0, x, y)              # FUN_14085ae30
if task.cell != 0:
    FUN_140d1a5e0()            # TESObjectCELL node-tree build (see §9b)
    if exterior:
        grid = GetCellGrid(cell); set grid flags +0x50 |= 2 (visible);
        distance check via FUN_140d1cbc0 clears the bit when too far
        cell->+0x53 = 1        # "in grid" flag
```

`FUN_140ccf610` — **PostProcess** (assert line 0xd2): CAS `+0x58` 3→4;
for exterior calls `FUN_140cd1030(worldspace, x, y)`, for interior
`FUN_140cd10b0(worldspace, formID)` (both resolve the cell), registers the
cell into `TESDataHandler` (`FUN_140838780`/`FUN_140844a20`), sets state 5.

### 9b. Cell node-tree build (`FUN_140d1a5e0`, `tesobjectcell.cpp` 0xffa)

Called from `LoadCellData` on first load of a cell; builds the full NiNode
scene graph for the cell (`CellGrid` → child nodes):

```
CellGridNode ("Cell \"%s\" (%d, %d)" / "(Interior)")          0x150 alloc
├─ ActorNode
├─ MarkerNode
├─ LightMarker Node
├─ SoundMarker Node
├─ LandNode + "Land 0..3" children
├─ StaticNode
├─ CombinedNode
├─ DynamicNode
├─ EditorStaticNode / EditorDynamicNode / EditorWaterNode
├─ OcclusionPlane Node, Portal Node, MultiBoundNode Node,
│  Collision Node, Small Object Node
└─ (child list grows to exactly 10)
```

Every node gets `NiBound` set (`0x3f800000` radius, assert via `nibound.inl`
0x2b) and flag bits `0x800|0x2000`. At the end the child count is validated:
`*(short*)(cellNode + 0x132) != 10` → assert
`"Improperly sized cell node array."` (`tesobjectcell.cpp` 0x113e). For
interiors it also loads precomputed visibility:
`Data\Vis\%s\%08X.uvd` (worldspace name, cell formID & 0xFFFFFF).

### 9c. Cell-grid manager (`FUN_140ffc050` + `FUN_140ff9450`)

`FUN_140ff9450` is the manager update (drives the `DAT_143df22b0` BST
singleton, checks `+0x240`); it calls `FUN_140ffc050` on the grid array
(`param_1 + 0x100`). `FUN_140ffc050` builds the `BSTArray` of grids:

```
copy singleton +0x88 -> grid array +0x23            # count/version
alloc BSTArray (8-byte elems, cap = singleton +0x50)
for each entry i in singleton array (+0x40, count +0x50):
    grid = FUN_140ffc870(owner, entry[i])           # build one cell grid
    if grid: push into BSTArray
for each built grid: FUN_140ffd360(owner, grid)     # attach/init
log "Constructed %u cell grids in %.2f ms"          # +0xff9cd0
```

### 9d. The block/sub-block walk (`FUN_140900e40`, `bgsconstructformsinallfilesmap.cpp`)

`BGSConstructFormsInFileMap::ConstructForms` walks the WRLD record's GRUP
tree, dispatching on the group type at `group+0x2B0` (the GRUP record's type
slot — same enum as §6) and using the group label (`+0x2AC`) as the map key:

| group type | key / lookup | behavior |
|---|---|---|
| 0 | form type name | `FUN_140906660` (forms map) |
| 1 | worldspace formID | `+0x38`/`+0x40` worldspace maps |
| 2/4 | block label | `FUN_140906380` — "NULL current cell **blocks** map" (0x313) |
| 3/5 | sub-block label | `FUN_1409064f0` — "NULL current cell **sub-blocks** map" (0x323) |
| 6 | cell formID | cell children; persistent/temporary split on flag bit 1 |

Each non-empty group is descended (`FUN_14087de20(param, 1)` = descend;
`FUN_14087d930` = skip), so the walk is: WRLD → Exterior/Interior **Block**
(type 2/4) → **Sub-block** (3/5) → **Cell** (6) → REFR/LAND/etc. records. The
sub-record data is appended into 0x10-byte scrap-array entries (`+0x2B4`,
`BSScrapArrayAllocator`). The label format strings live in the group-name
formatter `FUN_140676cd0` (§6).

Practical contract: cell grids are **lazily created on demand** (LoadCellData
→ NewCell on miss), **built eagerly in bulk** at world load (grid manager
"Constructed %u cell grids"), and the GRUP walk only **queues** work keyed by
block/sub-block labels — the task queue owns the streaming.

## 10. LoadRecord / SaveRecord virtual slots (CELL + WRLD)

Method: diffed each class's primary vftable against `TESForm`'s; slots whose
function differs from the base are genuine overrides. Base `TESForm` slots 24
and 27 are stubs (`FUN_140720ac0` = `return 1`, `FUN_140720ad0` = `return`) —
so **slot 24 = LoadRecord, slot 27 = SaveRecord** is the inherited convention,
and both world classes override exactly those slots with the real file I/O.

The two file-I/O primitives (both in the `TESFile`-family, `bscore`/`bsresource`
code):

| function | role |
|---|---|
| `FUN_14087e870(file, dst, size)` | **read** `size` bytes into `dst` (LoadRecord's workhorse — also the `TESFile` read used by every record) |
| `FUN_1408c7710(4CC, data, size)` | **write subrecord** — appends 4CC + u16 size + data to the output buffer `DAT_143d4a470` (SaveRecord's workhorse) |

### 10a. TESObjectCELL overrides (primary vftable `0x309c780`)

| slot | function | name | evidence | `__FILE__` anchors |
|---|---|---|---|---|
| 23 | `FUN_140d13f00` | `ClearItem`-family | frees cell data `+0x58` via `FUN_1407d2060`, checks flag `+0x50` bit 0 | `tesobjectcell.cpp:0x49f` |
| **24** | `FUN_140d146d0` | **`LoadRecord`** | 14× `FUN_14087e870` (reads); dispatches the CELL subrecord set; `"Cell flag data is not a short or char"`; calls the cell-data allocator `FUN_140d18510` | `tesobjectcell.cpp:0x4d7, 0x4ee, 0x555` |
| 27 | `FUN_140d44ba0` | **`SaveRecord`** | 13× `FUN_1408c7710` (subrecord writer); workhorse calls anchored in `tesform.cpp` — **no `tesobjectcell.cpp` anchors** (see below) | — |
| 28 | `FUN_140d44f00` | flag/ref-count helper | `FUN_14087f240` (flag bit `+0x33C`), `FUN_1408be010` (form lookup), refcount asserts | — |
| 30 | `FUN_140d45150` | form-registration helper | `FUN_14087ec50`, `FUN_14087d2d0`, `FUN_140d55ca0` (TESDataHandler register path) | — |
| 31 | `FUN_140d45400` | handle→form resolver | type check vs `DAT_143b9a580`, switch on `param_2[3]` (TESFile handle decode) | — |
| 32 | `FUN_140d451d0` | ref-iteration predicate | `FUN_140d169e0`/`FUN_140d16a70` (ref list walk), `FUN_140d268d0` (GetWorldspace) | — |
| 33 | `FUN_140d14040` | alloc/copy helper | array allocs, cell-data realloc | `tesobjectcell.cpp:0x133a` |
| 35 | `FUN_140d153d0` | cell-validation/dangling-check | `"Bad cell value. Attempting to create new cell."`, `"Cell at (%i, %i) in world %s (%08X) may be a dangling cell."`, `"Unable to delete invalid coord cell ..."`, cell-name-length check | `tesobjectcell.cpp:0x5f9, 0x1350` |
| 38 | `FUN_140d17dc0` | cell-label formatter | `"'%s' (%08X)"`, `"Persistent cell '%s' (%08X) (%i, %i) in WorldSpace '%s' (%08X)"` (editor cell label) | — |

CELL `LoadRecord` dispatches: `CNAM`, `DATA`, `EDID`, `FULL`, `LNAM`,
`LTMP`, `MHDT`, `OBLT`, `OBML`, `OBND`, `OBTE`, `PBTE`, `PCMB`, `PTRN`,
`RNAM`, `RVIS`, `STCP`, `SVIS`, `TTCP`, `TVDT`, `UVDT`, `VISI`, `VMAD`,
`XACR`, `XACT`, `XALP`, `XAMC`, `XAMT`, `XAPD`, `XAPR`, `XATP`, `XATR`,
`XBSD`, `XCAS`, `XCCM`, `XCET`, `XCGD`, `XCHG`, `XCIM`, `XCLC` …

CELL `SaveRecord` writes: `VISI`, `RVIS`, `PCMB`, `DATA`, `XCLC`, `CNAM`,
`MHDT`, `LTMP`, `XCLW`.

### 10b. TESWorldSpace overrides (primary vftable `0x30aa510`)

| slot | function | name | evidence | `__FILE__` anchors |
|---|---|---|---|---|
| 22 | `FUN_140e4a070` | ref-count/ownership helper | `FUN_1408bef30` | — |
| 23 | `FUN_140e4a1f0` | `ClearItem`-family | `FUN_140e4db70`, `FUN_140d3c040`/`d3bc30` | — |
| **24** | `FUN_140e4a750` | **`LoadRecord`** | 26× `FUN_14087e870` (reads); `"Found invalid cell level data in file."`, `"Loading cell level data over top of existing data"`, `"Cell level data in file '%s' for worldspace '%s' (%08X) does not match dimensions in file..."` | `tesworldspace.cpp:0x360, 0x378, 0x4b2, 0x4ba, 0x4ca` |
| 25 | `FUN_140e4b040` | flag getter | returns byte (flag accessor) | `tesworldspace.cpp:0x316` |
| 27 | `FUN_140e54420` | **`SaveRecord`** | 23× `FUN_1408c7710` (subrecord writer); asserts carry **`tesform.h:0x56e`** (header-inline save helpers) — **no `tesworldspace.cpp` anchors** (see below) | — |
| 31 | `FUN_140e54b20` | handle→form resolver | type check + `FUN_1408bcfc0`/`FUN_1408bdd20` (mirror of CELL slot 31) | — |
| 32 | `FUN_140e549d0` | form-comparison predicate | `FUN_1408bcfa0`, `FUN_140d268d0` | — |
| 33 | `FUN_140e4a320` | cell-map iterator/insert | `FUN_140e4d180` (worldspace cell-map insert — §8.3), `FUN_140e53eb0`, `FUN_140d196a0` | `tesworldspace.cpp:0x62a` |
| 35 | `FUN_140e4b080` | cell-grid child iteration | 9× `FUN_1408be010` (form lookup) + `FUN_1408c68d0`/`FUN_1408bdd20` — walks the cell array | `tesworldspace.cpp:0x50d` |

WRLD `LoadRecord` dispatches: `ATDM`, `BTDM`, `CNAM`, `DATA`, `DNAM`, `EDID`,
`FULL`, `ICON`, `LTMP`, `MHDT`, `MNAM`, `NAM2`, `NAM3`, `NAM4`, `NAMA`,
`NNAM`, `OBLT`, `OBML`, `OBND`, `OBTE`, `ONAM`, `PNAM`, `PTRN`, `RNAM`,
`STCP`, `TNAM`, `TTCP`, `UNAM`, `VMAD`, `WCTR`, `WLEV`, `WMAD`, `WNAM`,
`XEZN`, `XLCN`, `XWEM`, `ZNAM`.

WRLD `SaveRecord` writes: `CNAM`, `DATA`, `DNAM`, `LTMP`, `MHDT`, `MNAM`,
`NAM0`, `NAM2`, `NAM3`, `NAM4`, `NAM9`, `NAMA`, `ONAM`, `PNAM`, `WCTR`,
`WLEV`, `WNAM`, `XEZN`, `XLCN`, `ZNAM`.

### 10c. What the diff teaches

- The shared `TESForm` surface (slots 25/26/29/34/36–49…) is *identical*
  across CELL/WRLD — the class-specific bits are concentrated in slots
  22–35.
- The editor extras (labels, dangling-cell validation, cell-map insert)
  occupy the low override slots; the I/O pair sits at the fixed 24/27
  positions inherited from `TESForm`.
- Subrecord codes are inline immediates in the Load/Save bodies — the
  `strings` inventory in §11 is a superset (includes codes the CK never
  emits via these two functions).

### 10d. Anchor asymmetry: Load anchors in the class, Save anchors in `tesform`

Every override was checked against its `__FILE__` assert strings (the
`thunk_FUN_141ffb470`/`141ffa0a0` assert machinery — file path + line
hex). The result is asymmetric in a way that confirms the slot naming:

- **`LoadRecord` anchors in the class file.** CELL `LoadRecord` carries
  `tesobjectcell.cpp:0x4d7/0x4ee/0x555` (the `0x555` assert is the
  `"Cell flag data is not a short or char"` DATA-flag check); WRLD
  `LoadRecord` carries `tesworldspace.cpp:0x360/0x378/0x4b2/0x4ba/0x4ca`
  (cell-level-data asserts). Read paths assert against their own
  subrecord/flag layout, so the file string is the class's `.cpp`.
- **`SaveRecord` has no class-file anchors.** CELL `SaveRecord`
  (`FUN_140d44ba0`) contains *zero* assert calls and zero string
  literals — it is a straight 13× call chain into the shared writer.
  WRLD `SaveRecord` (`FUN_140e54420`) does assert, but the file string
  is `tesform.h:0x56e` — the header-inline form-component save helpers
  (`MANA`/`MNAM` subrecord saves), not `tesworldspace.cpp`. Write paths
  delegate to `TESForm` machinery, so the assert anchors live in the
  base layer.
- **The save workhorses, decompiled (both in `tesform.cpp`):**

  | function | anchor | role |
  |---|---|---|
  | `FUN_1408c7a70` | `tesform.cpp:0x9a4` | form-header writer — allocates the 0x18 header block, writes formID/version/flags, emits `EDID`, then calls the vtable save slots (`+0x1b0` subrecords, `+0x3b8` → `PTRN`-family `FUN_14036d7c0`, `+0x3c0` → `STCP`-family `FUN_1403706a0`) |
  | `FUN_1408c7710` | (write primitive) | subrecord writer — appends 4CC + u16 size + data to `DAT_143d4a470` |
  | `FUN_1408c7bf0` | `tesform.cpp:0x9e0` | subrecord close / size-patch — rewinds the buffer size (`DAT_143d4a470+4 = DAT_143d4a46c - 0x18`) unless form flag `+0x10` bit 0xe is set |
  | `FUN_140d18490` | — | CELL exterior cell-data getter (`+0x60`), `0` if interior (`+0x50 & 1`) — called 5× inside CELL `SaveRecord` |

  So CELL `SaveRecord` = `FUN_140d18490` (get cell data) + 13×
  `FUN_1408c7710` + 3× `FUN_1408c7bf0` (close each subrecord), with the
  form header handled by the `FUN_1408c7a70` family in the caller.
- **Anchor `0x62`/`0x67` in the assert thunks is the severity/kind
  constant, not a source line** — the first pass briefly misread those
  as line anchors; the real line is the hex after the file string.

## 11. Known record strings (load/save surface)

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

## 12. Open questions

- The 0x20/0x88-byte cell-data block internals (what lives in the 0x88
  exterior block beyond the coords + 1.0f seeds; the `0x79F` at +0x58).
- Whether any CK feature writes WRLD/CELL records with non-standard subrecords
  (editor-only `EditorWarnings.txt` paths noted in §10 of doc 03).

## Artifacts

- `ghidra-dump/cell-worldspace-vtables/{TESForm,TESObjectCELL,TESWorldSpace,TESObjectLAND,TESObjectREFR}.vftable.txt`
- `ghidra-dump/FindVftable.java` (RTTI → vftable resolver),
  `FindStringXrefs.java`, `FindCallers.java` (also mirrored to `scripts/`)
- `ghidra-dump/decompile-GRUP-reader-FUN_140676cd0.txt`
- `ghidra-dump/decompile-CELL-overrides.txt` / `decompile-WorldSpace-overrides.txt` (all slot-23+ override bodies with `__FILE__` anchors)
- `ghidra-dump/decompile-save-helpers.txt` (`FUN_1408c7a70` form-header writer, `FUN_1408c7bf0` subrecord close, `FUN_140d18490` cell-data getter)
- `ghidra-dump/decompile-TESDataHandler-CreateForm-FUN_140865b10.txt`
- `ghidra-dump/decompile-TESObjectCELL-LoadRecord-candidate.txt`
- `ghidra-dump/cell-ctor-0xd13940.txt`, `cell-setters.txt`,
  `worldspace-cellmap-insert-0xe4d180.txt` (raw decompiles; local only)
- `ghidra-dump/decompile-REFRFactory-slots.txt`,
  `decompile-REFRSerializer-slots.txt`, `decompile-REFR-serializer-thunks.txt`,
  `decompile-serializer-virtuals.txt`, `decompile-factory-compare.txt`,
  `decompile-json-registry.txt`, `callers-{REFRFactory-ctor,REFRFactory-Create,serializer-read,serializer-reg,json-registry}.txt` (raw decompiles; local only)
- `ghidra-dump/decompile-exteriorcellloader.txt` (grid ctor FUN_140ffc050 +
  node-tree FUN_140d1a5e0), `decompile-CellLoaderTask.txt` (task name),
  `decompile-CellLoaderTask-run.txt` (LoadCellData + PostProcess),
  `decompile-GRUP-walk-FUN_140680320.txt` (group label formatter),
  `decompile-ConstructForms.txt` (block/sub-block walk),
  `decompile-grid-manager.txt`, `callers-grid-ctor.txt` (raw decompiles; local only)
- `ghidra-dump/FindCodeRefsTo.java` (code xref finder)
- `ghidra-dump/decompile-CELL-overrides.txt` (all 10 CELL slots 23+),
  `decompile-WorldSpace-overrides.txt` (all 9 WRLD override slots) —
  raw decompiles; local only
