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

### 7c. Two paths, not one — end-to-end trace of REFR creation and serialization

REFR is unique among the world-building form classes in having two
independent record paths — a **binary record factory** (like CELL/WRLD/LAND)
and a **JSON serializer** (shared with WEAP/AIMS and 8 other form types).
Here is the full trace of both paths, from entry to stored record.

#### 7c.1 Binary factory path (same as CELL/WRLD/LAND)

```
TESFile::LoadRecord (in exteriorcellloader or file reader)
  → reads 4CC 'REFR'
  → TESDataHandler::CreateForm(0x40)         // FUN_140865b10, tesdatahandler.cpp:0x1bc4
    → factory_getter(0x40)                    // FUN_1408bc2f0
      → reg[0x40] = TESObjectREFRFactory instance (set by ctor on init)
    → factory->vftable[8]                    // slot 2 = Create
      → FUN_140d64ae0
        → alloc(0x130, 0x10, 1)              // 304 bytes, 16-byte aligned
        → FUN_140d64f50(obj)                 // full in-place ctor
          → lays down 9 vtables:
            TESChildCell → NiRefObject → BSHandleRefObject →
            BSTEventSink<...> ×3 → IKeywordFormBase →
            ActorValueOwner → TESObjectREFR::vftable (0x30a1990)
          → sets form-type byte *(obj+0x26) = 0x40
  → obj->LoadRecord(file)                    // slot 24: FUN_140d65f60
    → reads ~160 subrecords into the object
  → TESDataHandler::InsertForm(obj)
```

This path creates a **blank REFR object** (alloc + ctor), then **hydrates** it
from the binary `.esm`/`.esp` subrecord stream via `LoadRecord`. Every form
type uses this pattern — the REFR factory is hand-written (`TESObjectREFRFactory`
in `tesobjectrefr_editor.cpp`, anonymous namespace `?A0xd5936d08@@`) but the
dispatch shape is identical to the template `ConcreteFormFactory`.

#### 7c.2 JSON serializer path (unique to 11 form types)

**Registration** (at CK startup, before any record is loaded):

```
TESObjectREFRSerializer::ctor                      // FUN_140d64a80
  → JSONSerializerBase<anon, TESObjectREFR, 64>::vftable  // multi-inheritance
  → FUN_140396a50(0x40)                            // register in JSON hash map
    → FUN_140399700()                               // singleton getter
      → returns &DAT_143d1b0d0 (hash map, entry size 0x18)
    → thunk_FUN_140398960(map+8, &0x40, &this, ...) // insert serializer ptr
  → FUN_1403969e0(this)                             // set IJSONSerializer::vftable base
```

The hash map at `DAT_143d1b0d0` maps `formType (u32)` → `IJSONSerializer*`.
After all 11 ctor calls, the map contains entries for types
`{0x4A,0x2B,0x8E,0x03,0xA0,0x63,0x4A,0x3D,0x86,0x84,0x43}`.

**Read (JSON → record object)** — called from the HTTP API handler `addformoftype`:

```
FUN_1408cb460 (HTTP: "addformoftype")              // RVA 0x8cb460
  → parse JSON body from HTTP request
  → FUN_1408caec0(req)                              // resolve base form from request
    → *(baseForm+0x26)                              // get form-type byte
  → FUN_140396af0(formType)                         // check: serializer exists?
    → FUN_140399700() → hash lookup → found? yes/no
  → [if no: return {"error": "no serializer for type"}]
  → factory::vftable[8]                             // clone base form
  → FUN_1403979b0(formType, newObj, jsonData)       // JSON read dispatch
    → FUN_140399700() → hash lookup by formType
    → IJSONSerializer::vftable[0x10]                // slot 2 = JSON read virtual
      → FUN_140d649d0(serializer, obj, jsonData)    // REFR serializer thunk
        → FUN_140d82720(obj, jsonData)              // the 1500-line JSON reader
          // For each JSON key in the schema:
          //   "Base Object"  → FUN_140d82720 reads formID → resolves via vftable[0x1d0]
          //   "Pos X/Y/Z"    → *(obj+0x1C)/+0x1A/+0x1B (float fields)
          //   "Rot X/Y/Z"    → *(obj+0x1D)/+0xE4/+0xD4 (rotation)
          //   "Scale"        → *(float*)(obj+0x1b) (scale uniform)
          //   "Health"       → *(u32*)(obj+0x228)
          //   etc. — all 73 schema properties mapped to object offsets
  → validate + insert into TESDataHandler singleton
  → callback("{"success":"true", "id":"%d"}")
```

**Write (record object → JSON):**

```
FUN_1408cb750 (HTTP: "getformdatabyid")
  → TESDataHandler::LookupFormByID(formID)
  → *(form+0x26)                                    // get form-type byte
  → FUN_140396af0(formType)                         // serializer exists?
  → FUN_140397b30(formType, form, &jsonBuffer)      // JSON write dispatch
    → FUN_140399700() → hash lookup
    → IJSONSerializer::vftable[0x18]                // slot 3 = JSON write virtual
      → FUN_140d649f0(serializer, form, &jsonBuffer)
        → FUN_140d85830(form, &jsonBuffer)          // the 1300-line JSON writer
          // Reads each schema property from the object and serializes
```

**Schema introspection (form → property list):**

```
FUN_1408cb320 (HTTP: "getmetadatafortype")
  → IJSONSerializer::vftable[0x20]                  // slot 4 = schema builder
    → FUN_140d64a70(serializer, &metadataOut)
      → FUN_140d87d90(&metadataOut)                 // 73-property schema
        → for each of 73 named properties:
          call 0x16248(metadataOut, label, desc, jsonKey, type, default, ...)
```

#### 7c.3 Dispatch key: `FUN_140396af0` — the serializer existence check

```c
bool FUN_140396af0(int formType) {
    map = FUN_140399700();               // get hash-map singleton
    return hash_lookup(map+8, &formType); // true = serializer exists for this type
}
```

This function is called by **five HTTP handlers** (`addformoftype`,
`getformdatabyid`, `updateform`, `getformsoftype`, `getmetadatafortype`)
to gate access — a form type without a registered serializer returns
`{"error": "..."}` immediately. Every other form type in the game (hundreds
more) cannot be created or edited through the JSON API.

#### 7c.4 The HTTP REST API surface

11 URL routes are registered by `FUN_1408cb840` (at RVA `0x8cb840`, the
route array at `0x1840b0`):

| route (string at RVA) | handler RVA | JSON virtual | role |
|---|---|---|---|
| `checkoutform` (0x300d930) | `FUN_1408cafa0` | — | lock a form for editing |
| `undocheckout` | `FUN_1408cb010` | — | release edit lock |
| **`addformoftype`** (0x300d950) | **`FUN_1408cb460`** | **slot 2 (read)** | **create new form from JSON** |
| `getformsoftype` (0x2fad390) | `FUN_1408cb130` | slot 2 (read) | query forms by type |
| `getemptyformoftype` | `FUN_1408cb280` | — | get empty form template |
| **`getmetadatafortype`** (0x300d990) | `FUN_1408cb320` | **slot 4 (schema)** | get property metadata |
| `getformtypeswithmetadata` | `FUN_1408cb3b0` | slot 4 (schema) | list types with metadata |
| **`updateform`** (0x300d9c8) | `FUN_1408cb600` | **slot 3 (write)** | edit existing form |
| `gettypeofformbyid` | `FUN_1408cb690` | — | resolve form ID → type |
| **`getformdatabyid`** (0x300d9f0) | `FUN_1408cb750` | **slot 3 (write)** | export form as JSON |
| `getformtypeswithpropertybridges` | `FUN_1408cb7a0` | — | list types with property bridges |

Routes 8–12 characters long, registered via a `BSTHashMap`-style
string→function-pointer table. The handler bodies are in the
`0x8ca000–0x8cc000` range (construction-set HTTP server).

#### 7c.5 Key differences: factory vs. serializer

| aspect | ConcreteFormFactory (binary) | TESObjectREFRSerializer (JSON) |
|---|---|---|
| **trigger** | `CreateForm(type)` in file loader | HTTP POST/GET from embedded REST API |
| **registry** | `DAT_143d49cd0` flat pointer array (0x9F entries) | `DAT_143d1b0d0` hash map (11 entries) |
| **creation** | `alloc(0x130)` + `FUN_140d64f50` full ctor → blank object | `factory::Create` clones a base form, then **JSON reader** hydrates fields |
| **hydration** | `LoadRecord(file)` → ~160 sequential subrecord reads (`FUN_14087e870`) | `IJSONSerializer::read(obj, json)` → key-value dispatch on 73 named properties |
| **persistence** | `SaveRecord` → `FUN_1408c7710` subrecord writer (4 + subclass chain) | `IJSONSerializer::write(obj, &buf)` → key-value JSON serialization |
| **scope** | every form type (CELL, WRLD, LAND, REFR, WEAP, … all ~159 types) | only 11 form types (REFR, WEAP, AIMS, KYWD, Mod, CameraShot, Explosion, Projectile, SoundCat, SoundDesc, Weather) |
| **schema** | implicit — subrecord 4CC dispatch in LoadRecord body | explicit — schema builder virtual returns human-readable property list |
| **source file** | `tesobjectrefr_editor.cpp` (ctor + factory), `tesobjectrefr.cpp` (LoadRecord/SaveRecord) | `tesobjectrefr_editor.cpp` (anonymous namespace `?A0xd5936d08@@`) |

The JSON serializer is an **opt-in parallel record surface** for editing
records through a remote HTTP API — it never touches the `.esm`/`.esp`
binary format. The binary factory creates blank objects hydrated from
plugin files; the JSON serializer creates objects from HTTP JSON payloads.

### 7d. The full JSONSerializerBase roster — 11 record types

The RTTI strings in the binary reveal **11** serializer classes (not 10 as
previously reported), all in anonymous namespaces with the
`JSONSerializerBase<TSub, TRecord, FormType>` template:

