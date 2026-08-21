# Creation Kit NIF block-format reference

This is the complete **factory-backed** SaveBinary inventory recovered from the
64-bit Fallout 4 Creation Kit. It is intentionally narrower than the 4,438-name
RTTI dump: a type is in this reference only when the engine factory symbol
inventory contains `?CreateObject@Type@@SAPAVNiObject@@XZ` and the CK has an RTTI
vtable whose slot `+0xe0` points into `.text`.

## Scope and recovery method

Input image:

```text
Fallout 4 1946160/CreationKit.exe
image base: 0x140000000
```

The recovery tool is `tools/extract_ck_nif_savebinary.py`. In the available
artifacts, the CK's `RegisterLoader` registration surface is represented by the
engine's `CreateObject` factory entries plus the CK RTTI/vtable registrations;
the private loader table itself has no exported symbol. It performs these
steps:

1. Extract factory names from `ghidra-dump/nimain-exports.txt` and
   `ghidra-dump/elric-pdb-symbols.txt` (`CreateObject` is the loader/factory
   contract available in the Elric/NiMain evidence).
2. Reconstruct MSVC x64 RTTI Complete Object Locator → vtable relationships.
3. Read the per-object SaveStream slot at `vtable + 0xe0`.
4. Resolve hot-patch thunks and use `.pdata` to bound the CK body.
5. Disassemble each unique body with `objdump -d -M intel`.

Reproduction:

```bash
python3 tools/extract_ck_nif_savebinary.py \
  /path/to/CreationKit.exe \
  --factory-symbols ghidra-dump/nimain-exports.txt \
                   ghidra-dump/elric-pdb-symbols.txt

python3 tools/extract_ck_nif_savebinary.py \
  /path/to/CreationKit.exe \
  --factory-symbols ghidra-dump/nimain-exports.txt \
                   ghidra-dump/elric-pdb-symbols.txt --asm
```

The run against the installed 2021 CK reports **110 factory names, 109 mapped
RTTI/vtables**, and one factory with no matching CK vtable (`NiStencilProperty`).
The map is a loader/writer capability inventory, not a claim that every listed
class occurs in one `_OC` or `_Physics` file.

## NIF wire contract around each writer

The common NiStream writer emits the fixed header and tables before dispatching
the per-object slot:

```text
"Gamebryo File Format, Version 20.2.0.7\n"
u32 0x14020007
u8  endian = 1
u32 userVersion = 12
u32 rootCount
BS version tuple / version strings
u16 blockTypeCount
(blockTypeName: u32 byteLength + bytes) * blockTypeCount
u16 typeIndexAndStreamableBit * rootCount
u32 objectSize * rootCount
fixed-string pool
object payloads
u32 linkCount
u32 linkID * linkCount
```

For the CK's SaveStream core at `0x142499070`, the relevant call is:

```asm
mov  r8, [object]
call qword ptr [r8 + 0xe0]       ; object, NiStream
```

`SaveBinary` writes only the payload for that object. References are registered
in the earlier `+0xd8` pass and resolved by the later `+0x110` pass. The object
size table is back-filled by the common writer, so the payload body does not
write its own byte count.

The disassemblies below are therefore the exact CK body contract at the
instruction level. A class inheriting a body shares its parent's payload
implementation; a body ending in `c2 00 00` is the CK's empty/no-op base writer,
not a missing disassembly.

## Complete block-type → SaveBinary body map

Every mapped factory type is listed below. Types sharing a body are grouped so
the exact body is disassembled once without duplicating identical instruction
text.

