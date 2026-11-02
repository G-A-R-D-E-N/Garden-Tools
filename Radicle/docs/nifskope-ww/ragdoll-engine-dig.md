> Unverified in this repository: reference copy from NifSkope Wild Wasteland Edition. See README.md.

# What the engine does with a ragdoll

Findings from Todd's treat (an older game build), queried with the Todd's treat tooling (kept outside this repo).
The live game is 1.11.221; the behaviour described matches it closely.

Written after a rebuilt human ragdoll misbehaved in game while every offline
comparison of the file said it matched vanilla. The rule this exists to serve:
a byte diff answers "is our file the same", never "what does the engine do with it".

## Closed: nothing validates our data on load

The ragdoll data's consistency check does nothing in the release game.
There is no gate to fail, so "the engine rejected the ragdoll" is not a
hypothesis.

## Closed: the mass the engine reads is the mass we write

When the behaviour graph asks for a ragdoll body's original mass, the engine
finds the bone's body index in the ragdoll data, takes that body's
`hknpBodyCinfo` (stride 0x60 in the file), reads its motion index (`+0x0c`),
and returns the inverse mass stored at `+0x04` of that motion's entry in the
dynamic-inertia array (stride 0x70).

Exactly the fields we write, resolved *through* the motion index — so the index
being a permutation of vanilla's is harmless.

## Closed: the grab weight is a scene-graph SUM, and ours equals vanilla's

When the player grabs an object, the engine walks the object's scene graph and,
for every collision object in it:

  * skips it if the target object carries a "skip" flag;
  * else adds that collision object's mass.

The total is compared against the game setting `fGrabMaxWeightWalking`.

A `bhkNPCollisionObject`'s mass is **0** when:

  * the system has no physics world, or
  * the body is not the FIRST body attached to its motion — which is how Havok
    avoids counting a shared motion twice.

Otherwise it is the reciprocal of the motion's inverse mass, which the live
physics world stores as a **16-bit half-float**; a zero inverse mass gives 0.

Measured on the human skeleton, vanilla vs our rebuild: 18 bodies with a motion,
18 distinct motions, **sum 93.5000 kg on both**. The grab path is identical, so it
cannot be the cause of a corpse that lifts differently.

## Closed: the file is equivalent to vanilla, at every level I can compare

Done after the mass and trigger fixes, on the human skeleton, vanilla vs our
decompile/recompile. Four independent layers:

**1. The NIF outside the blobs.** 166 blocks, and with every Ref/Ptr resolved to
its target's type and name so renumbering compares equal: **0 blocks differ**. All
129 node world transforms identical. All 19 `bhkNPCollisionObject` carry flags 128
(ANIM_TARGETED), as vanilla.

**2. The packfile container.** Header and section table byte-identical (0x00..0x100).
Section stride is **64**: tag[19] + 0xff pad + 7 ints + 16 bytes padding, first
record at 0x40, data at 0x100. `__data__` local fixups identical (10, same order),
virtual fixups identical (38, same order). Global fixups: 89 each, **16 differ** --
all in one array, discussed below.

**3. Havok's own deserializer** (`FileConvert.exe -x`, the real hk_2014 reader).
4,529 scalar leaves, **137 differ**, and every one is inert:
  * 32 are `materialId` / `motionPropertiesId`, indices into arrays whose 18
    entries are **all identical to each other** (checked: 1 distinct signature);
  * 74 are `hkVector4` w lanes -- padding. One dumped in full to be sure: a
    unit-Z basis vector whose w vanilla left as 1.0;
  * 31 are capsule polytope `planes`, which compare **equal as a set** -- the core
    box is rolled 180 degrees about its own axis and its cross-section is square,
    so the solid is identical and each file is internally consistent (plane k is
    defined from that file's own e1/e2, and the face/index table is expressed in
    the same local frame).
Separately, **all 197 pointer fields resolve to the same object** in both.

**4. The differing global fixups are the SHAPE LIST, and only its order.**
The root's array descriptors, read from the local fixups:

    +0x10 -> 144    materials      18 x 0x50
    +0x20 -> 1584   motionProps    18 x 0x40
    +0x30 -> 2736   dyn_inertia    18 x 0x70
    +0x40 -> 4752   bodyCinfos     18 x 0x60
    +0x50 -> 6480   constraints    17 x 0x18
    +0x60 -> 6896   SHAPE LIST     18 pointers   <-- the 16 differing fixups
    +0x80 -> 7040   boneToBodyMap  18 x 4

