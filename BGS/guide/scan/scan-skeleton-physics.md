# `--scan-skeleton-physics` — census the physics a skeleton carries

```bash
BehaviourGraphStudio.exe --scan-skeleton-physics "<folder>" [--json]
```

Walks every `skeleton*.hkx` value the game data resolves under `<folder>` — loose files **and** the
same names inside `.ba2` archives — and reports, for each winner, what physics it holds: the load
status, the bodies, shapes and constraints the file declares, whether the ragdoll skeleton mapper is
there, and whether the file carries `hknpBSMaterial`.

Discovery belongs to `GameData`: the folder's archives in the game's own load order, plus loose files
(and, for an MO2 instance, the enabled mods), merged by virtual path with the game's precedence. The
archives it reads are the `.ba2` files directly under the `Data` folder — the ones the game loads —
ordered by `plugins.txt` (alphabetically when there is none), plus the enabled mod roots' own top-level
archives; an archive buried in a Data subfolder is not part of the game's data tree and is not censused.
A winner that hides copies from another source (a loose file over an archive, or one archive over
another) is censused once, and the copies it hides are listed in the row's `details` (JSON mode) as
`shadowed: <virtual path> [<archive or loose>]`. The name rule is
`GameData.IsSkeletonName` — a `.hkx` whose file name starts with `skeleton`, matched case-insensitively
— so `Skeleton.hkx` is found on Linux as well as on Windows. A skeleton that cannot be read (over the
512 MB HKX cap, or an IO or permission failure) is reported as `unreadable` with the reason.

This is the command to reach for when a character "loads with physics missing": it says whether the
file has no ragdoll at all, has one BGS cannot read completely, or stores physics in a format
Fallout 4 does not load.

It reads the folder you point it at: a game `Data` folder, or — like the other scans — an MO2
instance, whose `ModOrganizer.ini` game path and enabled modlist supply the Data folder and the mod
roots. A skeleton file with no ragdoll is a normal thing to find (hair and props have none), so the
output is a census rather than a pass/fail list: `SKELETON ONLY` is reported and never fails the run.

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

Beyond its status, a row can have extra classifier findings: an unusual container that still read,
bodies whose shapes BGS cannot preview, body or constraint entries that could not be read, and older
`hkp` objects next to a readable ragdoll. The census does not print those extra findings. Each row shows
its headline (`why:` in `details` in JSON mode) and the first `next:` hint any finding carries. Human
mode prints the headline and `next:` under the status line only for rows that are not `OK`.

### Statuses

| Status | What it means |
|---|---|
| `OK` | A readable ragdoll: bodies, capsule shapes and a usable mapper. |
| `SKELETON ONLY` | No `hknpRagdollData` and no older `hkp` bodies or shapes (those are `LEGACY PHYSICS`). Legitimate for animations, hair and props; the reason a custom skeleton's physics is "missing". 154 of the 195 vanilla skeletons are this, and it never fails the scan. |
| `LEGACY PHYSICS` | The file holds older `hkp` rigid bodies, shapes or a physics system and no `hknpRagdollData`. When a readable ragdoll is present, those older objects are ignored and never make the status `LEGACY PHYSICS`. `hkp` **constraint data** alone is normal inside a Fallout 4 ragdoll and never causes this. |
| `UNSUPPORTED SHAPES` | A ragdoll is present and readable, but some bodies use a shape BGS cannot preview yet (only `hknpCapsuleShape` is drawn), or a body/constraint entry could not be read. The class names and counts are listed. |
| `MAPPER MISSING` | A readable ragdoll that cannot follow the animation. Either the file has no `hkaSkeletonMapper` at all, or one is present but none of its mappings link this ragdoll to the animation skeleton. |
| `NO BODIES` | `hknpRagdollData` is present but declares no body, or none of its bodies carried a readable frame. |
| `WRONG CONTAINER` | The bytes are not a Fallout 4 packfile and no usable ragdoll came out of them — BGS still reads them first: an XML (unpacked) tagfile, a binary tagfile (its `TAG0` marker sits at byte offset 4), a different packfile version, a pointer width other than 8 bytes, big-endian, other layout rules, or a file too small to hold a header. The detected value is named. |
| `PARSE ERROR` | Reading failed after the header. The failing stage (`container`, `byte-backed XML`, `model` or `ragdoll`) and message are reported. |

The container is never refused before it is read: BGS attempts the parse and the extraction first, so
a file with an unusual container that still yields a readable ragdoll keeps the status its physics
deserves. The classifier adds an `Info` finding that names the container problem, but the census does
not print it: the row's JSON `next:` line carries that finding's hint, which does not name the problem.

## Reading the output

```
scanning skeleton .hkx under D:\SteamLibrary\steamapps\common\Fallout 4\Data
found 195 packed and 0 loose skeleton file(s)
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
done  195 skeleton file(s) scanned across 7 archive(s) + loose, 21 with a readable ragdoll, 20 with preview gaps, 154 skeleton-only, 0 with no usable ragdoll, 0 unreadable
```

The three rows above are real output from the vanilla install; the full run is written up in
[`docs/research/fo4-skeleton-physics-census.md`](../../docs/research/fo4-skeleton-physics-census.md).

- `<status> <path>  [<source>]` — `loose` is a file on disk; anything else is the `.ba2` the entry
  came from.
- `  read N/N packed` and `  checked N/N loose` are progress written to **stderr**, throttled to about
  20 updates per loop. Under `--json` stdout carries only the report.

## Exit codes

- `0` — every file scanned is `OK`, `UNSUPPORTED SHAPES` or `SKELETON ONLY`, and nothing was
  unreadable. A `SKELETON ONLY` file is a normal census row (154 of the 195 vanilla skeletons),
  not a defect, so it never fails the scan.
- `1` — at least one file is `LEGACY PHYSICS`, `MAPPER MISSING`, `NO BODIES`, `WRONG CONTAINER` or
  `PARSE ERROR`, or an entry could not be read at all.
- `2` — usage error: the folder is missing or was not given.

The `done` line reports skeleton-only files separately from the `with no usable ragdoll` count. That
count is exactly the `LEGACY PHYSICS`, `MAPPER MISSING`, `NO BODIES`, `WRONG CONTAINER` and `PARSE ERROR`
rows: skeleton-only files are never part of it, and unreadable entries are counted on their own
(`N unreadable`), though they still make the exit code `1`.

## JSON summary and findings

`summary` keys: `archives` (archives that held at least one `skeleton*.hkx`, winners and shadowed
copies alike), `scanned`, `ok`, `skeletonOnly`, `legacyPhysics`, `unsupportedShapes`, `mapperMissing`,
`noBodies`, `wrongContainer`, `parseError`, `unreadable`, `skippedArchives` and `shadowed` (how many
copies a higher-priority winner hid).

Every scanned file produces one finding whose `kind` is the status slug (`ok`, `skeleton-only`,
`legacy-physics`, `unsupported-shapes`, `mapper-missing`, `no-bodies`, `wrong-container`,
`parse-error`), with `path`, `source` and `details` carrying the bodies, class censuses, mapper and
`hknpBSMaterial` line, plus one `shadowed:` line per copy the winner hides. An archive that could not
be opened is a `skip` finding (a warning: it is usually a texture archive, and it does not change the
exit code), and a winner that could not be read is `unreadable`.

```bash
BehaviourGraphStudio.exe --scan-skeleton-physics "D:\SteamLibrary\steamapps\common\Fallout 4\Data" --json > skeleton-physics.json
```