| CK factory block type(s) | slot `+0xe0` body | `.pdata` end | body interpretation |
|---|---:|---:|---|
| `BSGeometrySegmentData`, `BoneData@BSSkin`, `NiAdditionalGeometryData`, `NiAlphaAccumulator`, `NiBackToFrontAccumulator`, `NiSkinData`, `NiSkinPartition`, `bhkCompressedMeshShapeData`, `bhkPhysicsSystem`, `bhkPoseArray`, `bhkRagdollSystem`, `bhkRagdollTemplateData` | `0x14248e430` | — | Empty/base writer: `ret 0` (`c2 00 00`), followed by alignment padding. These classes can be registered, but emit no additional bytes at this virtual slot. Their references, if any, are handled by registration/link phases or inherited data paths. |
| `BSClothExtraData`, `BSPositionData`, `NiBinaryExtraData`, `NiBooleanExtraData`, `NiColorExtraData`, `NiExtraData`, `NiFloatExtraData`, `NiFloatsExtraData`, `NiIntegerExtraData`, `NiIntegersExtraData`, `NiStringExtraData`, `NiStringsExtraData`, `NiSwitchStringExtraData`, `NiVectorExtraData` | `0x14248fd60` | — | Empty/base writer: `ret 0` (`c2 00 00`), followed by alignment padding. These classes can be registered, but emit no additional bytes at this virtual slot. |
| `NiCollisionObject`, `bhkCollisionObject`, `bhkPCollisionObject`, `bhkSPCollisionObject` | `0x141ada0f0` | `0x141ada141` | Calls the base collision-object writer, sets a flag in the attached collision object (`+0x108`), then registers/writes the referenced object through the stream helper. |
| `bhkAabbPhantom` | `0x141ac22c0` | `0x141ac2340` | Registers the referenced Havok/world object, conditionally sets a byte for older stream versions, stores the resulting link/reference, then tail-jumps to the shared base writer. |
| `bhkRigidBody`, `bhkRigidBodyT` | `0x141ac4d90` | `0x141ac4e63` | Base-body serialization plus Havok rigid-body/reference validation and version/type checks. The body continues beyond the displayed prologue into the rigid-body field path; the `.pdata` range is the authoritative extent. |
| `bhkBallAndSocketConstraint`, `bhkFixedConstraint`, `bhkGenericConstraint`, `bhkGroupConstraint`, `bhkHingeConstraint`, `bhkHingeLimitsConstraint`, `bhkLimitedHingeConstraint`, `bhkPointToPathConstraint`, `bhkPrismaticConstraint`, `bhkRagdollConstraint`, `bhkRagdollLimitsConstraint`, `bhkStiffSpringConstraint`, `bhkWheelConstraint` | `0x141adc900` | `0x141adc99e` | Resolves two referenced stream objects and writes their link IDs into the constraint payload; null references use the zero/empty path. |
| `bhkBoxShape`, `bhkCapsuleShape`, `bhkConvexVerticesShape`, `bhkCylinderShape`, `bhkPlaneShape`, `bhkSphereShape`, `bhkTriangleShape` | `0x141b3a840` | `0x141b3a86f` | Shared primitive-shape writer. Calls the shape base, checks the shape's data/reference at `this + 0x10`, and tail-dispatches through the referenced object's `+0x170` operation. |
| `bhkLiquidAction`, `bhkMotorAction`, `bhkMouseSpringAction`, `bhkOrientHingedBodyAction`, `bhkStickyContactAction` | `0x141b3f5a0` | `0x141b3f5f5` | Resolves the action's referenced stream object, stores its link/reference at the payload's `+8`, then invokes the common/base action writer. |
| `bhkCompressedMeshShape` | `0x141b47d40` | `0x141b47dd7` | Replaces/releases the shared referenced data object with the stream-resolved object, performs reference-count handling, then tail-jumps to the compressed-shape/base writer. |
| `bhkConvexTransformShape` | `0x141b4d6d0` | `0x141b4d72a` | Resolves one child shape reference, writes its stream pointer/link slot, and tail-jumps to the shape writer. |
| `bhkConvexTranslateShape` | `0x141b4eb20` | `0x141b4eb7a` | Same wrapper pattern as the transform shape: resolve child reference, store at `+8`, then invoke the shared shape writer. |
| `bhkNiTriStripsShape` | `0x141b53850` | `0x141b5387c` | Starts the triangle-shape payload path, resolves the stream data object, tests the returned status, and continues into the class-specific triangle data writer. This is the collision-geometry writer used by the classic Havok triangle shape family. |
| `bhkPackedNiTriStripsShape` | `0x141b563d0` | `0x141b56448` | Packed triangle shape reference replacement with intrusive reference-count updates, then tail-jumps to the shared packed-shape/base writer. |
| `bhkMoppBvTreeShape` | `0x141b5ca40` | `0x141b5caa3` | Checks the MOPP/Bv-tree child shape and its type bytes, calls the MOPP helper, clears a flag in the child payload, and tail-jumps to the base shape writer. |
| `bhkTransformShape` | `0x141b5e450` | `0x141b5e587` | Full wrapper-shape path: resolves child shape, applies the default transform/float fallback when absent, performs stream/version checks, and serializes the wrapper's transform/reference data. |
| `bhkListShape` | `0x141b61760` | `0x141b61947` | Allocates/updates the child-shape collection and iterates the list of referenced shapes; the body has the collection bounds and link-array work rather than a single-link wrapper. |
| `bhkBreakableConstraint` | `0x141b6f120` | `0x141b6f16c` | Constraint-specific field/reference writer. |
| `bhkMalleableConstraint` | `0x141b70430` | `0x141b7047c` | Constraint-specific field/reference writer. |
| `bhkBlendCollisionObject` | `0x141b7abd0` | `0x141b7ac23` | Collision-object blend-state/reference writer. |
| `bhkRagdollTemplate` | `0x141b7fcf0` | `0x141b7fd15` | Ragdoll-template payload writer. |
| `bhkBallSocketConstraintChain` | `0x141bf8b30` | `0x141bf8c22` | Chain constraint writer with two endpoint/reference slots. |
| `bhkAngularDashpotAction`, `bhkDashpotAction`, `bhkSpringAction` | `0x141bfead0` | `0x141bfeb4f` | Shared dashpot/action writer with action references and base action fields. |
| `bhkCachingShapePhantom`, `bhkSimpleShapePhantom` | `0x141c03be0` | `0x141c03c57` | Phantom-specific shape/reference writer. |
| `bhkNPCollisionObject` | `0x1420b4500` | `0x1420b462e` | Fallout 4/new-Havok collision-object writer. The body performs the FO4 collision reference path rather than the classic `bhkNiCollisionObject` path. |
| `bhkExtraData` | `0x14210ce00` | `0x14210cef0` | Extra-data writer with its own data/reference payload. |
| `NiAlphaProperty`, `NiFogProperty`, `NiShadeProperty` | `0x1424917d0` | `0x1424918ac` | Property payload writer family. The body calls the common property/base path and writes the property data/reference. |
| `NiBSPNode`, `NiBillboardNode`, `NiNode`, `NiSwitchNode` | `0x142492f90` | `0x142492f9a` | Node family tail path. The tiny body is a vtable/base forwarding writer; child-array serialization is performed by the common node/base machinery. |
| `bhkBlendController` | `0x14249e570` | `0x14249e5cf` | Controller/reference writer. |
| `BSDynamicLines`, `BSDynamicTriShape`, `BSTriShape` | `0x14249f980` | `0x14249fa8d` | Bethesda geometry writer family. The body calls stream helpers for the geometry/data references and then writes geometry-side fields. |
| `Instance@BSSkin` | `0x1424a6240` | `0x1424a631d` | Skin instance collection/reference writer. This is a factory-registered helper type, not normally a top-level combined-cell block. |
| `NiDefaultAVObjectPalette` | `0x1424aafb0` | `0x1424ab53b` | Palette collection writer; disassembly walks palette entries and validates pointer/range ownership. |
| `NiAmbientLight`, `NiDirectionalLight`, `NiPointLight`, `NiSpotLight` | `0x1424b7c20` | `0x1424b7c9c` | Light writer family: resolves the light's referenced object and invokes the light-specific virtual payload operation with the stream's version field. |
| `NiCamera` | `0x1424bcca0` | `0x1424bcd88` | Camera collection/reference writer with repeated link/reference-count handling. |
| `NiTriShapeData`, `NiTriStripsData` | `0x1424cd820` | `0x1424cd878` | Geometry-data reference writer: resolves and replaces the data object at `this + 0x60`, retaining/releasing the intrusive reference as needed. |
| `BSLODTriShape`, `BSSegmentedTriShape`, `NiTriShape`, `NiTriStrips` | `0x1424d1770` | `0x1424d1868` | Tri-shape/node geometry writer family. Resolves the data/skin/material references at `+0x128`, `+0x130`, and `+0x138`, then invokes the final class-specific virtual payload operation. |
| `NiSkinInstance` | `0x1424da3a0` | `0x1424da497` | Skin-instance writer: resolves the data, partitions, and bone references, allocates the pointer array, and writes the collection links. |
| `NiParticleMeshes`, `NiParticles` | `0x1424e75b0` | `0x1424e7629` | Particle collection writer with count/version bounds and referenced particle-data object. |
| `NiParticleMeshesData` | `0x1424e8130` | `0x1424e818e` | Particle-mesh data reference writer, replacing the data object at `this + 0x90`. |
| `NiParticlesData` | `0x1424e5290` | — | Particle-data body is a short tail/helper path; the owning particle writer supplies the collection references. |\n| `hkPackedNiTriStripsData` | `0x141b584e0` | — | Packed-triangle data factory entry; the slot is the empty/base writer in this CK build. |
| `Children@BSConnectPoint` | `0x1403305d0` | `0x1403305f0` | Connect-point child-array helper writer; not a normal top-level NIF block. |
| `Parents@BSConnectPoint` | `0x1403315e0` | `0x140331600` | Connect-point parent-array helper writer; not a normal top-level NIF block. |

