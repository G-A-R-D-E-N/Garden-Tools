# `--scan-skeleton-physics` — census the physics a skeleton carries

```bash
BehaviourGraphStudio.exe --scan-skeleton-physics "<folder>" [--json]
```

Walks every `skeleton*.hkx` under `<folder>` (recursively) — loose files **and** the same names
inside every `.ba2` — and reports, for each one, what physics it holds: the load status, the bodies,
shapes and constraints the file declares, whether the ragdoll skeleton mapper is there, and whether
the file carries `hknpBSMaterial`.

This is the command to reach for when a character "loads with physics missing": it says whether the
file has no ragdoll at all, has one BGS cannot read completely, or stores physics in a format
Fallout 4 does not load.

Like `--scan-archives` it does **not** need an MO2 instance — it reads the folder you point it at. A
skeleton file with no ragdoll is a normal thing to find (hair and props have none), so the output is
a census rather than a pass/fail list.

## What it reports per file

| Field | Meaning |
|---|---|
| status | `OK`, `SKELETON ONLY`, `LEGACY PHYSICS`, `UNSUPPORTED SHAPES`, `MAPPER MISSING`, `NO BODIES`, `WRONG CONTAINER` or `PARSE ERROR`. |
| bodies | Rigid bodies read from `hknpRagdollData`, out of the body entries it declares. |
| shapes / constraints | How many the preview measured, and how many constraint entries could not be read. |
| shape classes | Every shape class in the file with its object count, e.g. `hknpCapsuleShape x19`. |
| constraint classes | Every `*ConstraintData` class with its object count, e.g. `hkpRagdollConstraintData x9`. |
| physics classes | Every `hknp*`/`hkp*` class with its object count — the census behind the status. |
| mapper | Whether `hkaSkeletonMapper` is present, and how many animation-to-ragdoll mappings it produced. |
| `hknpBSMaterial` | Whether the file carries the Fallout 4 material object. |

### Statuses

| Status | What it means |
|---|---|
| `OK` | A readable ragdoll: bodies, capsule shapes and a usable mapper. |
| `SKELETON ONLY` | No `hknpRagdollData`. Legitimate for animations, hair and props; the reason a custom skeleton's physics is "missing". |
| `LEGACY PHYSICS` | The file holds older `hkp` rigid bodies, shapes or a physics system instead of `hknp` bodies. `hkp` **constraint data** alone is normal inside a Fallout 4 ragdoll and never causes this. |
| `UNSUPPORTED SHAPES` | A ragdoll is present and readable, but some bodies use a shape BGS cannot preview yet (only `hknpCapsuleShape` is drawn), or a body/constraint entry could not be read. The class names and counts are listed. |
| `MAPPER MISSING` | A readable ragdoll with no usable `hkaSkeletonMapper`, so it cannot follow the animation. |
| `NO BODIES` | `hknpRagdollData` is present but declares no body, or none of its bodies carried a readable frame. |
| `WRONG CONTAINER` | Not a Fallout 4 packfile: a tagfile, a different packfile version, or a pointer width other than 8 bytes. The detected value is named. |
| `PARSE ERROR` | Reading failed after the header. The failing stage and message are reported. |

## Reading the output