| serializer anon-class | record class | encoded ft | schema builder RVA | props |
|---|---|---|---|---|
| `TESObjectREFRSerializer` | `TESObjectREFR` | `$0EA` (0x4A) | `0xd87d90` | 73 |
| `TESObjectWEAPSerializer` | `TESObjectWEAP` | `$0CL` (0x2B) | ~`0xc954b0` | 46 |
| `BGSAimModelSerializer` | `BGSAimModel` | `$0IN` (0x8E) | ~`0x445e46` | 16 |
| `BGSKeywordSerializer` | `BGSKeyword` | `$03` (0x03) | (TBD) | (≥1) |
| `BGSMod_ObjectSerializer` | `BGSMod::Attachment::Mod` | `$0JA` (0xA0) | (TBD) | (≥1) |
| `CameraShotSerializer` | `BGSCameraShot` | `$0GD` (0x63) | (TBD) | (≥1) |
| `ExplosionSerializer` | `BGSExplosion` | `$0FK` (0x4A) | (TBD) | (≥1) |
| `ProjectileSerializer` | `BGSProjectile` | `$0DD` (0x3D) | (TBD) | (≥1) |
| `SoundCatSerializer` | `BGSSoundCategory` | `$0IG` (0x86) | (TBD) | (≥1) |
| `SoundDescSerializer` | `BGSSoundDescriptorForm` | `$0IE` (0x84) | (TBD) | (≥1) |
| `WeatherSerializer` | `TESWeather` | `$0DJ` (0x43) | (TBD) | (≥1) |

All 11 share the same `IJSONSerializer` base interface (vftable layout:
slot 1 = ctor/register, slot 2 = JSON read, slot 3 = JSON write,
slot 4 = schema builder). Each ctor calls `FUN_140396a50(form_type)` to
register in the JSON serializer hash map at `DAT_143d1b0d0`.

**Schema-building protocol:** the schema builder virtual (slot 4) calls
a property-registration API at RVA `0x16248` (`lea rdx, [RIP+string]`;
`call 0x16248`), one invocation per named property. Extracting the
`lea rdx` targets from each builder's bytecode recovers the full
property surface:

#### 7d.1 TESObjectREFR (73 properties, split across two schema functions)

Part 1 at RVA `0xd87d90` (39 props — identity, flags, transform, misc):
`Base Object`, `Layer`, `Encounter Zone`, `Comments`, `Num. Materials`,
`Turn Off Fire`, `Initially Disabled`, `Hidden From Local Map`,
`Inaccessible`, `Open by Default`, `Motion Blur`, `Starts Dead`,
`Starts Unconscious`, `Respawn`, `Reflected by Autowater`,
`Ignored by Sandbox`, `Is Full LOD`, `Don't Havok Settle`,
`Is Ground Piece`, `LOD Respects Enable State`,
`Pos X/Y/Z`, `Rot X/Y/Z`, `Scale`, `Test radius`, `Color`,
`Player Activate`, `Collision Layer`, `Bound X/Y/Z`,
`Count`, `Health`, `Ammo Count`, `Time Left`, `Alpha Cutoff`.

Part 2 at RVA `0xd89e0e` (34 props — physics, linkage, door/teleport):
`Required Rank`, `Has Patrol Data`, `Idle Time`, `Width`, `Height`,
`Radio Station`, `Ignores Distance Check`, `Min Weak Distance`,
`Max Weak Distance`, `Imagespace`, `Lighting Template`, `Thickness`,
`Slack`, `Has Teleport Data`, `Linked Door`, `No Alarm`,
`Teleport Loc Name`, `Random Teleport Marker`,
`Linear Velocity X/Y/Z`, `Angular Velocity X/Y/Z`,
`Attach Reference`, `Enable State Parent`, `Enable State Opposite`,
`Linked Reference`, `Keyword Linked References`,
`Activate Parents`, `Activate Parent Only`, `Multibound Ref`,
`Spawn Container`, `Acoustic Parent`.

#### 7d.2 TESObjectWEAP (46 properties)

Schema builder at ~RVA `0xc954b0`: `Weapon speed`, `Attack delay`,
`Weapon reach`, `Automatic weapon`, `Has scope`, `Can't be dropped`,
`Hides backpack`, `Accuracy Bonus`, `Attack Seconds`, `Reload Seconds`,
`Bolt Charge Seconds`, `Number of projectiles fired per shot`,
`Iron sight FOV mult`, `Critical damage multiplier`, `Critical charge bonus`,
`Critical effect on death`, `Minimum range`, `Maximum range`,
`Out of range damage multiplier`, `Hit behavior`, `Weapon type`,
`Ignore resistance`, `Is playable`, `Is player-only`, `Is minor crime`,
`Fixed range`, `Use prevented in normal combat`, `NPCs use ammo`,
`No jam after reload`, `Override damage to weapon`,
`Use alternating rumble`, `Non hostile weapon`, `Is bound weapon`,
`Damage to weapon multiplier.`, `Sound level multiplier`,
`Critical effect`, `Attack sound`, `2D attack sound`,
`Attack loop sound`, `Attack fail sound`, `Idle sound`,
`Equip sound`, `Unequip sound`, `Fast Equip sound`,
`Aim model`, `Zoom data`.

#### 7d.3 BGSAimModel (16 properties)

Schema builder at ~RVA `0x445e46`: `Min cone angle`, `Max cone angle`,
`Cone increase per shot`, `Cone decrease per sec`,
`Time after shot before cone starts decreasing (ms)`,
`Sneaking multiplier on cone angle`,
`Force of spring controlling how fast recoil diminishes`,
`Multiplier on speed recoil diminishes (IronSights)`,
`Maximum recoil added per shot`, `Minimum recoil added per shot`,
`Multiplier on recoil when not in iron sights`,
`Recoil will runaway to maximum after this many shots`,
`Width of the "fan" that the recoil can fall within`,
`Rotates the recoil arc`,
`Multiplier on cone of fire extents when in iron sights`,
`Base stability value factored into stability formula`.

#### 7d.4 Remaining 8 serializers

The remaining eight serializers (BGSKeyword, BGSMod_Object, CameraShot,
Explosion, Projectile, SoundCat, SoundDesc, Weather) have confirmed RTTI
and vftable entries, but their schema builders either have very small
property sets (< 5 calls to `0x16248`) or use a different registration
API — their exact property names are still TBD pending vftable COL
recovery.

**Two non-serializer schema builders** were also detected via the
`call 0x16248` pattern: one at `0x2bbdf70` (35 props — texture blend,
alpha, base texture, envmap, normal, palette) and another at `0x2bbff00`
(34 props — material effects, shadow casting, displacement, hair/eye/skin).
These belong to `BGSMaterialObject`/`BGSEffectShader`-family classes,
not JSON serializers — the `call 0x16248` API is a general-purpose
property editor registration, not JSON-specific.

### 7e. REFR binary LoadRecord / SaveRecord — vftable slots and subread order

REFR's primary vftable is at RVA `0x30a1990`, decoded from the in-place
ctor (`FUN_140d64f50`) via the RIP-relative LEA chain. The vftable is
**shifted by +1 slot** compared to CELL/WRLD (slot 0 = an extra entry before
the first virtual, likely the RTTI COL pointer stored inside the array rather
than at `vftable[-1]`). Mapping against the CELL reference: REFR[s] =
CELL[s+1] for all shared TESForm slots.

| REFR slot | CELL equiv | thunk RVA | resolves to | role |
|---|---|---|---|---|
| **23** | CELL 24 (LoadRecord) | `0x937d9` | `jmp 0xd65f60` | **LoadRecord** — switch-dispatch on ~160 subrecord 4CCs |
| **26** | CELL 27 (SaveRecord) | `0x43ea5` | `jmp 0xda7820` | **SaveRecord** — virtual-subclass save chain |

#### 7e.1 LoadRecord (`FUN_140d65f60`, refr-loadsave-decomp.txt)

Signature: `bool FUN_140d65f60(longlong *this, longlong tesfile)`

Flow:
1. `FUN_14000ff83(this, 0)` — zero-init
2. `thunk_FUN_1408bf480(this, tesfile)` — **TESForm base LoadRecord**
   (reads EDID, FULL, and other base form data)
3. Sets form-type byte `*(this + 0x26) = 0x40`
4. Enters the subrecord dispatch loop: `thunk_FUN_14087e760(tesfile)` reads
   the next 4CC + u16 size, then a **cascading if/else tree** (not a jump
   table) dispatches on the 4CC value.

The dispatch tree is organized by numeric 4CC ranges. Representative
subrecords from the first ~70 decoded:

| 4CC | handler | description |
|---|---|---|
| `VMAD` (0x44414D56) | `thunk_FUN_1408bf1a0` | VMAD data block |
| `DATA` (0x41544144) | inline read (0x18 bytes) | position + rotation (6 floats at +0x1C/+0x1A/+0x1B/+0x1D/+0xE4/+0xD4) |
| `NAME` (0x454D414E) | base-object setter (vt+0x198) | set base object + read EDID override |
| `XIS2` (0x32534958) | flag handler | (handled inline) |
| `XPPA` (0x41505058) | flag handler | (handled inline) |
| `XCZA` (0x415A4358) | flag handler | (handled inline) |
| `XRGB` (0x42475258) | flag handler | (handled inline) |
| `XLIB` (0x42494C58) | flag handler | (handled inline) |
| `XGLB` (0x424C4758) | flag handler | (handled inline) |
| `XLMB` (0x424D4C58) | flag handler | (handled inline) |
| `XSPC` (0x43505358) | flag handler | (handled inline) |
| `XAMC` (0x434D4158) | flag handler | (handled inline) |
| `XENC` (0x434E4558) | encounter zone link | (handled inline) |
| `XLOC` (0x434F4C58) | lock data | (handled inline) |
| `XMRC` (0x43524D58) | merchant container | (handled inline) |
| `XFVC` (0x43564658) | follower/command ref | (handled inline) |
| `XCZC` (0x435A4358) | flag handler | (handled inline) |
| `XCIM` (0x4D494358) | image-space modifier | (handled inline) |
| `XUSE` (0x45535558) | use-report ref | (handled inline) |

The full dispatch table spans ~160 distinct 4CC values (the complete set
includes all hex constants listed in the decompile artifact). The pattern
is always `if (4CC < threshold) { ... } else if (4CC == NNNNN) { handler; }
else ...` — Ghidra flattened the switch into a binary-search tree.