### Factory entry without an RTTI/vtable match

`NiStencilProperty` is present in the factory symbol inventory but did not have a
matching CK RTTI Complete Object Locator/vtable found by the extractor. It is
therefore **unresolved**, not silently classified as non-writable. Its writer
must be recovered from the registration table or a second RTTI representation
before assigning a body address.

## Reading the disassembly as a format reference

The CK bodies fall into a few mechanically identifiable serialization patterns:

- **Empty/base writer:** `0x14248e430` is `ret 0`; no bytes are added by the
  slot-`0xe0` call.
- **Single reference:** a body calls the stream's object/string helper, reads
  the returned object pointer's `+0x18`, and stores that value into a class
  field. The common writer later emits the corresponding link ID.
- **Reference replacement:** bodies such as `0x141b47d40`, `0x141b563d0`, and
  `0x1424cd820` compare the old and new intrusive-reference pointers, increment
  the new object and release the old object, then continue to the inherited
  writer.
- **Reference array:** `0x1424da3a0` allocates `count * 8` bytes and fills the
  array in a loop; the common link phase emits one link ID per registered entry.
- **Tail-dispatch:** geometry and shape writers finish with an indirect virtual
  jump (`[vtable + 0x168]` or `[vtable + 0x170]`) to the class-specific payload
  operation. The slot-`0xe0` body is consequently a wrapper around the payload
  implementation, not necessarily the whole field writer in one contiguous
  function.