Ours lists the capsules in ascending file order; vanilla's is a permutation. It
does not matter, and that is measured rather than argued: **every body resolves to
the identical capsule**, radius and length, body for body, all 18. `bodyCinfos[i]
.shape` points at the same object in both.

Note for later: `hknpBodyCinfo +0x12` is **`materialId`** by Havok's own
reflection, and this codebase writes the body's shape-list SLOT there. The two
coincide in everything we produce (our shape list is the identity, so slot ==
body index) and in vanilla (its materials are all identical), so nothing is wrong
today -- but they are not the same concept, and a system with per-body materials
would expose it.

## Not the cause either: mod conflict

No other mod in `E:\Projects\Fallout 4 Mods\mods` provides
`Actors/Character/CharacterAssets/skeleton.nif`, loose or in any of the 12 BA2s,
and the game's own Data folder has no loose copy. Our mod is the only source, so
it is not overriding somebody else's skeleton.

## Where that leaves it

Our rebuilt human skeleton is equivalent to vanilla's in the NIF, in the packfile
container, in every scalar Havok reads, in every pointer, and in the two engine
paths traced above. Two real defects WERE found and fixed on the way (primitive
mass properties, the trigger material), and both were found by comparing against
vanilla -- but neither was found by comparing the ragdoll packfile, which is where
the effort went.

If the symptoms persist on build 8b1b312, the cause is not in this file, and the
next move is an A/B in one session rather than more analysis.

## Both skeletons, and the complete taxonomy of what differs

The same comparison run on the brahmin (39 bodies) as on the human (18):

                              human        brahmin
    scalar leaves compared     4529          10178
    differing                   137            270
      array indices              32             76
      hkVector4 w lanes          63            137
      capsule plane order        42             44
      negated quaternions         0             13
      anything else               0              0
    pointer fields              197            449
      differing targets            0              0

Every class is inert, and each was checked rather than asserted:

  * **array indices** -- `materialId` and `motionPropertiesId` index arrays whose
    entries are all identical to one another (1 distinct signature across 18);
  * **w lanes** -- `hkVector4` padding. Dumped one in full: a unit-Z basis vector
    whose w vanilla left as 1.0;
  * **capsule planes** -- equal as a SET; the core box is rolled 180 degrees about
    its own axis and its cross-section is square, so the solid is unchanged;
  * **negated quaternions** -- all seven are complete negations, dot = -1.000000
    to six places, which is the same rotation. The components that appear not to
    differ are zeros, where a sign flip is invisible.

The shape-list pointer array (root +0x60) is ordered differently, and **every body
still resolves to the identical capsule**, radius and length, body for body.

## Engine paths traced, and what each ruled out

    ragdoll consistency check     does nothing -- no load validation
    original mass of a body       read via the body cinfo's motion index ->
                                  dynamic inertia entry, inverse mass
    player grab                   sums the scene graph's collision masses,
                                  skipping flagged targets
    collision object mass         0 if no world, 0 if not the motion's first
                                  body; else 1 / the half-float inverse mass
    restore original ragdoll mass
    ease-in constraints           loosens then restores the ragdoll constraints
                                  over a duration -- this is the vanilla
                                  "settle" and the reason a ragdoll is briefly
                                  loose after death

Measured consequence: the grab weight is the SUM over the scene graph, and both
files give **18 bodies, 18 distinct motions, 93.5000 kg** on the human skeleton.
Identical, so the grab path cannot produce a different result.

The ease-in step is worth remembering: the engine deliberately loosens a
ragdoll's constraints on death and eases them back over a duration. A ragdoll that
looks loose for a moment after death and then firms up is vanilla behaviour, not a
defect -- and its input is constraint data that matches vanilla in every scalar.

## THE REFERENCE WAS WRONG: DataUnpacked is a different build of the game

Found last, after everything above said the file was equivalent to vanilla. The
one assumption nothing had tested was the meaning of "vanilla": every comparison
in this session, and in this project's collision work generally, used
`E:\Tools\Fallout 4\DataUnpacked`. That tree is **not what the installed game
loads.**

Measured by reading the installed archives directly (`ba2get.py`, GNRL format:
'BTDX', version, type, numFiles, nameTableOffset; then 36-byte records
nameHash/ext/dirHash/flags/offset/packed/unpacked/align; then a u16-length name
table in record order):

    NIFs sampled from Fallout4 - Meshes.ba2      600
    identical to the unpacked tree                 0
    different                                    600