Key observation: many subrecords (especially the X* flag group) are checked
against a flag mask `*(this + 7) & 0x3ff` to determine if the data should
be read. The internal flag at `this + 0x50` gates the loop exit condition.

#### 7e.2 SaveRecord (`FUN_140da7820`)

Signature calls `thunk_FUN_1408c7710` (subrecord writer) only 4× for the
base form fields: `NAME`, `XLRL` (location ref), `XSCL` (scale), `DATA`
(position+rotation). The REFR-specific subrecords are written through the
**virtual subclass save chain** — SaveRecord calls through vftable slots on
`this` (e.g. `vt+0x1b0` → PTRN, `vt+0x3b8` → save extensions, `vt+0x3c0`
→ STCP), the same pattern as CELL/WRLD §10d. The workhorses are the shared
`tesform.cpp` save helpers (`FUN_1408c7a70` at `tesform.cpp:0x9a4` =
form-header writer, `FUN_1408c7bf0` at `tesform.cpp:0x9e0` = subrecord
close/size-patch), not REFR-specific writers.

**Artifacts:** `ghidra-dump/decompile-REFR-LoadSaveRecord.txt` (two-function
decompile, 490 lines).

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

### 8.4 Interior-cell XCLC path — why the coordinate setter skips them

**Short answer:** interior cells don't use XCLC at all. The subrecord is
**exterior-only** (12 bytes: cell-grid coords x, y + a field at +0x1e).
Interior cells use **XCLL** (`0x4C4C4358`, 0x88 bytes — lighting/ambient
data), which is the interior counterpart in both LoadRecord and SaveRecord.

**The three functions that create the asymmetry:**

- **`FUN_140d18490`** (cell-data getter, RVA `0xd18490`):
  ```c
  if (cell->+0x50 & 1) return NULL;   // interior → NULL
  return cell->+0x60;                  // exterior → cell-data block
  ```
  This is called by both LoadRecord and SaveRecord to gate XCLC
  access. There's a duplicate at `0xd184b0` with identical logic
  (same bytes: `F6 41 50 01 75 03 33 C0 C3 48 8B 41 60 C3`).

- **`FUN_140d197c0`** (coordinate setter, RVA `0xd197c0`):
  ```c
  if (cell->+0x50 & 1) return;        // interior → skip
  if (cell->+0x60) {
      cell_data[0] = x;                // write grid x
      cell_data[4] = y;                // write grid y
  }
  ```
  Called by `NewCell` after constructing an exterior cell. Skips
  interior cells entirely — the programmatic setter, not the loader.

- **`FUN_140d19770`** (simple world-position getter, RVA `0xd19770`):
  ```c
  if (cell->+0x50 & 1) { *x=0.0f; *y=0.0f; return; }  // interior → (0,0)
  cell_data = cell->+0x60;
  *x = (float)(cell_data[0] * 4096);   // grid x → world units
  *y = (float)(cell_data[4] * 4096);   // grid y → world units
  ```
  Used by the cell-map insert (`FUN_140e4d180`) to grow world bounds.
  Returns (0,0) for interior — they don't participate in the grid.

**In LoadRecord (FUN_140d146d0):** The XCLC handler dispatches on
subrecord 4CC `0x434C4358` and calls `FUN_140d18490`. For interior
cells, this returns NULL, the NULL guard fires, and the read is
**skipped** — the 12 bytes are read into locals and discarded. XCLL
(`0x4C4C4358`) has its own handler in the same switch; it writes
0x88 bytes into a different target.

**In SaveRecord (FUN_140d44ba0):** The exterior/interior split is
explicit at the top of the XCLC/XCLL block:

```c
if ((cell->+0x50 & 1) == 0) {          // exterior
    data = FUN_140d18490(cell);         // cell-data ptr
    write_subrecord('XCLC', data, 12);  // coords + field
    if (data->+0x07) write_subrecord('CNAM', ...);
    if (data->+0x1d) write_subrecord('ZNAM', ...);
} else {                                // interior
    data = FUN_140d184b0();             // NULL call — different accessor
    write_subrecord('XCLL', data, 0x88); // lighting, not coords
}
```

**Why:** interior cells have no grid position. Their "coordinates" are
implicit from the worldspace parent (stored at `cell+0x150` back-pointer).
The cell-data block at `+0x60` still exists for interior cells (allocated
by `FUN_140d18510`), but it carries XCLL lighting/ambient data, not
XCLC grid coords. Functions that need interior cell position navigate
through `cell+0x150` → worldspace → cell-level array (see the complex
getters at `0xd197e0`/`0xd19820` which do handle the interior path).

### 8.5 Cell-data block — exterior (0x20) and interior (0x88) layout

Recovered from the allocator (`FUN_140d18510`, `tesobjectcell.cpp:0xaee`),
the coordinate setters, the LoadRecord/SaveRecord field accessors, and the
world-position getter. The cell-data pointer lives at `cell+0x60`; the block
is allocated by `FUN_140d18510` with type gated by `cell+0x50 & 1`.

#### 8.5a Allocator: `FUN_140d18510(cell)`

```c
// Free old block
old = cell->+0x60;
if (old) {
    if (exterior) {
        if (formFlags & 0x4000) { old->+0x08 = 0; old->+0x10 = 0; }
        thunk_FUN_140d3fec0(old, 1);  // decrement refcount on linked data
    } else {
        FUN_140001c4e(old, 0x88);     // free 0x88-byte interior block
    }
    cell->+0x60 = NULL;
}

// Allocate new block
if (exterior) {
    block = thunk_FUN_141ff6d30(heap, 0x20, 0);  // 32 bytes
    if (block) {
        block[0:8] = 0;                         // +0x00: field0
        block[8:16] = 0;                        // +0x08: linked-data ptr
        block[16:24] = 0;                       // +0x10: linked-data ptr2
        *(u32*)(block+24) = 0;                  // +0x18: field3
        *(u16*)(block+28) = 0;                  // +0x1C: short
        *(u8*)(block+30) = 0;                   // +0x1E: byte flag
    }
} else {
    block = thunk_FUN_141ff6d30(heap, 0x88, 0, 0);  // 136 bytes
    if (block) {
        thunk_FUN_140911970(block + 0x28);      // +0x28: float-array init (5 floats)
        block[0:8] = 0;                         // +0x00: int32 x coord
        block[8:16] = 0;                        // +0x08: int32 y coord
        block[16:24] = 0;                       // +0x10: field2
        *(u32*)(block+24) = 0;                  // +0x18: field3
        *(u64*)(block+28) = 0x3f800000;         // +0x1C: 1.0f float
        *(u32*)(block+36) = 0x3f800000;         // +0x24: 1.0f float
        *(u32*)(block+72) = 0;                  // +0x48: zero
        *(u64*)(block+76) = 0x3f800000;         // +0x4C: 1.0f float
        *(u32*)(block+84) = 0;                  // +0x54: zero
        block[11] = 0x79f;                      // +0x58: u64 0x79F constant
        block[12] = 0x461c4000;                 // +0x60: 10000.0f float
        block[16] = 0;                          // +0x80: pointer
        *(u32*)(block+104) = 0;                 // +0x68: zero
        *(u32*)(block+108) = 0x3f800000;        // +0x6C: 1.0f float
        *(u32*)(block+112) = 0x3f800000;        // +0x70: 1.0f float
        *(u32*)(block+116) = 0x3f800000;        // +0x74: 1.0f float
        *(u32*)(block+120) = 0x3f800000;        // +0x78: 1.0f float
        *(u32*)(block+124) = 0x3f800000;        // +0x7C: 1.0f float
    }
}
cell->+0x60 = block;
```

#### 8.5b Exterior 0x20-byte cell-data layout

32 bytes, allocated for exterior cells. Verified by the coordinate setter
(`FUN_140d197c0`), LoadRecord XCLC handler, and SaveRecord exterior path.

| offset | size | name | init | evidence |
|---|---:|---:|---|
| +0x00 | 4 | **gridX** (int32) | 0 | `FUN_140d197c0`: `*block = x`; LoadRecord: `read(block, 4)`; `FUN_140d19770`: `*worldX = (float)(block[0] * 4096)` |
| +0x04 | 4 | **gridY** (int32) | 0 | `FUN_140d197c0`: `block[1] = y`; `FUN_140d19770`: `*worldY = (float)(block[4] * 4096)` |
| +0x07 | 1 | **CNAM flag** (byte) | 0 | SaveRecord: `if (block[7] != 0) write_subrecord('CNAM', block+7, 1)` |
| +0x08 | 8 | **linked-data ptr** (void*) | 0 | `FUN_140d18510`: `old->+0x08 = 0`; SaveRecord: `if (block[8] != 0) write_TVDT(block[8])` |
| +0x10 | 8 | **linked-data ptr2** (void*) | 0 | `FUN_140d18510`: `old->+0x10 = 0`; only accessed in allocator free path |
| +0x18 | 4 | (u32) | 0 | zeroed in allocator; role TBD |
| +0x1C | 2 | (u16) | 0 | zeroed in allocator; role TBD |
| +0x1D | 1 | **ZNAM flag** (byte) | 0 | LoadRecord ZNAM: `read(block+0x1d, 1)`; SaveRecord: `if (block[0x1d] != 0) write('ZNAM', block+0x1d, 1)` |
| +0x1E | 1 | byte | 0 | XCLC position 2 (byte at +0x1e in the 12-byte XCLC payload) |

**XCLC wire format (12 bytes):** `[gridX:4][gridY:4][+0x1e:1][padding:3]` —
confirmed by SaveRecord: `puVar4 = getter(cell); local_18 = *puVar4; local_14 = puVar4[1]; local_10 = *(puVar4+0x1e); write_subrecord('XCLC', &local_18, 0xc)`.

#### 8.5c Interior 0x88-byte cell-data layout

136 bytes, allocated for interior cells. Used for the **XCLL** lighting/ambient
subrecord (the interior counterpart to XCLC). The full field map from the
allocator init sequence:

| offset | size | name / init | notes |
|---|---:|---|---|
| +0x00 | 8 | 0 (u64) | field0 — zeroed |
| +0x08 | 8 | 0 (u64) | field1 — zeroed |
| +0x10 | 8 | 0 (u64) | field2 — zeroed |
| +0x18 | 4 | 0 (u32) | field3 — zeroed |
| +0x1C | 4 | **1.0f** (`0x3f800000`) | scale seed #1 |
| +0x20 | 4 | (in `thunk_FUN_140911970`) | ambient R? (5-float array at +0x28 starts here) |
| +0x24 | 4 | **1.0f** (`0x3f800000`) | scale seed #2 |
| +0x28 | 20 | `thunk_FUN_140911970(block+0x28)` | **5-float ambient/lighting array** — directional, ambient, fog color, fog near/far (standard XCLL layout) |
| +0x3C | 4 | (in float-array) | 4th float of lighting group |
| +0x40 | 4 | (in float-array) | 5th float of lighting group |
| +0x44 | 4 | (padding/alignment) | |
| +0x48 | 4 | 0 (u32) | zeroed |
| +0x4C | 8 | **1.0f** (`0x3f800000`) | scale seed #3 (stored as u64 pair) |
| +0x48 | 4 | 0 (u32) | zeroed |
| +0x4C | 8 | **1.0f** (`0x3f800000`) | scale seed #3 (stored as u64 pair) |
| +0x54 | 4 | 0 (u32) | zeroed |
| +0x58 | 8 | **0x79F** (u64) | validity marker (same as exterior — bit 2 set) |
| +0x60 | 8 | **10000.0f** (`0x461c4000`) | default water height (u64 — shared with exterior) |
| +0x68 | 4 | 0 (u32) | zeroed |
| +0x6C | 4 | **1.0f** (`0x3f800000`) | scale seed #4 |
| +0x70 | 4 | **1.0f** (`0x3f800000`) | scale seed #5 |
| +0x74 | 4 | **1.0f** (`0x3f800000`) | scale seed #6 |
| +0x78 | 4 | **1.0f** (`0x3f800000`) | scale seed #7 |
| +0x7C | 4 | **1.0f** (`0x3f800000`) | scale seed #8 |
| +0x80 | 8 | 0 (ptr) | pointer — zeroed |

**Key findings from the full decompile:**
- The **0x79F** and **10000.0f** water height are present in BOTH layouts — they're not exterior-specific.
- The interior layout has **8 identity scale seeds** (all `1.0f`) vs. exterior's **2** — confirming the interior block carries XCLL ambient/lighting data.
- The `thunk_FUN_140911970` call at `block+0x28` initializes a 5-float lighting array (directional, ambient, fog) — the XCLL core.
- Interior free path: `FUN_140001c4e(old, 0x88)` — single allocation, no refcounted linked data.
- Exterior free path: `thunk_FUN_140d3fec0(old, 1)` — refcount decrement on linked data at `+0x08`/`+0x10`.

#### 8.5d The 0x79F constant at +0x58

The value `0x79F` (1949 decimal) is stored as a **u64** at exterior
cell-data offset `+0x58` (confirmed: `puVar1[0xb] = 0x79f`). It is
written once in the allocator and **never read by any function in the
cell-data range** (0xd17000–0xd1a000) — no MOV from `[RAX+0x58]` exists.

This is likely a **version marker** or **magic constant** that identifies
the cell-data block as valid — the CK's cell-loader checks byte `+0x58`
for `4` (bit 3) before doing the worldspace→cell lookup in
`FUN_140d184d0` (the exterior-path continuation of the getter function):
`TEST [RAX+0x58], 4; JZ skip`. The value `0x79F` has bit 2 set (`4`),
so the test passes for freshly allocated blocks.