The disassembly proves control flow and field/reference offsets. It does **not**
by itself prove the semantic name or scalar width of every field; those require
matching class layouts or a real generated NIF. In particular, this reference
must not be used to assume that all 109 registered types appear in one
`_Physics.NIF`.

## Validator cross-checks

`tools/validate_nif.py` was run against real Fallout 4 NIFs already present in
the workstation modlist. No files were copied into the repository; the validator
reads them in place:

```text
Remove Grenade Bubble/Meshes/Effects/ExplosionGrenade01.nif
  version 0x14020007, BS 0x82, roots 216, block types 52
  fixed strings 70 (maximum 39), one object group, payload 15,725 bytes
  trailing link count 0

Remove Grenade Bubble/Meshes/Effects/ExplosionGrenadeCryo.nif
  version 0x14020007, BS 0x82, roots 618, block types 56
  fixed strings 120 (maximum 39), one object group, payload 71,679 bytes
  trailing link count 0

Remove Grenade Bubble/Meshes/Effects/ExplosionGrenadeFrag.nif
  version 0x14020007, BS 0x82, roots 590, block types 59
  fixed strings 142 (maximum 55), one object group, payload 74,500 bytes
  trailing link count 0
```

The validator also caught the old documentation's `31-byte` header-size claim:
the literal written by `PutS` is 39 bytes, so the version field starts at file
offset `0x27`, followed by endian at `0x2b`, user version at `0x2c`, root count
at `0x30`, and BS version at `0x34`.

## Evidence and limits

- `ghidra-dump/nistream-savecore-0x142498a30.asm`: `0x142499070` and the three
  per-object virtual passes;
- `ghidra-dump/nimain-exports.txt`: NiMain factory symbols;
- `ghidra-dump/elric-pdb-symbols.txt`: Elric factory and `SaveBinary`/streaming
  symbols;
- `ghidra-dump/rtti-classes.txt`: CK RTTI inventory;
- `tools/extract_ck_nif_savebinary.py`: reproducible PE/RTTI/vtable/body
  extraction;
- `ghidra-dump/nistream-block-save-map.md`: earlier `_Physics`-focused writer
  map and stream contract;
- `ghidra-dump/elric-pdb-symbols.txt` (local): cross-build NiStream method
  names and class layout;
- `decompile-SCOL-combined-mesh-writer.md`: SCOL/`_OC`/`_Physics` writer
  boundary.

The previous `_Physics` map remains the conservative sample-oriented guide.
This document is the exhaustive factory/vtable inventory and should be read
with the unresolved-entry and helper-type qualifications above.
