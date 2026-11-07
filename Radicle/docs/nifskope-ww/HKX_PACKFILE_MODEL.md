> Unverified in this repository: reference copy from NifSkope Wild Wasteland Edition. See README.md.

# The Havok packfile object model (src/hkxfile, src/hkxmodel, the class database)

**Status: MEASURED and GATED, 2026-09-10, lane HKXEDIT1.** This page is the
contract for the generic `.hkx` layer: every FO4 packfile the game's own class
registry describes, read into typed objects and written back byte-identically.
`docs/HKX_ANIMATION_FORMAT.md` (HKX1) settles the container's header, section
table and fixup tables and the clip's spline blob; `docs/HKX_WRITE_FORMAT.md`
(HKX5) the interleaved clip a writer emits. Neither is repeated; this page adds
what a GENERIC model needs: where the class layouts come from, how a field of
each type is read, and the layout rules of a written file, each traced to the
file that taught it.

Authorities: **EXE** (the hkClass reflection in the user's own older game build
`Fallout4.exe`, checked in Todd's treat), **HKXPACK** (0.1.6-beta's `classxml/`, the
second oracle), **CENSUS** (every `.hkx` in `Fallout4 - Animations.ba2`:
15,320 files), **FILE** (a byte offset read in a named shipped file).

Provenance stamps (step 1 of `ww-contract-provenance`; re-derived at the end):

| file | sha256 (16) | bytes | lines |
|---|---|---|---|
| `src/hkxfile.h` | `77bb498a9649a858` | 10,433 | 244 |
| `src/hkxfile.cpp` | `82f22b12266063bb` | 54,306 | 1,522 |
| `src/hkxmodel.h` | `79801bd14b1a2183` | 5,570 | 117 |
| `src/hkxmodel.cpp` | `d389bb0c8cc3f161` | 26,715 | 788 |
| `tests/spells/hkxfile_oracle.py` | `7efba5383ca37874` | 40,498 | 915 |

---

## 1. The class database, and why it is the exe's and not HKXPACK's

A packfile stores, per object, only a CLASS NAME (the `__classnames__`
section) and the object's bytes. What the bytes mean -- which member sits at
which offset with which type -- comes from the class definition, and the only
class definitions that matter are the ones the game LOADS WITH. An older FO4 build
links **908** classes into its built-in class registry [EXE]. The class
database is **not shipped in this repo**: it is generated on the user's machine
from their own installed `Fallout4.exe` by
`python tools/hkclassdb_extract.py --exe <path to Fallout4.exe>`, which writes
`res/hkclasses_fo4.json` (format `ww-hkclassdb-1`):

* An `hkClass` object is 0x50 bytes (`name*` +0, `parent*` +8, `objectSize`
  +0x10, `numImplementedInterfaces` +0x14, `declaredEnums*` +0x18, `numEnums`
  +0x20, `declaredMembers*` +0x28, `numMembers` +0x30, `defaults*` +0x38,
  `attributes*` +0x40, `flags` +0x48, `describedVersion` +0x4c) but it is
  **built at run time**: the on-disk `.data` page has no bytes for it. Each
  is filled in by start-up code that passes the class constructor its
  thirteen arguments, stored in that order; the extractor EMULATES that
  start-up code (one stack-addressing shape hid 30 classes' enums until the
  emulation tracked it).
  952 initializers, 950 constructor calls, 943 distinct names: the four Havok
  meta classes are built 2-4 times and the REGISTERED object is the class
  (its signature is the one shipped files carry), the others `variants`.
* Members: `XClass_Members` / `X::Members` are const 0x28-byte
  `hkClassMember` records (`name*` +0, `class*` +8, `enum*` +0x10, `type` u8
  +0x18, `subtype` u8 +0x19, `cArraySize` u16 +0x1a, `flags` u16 +0x1c,
  `offset` u16 +0x1e). The `enum*` slot is zero on disk and patched by a
  second piece of start-up code (178 enum-pointer patches), emulated too. Enum objects are const (`.rdata`,
  0x28 bytes: `name*`, `items*`, `numItems` +0x10, `attributes*`, `flags`;
  items 16 bytes `{int value; pad; const char* name}`).
