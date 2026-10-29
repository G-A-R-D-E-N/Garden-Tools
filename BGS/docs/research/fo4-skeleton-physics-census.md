# Fallout 4 skeleton physics census

Date: 2026-10-16

Command (from `BGS/`):

```bash
BehaviourGraphStudio.exe --scan-skeleton-physics "<Steam library>/steamapps/common/Fallout 4/Data" --json
```

Install: a Steam install of Fallout 4 (read-only). The scan walks every `.ba2` under the folder plus
every loose `skeleton*.hkx`. This install has **no loose skeleton files**: all 195 of them live inside
7 `.ba2` archives. 37 further archives were skipped because they are DX10 texture archives
(`Textures are stored differently`), which cannot hold a skeleton.

Only class names, statuses and counts are recorded here. No game bytes are reproduced.

## Summary

| Status | Files | Meaning |
|---|---:|---|
| OK | 21 | Readable ragdoll: every body has an hknpCapsuleShape and the mapper is usable. |
| UNSUPPORTED SHAPES | 20 | Readable ragdoll, but at least one body uses a shape BGS cannot preview (hknpConvexPolytopeShape, hknpCompressedMeshShape). |
| SKELETON ONLY | 154 | No hknpRagdollData at all. Dominated by animated props and first-person rigs (below). |
| LEGACY PHYSICS | 0 | No hkp rigid bodies, shapes or physics systems anywhere in vanilla. |
| MAPPER MISSING | 0 | Every ragdoll carried a usable hkaSkeletonMapper. |
| NO BODIES | 0 | Every hknpRagdollData produced at least one body. |
| WRONG CONTAINER | 0 | Every file was a version 11, 8-byte-pointer packfile. |
| PARSE ERROR | 0 | Nothing failed after the container header. |

- Scanned: **195** files; archives that held a skeleton: **7**; unreadable entries: **0**; skipped archives: **37**.
- With an hknpRagdollData ragdoll: **41** (21 OK + 20 UNSUPPORTED SHAPES).
- hknpBSMaterial appears in **none** of the 195 vanilla skeleton files.
- Bodies read: **950** across all ragdolls; bodies skipped: **0**; constraints read: **900**.

## Physics classes across the whole install

| Class | Objects | Note |
|---|---:|---|
| hknpShapeMassProperties | 1000 | Per-shape mass properties. |
| hknpCapsuleShape | 933 | The only shape BGS previews today. |
| hkpRagdollConstraintData | 666 | Legacy-format constraint data, normal inside an FO4 ragdoll. |
| hkpLimitedHingeConstraintData | 231 | Legacy-format constraint data, normal inside an FO4 ragdoll. |
| hknpConvexPolytopeShape | 111 | Convex hull; parent class of the capsule. Not previewed. |
| hknpRagdollData | 41 | The ragdoll itself: one per skeleton that has one. |
| hknpPhysicsSceneData | 41 | Present in the same files as `hknpRagdollData`. |
| hkpPositionConstraintMotor | 40 | Legacy-format motor object referenced by a ragdoll constraint. |
| hknpCompressedMeshShape | 10 | Compressed triangle mesh. Not previewed. |
| hknpCompressedMeshShapeData | 10 | Payload the compressed mesh shape points at. |
| hkpFixedConstraintData | 3 | Legacy-format constraint data, normal inside an FO4 ragdoll. |

## Per archive

| Archive | Skeleton files | `OK` | `UNSUPPORTED SHAPES` | `SKELETON ONLY` |
|---|---:|---:|---:|---:|
| DLCCoast - Main.ba2 | 20 | 3 | 3 | 14 |
| DLCNukaWorld - Main.ba2 | 27 | 3 | 1 | 23 |
| DLCRobot - Main.ba2 | 9 | 1 | 1 | 7 |
| DLCworkshop01 - Main.ba2 | 3 | 0 | 0 | 3 |
| DLCworkshop02 - Main.ba2 | 9 | 0 | 0 | 9 |
| DLCworkshop03 - Main.ba2 | 8 | 0 | 0 | 8 |
| Fallout4 - Animations.ba2 | 119 | 14 | 15 | 90 |