Most differ by exactly 44 bytes -- the NIF header's export path string -- but not
all, and **collision blobs differ too**: 6 of 6 sampled, including plain statics.
`CeilingFan01` is 3904 bytes in the archive against 3920 unpacked, 462 bytes
differing, while decoding to the same 2 shapes and the same 54 preview triangles.
So it is a re-export, not corruption. The installed archives are **BA2 version 8**,
i.e. the next-gen update.

### Why it matters for ragdolls specifically, and not much elsewhere

The two builds order a ragdoll's BODIES differently:

    bone   the game        DataUnpacked
    0      COM             COM
    1      LLeg_Thigh      LLeg_Thigh
    2      RLeg_Thigh      SPINE1
    3      SPINE1          RLeg_Thigh
    5      RLeg_Calf       SPINE2
    8      RLeg_Foot       Chest
    10     LArm_UpperArm   RArm_UpperArm

Each file is internally consistent -- every bone's rest pose matches its own body
order, `boneToBodyMap` is the identity in both -- so both are valid ragdolls. The
brahmin is reordered the same way (62041 bytes in the archive, 62085 unpacked).

Our pipeline faithfully preserves whichever order it is given. Built from
DataUnpacked it produces DataUnpacked's order, and that was then installed over a
game whose animation and behaviour data -- which lives outside the NIF and which
we never touch -- expects the other order.

**Rebuilt from the game's own archive, our output reproduces the game's order
exactly**: bone for bone, parent for parent, node for node, with identical
body-to-node mapping, total mass (93.500 kg human, 121.000 kg brahmin), mean
density and trigger-body counts.

### What this means for the rest of the collision work

Every "vanilla says X" figure in this project was measured against DataUnpacked.
The corpus statistics are still self-consistent and the format conclusions drawn
from them are still sound -- both builds are the same FORMAT. But any claim of the
shape "our output is byte-identical to vanilla" is byte-identical to *that* build,
and any mod built from the corpus is built from *that* build's assets. For static
collision that has been harmless. For ragdolls it was not.

## Static analysis is saturated; the file compares equal and the game disagrees

Rebuilt from the game's own archive, our output was compared against that archive's
file at every level available:

    NIF header             same version 20.2.0.7, user 12, bs 130; same census
    NIF blocks             0 of 166 differ, links resolved to type + name
    packfile container     header and section table identical; local and virtual
                           fixups identical; global fixups differ only in the
                           shape-list array's ORDER
    Havok's deserializer   ragdoll 127 of 4529 scalars differ, ALL inert
                           (22 array indices, 63 w lanes, 42 capsule plane order,
                           and zero of anything else); bumper 0 of 124
    pointers               197 of 197 resolve to the same object
    materials/motionProps  all 18 entries byte-identical TO EACH OTHER, pointers
                           included, so the index permutation cannot matter
    our own ragdoll solver energy 6.77 vs 6.76, maxSpeed 0.836 vs 0.835, joint
                           error 0.000178 vs 0.000175 over 120 steps

bungo reports the rebuilt ragdolls still thrash on death where vanilla plays its
animation, with MO2 confirming only `WW Ragdoll Test` enabled. So the conclusion
"equivalent" is being asserted by every instrument here and contradicted by the
only oracle that counts.

**The instruments are saturated. The next move is empirical bisection, not more
comparison.** Three mods are staged to split it in three loads:

    WW Ragdoll Placebo   the game's own files, byte-identical, shipped loose.
                         If these misbehave, the content is not the cause at all.
    WW Ragdoll Hybrid    our wrapper, the game's blobs (tools/blobswap.py).
                         Separates the packfiles from everything around them.
    WW Ragdoll Test      the full rebuild.

If the Hybrid behaves and the Test does not, the fault is one of the three
differences called inert above, and they get bisected individually. If the Placebo
misbehaves, none of this file analysis was ever relevant.

**Standing lesson:** four separate times this investigation reached "the file is
equivalent" and was wrong about what that implied -- once because the reference was
a different build, once because a second packfile was dismissed unexamined, and
twice because "inert" was an argument rather than a measurement. A comparison that
cannot fail is not evidence, and neither is one whose scope is smaller than its
conclusion.
