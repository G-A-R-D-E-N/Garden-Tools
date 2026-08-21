# NiStream SaveStream block dispatch map

This note labels the per-object calls in the Creation Kit's 64-bit SaveStream
core at `0x142499070` and turns the Elric/NiMain symbol inventory into a
writer map for the combined-mesh `_Physics` output family.

## 1. The three per-object passes

The core owns a pointer array at `stream + 0x320`, its object count at
`stream + 0x328`, and three pass cursors at `+0x3d8`, `+0x3dc`, and `+0x3e0`.
The relevant instructions are:

| pass | instructions | object vftable call | semantic role |
|---:|---|---|---|
| 1 | `0x1424991f0`–`0x14249924e` | `call [object->vftable + 0xd8](object, stream)` | `RegisterStreamables` |
| 2 | `0x142499270`–`0x14249929e` | `call [object->vftable + 0xe0](object, stream)` | `SaveBinary` |
| 3 | `0x1424992b0`–`0x1424992de` | `call [object->vftable + 0x110](object, stream)` | `LinkObject` |

Each loop skips a null object. The null path in pass 1 calls a stream helper
(`stream->vftable + 0x18`) to preserve the corresponding registration slot;
it does not invoke a nonexistent object writer. The three loops are separate,
so all registration completes before any object payload is emitted and all
link resolution begins after payload serialization.

After the three passes, the core walks the stream's post-processing callback
array (`stream + 0x360`, bounded by the global callback-table count). That is
not another per-object `SaveBinary` pass. It is the later post-link/global
cleanup stage and must not be confused with slot `0x110`.

The slot names are cross-build labels, not guesses from the offset values. The
2016 Elric/NiMain contract names the same virtual operations as
`RegisterStreamables(NiStream&)`, `SaveBinary(NiStream&)`, and
`LinkObject(NiStream&)`; the CK's 2021 x64 class layout places them at
`0xd8`, `0xe0`, and `0x110` respectively. `WriteCInfo`/`LoadCInfo` are
Havok/NIF class operations, but are not calls made by this three-pass driver.

## 2. Block-type → SaveBinary writer map

`SaveRTTI` records the dynamic RTTI name of each registered object. The name
selects the block type/index in the NIF header; the actual writer is always the
same dispatch in the second pass:

```text
block object -> *(object->vftable + 0xe0)(object, NiStream&)
```

The table below maps the relevant names to the writer symbol confirmed by the
Elric PDB/NiMain export inventory. The symbol names are the 2016 engine's
address-accurate reference contract; the CK's 64-bit slot-`0xe0` bodies are
private and are not assigned a false 32-bit address.

| NIF RTTI/block type | SaveBinary writer | registration/link methods | `_Physics` status |
|---|---|---|---|
| `NiNode` | `NiNode::SaveBinary(NiStream&)` | `NiNode::RegisterStreamables`, `NiNode::LinkObject` | scene/root base; confirmed catalog entry |
| `BSFadeNode` | inherited/engine-specific `BSFadeNode` override if present; otherwise the `NiNode` family writer | `BSFadeNode` RTTI is present | common geometry-side candidate, not proven in a recovered `_Physics` sample |
| `bhkPhysicsSystem` | `bhkPhysicsSystem::SaveBinary(NiStream&)` | same-named `RegisterStreamables`/`LinkObject` | physics-system container; confirmed writer catalog entry |
| `bhkNPCollisionObject` | `bhkNPCollisionObject::SaveBinary(NiStream&)` | same-named `RegisterStreamables`/`LinkObject` | FO4/new-Havok collision-object candidate |
| `bhkNiCollisionObject` | `bhkNiCollisionObject::SaveBinary(NiStream&)` | same-named `RegisterStreamables`/`LinkObject` | collision-object candidate |
| `bhkRigidBody` | `bhkRigidBody::SaveBinary(NiStream&)` | same-named `RegisterStreamables`/`LinkObject` | rigid-body candidate |
| `bhkRigidBodyT` | `bhkRigidBodyT::SaveBinary(NiStream&)` | inherited/typed rigid-body streaming family | migration/typed-body candidate |
| `bhkNiTriStripsShape` | `bhkNiTriStripsShape::SaveBinary(NiStream&)` | same-named `RegisterStreamables`/`LinkObject`; `WriteCInfo` is separate | triangle collision-shape candidate; PDB-confirmed complete class |
| `bhkPackedNiTriStripsShape` | `bhkPackedNiTriStripsShape::SaveBinary(NiStream&)` | same-named `RegisterStreamables`/`LinkObject` | packed triangle-shape candidate |
| `hkPackedNiTriStripsData` | `hkPackedNiTriStripsData::SaveBinary(NiStream&)` | same-named `RegisterStreamables`/`LinkObject` | packed shape data companion when packed geometry is used |
| `bhkShape` | `bhkShape::SaveBinary(NiStream&)` | same-named `RegisterStreamables`/`LinkObject` | base/fallback shape catalog entry |
| `bhkCompressedMeshShapeData` | `bhkCompressedMeshShapeData::SaveBinary(NiStream&)` | same-named `RegisterStreamables`/`LinkObject` | alternate collision-shape catalog entry |
| `bhkCompressedMeshShape` | `bhkCompressedMeshShape::SaveBinary(NiStream&)` | same-named `RegisterStreamables`/`LinkObject` | alternate collision-shape catalog entry |
| `bhkMoppBvTreeShape` | `bhkMoppBvTreeShape::SaveBinary(NiStream&)` | same-named `RegisterStreamables`/`LinkObject` | alternate collision-shape catalog entry |
| `bhkBvTreeShape` | `bhkBvTreeShape::SaveBinary(NiStream&)` | same-named `RegisterStreamables`/`LinkObject` | alternate collision-shape catalog entry |
| `bhkTransformShape` | `bhkTransformShape::SaveBinary(NiStream&)` | same-named `RegisterStreamables`/`LinkObject` | wrapper-shape catalog entry |
| `bhkConvexTransformShape` | `bhkConvexTransformShape::SaveBinary(NiStream&)` | same-named `RegisterStreamables`/`LinkObject` | wrapper-shape catalog entry |
| `bhkConvexTranslateShape` | `bhkConvexTranslateShape::SaveBinary(NiStream&)` | same-named `RegisterStreamables`/`LinkObject` | wrapper-shape catalog entry |
| `bhkListShape` | `bhkListShape::SaveBinary(NiStream&)` | same-named `RegisterStreamables`/`LinkObject` | compound-shape catalog entry |
| `bhkBoxShape`, `bhkCapsuleShape`, `bhkCylinderShape`, `bhkPlaneShape`, `bhkSphereShape`, `bhkTriangleShape`, `bhkConvexVerticesShape` | corresponding `Class::SaveBinary(NiStream&)` | corresponding registration/link family | primitive-shape catalog entries; not evidence that a combined cell emits all of them |