## The 41 skeletons that carry a ragdoll

Bodies/constraints are the measured counts; "capsules" is how many `hknpCapsuleShape` objects were read
(a file can hold more shapes than bodies). Paths are archive paths, with `/` separators.

| Status | Path | Archive | Bodies | Capsules | Constraints | Mapper |
|---|---|---|---:|---:|---:|---|
| UNSUPPORTED SHAPES | Meshes/Actors/Alien/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 18 | 16 | 17 | present (18 mappings) |
| OK | Meshes/Actors/Bloatfly/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 23 | 25 | 22 | present (23 mappings) |
| OK | Meshes/Actors/Brahmin/CharacterAssets/Skeleton.hkx | Fallout4 - Animations.ba2 | 39 | 48 | 38 | present (39 mappings) |
| OK | Meshes/Actors/Cat/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 24 | 26 | 23 | present (25 mappings) |
| OK | Meshes/Actors/Character/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 18 | 19 | 17 | present (18 mappings) |
| UNSUPPORTED SHAPES | Meshes/Actors/CreateABot/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 24 | 23 | 23 | present (21 mappings) |
| UNSUPPORTED SHAPES | Meshes/Actors/Deathclaw/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 28 | 27 | 27 | present (28 mappings) |
| UNSUPPORTED SHAPES | Meshes/Actors/DLC01/CreateABot/CharacterAssets/skeletonRoboBrain.hkx | DLCRobot - Main.ba2 | 17 | 13 | 16 | present (17 mappings) |
| OK | Meshes/Actors/DLC01/Turret/CharacterAssets/SkeletonMountedLight.hkx | DLCRobot - Main.ba2 | 2 | 2 | 1 | present (2 mappings) |
| OK | Meshes/Actors/DLC03/Angler/CharacterAssets/skeleton.hkx | DLCCoast - Main.ba2 | 21 | 22 | 20 | present (21 mappings) |
| UNSUPPORTED SHAPES | Meshes/Actors/DLC03/CreateABot/CharacterAssets/skeletonRoboBrain.hkx | DLCCoast - Main.ba2 | 17 | 13 | 16 | present (17 mappings) |
| UNSUPPORTED SHAPES | Meshes/Actors/DLC03/FogCrawler/CharacterAssets/skeleton.hkx | DLCCoast - Main.ba2 | 65 | 64 | 64 | present (65 mappings) |
| UNSUPPORTED SHAPES | Meshes/Actors/DLC03/Hermit/CharacterAssets/skeleton.hkx | DLCCoast - Main.ba2 | 29 | 28 | 25 | present (26 mappings) |
| OK | Meshes/Actors/DLC03/RadChicken/CharacterAssets/skeleton.hkx | DLCCoast - Main.ba2 | 7 | 7 | 6 | present (7 mappings) |
| OK | Meshes/Actors/DLC03/RadRabbit/CharacterAssets/skeleton.hkx | DLCCoast - Main.ba2 | 22 | 22 | 21 | present (22 mappings) |
| UNSUPPORTED SHAPES | Meshes/Actors/DLC04/Animatronic/CharacterAssets/skeleton_Alien.hkx | DLCNukaWorld - Main.ba2 | 2 | 0 | 1 | present (2 mappings) |
| OK | Meshes/Actors/DLC04/Bloodworm/CharacterAssets/skeleton.hkx | DLCNukaWorld - Main.ba2 | 15 | 15 | 14 | present (15 mappings) |
| OK | Meshes/Actors/DLC04/CaveCricket/CharacterAssets/skeleton.hkx | DLCNukaWorld - Main.ba2 | 32 | 34 | 31 | present (32 mappings) |
| OK | Meshes/Actors/DLC04/Swarm/CharacterAssets/skeleton.hkx | DLCNukaWorld - Main.ba2 | 6 | 8 | 5 | present (5 mappings) |
| OK | Meshes/Actors/Dogmeat/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 24 | 24 | 23 | present (24 mappings) |
| OK | Meshes/Actors/Eyebot/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 10 | 12 | 9 | present (10 mappings) |
| OK | Meshes/Actors/FeralGhoul/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 18 | 20 | 17 | present (18 mappings) |
| OK | Meshes/Actors/FEVHound/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 22 | 22 | 21 | present (22 mappings) |
| OK | Meshes/Actors/Gorilla/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 18 | 20 | 17 | present (18 mappings) |
| UNSUPPORTED SHAPES | Meshes/Actors/LibertyPrime/CharacterAssets/Skeleton.hkx | Fallout4 - Animations.ba2 | 15 | 13 | 13 | present (14 mappings) |
| UNSUPPORTED SHAPES | Meshes/Actors/Mirelurk/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 23 | 20 | 22 | present (23 mappings) |
| UNSUPPORTED SHAPES | Meshes/Actors/MirelurkHunter/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 62 | 58 | 61 | present (62 mappings) |
| UNSUPPORTED SHAPES | Meshes/Actors/MirelurkKing/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 22 | 23 | 21 | present (22 mappings) |
| UNSUPPORTED SHAPES | Meshes/Actors/MirelurkQueen/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 63 | 58 | 58 | present (59 mappings) |
| OK | Meshes/Actors/Molerat/CharacterAssets/Skeleton.hkx | Fallout4 - Animations.ba2 | 20 | 22 | 19 | present (20 mappings) |
| OK | Meshes/Actors/Mosquito/CharacterAssets/Skeleton.hkx | Fallout4 - Animations.ba2 | 23 | 25 | 22 | present (23 mappings) |
| UNSUPPORTED SHAPES | Meshes/Actors/PowerArmor/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 15 | 12 | 14 | present (15 mappings) |
| OK | Meshes/Actors/RadRoach/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 25 | 25 | 24 | present (25 mappings) |
| UNSUPPORTED SHAPES | Meshes/Actors/Radscorpion/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 50 | 47 | 48 | present (49 mappings) |
| UNSUPPORTED SHAPES | Meshes/Actors/RadStag/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 31 | 30 | 30 | present (31 mappings) |
| OK | Meshes/Actors/Stingwing/CharacterAssets/Skeleton.hkx | Fallout4 - Animations.ba2 | 31 | 32 | 30 | present (32 mappings) |
| OK | Meshes/Actors/Supermutant/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 17 | 18 | 16 | present (17 mappings) |
| UNSUPPORTED SHAPES | Meshes/Actors/SuperMutantBehemoth/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 19 | 19 | 18 | present (21 mappings) |
| UNSUPPORTED SHAPES | Meshes/Actors/Turret/CharacterAssets/SkeletonTurretWorkshop.hkx | Fallout4 - Animations.ba2 | 4 | 0 | 3 | present (4 mappings) |
| UNSUPPORTED SHAPES | Meshes/Actors/Vertibird/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 11 | 2 | 10 | present (11 mappings) |
| UNSUPPORTED SHAPES | Meshes/Actors/YaoGuai/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 18 | 19 | 17 | present (18 mappings) |