The `0x461c4000` at `+0x60` is `10000.0f` — the **default water height**
for the cell (CK's standard water level).

#### 8.5e Accessor summary

| function | RVA | role | interior/exterior behavior |
|---|---|---|---|
| `FUN_140d18510` | `0xd18510` | **allocator** — frees old block, allocates new (0x20 or 0x88) | `cell+0x50 & 1` → 0x20 exterior / 0x88 interior |
| `FUN_140d18490` | `0xd17ab0` | **getter** — returns `cell+0x60` pointer | interior → NULL; exterior → `cell+0x60` |
| `FUN_140d184b0` | `0xd17a90` | **getter (alt)** — returns `cell+0x60` pointer | interior → `cell+0x60`; exterior → NULL (inverted from 0xd18490) |
| `FUN_140d197c0` | `0xd197c0` | **coord setter** — writes gridX/gridY to `+0x00/+0x04` | interior → no-op; exterior → `block[0:2] = (x,y)` |
| `FUN_140d19770` | `0xd19770` | **world-pos getter** — returns `(gridX*4096, gridY*4096)` | interior → (0.0, 0.0); exterior → `block[0:2] * 4096.0f` |
| `FUN_140d184d0` | `0xd184d0` | **validity check** — `TEST [block+0x58], 4` | continuation of getter; calls `cell+0x150` → worldspace when valid |

### 8b. TESWorldSpace member layout (constructor)

Recovered from the factory Create (`FUN_140e49880` → alloc 0x2B8 + call ctor
`FUN_140e49960`, `tesworldspace.cpp`). The factory vftable is at RVA
`0x30aa450` (COL `0x35d03c0`, `.?AV?$ConcreteFormFactory@VTESWorldSpace@@$0EK@@@`),
identified via the shared IFormFactory slots (5–7) common to all
`ConcreteFormFactory` instances. `param_1` is the 8-byte slot pointer
(`this`) passed to the ctor.

**Primary vftable** at `0x30aa510` (slot 0 = COL `0x35d0450`),
**secondary1** at `0x30aa9d8` (COL `0x35d0528`),
**secondary2** at `0x30aaac0` (COL `0x35d0558`).
The ctor initializes all three:
  - `+0x00` ← primary (`0x30aa510`), `+0x28` ← secondary1 (`0x30aa518` = slot 1),
    `+0x40` ← secondary2 (`0x30aaac8` = slot 2).

The `0x30aa518` and `0x30aaac8` entries are the **vftable+8** offsets
(effective slot 1 for each subvtable), per x64 MSVC's
Complete Object Locator offset/cdOffset layout.

| offset | size | init | meaning |
|---|---:|---:|---|
| +0x00 | 8 | `0x30aa510` | primary vftable (TESWorldSpace) |
| +0x28 | 8 | `0x30aa518` | secondary1 (TESFullName base) |
| +0x30 | 16 | ctor `0x914c20` | sub-object init (same as CELL §8.1 +0x30) |
| +0x40 | 8 | `0x30aaac8` | secondary2 vftable |
| +0x70 | 8 | zero → alloc 0x3f319 | **cell map** (BST map, keyed by coords — `FUN_140e539e0` lookup, `FUN_140e4d180` insert) |
| +0x74 | 4 | 0 | |
| +0x78 | 4 | 0 | |
| +0x7C | 4 | 0 | |
| +0x80 | 8 | `0x3c088` ctor | sub-object (likely land-data map) |
| +0x88 | 8 | 0 | |
| +0x90 | 8 | `0x3c088` ctor | sub-object |
| +0x98 | 8 | `0x3c088` ctor → alloc `0x497bf` | **cell-level array** (BSTArray, element size 8 × `param_1` → 0x20 stride) |
| +0xA0 | 8 | 0 | **lone interior cell** — set in `FUN_140e4d180` when an interior cell is stored |
| +0xB8 | 8 | `0x143bd7174` | static marker |
| +0xC0 | 8 | `0x143bd7170` | static marker |
| +0x140 | 4 | — | **minX** (world bounds, float) — grown by `FUN_140e4d180` |
| +0x144 | 4 | — | **minY** (float) |
| +0x148 | 4 | — | **maxX** (float) |
| +0x14C | 4 | — | **maxY** (float) |
| +0x10 | 4 | (TESForm base) | form flags |
| +0x26 | 1 | **0x4A = 74** | **form-type byte = WRLD** |

**Key offsets cross-checked against `FUN_140e4d180` (cell-map insert):**

- **+0x70** = cell map (`thunk_FUN_140e539e0(worldspace+0x70, key, ...)` —
  BST map keyed by cell coord pair, the `FUN_140e539e0` lookup returns *found*).
- **+0xA0** = interior-cell slot (single ptr, set when interior and
  `worldspace+0xA0 == 0`). Non-interior cells skip this path.
- **+0x140…+0x14C** = world bounds (minX, minY, maxX, maxY floats). The
  insert function reads the cell's world position via `FUN_140d19770`, then
  `fmin(fmin)` for each axis. Grows outward — never shrinks.

**Comparison with CELL layout (§8):** Both start with three vftables
(primary at +0x00, +0x28, +0x40) and a 16-byte sub-object at +0x30. CELL's
+0x58/+0x60 (cell-data handle/pointer) vs WRLD's +0x70 (cell map) is the
natural difference — CELL has its own data block allocator, WRLD has a
cell-coord keyed map. Both have the form-type byte at +0x26 (0x3F vs 0x4A)
and flags in the TESForm base at +0x10.

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

#### 10a.1 Cross-check against Fallout4.esm CELL records (2026-08-22)

Byte-parsed 3 interior cells from Fallout4.esm (all `0x40000` = compressed).
Exterior cells all have `0x40000` too — no clean `0x0002` cells found in Fallout4.esm
(all cells are compressed in the shipped master).

**Core interior set (present in every cell, 10):**
`EDID`, `FULL`, `DATA`, `VISI`, `PCMB`, `XCLL` (int. lighting, 0x88B),
`LTMP`, `XCLW`, `XCIM`, `XLCN`.

**Common interior extras (present in 2/3 cells, 4):**
`XCCM`, `XILW`, `XOWN`, `XPRI`.

**Rare/sporadic interior (1/3 cells, 4):**
`XCMO`, `XCAS`, `XEZN`, `XCRI`.

**Verdict — 100% match with LoadRecord dispatch.**
All 18 subrecords seen in real cells appear in the doc's LoadRecord list
(`XCLC` and `XCLW` are exterior see §10a.2). No missing subrecords found.
The 10 `SaveRecord` writes match the core interior set: interior cells
write `XCLL` where exterior cells write `XCLC`.

#### 10a.2 Interior vs. exterior subrecord split

| subrecord | interior | exterior | verified in real file |
|---|---|---|---|
| `XCLL` (0x88B lighting) | ✅ core | — | every interior cell |
| `XCLC` (12B coords) | — | ✅ core | exterior-only (§8.4) |
| `XCLW` (water height) | ✅ core | ✅ | both types |
| `XCLR` (region list) | sporadic | rare | varies |
| `LTMP` (lighting template) | ✅ core | rare | ✓ |
| `XCWT` (water type) | rare | ? | FO4-only (1/46 cells), 4B formID |

#### 10a.3 SaveRecord validator (`validate_cell_save.py`)

A dedicated byte-parser cross-checks every CELL subrecord in any `.esm` against the
SaveRecord dispatch. Running against Fallout4.esm (46 interior CELLs):

- **18 unique subrecords found** — 100% in SaveRecord, none in TESForm base or LoadRecord-only
- **0 unknowns** — every 4CC has a known write path
- **9 SaveRecord keys not seen in the sampled cells** (exterior-only `XCLC`/`CNAM`/`XCLR`/`XCLP`/`XCLS`,
  rare `MHDT`/`RVIS`/`XILW`/`XWEM`) — all explained by the sample being interior-only
- **`XCWT`** (cell water type, 4B formID) is new to the SaveRecord list — found in 1/46 cells

Validator: `ghidra-dump/validate_cell_save.py` — `--json` flag for machine output.

#### 10a.4 Full-file recheck + XCLC audit (2026-08-22)

The validator's scan was rebuilt to byte-step every 4-aligned offset and accept a
record only when its subrecord stream parses cleanly to EOF (false `CELL` matches
inside compressed payloads and GRUP data are rejected). This finds the full
population the earlier GRUP-walk sample missed:

- **10,027 CELL records** — 9,741 exterior + 286 interior (the earlier doc-10a
  sample of 46 was interior-only)
- **26 unique subrecords** — all accounted for; three were added to the
  SaveRecord dispatch as a result: `XRNK` (Owner Faction Rank, s32),
  `XILL` (Lock List, formID → FLST/NPC_), `XGDR` (God Rays, formID → GDRY)
- Exterior detection uses **DATA bit 0 (Interior)** — the record-header flags do
  not carry interior-ness in FO4; DATA-bit0 vs XCLC presence has 0 mismatches

**XCLC Force-Hide-Land audit** (new `--json` `xclc` section, doc 26 §3):

- 9,741 exterior cells carry XCLC, **0 interior** (exterior-only rule holds)
- All payloads are exactly **12 bytes** = [X s32][Y s32][hide byte][3B padding]
- Only **byte 8** is meaningful: low nibble = quad mask; **high nibble must be
  0**. Bytes 9-11 are padding the CK reader skips (FUN_140d146d0 stores byte 8
  into cell+0x1E) and are nonzero in 9,725/9,741 vanilla cells — they are NOT
  flagged (xEdit's u32 display of the trailing dword is a UI convenience, not
  engine semantics)
- Quad values in the master: `0x0` × 9,727 (no quads hidden), `0xF` × 14
  (all four quads hidden, formIDs 0x00000FCA–0x00000FE1) — no partial masks
- Violations caught: XCLC on interior cells, wrong payload length, high nibble
  of byte 8 set (values above 0x0F)

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

WRLD `LoadRecord` dispatches (cross-checked against 3 live WRLD records from
Fallout4.esm — DiamondCityFX, DiamondCity, Goodneighbor):

**Confirmed in real files (15):** `DATA`, `DNAM`, `EDID`, `FULL`,
`NAM0`..`NAM9` (some optional), `NAMA`, `ONAM`, `PNAM`, `WCTR`, `WNAM`,
`XLCN`, `XWEM`, `ZNAM`.

**In dispatch but absent from these 3 (19 — optional/rare/DLC):**
`ATDM`, `BTDM`, `CNAM`, `ICON`, `LTMP`, `MHDT`, `MNAM`, `TNAM`, `UNAM`,
`WLEV`, `WMAD`, `XEZN`. Plus `NAM1`/`NAM3`–`NAM8` (climate layers —
DiamondCityFX has only `NAM2` and `NAM9` because DiamondCity uses climate
NAM2; interior worlds skip climate entirely).

**Missing from the dispatch, found in every real WRLD (3 — §10b.1):**
`CLSZ` (cell size for world map), `OFST` (world map offset data array),
`RNAM` (region references — repeated per-region; 41× in DiamondCity).

**Not WRLD-specific** (handled by `TESForm` base; removed from the
per-class list): `OBND`, `PTRN`, `STCP`, `VMAD`.

**Correction:** the earlier list entry `NNAM` was a misdecode — WRLD has
no `NNAM` subrecord (that's a LeveledList subrecord). It was likely
`MNAM` (world map path) rendered by an arg-display mismatch.

#### 10b.1 Newly discovered: CLSZ, OFST, RNAM

Byte-parsed from Fallout4.esm WRLD records:

| 4CC | size | description |
|---|---|---|
| `CLSZ` | 4 | cell size for world map (u32) — present in all 3 sampled WRLDs |
| `OFST` | N × 4 | world map offset data (u32 array) — present in all 3 |
| `RNAM` | 4 | region reference (formID of a REGN record) — repeated, 41× in DiamondCity, many in Goodneighbor |

These are in the LoadRecord switch tree (recovered subrecord set of 26 reads)
but were missed in the initial doc summary because `CLSZ`/`OFST` appear as
hex immediates without adjacent CONCAT44 clues, and `RNAM` blended with
CELL's `RNAM`. The dispatch tree decoder now confirms them as part of the
WRLD surface.

WRLD `SaveRecord` writes: `CNAM`, `DATA`, `DNAM`, `LTMP`, `MHDT`, `MNAM`,
`NAM0`, `NAM2`, `NAM3`, `NAM4`, `NAM9`, `NAMA`, `ONAM`, `PNAM`, `WCTR`,
`WLEV`, `WNAM`, `XEZN`, `XLCN`, `ZNAM`. (Note: `CLSZ`, `OFST`, and `RNAM`
are also saved — they go through the `NAM*`-array and map-data iterators
dispatched from `vl+0x3b8`/`vl+0x3c0`, not inline in the SaveRecord body.)

#### 10b.3 WRLD DATA — byte-level field map

In FO4, DATA is **1 byte** — the worldspace flags only. Previous TES games (FO3,
Skyrim) embedded bounds and parent worldspace in a ~12-16 byte DATA; FO4 split these
into separate subrecords. Byte-parsed from 3 real records against the CK's
`FUN_140e4a750` LoadRecord:

**Flags (DATA, 1B — mandatory in every WRLD):**

| bit | mask | meaning | DiamondCityFX | DiamondCity | Goodneighbor |
|---|---|---|---|---|---|
| 0 | 0x01 | SmallWorld (interior) | ✅ | | ✅ |
| 1 | 0x02 | CanNotFastTravel | | | |
| 2 | 0x04 | (no LOD water — TES5 compat) | | | |
| 3 | 0x08 | (no LOD noise — TES5 compat) | | | |
| 4 | 0x10 | NoLandscape | ✅ | | |
| 5 | 0x20 | (no sky — TES5 compat) | | | |
| 6 | 0x40 | FixedDimensions | ✅ | | |
| 7 | 0x80 | NoGrass | | | |

DiamondCityFX: `0x51` = SmallWorld | NoLandscape | FixedDimensions (FX-only world,
no terrain). DiamondCity: `0xD9` = SmallWorld | NoLandscape | NoGrass | FixedDimensions
| (bit 5 set = no sky implied, bit 7 = NoGrass). Goodneighbor: `0x01` = SmallWorld only.

**Parent worldspace (WNAM, 4B):** `formID` — always `0x0000003C` (Commonwealth)
for all 3. Sets the parent worldspace for weather/lighting inheritance.

**Center cell (WCTR, 4B):** `(int16 cellX, int16 cellY)` at worldspace origin.
DiamondCityFX: `(0,0)`. Interior-world centers are always at grid origin.

**Land LOD dimensions (DNAM, 8B):** `(float minX, float minY)` — DiamondCityFX:
`(-2048.0, -10000.0)`. These define the land-LOD grid origin in world units.
For interior worlds they're often at extreme values to disable land LOD.

**Object bounds (NAM0, 8B):** `(float minX, float minY)` — DiamondCityFX:
`(-36864.0, -12288.0)`. Object LOD bounding box minimum in world units.

**Object bounds max (NAM9, 8B):** `(float maxX, float maxY)` — DiamondCityFX:
`(24576.0, 16384.0)`. Together with NAM0 this forms the worldspace AABB.

**Climate data (NAM2, 8B):** `(float landFadeStart, float landFadeEnd)` —
DiamondCityFX: near-zero (disabled). Interior worlds don't use land fade.

**Use-flags (PNAM, 2B):** bitmask of parent-worldspace data inheritance.
DiamondCityFX `0x0016`: UseLODData | UseMapData | UseClimateData.
Goodneighbor `0x001F`: all bits except bit 6 (0x40).

**Complete FO4 WRLD subrecord field summary:**

| 4CC | size | type | field |
|---|---|---|---|
| DATA | 1 | u8 | worldspace flags |
| EDID | var | zstring | editor ID |
| FULL | 4 | formID | name record (FULL subrecord) |
| WNAM | 4 | formID | parent worldspace |
| WCTR | 4 | (i16,i16) | center cell coords |
| PNAM | 2 | u16 | parent use-flags |
| DNAM | 8 | (f32,f32) | land LOD origin |
| NAM0 | 8 | (f32,f32) | object bounds min |
| NAM9 | 8 | (f32,f32) | object bounds max |
| NAM2 | 8 | (f32,f32) | climate land fade (NAM2 only) |
| NAMA | 4 | f32 | unknown (always ~1.0) |
| ONAM | 16 | binary | music/audio data |
| ZNAM | 4 | formID | music type? |
| XWEM | var | zstring | water cubemap path (always mipblur_DefaultOutside1.dds) |
| XLCN | 4 | formID | location formID |
| RNAM | 4 | formID | region reference (repeated) |
| CLSZ | 4 | u32 | world map cell size (always 0) |
| OFST | var | f32[] | world map offset array |

#### 10b.2 XWEM → distant-terrain water mesh builder

`XWEM` is the **worldspace-default water height** (float, RVA `0xe4a276` in WRLD
LoadRecord). This value feeds the CK's `bgsterrainwatermeshbuilder.cpp`
(0xf76000–0xf83fff, `shared\distantterrain\`) which generates the runtime LOD
water mesh. The xLODGen engine's `WaterDelta` setting controls how the water
plane interacts with terrain below:

- **Engine default: `WaterDeltaLOD<n> = -1`** (disabled — no optimization).
- At `>= 0`, below-water heightfield samples are clamped to `waterHeight − delta/LOD`
  and interior underwater vertices are marked `Ignored` (greedy reducer skips them).
- **`WaterDelta = 0`** is NOT a no-op: `0 >= 0` enters the block but the clamp
  is `waterHeight − 0 = waterHeight`, so heights are never flattened (below-water
  samples are < waterHeight by definition). Only the vertex handling changes —
  underwater interior vertices are still marked `Ignored`, and at `lodLevel > 1`
  underwater coarse-grid vertices are forced `Used` for seam matching.
- The integer division `waterDelta / lodLevel` is **truncating** — small deltas
  silently vanish: `3` at LOD4, `7` at LOD8, `15` at LOD16, `31` at LOD32.

Full xLODGen water analysis: [`lodgen/reverse-engineering/XLODGEN_TERRAIN_INTERNALS.md` §1–§6](../lodgen/reverse-engineering/XLODGEN_TERRAIN_INTERNALS.md) — per-quad `GenerateWater`, water-height grouping (exact at LOD4, merged at LOD8+), `BSSubIndexTriShape`/`BSSegmentedTriShape` shape selection, land-clipped water under `SPECIALWATER`, and the `WATERDELTALOD4/8/16/32` → `TerrainLOD.GenerateQuad` pipeline.

### 10c. Complete override map — named TESForm virtuals for CELL and WRLD

Every override was decompiled and its call pattern matched against
the TESForm base vftable surface to assign a concrete virtual name.
Base-stub slots (25, 26, 29, 34, 36, 37, 39–79) are omitted;
they carry the same TESForm thunk in every class.

#### 10c.1 TESObjectCELL (primary vftable `0x309c780`)

| slot | base | CELL override | virtual name | call-pattern evidence |
|---|---|---|---|---|
| 23 | `FUN_140004c28` | `FUN_140d13f00` | **`ClearItem`** | `tesobjectcell.cpp:0x49f`; frees cell-data at `+0x58` via `FUN_1407d2060`; frees `+0x60` block (0x20 interior / 0x88 exterior); nulls `+0x150` multibound |
| **24** | `thunk_FUN_140720ac0` | `FUN_140d146d0` | **`LoadRecord`** | 14× `FUN_14087e870` reads; `tesobjectcell.cpp:0x4d7/0x4ee/0x555` |
| **27** | `thunk_FUN_140720ad0` | `FUN_140d44ba0` | **`SaveRecord`** | 13× `FUN_1408c7710` writes |
| **28** | `thunk_FUN_1408bccb0` | `FUN_140d44f00` | **`ReleaseForm`** | walks ref-array at `+0x10`/`+0x12`; per-ref IncRefCount→DecRefCount via `bshandlerefobject.inl:0x3b/0x47/0x55`; calls `thunk_FUN_14087ec50`(unregister) + `thunk_FUN_14087d2d0`(notify DataHandler in GRUP type 6) + `thunk_FUN_140d55ca0`(cell→worldspace); returns bool |
| 30 | `thunk_FUN_1408bcee0` | `FUN_140d45150` | **`InitItem`** | same cleanup chain as slot 28 (unregister→notify→associate) but **without** the ref-array walk — direct single-form init path; always returns 1 |
| 31 | `thunk_FUN_1408bcfc0` | `FUN_140d45400` | **`ResolveFormIDFromFile`** | 10-case switch on `param_2[3]` (TESFile handle type): case 0→base `FUN_1408bcfc0`; case 1→TESWorldSpace RTDynamicCast; case 2/3→ref-count iteration; case 4/5→`thunk_FUN_140d17890` flag check; case 6/8/9→TESObjectCELL RTDynamicCast→`vt+0xf8`(slot 31) |
| 32 | `thunk_FUN_1408bcfa0` | `FUN_140d451d0` | **`IsParentForm`** | switch on `param_2+0x26` (form-type byte): case 0x3f(CELL)→compares interior/exterior flag parity, worldspace ptr equality via `vt+0xf8`, ref-count ordering; case 0x40–0x4c(REFR…LCTN)→RTDynamicCast to `TESChildCell`→`vt+8`(GetParentCell)→check `== this` or `vt+0xf8` |
| 33 | `thunk_FUN_1408bd340` | `FUN_140d14040` | **`Copy`** | array allocs, cell-data realloc at `tesobjectcell.cpp:0x133a` |
| 35 | `thunk_FUN_14040ef70` | `FUN_140d153d0` | **`ValidateCell`** | `tesobjectcell.cpp:0x5f9/0x1350`; dangling-cell checks, bad-coord errors, cell-name-length validation |
| 38 | `thunk_FUN_1408be250` | `FUN_140d17dc0` | **`GetEditorLabel`** | `void(this, wchar_t*, size)`; interior→`"'%s' (%08X)"`; exterior→`"Persistent cell '%s' (%08X) (%i, %i) in WorldSpace '%s' (%08X)"` via `vt+0x238`(GetName)×2 + coord extraction from `+0xc`; invalid worldspace→`"'%s' (%08X) Invalid WorldSpace"` |

#### 10c.2 TESWorldSpace (primary vftable `0x30aa510`)

| slot | base | WRLD override | virtual name | call-pattern evidence |
|---|---|---|---|---|
| 22 | `thunk_FUN_1408bca80` | `FUN_140e4a070` | **`Initialize`** | zeroes +0xf0/+0x160/+0x168/+0xb8/+0xba/+0xbc; sets texture scale `0x3f800000`(1.0f), `0xc5000000`; calls `thunk_FUN_142424550`(texture init at +0x48); frees refcounted block at +0xc8; destructs +0x178/+0x1a0/+0x1c8/+0x1f0 |
| 23 | `FUN_140004c28` | `FUN_140e4a1f0` | **`ClearItem`** | frees cell-map block at +0xc8(DecRef→dtor); frees +0xd0(0x38 via `thunk_FUN_140d3c040`) + +0xd8(0x38 via `thunk_FUN_140d3bc30`); frees heap blocks +0x2a8/+0x2b0 |
| 24 | `thunk_FUN_140720ac0` | `FUN_140e4a750` | **`LoadRecord`** | 26× `FUN_14087e870`; `tesworldspace.cpp:0x360/0x378/0x4b2/0x4ba/0x4ca` |
| **25** | `thunk_FUN_14039c240` | `FUN_140e4b040` | **`IsWorldSpace`** | `tesworldspace.cpp:0x316`; returns `1` unconditionally |
| 27 | `thunk_FUN_140720ad0` | `FUN_140e54420` | **`SaveRecord`** | 23× `FUN_1408c7710` |
| 31 | `thunk_FUN_1408bcfc0` | `FUN_140e54b20` | **`ResolveFormIDFromFile`** | 7-case switch on `param_2[3]`: case 0→base `FUN_1408bcfc0`; case 1→TESWorldSpace RTDynamicCast→`vt+0xf8`; case 2/3→return 0; case 6→TESObjectCELL RTDynamicCast→`vt+0xf8`; case 7→return 1 |
| 32 | `thunk_FUN_1408bcfa0` | `FUN_140e549d0` | **`IsParentForm`** | switch on `param_2+0x26`: case 0x3f(CELL)→break(fallthrough); case 0x40–0x4c(REFR…LCTN)→RTDynamicCast to `TESChildCell`→`vt+8`(GetParentCell)→`vt+0xf8`(recursive IsParentForm on parent); **case 0x4a(WRLD)**→compare formIDs: `this+0x14 < param_2+0x14`; default→base |
| 33 | `thunk_FUN_1408bd340` | `FUN_140e4a320` | **`AddCell`** | `tesworldspace.cpp:0x62a`; calls `FUN_140e4d180`(cell-map insert §8.3), `FUN_140e53eb0`, `FUN_140d196a0` |
| 35 | `thunk_FUN_14040ef70` | `FUN_140e4b080` | **`IterateCells`** | `tesworldspace.cpp:0x50d`; 9× `FUN_1408be010`(form lookups) + `FUN_1408c68d0`/`FUN_1408bdd20` walking the cell array |

#### 10c.3 What the naming reveals

- **Four virtuals are unique to one class but not the other:**

  | slot | CELL | WRLD | why only one? |
  |---|---|---|---|
  | 22 | (base stub) | `Initialize` | Only worldspaces need a full reinit; cells are lightweight |
  | 25 | (base stub) | `IsWorldSpace` | Only WRLD needs to answer "yes"; CELL's form type says "no" via base |
  | 28 | `ReleaseForm` | (base stub) | REFR removal walks the cell's ref array (unique to CELL) |
  | 30 | `InitItem` | (base stub) | Direct form registration is cell-specific |

- **Two virtuals are overridden by both with identical semantics but different casts:**
  - Slot 31 (`ResolveFormIDFromFile`): CELL checks CELL+WRLD; WRLD checks WRLD+CELL
  - Slot 32 (`IsParentForm`): CELL compares worldspace pointers; WRLD compares formIDs for nested worlds and delegates through `TESChildCell` for children

- **Three virtuals are overridden by both with *different* semantics:**
  - Slot 33: CELL = `Copy` (clone), WRLD = `AddCell` (insert)
  - Slot 35: CELL = `ValidateCell` (sanity checks), WRLD = `IterateCells` (enumeration)
  - Slot 38: CELL = `GetEditorLabel`, WRLD = (base stub)

- **The base TESForm stub for slot 24 returns `1`** (always-success LoadRecord);
  **slot 27 returns void** (no-op SaveRecord) — explaining why non-world records
  with no override still load/save through TESForm base machinery via
  `FUN_1408c7a70`/`FUN_1408c7710` in `tesform.cpp`.

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
  | `FUN_140d18490` | — | CELL exterior cell-data getter (`cell+0x60`), `NULL` if interior (`cell+0x50 & 1`) — 13-byte function at `0xd17ab0`; the interior counterpart is `FUN_140d184b0` at `0xd17a90` which returns the pointer when `cell+0x50 & 1` is **set** (inverted logic). Called 5× inside CELL `SaveRecord`. |

  So CELL `SaveRecord` = `FUN_140d18490` (get cell data) + 13×
  `FUN_1408c7710` + 3× `FUN_1408c7bf0` (close each subrecord), with the
  form header handled by the `FUN_1408c7a70` family in the caller.
- **Anchor `0x62`/`0x67` in the assert thunks is the severity/kind
  constant, not a source line** — the first pass briefly misread those
  as line anchors; the real line is the hex after the file string.

### 10e. TESObjectLAND overrides (primary vftable `0x309a5c8`)

LAND's vftable at RVA `0x309a5c8` (COL `0x35cbfb8`,
`.?AVTESObjectLAND@@`). Diff against `TESForm` base vftable
(`0x300da88`) — slots where the function target differs are LAND overrides:

| slot | TESForm base | LAND override | name | evidence |
|---|---|---|---|---|
| 23 | `FUN_140004c28` | `thunk_FUN_140cf09d0` | `ClearItem`-family | cell-data teardown |
| **24** | `thunk_FUN_140720ac0` | `thunk_FUN_140cefdb0` | **`LoadRecord`** | 5× subrecord read via `cmp eax, 4CC` chain |
| 27 | `thunk_FUN_140720ad0` | `thunk_FUN_140d01900` | **`SaveRecord`** | OBTE→OBLT subrecord sequence + TESForm base chain |

#### 10e.1 LoadRecord (`FUN_140cefdb0`)

The decompiler flattens the subrecord dispatch into a tight `if/else` chain.
Five subrecord 4CCs are resolved inline via `cmp eax, imm32` (RVA offsets
from the function start):

| func offset | 4CC | hex value | description |
|---|---:|---|---|
| +0x41fb | `DATA` | 0x41544144 | land flags/header (u32 at cell+0x53 per `tesobjectcell.cpp:0x555` assert) |
| +0x41ea | `VHGT` | 0x54484756 | vertex height field (33×33 or similar per-cell grid) |
| +0x41f6 | `MPCD` | 0x4443504D | material/paint color data (Fallout 4 specific) |
| +0x4201 | `VNML` | 0x4C4D4E56 | vertex normals |
| +0x420c | `VCLR` | 0x524C4356 | vertex colors |

The read pattern follows the same `FUN_14087e870(file, dst, size)` dispatch
used by every LoadRecord (§10a/§10b). The tight cluster of 5 `cmp` insns
(+0x41ea–+0x420c, within 34 bytes) is the core vertex-data I/O path.

**Texture layer subrecords** (`BTXT`/`ATXT`/`VTXT`) are **not** in this
function. They are dispatched from a separate texture-layer reader at RVA
`0x457100` (the only `cmp eax, 'BTXT'` in the entire binary), called from the
LAND subrecord dispatch chain outside the main LoadRecord body. This is a
separate handling path — unlike CELL/WRLD whose LoadRecord handles everything
inline, LAND splits the core vertex data (LoadRecord) from the texture layer
stack (delegated helper).

#### 10e.2 SaveRecord (`FUN_140d01900`)

Subrecord writes via `FUN_1408c7710` (the shared `tesform.cpp` subrecord
writer, §10d) scattered across the function body:

| func offset | 4CC | hex value | description |
|---|---:|---|---|
| +0xde29 | `OBTE` | 0x4554424F | object terrain edge links |
| +0xde4c | `OBND` | 0x444E424F | object bounds (shared with TESForm base) |
| +0xdef6 | `OBML` | 0x4C4D424F | object material layers |
| +0xdf01 | `GNAM` | 0x4D414E47 | grass/ground name references |
| +0xde18 | `HNAM` | 0x444E4148 | height-related data |
| +0xde3a | `VMAD` | 0x44414D56 | VMAD data block (to TESForm base SaveRecord) |
| +0xde45 | `EDID` | 0x44494445 | editor ID (to TESForm base) |
| +0xdfd3 | `PTRN` | (TESForm base) | navigation mesh link |
| +0xe075 | `STCP` | (TESForm base) | sound/trap/collision |
| +0xe07c | `OBLT` | 0x544C424F | object light template |

SaveRecord delegates most writes to the TESForm base chain. The
LAND-specific writes (OBTE, OBML, GNAM, HNAM, OBLT) are the land-texture,
grass, and terrain-edge metadata. Vertex data (VHGT/VNML/VCLR/MPCD) is
written via the texture-layer save chain (same helper that handles BTXT/ATXT),
not inline in SaveRecord.

**The complete LAND I/O contract:**
- **LoadRecord** handles the 5 core vertex subrecords (DATA, VHGT, MPCD, VNML, VCLR)
- **Separate texture handler** (`0x457100`) handles layer subrecords (BTXT/ATXT/VTXT)
- **SaveRecord** handles terrain metadata (OBTE/OBML/GNAM/HNAM/OBLT) + base form subrecords
- **Vertex data save** is dispatched through the texture-layer save chain

### 10f. Complete world-class I/O table

| class | vftable RVA | LoadRecord RVA | SaveRecord RVA | subrecord count |
|---|---|---|---|---|
| `TESObjectCELL` | `0x309c780` | `FUN_140d146d0` (slot 24) | `FUN_140d44ba0` (slot 27) | ~40 load / 9 save |
| `TESWorldSpace` | `0x30aa510` | `FUN_140e4a750` (slot 24) | `FUN_140e54420` (slot 27) | ~26 load / 23 save |
| `TESObjectLAND` | `0x309a5c8` | `FUN_140cefdb0` (slot 24) | `FUN_140d01900` (slot 27) | 5 load + texture / 10 save |
| `TESObjectREFR` | `0x30a1990` | `FUN_140d65f60` (slot 24) | `FUN_140da7820` (slot 27) | ~160 load / 4 save + subclass chain |

### 10g. TESForm save header — the 0x18-byte wire block

The shared save pipeline is driven by three functions in `tesform.cpp`
(all verified by Ghidra decompile against
`code\shared\formcomponents\tesform.cpp` assert anchors):

| function | RVA | anchor | role |
|---|---|---|---|
| `FUN_1408c7a70` | `0x8c7a70` | `tesform.cpp:0x9a4` | **form-header writer** — emits the 0x18-byte record header + EDID + subclass chains |
| `FUN_1408c7bf0` | `0x8c7bf0` | `tesform.cpp:0x9e0` | **size-patch / close** — backfills the data-size field |
| `FUN_1408c7710` | `0x8c7710` | (write primitive) | **subrecord writer** — appends `[4CC:4][size:2][data:size]` to the output buffer |

#### 10g.1 Form-header writer (`FUN_1408c7a70`)

```c
void FUN_1408c7a70(TESForm *form) {
    // Guard: skip if form flags bit 14 (0x4000) is set
    if ((form->flags >> 14) & 1) return;

    // Initialize write cursor and allocate output buffer
    DAT_143d4a46c = 0x18;                          // cursor = header size
    buf = thunk_FUN_141ff6d30(heap, 0x18, 0);      // alloc 24 bytes
    DAT_143d4a470 = buf;                           // global output-buffer ptr

    // Write form flags (masked for non-TES4 records)
    buf[2] = form->flags;                          // +0x08
    if (form->formType != 0x01)                    // if not TES4
        buf[2] = form->flags & 0xfe9baff4;         // mask off bits 0/1/3/10/11/16-19/24/31

    // Write record type 4CC
    *buf = DAT_143b9a550[form->formType * 6];      // +0x00: formEnumString lookup

    // Write formID and revision
    buf[3] = form->formID;                         // +0x0C: low 32 bits
    buf[1] = 0;                                    // +0x04: data-size (zero; patched later)
    buf[5] = thunk_FUN_1408808c0();                // +0x14: form version (u16)
    buf[6] = (u16)form->field_0x24;                // +0x16: byte from +0x24 as u16
    buf[4] = (u32)form->field_0x20;                // +0x10: revision counter

    // Write EDID subrecord if non-empty
    edid = FUN_140071152(&form->edid);             // BSString at +0x18
    if (edid && *edid)
        FUN_1408c7710('EDID', edid, strlen(edid)+1);

    // Pre-save hook
    thunk_FUN_1408bef50(form);

    // Subclass save chain: slot 54 (vt+0x1b0)
    (*(form->vftable + 0x1b0))(form);              // e.g. VMAD, OBND, OBLT writes

    // PTRN-family save chain: slot 119 (vt+0x3b8)
    ptrn_obj = (*(form->vftable + 0x3b8))(form);   // WRLD returns nav-mesh/region data
    if (ptrn_obj)
        thunk_FUN_14036d7c0(ptrn_obj, 'PTRN');     // write PTRN subrecord array

    // STCP-family save chain: slot 120 (vt+0x3c0)
    stcp_obj = (*(form->vftable + 0x3c0))(form);   // WRLD returns sound/trap data
    if (stcp_obj)
        thunk_FUN_1403706a0(stcp_obj, 'STCP');     // write STCP subrecord array
}
```

**The 0x18-byte header wire format** (little-endian, same as `.esm`/`.esp`
record header):

| offset | size | field | source |
|---|---:|---|---|
| +0x00 | 4 | **record type 4CC** | `DAT_143b9a550[formTypeByte * 6]` (the formEnumString table — maps 0x01→"TES4", 0x3F→"CELL", 0x4A→"WRLD", etc.) |
| +0x04 | 4 | **data size** | 0 initially; backfilled by `FUN_1408c7bf0` to `DAT_143d4a46c - 0x18` (total bytes written minus header) |
| +0x08 | 4 | **form flags** | `form+0x10` masked `& 0xfe9baff4` if type ≠ 0x01 (TES4 keeps flags unmasked). Mask clears: bit 0 (ESM), bit 1 (deleted?), bit 3, bits 10-11, bits 16-19, bits 24, 31 |
| +0x0C | 4 | **formID** (low 32) | `form+0x14` (the u32 formID; master file prefix is implicit from the file slot) |
| +0x10 | 4 | **revision counter** | `form+0x20` (u32 — incremented on each save? read from `form[4]` = byte offset +0x20) |
| +0x14 | 2 | **form version** | `thunk_FUN_1408808c0()` — likely a global version from the base game/editor |
| +0x16 | 2 | **form-type byte** | `*(u8*)(form+0x24)` zero-extended to u16 (the same byte checked by `GetFormType()` — 0x3F=CELL, 0x4A=WRLD, etc.) |

**Flags mask breakdown:** `0xfe9baff4` clears these bits from the TES4 flags
word: 0x0001 (ESM), 0x0002 (deleted/reserved), 0x0008, 0x0C00 (compressed +
ignored), 0x1E0000 (five bits — localized/risky flags), 0x01000000, and
0x80000000. The full `esm`/`esp` flag word has 32 bits but only ~16 are
meaningful; the mask strips editor-internal and runtime-only flags.

#### 10g.2 Size-patch (`FUN_1408c7bf0`)

```c
void FUN_1408c7bf0(TESForm *form) {
    // tesform.cpp:0x9e0
    if (((form->flags >> 14) & 1) == 0) {           // skip if bit 14 set
        *(u32*)(DAT_143d4a470 + 4) = DAT_143d4a46c - 0x18;
        // data-size = total_bytes_written - header_size
    }
}
```

`DAT_143d4a46c` is the global write cursor — incremented by every
`FUN_1408c7710` call (subrecord 4CC + u16 size + payload). At `SaveRecord`
end, `DAT_143d4a46c - 0x18` is the total subrecord payload size, written
back to the header at `DAT_143d4a470 + 4`. If form flags bit 14 (0x4000)
is set, the patch is skipped — the record has a custom size path.

#### 10g.3 Subrecord writer (`FUN_1408c7710`)

Appends to the growable output buffer at `DAT_143d4a470`:

```
[DAT_143d4a470 + DAT_143d4a46c] = 4CC (4 bytes, LE)
[DAT_143d4a470 + DAT_143d4a46c + 4] = data_size (2 bytes, LE u16)
[DAT_143d4a470 + DAT_143d4a46c + 6] = data (data_size bytes)
DAT_143d4a46c += 6 + data_size
```

Same wire format as the subrecords inside a `.esm`/`.esp` record body
(4CC type + u16 size + payload). The buffer is reallocated as needed
(the function starts by calling a `realloc`-style helper).

#### 10g.4 Subclass save dispatch (vt+0x1b0, +0x3b8, +0x3c0)

Three vftable slots form the subclass save chain, called from the
form-header writer *before* the class-specific SaveRecord:

| byte offset | slot | base TESForm | CELL override? | WRLD override? | role |
|---|---|---|---|---|---|
| **+0x1b0** | 54 | `thunk_FUN_1408bee00` | no (inherits base) | **yes** → `thunk 0x1f375` | **Subrecord save virtual** — writes class-generic subrecords. Base writes VMAD/OBND/OBLT; WRLD overrides to also write CNAM/NAM*/WCTR/etc. |
| **+0x3b8** | 119 | string data (returns 0) | string data (returns 0) | **yes** → `thunk 0x7389e` | **PTRN-family writer** — returns a `BSTArray`-like navigation-mesh or region object. If non-null, `thunk_FUN_14036d7c0(obj, 'PTRN')` writes a PTRN subrecord array. CELL/REFR return 0 (no PTRN for them). |
| **+0x3c0** | 120 | string data (returns 0) | string data (returns 0) | **yes** → `thunk 0x9bef7` | **STCP-family writer** — returns a sound/trap/collision data object. If non-null, `thunk_FUN_1403706a0(obj, 'STCP')` writes an STCP subrecord array. CELL/REFR return 0. |

This explains the annotation in §10b: "`CLSZ`, `OFST`, and `RNAM` are
also saved — they go through the `NAM*`-array and map-data iterators
dispatched from `vl+0x3b8`/`vl+0x3c0`, not inline in the SaveRecord body."
The WRLD vftable extends to at least 125 slots — much larger than CELL's
79 — because worldspaces have PTRN/STCP subobject chains that other record
types don't.

#### 10g.5 Full save call-order

For all record types, the SaveRecord call order is:

1. **`FUN_1408c7a70(form)`** — write 0x18-byte header + EDID
2. → **`vt+0x1b0`** — base subrecord virtual (VMAD, OBND, OBLT; WRLD adds CNAM/NAM*/WCTR/…)
3. → **`vt+0x3b8`** → PTRN (WRLD only; returns 0 for CELL/REFR/LAND)
4. → **`vt+0x3c0`** → STCP (WRLD only; returns 0 for CELL/REFR/LAND)
5. **`class::SaveRecord(form)`** — class-specific subrecords (the slot 27 override; e.g. CELL's XCLC/DATA/VISI/RVIS/PCMB, WRLD's DATA/DNAM/MHDT/LTMP/NAM*)
6. **`FUN_1408c7bf0(form)`** — backfill data-size

Steps 1–4 are shared by every form type (the `tesform.cpp:0x9a4` code).
Step 5 is the class-specific dispatch (the virtual we call LoadRecord/
SaveRecord). Step 6 patches the header.

**Consequence:** the `data-size` field in the header counts *all*
subrecord payload bytes from steps 2–5. The form-header writer's
subrecords and the class SaveRecord's subrecords are concatenated into
the same output buffer at `DAT_143d4a470`, and the final size-patch
subtracts 0x18 (the header) from the total cursor position.

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

LAND: `DATA` (land flags/u32), `VHGT` (vertex height field, LoadRecord inline),
`MPCD` (material/paint color data — FO4 only), `VNML` (vertex normals, LoadRecord inline),
`VCLR` (vertex colors, LoadRecord inline), `BTXT`/`ATXT`/`VTXT` (texture layers — dispatched
via separate handler at `0x457100`, not main LoadRecord), `OBTE`/`OBML`/`GNAM`/`HNAM`/`OBLT`
(terrain metadata, SaveRecord inline), `OBND`/`VMAD`/`EDID`/`PTRN`/`STCP` (TESForm base,
via SaveRecord delegate chain).

(All verified present as ASCII strings in the binary; the subrecord compare
constants themselves are inline immediates in `LoadRecord`, not string xrefs.)

## 12. Open questions

- Whether any CK feature writes WRLD/CELL records with non-standard subrecords
  (editor-only `EditorWarnings.txt` paths noted in §10 of doc 03).
- The full ~160-subrecord REFR LoadRecord dispatch table (currently partially
  decoded from the decompile tree; a complete table extraction from the
  binary if/else dispatch is pending).
- The LAND texture-layer reader at `0x457100` — full subrecord set and caller chain.
- The LAND vertex-data *save* path (which function writes VHGT/VNML/VCLR/MPCD
  to the output buffer, since these are not in SaveRecord).
- The interior XCLL 0x88-byte block format — the `thunk_FUN_140911970` init
  at `+0x28` writes 5 floats; the remaining fields between +0x08 and +0x87
  should be mapped against the XCLL subrecord byte layout.

## Artifacts

- `ghidra-dump/cell-worldspace-vtables/{TESForm,TESObjectCELL,TESWorldSpace,TESObjectLAND,TESObjectREFR}.vftable.txt`
- `ghidra-dump/FindVftable.java` (RTTI → vftable resolver),
  `FindStringXrefs.java`, `FindCallers.java` (also mirrored to `scripts/`)
- `ghidra-dump/decompile-GRUP-reader-FUN_140676cd0.txt`
- `ghidra-dump/decompile-CELL-overrides.txt` / `decompile-WorldSpace-overrides.txt` (all slot-23+ override bodies with `__FILE__` anchors)
- `ghidra-dump/decompile-save-helpers.txt` (`FUN_1408c7a70` form-header writer, `FUN_1408c7bf0` subrecord close, `FUN_140d18490` cell-data getter)
- `ghidra-dump/decompile-TESDataHandler-CreateForm-FUN_140865b10.txt`
- `ghidra-dump/decompile-TESObjectCELL-LoadRecord-candidate.txt`
- `ghidra-dump/cell-setters.txt` (10 functions: EDID setter FUN_1408bfb80,
  interior-flag setter FUN_140d1d2e0, cell-data allocator FUN_140d18510,
  coordinate setter FUN_140d197c0 + the getter at `0xd17ab0` and
  alt-getter at `0xd17a90`)
- `ghidra-dump/decompile-REFRFactory-slots.txt`,
  `decompile-REFRSerializer-slots.txt`, `decompile-REFR-serializer-thunks.txt`,
  `decompile-serializer-virtuals.txt`, `decompile-factory-compare.txt`,
  `decompile-json-registry.txt`, `callers-{REFRFactory-ctor,REFRFactory-Create,serializer-read,serializer-reg,json-registry}.txt`
- `ghidra-dump/decompile-REFR-LoadSaveRecord.txt` (REFR LoadRecord FUN_140d65f60 + SaveRecord FUN_140da7820; 490 lines, 160-subrecord dispatch tree)
  (raw decompiles; local only)
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
- `ghidra-dump/cell-worldspace-vtables/TESObjectLAND.vftable.txt` (LAND vftable
  `0x309a5c8` — 79 slots, LoadRecord at slot 24, SaveRecord at slot 27)
- `ghidra-dump/decompile-WRLD-factory-slots.txt` (WRLD factory vftable at
  `0x30aa450` — Create, dtor, GetFormTypeName, GetFormType),
  `decompile-WRLD-ctor-0xe49960.txt` (full TESWorldSpace constructor),
  `decompile-WRLD-Create-0xe49880.txt` (factory Create — alloc 0x2B8 + ctor) —
  raw decompiles; local only