The PDB also contains save writers for actions, constraints, phantoms,
controllers, and ragdoll objects. They are valid NiStream block writers but
are not part of the combined static-cell physics map unless the generated
scene actually registers one of those objects. `BSCombinedTriShape` is an
important exclusion: the CK embeds the diagnostics `Trying to save a type
that can't be saved : BSCombinedTriShape` and `Trying to load a type that
can't be saved : BSCombinedTriShape`; it is not a valid SaveStream block
writer for this purpose.

## 3. What is actually confirmed for `_Physics`

The CK's combined-cell routine at `0x140d20d90` reports failure as
`Failed to generate combined cell physics shape...` and constructs the
new-Havok `hknpPhysicsSystemData` object while assembling the collision
system. Elric further confirms the bridge constructor
`bhkPhysicsSystem(hknpPhysicsSystemData&)`. These facts establish the
physics-system/data boundary, but `hknpPhysicsSystemData` is a Havok data
object, not automatically a serialized NiStream block name.

The safe minimal interpretation for a real `_Physics.NIF` parser is therefore:

1. Read the dynamic block-type table; never hard-code an order from the file
   suffix.
2. For every listed block, dispatch its payload contract using the RTTI name.
3. Expect the scene/root and collision-object/body/triangle-shape families
   above; accept packed-data and alternate-shape variants when their RTTI is
   present.
4. Treat `hknpPhysicsSystemData` as an internal construction/runtime object
   unless a real file's RTTI table proves that it was registered as a NIF
   block.

The exact per-file sequence and the 64-bit payload bytes remain sample-dependent
until a real `*_Physics.NIF` is recovered. This document is a confirmed
writer/dispatch map, not a claim that every row appears in every cell.

## 4. Wire-level consequence

The writer selected by slot `0xe0` emits only the object payload. The common
NiStream wrapper still supplies the fixed FO4 header and tables:

```text
Gamebryo File Format, Version 20.2.0.7\n
u32 0x14020007
u8  endian = 1
u32 userVersion = 12
u32 root count
BS header/version fields
block-type RTTI names and indices
object-size table
fixed strings
object groups
slot-0xe0 SaveBinary payloads
u32 link count and link IDs
```

The object-size table lets a reader skip an unsupported class, but a compatible
writer must still preserve the exact per-class payload and link references.

## Evidence

- `ghidra-dump/nistream-savecore-0x142498a30.asm`, `0x1424991f0`–`0x1424992de`:
  the three x64 dispatch loops;
- `18-ELRIC-SYMBOLS.md §4a–§4e`: NiStream contract and cross-build virtual names;
- `ghidra-dump/elric-pdb-symbols.txt`: `SaveBinary`, `RegisterStreamables`,
  and `LinkObject` inventories for the `bhk*`, `hkPackedNiTriStripsData`,
  `NiNode`, and `BS*` families;
- `ghidra-dump/strings-asc.txt`: combined-cell diagnostics, shape catalog,
  and `bhkPhysicsSystem`/`hknpPhysicsSystemData` strings;
- `decompile-PreCombinedWorld-0xd20d90.log`: combined physics construction and
  `hknpPhysicsSystemData` initialization;
- `decompile-SCOL-combined-mesh-writer.md`: `_OC`/`_Physics` path and common
  NiStream boundary.