## Skeletons whose bodies use shapes BGS cannot preview

These are the `UNSUPPORTED SHAPES` files: the ragdoll is readable and every body has a frame, but the
collision hull is a shape the preview does not draw yet. `hknpSphereShape` does not appear in any
vanilla skeleton; the two non-capsule classes in the shipped game are `hknpConvexPolytopeShape` and
`hknpCompressedMeshShape`.

| Creature / object | Archive | Bodies | Shape classes |
|---|---|---:|---|
| Meshes/Actors/Alien/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 18 | hknpCapsuleShape x16, hknpConvexPolytopeShape x2 |
| Meshes/Actors/CreateABot/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 24 | hknpCapsuleShape x23, hknpConvexPolytopeShape x1 |
| Meshes/Actors/Deathclaw/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 28 | hknpCapsuleShape x27, hknpConvexPolytopeShape x8, hknpCompressedMeshShape x1 |
| Meshes/Actors/DLC01/CreateABot/CharacterAssets/skeletonRoboBrain.hkx | DLCRobot - Main.ba2 | 17 | hknpCapsuleShape x13, hknpConvexPolytopeShape x4 |
| Meshes/Actors/DLC03/CreateABot/CharacterAssets/skeletonRoboBrain.hkx | DLCCoast - Main.ba2 | 17 | hknpCapsuleShape x13, hknpConvexPolytopeShape x4 |
| Meshes/Actors/DLC03/FogCrawler/CharacterAssets/skeleton.hkx | DLCCoast - Main.ba2 | 65 | hknpCapsuleShape x64, hknpConvexPolytopeShape x1 |
| Meshes/Actors/DLC03/Hermit/CharacterAssets/skeleton.hkx | DLCCoast - Main.ba2 | 29 | hknpCapsuleShape x28, hknpConvexPolytopeShape x1 |
| Meshes/Actors/DLC04/Animatronic/CharacterAssets/skeleton_Alien.hkx | DLCNukaWorld - Main.ba2 | 2 | hknpCompressedMeshShape x2 |
| Meshes/Actors/LibertyPrime/CharacterAssets/Skeleton.hkx | Fallout4 - Animations.ba2 | 15 | hknpCapsuleShape x13, hknpConvexPolytopeShape x2 |
| Meshes/Actors/Mirelurk/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 23 | hknpCapsuleShape x20, hknpConvexPolytopeShape x4 |
| Meshes/Actors/MirelurkHunter/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 62 | hknpCapsuleShape x58, hknpConvexPolytopeShape x5 |
| Meshes/Actors/MirelurkKing/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 22 | hknpCapsuleShape x23, hknpConvexPolytopeShape x1 |
| Meshes/Actors/MirelurkQueen/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 63 | hknpCapsuleShape x58, hknpConvexPolytopeShape x4, hknpCompressedMeshShape x1 |
| Meshes/Actors/PowerArmor/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 15 | hknpCapsuleShape x12, hknpConvexPolytopeShape x4 |
| Meshes/Actors/Radscorpion/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 50 | hknpCapsuleShape x47, hknpConvexPolytopeShape x3 |
| Meshes/Actors/RadStag/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 31 | hknpCapsuleShape x30, hknpCompressedMeshShape x2 |
| Meshes/Actors/SuperMutantBehemoth/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 19 | hknpCapsuleShape x19, hknpConvexPolytopeShape x8 |
| Meshes/Actors/Turret/CharacterAssets/SkeletonTurretWorkshop.hkx | Fallout4 - Animations.ba2 | 4 | hknpConvexPolytopeShape x3, hknpCompressedMeshShape x1 |
| Meshes/Actors/Vertibird/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 11 | hknpConvexPolytopeShape x43, hknpCapsuleShape x2, hknpCompressedMeshShape x2 |
| Meshes/Actors/YaoGuai/CharacterAssets/skeleton.hkx | Fallout4 - Animations.ba2 | 18 | hknpCapsuleShape x19, hknpConvexPolytopeShape x1 |