```
scanning 79 .ba2 archive(s) under D:\SteamLibrary\steamapps\common\Fallout 4\Data
OK                Meshes/Actors/Character/CharacterAssets/skeleton.hkx  [Fallout4 - Animations.ba2]
    bodies 18 usable of 18, 0 skipped; shapes 19; constraints 17, 0 skipped; mapper present (18 mappings); hknpBSMaterial absent
    shape classes: hknpCapsuleShape x19
    constraint classes: hkpRagdollConstraintData x9, hkpLimitedHingeConstraintData x8
    physics classes: hknpCapsuleShape x19, hknpShapeMassProperties x19, hkpRagdollConstraintData x9, hkpLimitedHingeConstraintData x8, hknpPhysicsSceneData x1, hknpRagdollData x1, hkpPositionConstraintMotor x1
UNSUPPORTED SHAPES Meshes/Actors/Mirelurk/CharacterAssets/skeleton.hkx  [Fallout4 - Animations.ba2]
    The ragdoll is readable but not complete: 4 of 23 bodies use shapes BGS cannot preview yet (hknpConvexPolytopeShape x4).
    next: BGS previews capsule shapes (hknpCapsuleShape) only; the other bodies are still listed with their frames, but they are drawn with no collision hull.
    bodies 23 usable of 23, 0 skipped; shapes 20; constraints 22, 0 skipped; mapper present (23 mappings); hknpBSMaterial absent
    shape classes: hknpCapsuleShape x20, hknpConvexPolytopeShape x4
    constraint classes: hkpRagdollConstraintData x16, hkpLimitedHingeConstraintData x6
    physics classes: hknpCapsuleShape x20, hknpShapeMassProperties x23, hknpConvexPolytopeShape x4, hkpRagdollConstraintData x16, hkpLimitedHingeConstraintData x6, hknpPhysicsSceneData x1, hknpRagdollData x1, hkpPositionConstraintMotor x1
SKELETON ONLY     Meshes/SetDressing/Animated/TargetDummy01/CharacterAssets/Skeleton.hkx  [Fallout4 - Animations.ba2]
    This file has a skeleton but no ragdoll. Fallout 4 ragdolls are stored as hknpRagdollData; re-export with physics included.
    next: Fallout 4 keeps a ragdoll in the skeleton file itself, so check the sibling skeleton the game loads with this animation.
    bodies 0 usable of 0, 0 skipped; shapes 0; constraints 0, 0 skipped; mapper absent; hknpBSMaterial absent
    shape classes: none
    constraint classes: none
    physics classes: none
done  195 skeleton file(s) scanned across 7 archive(s) + loose, 21 with a readable ragdoll, 154 with no usable ragdoll, 0 unreadable
```

The three rows above are real output from the vanilla install; the full run is written up in
[`docs/research/fo4-skeleton-physics-census.md`](../../docs/research/fo4-skeleton-physics-census.md).

- `<status> <path>  [<source>]` — `loose` is a file on disk; anything else is the `.ba2` the entry
  came from.
- `  opened N/N` and `  checked N/N` are progress written to **stderr**, throttled to ~20 updates
  total. Under `--json` stdout carries only the report.

## Exit codes

- `0` — every file scanned either has a readable ragdoll (`OK`) or is readable with reported gaps
  (`UNSUPPORTED SHAPES`).
- `1` — at least one file has no usable ragdoll (`SKELETON ONLY`, `LEGACY PHYSICS`, `MAPPER MISSING`,
  `NO BODIES`, `WRONG CONTAINER`, `PARSE ERROR`), or an entry could not be read at all.
- `2` — usage error: the folder is missing or was not given.

## JSON summary and findings

`summary` keys: `archives` (archives that held at least one `skeleton*.hkx`), `scanned`, `ok`,
`skeletonOnly`, `legacyPhysics`, `unsupportedShapes`, `mapperMissing`, `noBodies`, `wrongContainer`,
`parseError`, `unreadable`, `skippedArchives`.

Every scanned file produces one finding whose `kind` is the status slug (`ok`, `skeleton-only`,
`legacy-physics`, `unsupported-shapes`, `mapper-missing`, `no-bodies`, `wrong-container`,
`parse-error`), with `path`, `source` and `details` carrying the bodies, class censuses, mapper and
`hknpBSMaterial` line. An archive that could not be opened is a `skip` finding (a warning: it is
usually a texture archive, and it does not change the exit code), and an entry that could not be
extracted is `unreadable`.

```bash
BehaviourGraphStudio.exe --scan-skeleton-physics "D:\SteamLibrary\steamapps\common\Fallout 4\Data" --json > skeleton-physics.json
```