* **The signature is computed** the way Havok computes a class signature:
  `~crc32` over -- numImplementedInterfaces (int32), each
  declared enum (name bytes, then per item name bytes + int32 value, then
  the item count), each member (the member's class chain's signature bytes
  unless the member is a pointer; its enum's signature bytes; the name bytes;
  type u16; subtype u16 -- for ENUM/FLAGS the subtype's width `<< 3` is XORed
  into the flags and 0 is written; a SERIALIZE_IGNORED member is written as
  type ZERO with the type as subtype and an extra u16 for a non-zero
  subtype; cArraySize u16; flags u16), the member count (int32); then the
  parent's signature bytes, and so on up. Standard CRC-32 (init ~0, final ~).
  It reproduces HKXPACK's stored signature for **908 of 908** classes both
  know [HKXPACK] -- every field entering the hash was read right.

Cross-check (`scratchpad/hkxedit1_20260910/classdb_crosscheck.tsv`): 906
classes agree in every field; 2 differ (`hclVolumeConstraintMx{Apply,Frame}SingleData`:
HKXPACK lists a 4th member `padding` the exe does not declare, with the
signature still agreeing -- the exe wins); 35 exe-only (`hkx*` scene classes);
0 HKXPACK-only. Self-check (offset + size inside the class, past the parent,
parents resolve, no cycles): 0 problems once the empty-base-class rule was
read off the header (layout byte 3 `emptyBaseClassOpt = 1`).