Creatures with at least one non-capsule body shape: Alien, CreateABot (all three variants including
`skeletonRoboBrain`), Deathclaw, FogCrawler, Hermit, Liberty Prime, Mirelurk, Mirelurk Hunter,
Mirelurk King, Mirelurk Queen, PowerArmor, Radscorpion, RadStag, SuperMutantBehemoth, Turret
(`SkeletonTurretWorkshop`), Vertibird, YaoGuai, and the Nuka-World Animatronic `skeleton_Alien`.
The Vertibird is the extreme case (43 `hknpConvexPolytopeShape` objects, only 2 capsules). Two files
have no capsule shape at all: the Nuka-World Animatronic `skeleton_Alien` (2 bodies, 2 compressed
mesh shapes) and `SkeletonTurretWorkshop` (4 bodies, 3 convex polytopes and 1 compressed mesh)  -  and
Mirelurk King is the closest to being fully previewable (one polytope against 23 capsules).

## The 154 skeletons with no ragdoll

Every one of these reports `SKELETON ONLY`: a skeleton (usually `hkaSkeleton`, `hkaAnimationContainer`)
with no `hknpRagdollData` and no `hkp` objects at all. They are not defects; they are the animated
props, doors, elevators and first-person rigs the game animates without physics. Roughly:

| Area | Files | Examples |
|---|---:|---|
| Animated props, set dressing, architecture, furniture, markers, interiors (built-in and all DLCs) | 147 | Meshes/SetDressing/Animated/TargetDummy01/CharacterAssets/Skeleton.hkx, Meshes/Markers/AttachRef/RotateHelper/CharacterAssets/Skeleton.hkx |
| First-person rigs (Meshes/Actors/Character/_1stPerson, Meshes/Actors/PowerArmor/_1stPerson) | 2 | Meshes/Actors/Character/_1stPerson/CharacterAssets/skeleton.hkx |
| Actor skeletons without physics (Meshes/Actors/Robot, Meshes/Actors/DLC01/HandyCreateABot, Meshes/Critters/Crow, Meshes/Baby, Meshes/BabyBundled) | 5 | Meshes/Actors/Robot/CharacterAssets/skeleton.hkx |

## Notes for the #660 work

- The vanilla human character skeleton (`Meshes/Actors/Character/CharacterAssets/skeleton.hkx`, inside
  `Fallout4 - Animations.ba2`) is `OK`: 18 bodies, 19 capsules, 17 constraints, 18 mappings. That is the
  same measurement the `tools/tests/fixtures/vanilla` copy gives, so the fixture is representative.
- `hknpPhysicsSceneData` appears exactly in the 41 files that carry `hknpRagdollData` (41 objects each), so
  "has a physics scene but no ragdoll" does not occur in vanilla.
- Legacy `hkp` objects in vanilla are constraint data and motors only: `hkpRagdollConstraintData`,
  `hkpLimitedHingeConstraintData`, `hkpFixedConstraintData` and `hkpPositionConstraintMotor`. No vanilla
  file contains an `hkp` rigid body, shape or physics system, which is why `LEGACY PHYSICS` is a real
  exporter bug rather than a vanilla variation.
- `hknpCompressedMeshShape` (10 objects in 7 files, 6 of which reference one from a body) was not on
  the shape list the issue started from; it is a real Fallout 4 ragdoll shape class and belongs with
  `hknpConvexPolytopeShape` as "cannot preview yet".
- `FEVHound` is `OK` even though it holds one `hknpCompressedMeshShape`: no body references it. The status
  is driven by the shape each body points at, not by every shape object in the file, which is why the
  per-file shape-class list and the status can legitimately differ.
- A file with a readable ragdoll always has a usable mapper in vanilla (0 `MAPPER MISSING`), so that status
  describes an export that dropped `hkaSkeletonMapper`, not a vanilla layout.
- Every body in every vanilla ragdoll parsed: 0 skipped bodies and 0 skipped constraints across all 41
  files, so the counting paths added in #660 are exercised by real data only through the shape classes.

## Reproducing

```bash
dotnet run --project app/BehaviourStudio.csproj -c Release -- \
  --scan-skeleton-physics "<Steam library>/steamapps/common/Fallout 4/Data" --json > census.json
```

Exit code was `0`: the 154 skeleton-only files are reported as `SKELETON ONLY` and are not counted
as failures. The per-file rows are the `findings` array (`kind` is the status slug), and `skip` rows
name the DX10 texture archives.
