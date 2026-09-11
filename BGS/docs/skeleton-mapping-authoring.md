# Skeleton mapping authoring

`SkeletonMapperAuthor` builds `hkaSkeletonMapperData::SimpleMapping` rows for a
pair of skeleton rigs. Everything it does comes from Fallout 4 itself, not from
the field names. The reverse-engineering record lives in `commonlib_NonVR`,
in the RE-Work docs numbered 63 (runtime semantics), 64 (the Creation Kit
authoring path) and 65 (validation matrix and unresolved questions).

## What a row is

A row's stored transform is

```
inverse(modelSpace_A[boneA]) * modelSpace_B[boneB]
```

in **model space**, where `modelSpace_x[i]` composes bone `i`'s reference
transform down its own hierarchy with the rule the runtime itself uses:

```
compose(a, b):  t = a.t + rotate(a.q, b.t)
                q = a.q * b.q
```

That is the whole computation. It was confirmed by measuring all 18 rows of both
mappers shipped in `Meshes/Actors/Character/CharacterAssets/skeleton.hkx`
against the same expression built from the reference poses: worst translation
error `3.8e-5` at unit scale, rotations exact. Reading the same expression in
local space is wrong by up to 13.3 units and 2 radians, which is why model space
is the one implemented.

Reference-pose scale is not used. Everything shipped is unit-scaled, so a
non-unit rig cannot be validated against the game yet, and the row scale lane is
deliberately left alone rather than guessed.

## Direction

The engine applies a row from `skeletonA` to `skeletonB` and writes `skeletonB`,
so a mapper's direction is a statement about which rig is the output. A rig that
needs both directions needs both mapper objects; one cannot serve the other.
`SkeletonMapperAuthor.Reciprocal` returns both, and the reverse rows are the
forward rows with the two bone indices swapped and each transform inverted.
That reproduces the shipped reciprocal mapper to `2.3e-5` with exact rotations,
which is tighter than recomputing it from the reference poses the way the
offline generator does (`4.4e-5`). Both are within float noise of the shipped
bytes.

## Refusals

The engine's generator refuses a candidate pair when either bone is already
mapped, and nothing else. Fallout 4 performs no validation at load time at all -
the only mapping checks in the game are debug-build warnings that the release
runtime never reaches. So an authoring surface is the last place a malformed
mapper can be stopped, and `Refusals` covers the conditions a mapper can fail on
before it is written:

* a bone index outside either skeleton;
* a bone of either side mapped more than once;
* an empty pair list.

## What is not here

Deriving the bone pairing from names is not implemented. Vanilla names do not
match between the animation rig and the ragdoll rig (`LArm_UpperArm` against
`Ragdoll_NPC L UpperArm`), so name matching demonstrably cannot reproduce the
shipped rows and it is not known which of the generator's matching modes
produced them. Callers supply the pairing.

Chain mappings are not implemented or interpreted. No vanilla ragdoll mapper
contains one, and the consumer's chain blend has not been reduced to an
equation.

Writing the rows back into a packfile is not implemented. This module computes
and validates mapping data; it does not yet serialise it.