The JSON carries, per class: name, registryIndex, parent, objectSize,
version, flags, signature (computed) and `signatureHkxpack`, enums with
items, members with name / offset / type / subtype / cArraySize / flags /
class / enum / `enumClass` (the class DECLARING the enum -- a member may use
an ancestor's) and, for an enum no registered class declares, its items
inline. `Hkx::ClassDb` (`src/hkxfile.cpp`) loads it, links parents and member
classes, resolves enums, and flattens each class's member list parent-first.
It is found beside the exe (the .pro copies it next to `nif.xml`), at
`WW_HKCLASSDB`, or under `res/`.

## 2. What is generic and what is special-cased

`Hkx::File::read` walks each virtual fixup's object from its class's
flattened member list and dispatches on `hkClassMember::Type` only:

| type(s) | bytes in the object | Value kind | notes |
|---|---|---|---|
| BOOL CHAR INT8 UINT8 INT16 UINT16 INT32 UINT32 INT64 UINT64 ULONG HALF | the width, x cArraySize | Scalar (`ints`) | |
| REAL | 4 x cArraySize | Scalar, the exact 32-bit pattern in `ints` | a NaN payload survives |
| ENUM FLAGS | the SUBTYPE's width | Scalar, `storage` = subtype | INT8/UINT8 81+60 uses, UINT32 20, ... |
| VECTOR4 QUATERNION (16) MATRIX3 ROTATION QSTRANSFORM (48) MATRIX4 TRANSFORM (64) | x cArraySize | Raw (bytes) | quaternion order in the file is x y z w |
| CSTRING STRINGPTR | 8, a LOCAL fixup to `name\0` | Str; `isNull` when no fixup | null and "" are different files |
| POINTER | 8, a GLOBAL fixup to an object start | Ptr (`object` index) | a pointer with a local fixup is refused by name |
| STRUCT | the class's objectSize, x cArraySize | Struct (recursive) / CArray | |
| ARRAY | 16: `T* data` (local fixup) `int size` `uint capacityAndFlags` | Array; plain subtypes packed in `bytes`, STRUCT / POINTER / string elements in `elems` | a null pointer with size 0 is legal and kept apart from an empty payload; `capacity` kept raw |
| RELARRAY | 4: `u16 size`, `u16 offset` from the member's own address | RelArray; payload INSIDE the object's chunk | skeleton.hkx's ragdoll capsule shapes |
| SERIALIZE_IGNORED (flag 0x400), any type | its width | Ignored (bytes kept) | vtable slot, refcount, run-time caches |
| VARIANT with a class fixup, HOMOGENEOUSARRAY, SIMPLEARRAY in data, hkRelArray of pointers / strings / inside an array element, cArraySize > 1 of hkArray | -- | refused by name | none occur in the census |

Every Struct keeps its HOLE bytes (the bytes no serialised member covers) and
writes them back verbatim; every serialised member is re-encoded from its
decoded value, so a round trip proves the decoding, not a copy. The only
per-class knowledge anywhere is in `HkxModel`'s presentation: an
`hkArray<hkUint8>` is shown as one byte-blob row (the spline `data`), not
68,000 rows.

## 3. The layout of a written file (the rules the census found)

`Hkx::File::write` (namespace-local `Writer` in `src/hkxfile.cpp`):

1. **Objects depth-first from the root.** An object's body is written, then
   its EXTRAS in member order (parents' members first), then the objects it
   points to, in encounter order, each recursively; then the remaining
   objects in file order. jog.hkx: root, hkaAnimationContainer, the
   animation, its extracted motion, the binding, hkMemoryResourceContainer
   [FILE jog.hkx virtual fixups 0x0 0xb0 0x130 0x2660 0x2830 0x2960].
2. **Alignment and padding, per chunk kind** (all measured, each rule cost
   one mismatch class in the census before it was read):
   * an OBJECT body is aligned to 16 before and padded to 16 after
     (hkaSkeleton is 0x88 bytes and its `name` string starts 16-aligned)
     [FILE skeleton.hkx];
   * hkRelArray payloads sit right after the body, each 16-aligned, in
     member order, and belong to the object's chunk (hknpCapsuleShape: body
     112, `vertices` at +0x70, `planes` +0xf0, `faces` +0x170, `indices`
     +0x190, chunk 432) [FILE skeleton.hkx obj #6];
   * an ARRAY payload is aligned to 16 before; after it: a payload of
     STRUCTS is NOT padded (jog: `annotationTracks` ends at 0xac8 and track
     0's "" trackName is AT 0xac8) [FILE jog.hkx]; a payload of POINTERS or
     plain values IS padded to 16 (skeleton: `referencedObjects` ends 0x4af8,
     the name string starts 0x4b00) [FILE skeleton.hkx obj #5]; a payload of
     STRING POINTERS is not padded, its strings are packed each at the next
     EVEN offset, and the run is padded to 16 once after the last one
     (AlienRootBehavior: `eventNames[0]` 9 bytes at 27736, `[1]` at 27746,
     `[2]` at 27756) [FILE Meshes/Actors/Alien/Behaviors/AlienRootBehavior.hkx obj #96];
   * a DIRECT string member is not aligned before and padded to 16 after
     (jog: "FootLeft\0" at 0xb10, next at 0xb20) [FILE jog.hkx];
   * array and string payloads follow their owner's body in member order,
     and the extras of a struct array's elements follow the whole payload,
     element by element [FILE jog.hkx local fixups].
3. **Fixup tables**: local fixups `(src, dst)` in write order; global fixups
   `(src, 2, dst)` in FLUSH order -- a pointer inside an earlier member's
   array payload precedes a later direct pointer member (skeleton:
   hknpRagdollData's `bodyCinfos[i].shape` before its own `skeleton` at
   +0x88) [FILE skeleton.hkx global fixups 0x3d30.. before 0x29f8]; virtual
   fixups `(objectOffset, 0, classNameOffset)` in object order; each table
   padded to 16 with 0xFF; exports = imports = end.
4. **Class names**: the file's order kept, a class used for the first time
   appended with the database's signature; 0xFF-padded to 16. **Header**:
   the first `0x40 + predicateArraySizePlusPadding` bytes kept verbatim
   (userTag, version 11, layout, `hk_2014.1.0-r1`, the predicate array) with
   `contentsSectionOffset` and `contentsClassNameSectionOffset` recomputed;
   three 0x40-byte section headers.

## 4. The Blocks-tab model (src/hkxmodel)

`HkxModel` is a `BaseModel` (the base of NifModel and KfmModel), so the block
list, the block details tree and the NifDelegate editors work on it unchanged
-- the KfmModel idiom (`NifSkope::load`'s `.kfm` branch). Each object is a
top item named by its class (displayed "class [i]"); each serialised member
a child; arrays as array items; inline structs, C arrays, matrices and
hkQsTransform as compounds (Translation / Rotation / Scale); enums and flags
as `hkEnum <Name>@…` / `hkFlags <Name>@…` types registered with
`NifValue::registerEnumType` so the delegate offers the combo and the
check-box list (flag options are BIT INDICES, as NifCheckBoxList expects);
pointers as `tLink` values shown "i (class)"; strings as `tSizedString`;
`hkArray<hkUint8>` as a read-only byte blob. Edits go through
`BaseModel::setData`, wrapped in an undo command on `HkxModel::undoStack`.
Saving syncs every item back into the `Hkx::File` the file was read into
(the layout store: object order, class-name order, header, capacities,
holes) and writes it; an unmodified document therefore saves byte-identical.
Known limits: a non-null "" string cannot be made null from a cell; the
blob is not editable here (HKXEDIT2's layer is); a new element of a struct
array is zero-filled with identity transforms.

`HkxModel::animFile()` is how the animation workspace reads a clip from an
open document: it writes the document's bytes and hands them to
`hkxAnimLoadPackfile` -- the reader HkxPlayback uses on a disk file. There
is no second decoder and no clip type on the model.

## 5. Refusals

Every refusal names the field and the value: a wrong magic / version /
pointer width, a section count or order that is not the three-section
layout, a fixup table outside the payload, a class name offset that is not
a class name, a class the database lacks (`hclClothSetupContainer`, 42
shipped cloth-setup files, the one census refusal), a pointer whose target
is not an object start, non-zero pointer bytes without a fixup, an array
size with no payload or past the payload end, an unmodelled type, and a
local or global fixup no modelled member consumed. A Havok packfile has no
checksum: a flipped PAYLOAD byte is a different well-formed value and is not
a refusal (gate (e)'s stated limit).

## 6. The gates (tests/spells/hkxfile_gates.py, run 2: 109 checks, 0 failures)

| gate | what | result |
|---|---|---|
| (a) | byte-identical read->write, C++ `release/hkxfile_gate.exe census` AND the oracle, over the whole census; the seven HKX1 fixtures by both | **15,278 of 15,278 parsed files identical, 0 mismatched, 42 refused** (both readers, the same 42, the same reason); 105 distinct classes; 3.0 s C++ / 29.8 s oracle |
| (b) | edit->save->reload on a copy of jog.hkx: numFrames 23->24 (int), frameDuration (float), originalSkeletonName (string), blendHint NORMAL->ADDITIVE (enum), blockOffsets 1->3 (array length); both writers; cross-read; per-field diff of the two trees | the two writers produce the SAME bytes; exactly the five edits differ (`#2.numFrames`, `#2.frameDuration`, `#4.originalSkeletonName`, `#4.blendHint`, `#2.blockOffsets.{n,vals}`), every other field including struct holes identical; values read back; FLOOR: a nudged `duration` is the one field the diff reports |
| (c) | HKXPACK unpacks what we wrote | jog_edit (both writers) and jog: rc 0, the XML shows `numFrames 24`, `EditedRoot`, `ADDITIVE` |
| (d) | the class database | 908 registered; 0 self-check problems; 908/908 signatures equal HKXPACK's; 0 HKXPACK-only classes; the eleven animation classes' objectSize and member offsets equal HKXCLASS's / HKX1's measurements; the twelve shipped signatures equal the files'; every member fits its class (0 violations, an independent pass) |
| (e) | 20 corruptions of jog.hkx refused by name by BOTH readers | 20/20 (magic, version, pointer width, section count, contents section, predicate padding, section tag, data start, local / virtual table offsets, class-name separator, class name, a string fixup to non-zero bytes, a global fixup to a non-object, a virtual fixup to a non-name, a section index, an oversize pointer array, an unconsumed local fixup, an array size past the payload, non-zero bytes on a null array); plus the stated limit shown accepted |
| (f) | the standalone binary (Qt6Core only) | `release/hkxfile_gate.exe`, built by `scratchpad/hkxedit1_20260910/build_gate.sh`; runs (a) (b) (e) |
| (g) | WW_HKXMODEL_TEST (`src/hkxmodeltest.cpp`, `tests/spells/hkxmodel_test.sh`) | WRITTEN, syntax-checked, NOT RUN (hook-up not applied) |

## 7. Provenance

| file | sha256 (16) | bytes | lines | role |
|---|---|---|---|---|
| Todd's treat: `Fallout4.exe` (an older game build) | `886d67fc955be02d` | -- | -- | EXE; the build every claim above was checked on |
| `E:\Tools\Fallout 4\HKXPACK\hkxpack-core.jar` (`classxml/`, 908 files) | -- | -- | -- | HKXPACK |
| `Fallout4 - Animations.ba2` (15,320 `.hkx`) | -- | 359,787,743 | -- | CENSUS; `scratchpad/hkxedit1_20260910/census_manifest.tsv` has each file's size and sha256 |
| `res/hkclasses_fo4.json` | -- | -- | -- | generated locally from the user's own `Fallout4.exe` by `tools/hkclassdb_extract.py`; not shipped |
| `src/hkxfile.h` | `77bb498a9649a858` | 10,433 | 244 | this page's source |
| `src/hkxfile.cpp` | `82f22b12266063bb` | 54,306 | 1,522 | this page's source |
| `src/hkxmodel.h` | `79801bd14b1a2183` | 5,570 | 117 | this page's source |
| `src/hkxmodel.cpp` | `d389bb0c8cc3f161` | 26,715 | 788 | this page's source |
| `tools/hkclassdb_extract.py` | -- | -- | -- | run locally: `python tools/hkclassdb_extract.py --exe <path to Fallout4.exe>` |
| `tests/spells/hkxfile_oracle.py` | `7efba5383ca37874` | 40,498 | 915 | this page's source |

| claim | line | anchor |
|---|---|---|
| the hole bytes | `src/hkxfile.cpp` | `v.bytes = blob.mid( atOff, size );` |
| hkRelArray read | `src/hkxfile.cpp` | `const int rel = qFromLittleEndian<quint16>( at( p + 2 ) );` |
| the depth-first object walk | `src/hkxfile.cpp` | `const QVector<int> pointees = flush( rest );` |
| struct payload not padded (the Struct branch ends without align16; Pointer and plain end with it) | `src/hkxfile.cpp` | `refuse( QStringLiteral( "%1: array count %2 but %3 elements" )` |
| string-array strings at even offsets | `src/hkxfile.cpp` | `if ( data.size() % 2 ) data.append( '\0' );` |
| global fixups in flush order | `src/hkxfile.cpp` | `globFix.append( qMakePair( ex.src, ex.object ) );` |
| the model's undo wrapper | `src/hkxmodel.cpp` | `undoStack->push( new HkxSetValueCommand( this, index, old, value` |
| the animation view of a document | `src/hkxmodel.cpp` | `return hkxAnimLoadPackfile( toBytes(), getFilename() );` |
| the byte-blob presentation | `src/hkxmodel.cpp` | `// a byte blob (the spline data, cloth buffers): one item, not one per byte` |
