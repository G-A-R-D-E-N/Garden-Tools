# `symrm` CLI reference

`symrm` is Behaviour Graph Studio's native verification and diagnostic command-line harness.
It is primarily a developer/reverse-engineering tool. Run it from `BGS/` with:

```bash
dotnet run --project tools/symrm/symrm.csproj -- <command> [arguments]
```

For the complete overview:

```bash
dotnet run --project tools/symrm/symrm.csproj -- --help
```

Every command has its own usage, flags, and example:

```bash
dotnet run --project tools/symrm/symrm.csproj -- <command> --help
```

The installed/published executable uses the shorter equivalent `symrm <command> --help`.

## Commands

| Command | Purpose |
| --- | --- |
| `corpus` | Extract matching HKX files from a BA2 corpus. |
| `check` | Validate native HKX data and optionally resolve game-data references. |
| `states` | Summarize state generators and dangling state references in unpacked XML. |
| `events` | Audit event declarations and their roles in unpacked behaviour XML. |
| `frames` | Inspect decoded animation frame/track data or print a compact digest. |
| `scale` | Inspect decoded animation scale channels. |
| `skeleton` | Print skeleton bones and reference transforms. |
| `rig` | Audit rig/skeleton data across one file or a corpus. |
| `mapper` | Read a file's skeleton mappers, rebuild them, and recompute simple rows. |
| `ragdoll` | Read a file's ragdolls, edit masses, materials and joint limits, and rebuild. |
| `extract` | Extract matching BA2 entries, optionally preserving their directory tree. |
| `ba2` | Inspect a Bethesda BA2 archive and its entries. |
| `motion` | Inspect decoded root-motion data, or dump the archive entries one substring matches. |
| `pose` | Pose a decoded animation against a skeleton and print the result. |
| `channels` | Compare animation channels with the rig they drive. |
| `packfile` | Inspect packfile sections, fixups, and round-trip structure. |
| `nullsave` | Run the diagnostic no-op native save and byte comparison. |
| `layout` | Audit native object layout and placement. |
| `relayout` | Exercise native relayout/rebuild behavior. |
| `ground` | Inspect pointer/fixup grounding for a native packfile. |
| `offsets` | Inspect reflected member offsets against shipped layout data. |
| `convert` | Convert a packfile between 32-bit and 64-bit pointer layouts. |
| `compare` | Byte/section compare two native packfiles. |
| `delete` | Exercise native object deletion on deletable graph objects. |
| `paste` | Exercise native object paste/copy authoring paths. |
| `template` | Exercise graph-template authoring against native files. |
| `conditions` | Inspect and exercise transition-condition authoring. |
| `savedelete` | Delete an eligible object, save, reload, and verify the result. |
| `classcheck` | Compare a class/layout dump with the shipped Havok class table. |
| `types` | Inspect class metadata carried by a packfile. |
| `chain` | Trace behaviour-to-animation resolution and report missing links. |
| `crash` | Resolve an AnimTextData subgraph crash hash and report missing animations. |
| `hash` | Compute the Fallout 4 AnimTextData subgraph id for a behaviour path and SAPT prefixes. |
| `sweep` | Sweep discovered subgraph manifests for animation coverage gaps. |
| `diff` | Compare vanilla and modded subgraph/behaviour coverage. |
| `notes` | Inspect animation annotations/notes in native files. |
| `saveevent` | Exercise event edits through native save/reload verification. |
| `savewide` | Exercise wide/vector field edits through native save/reload verification. |
| `savenumbers` | Exercise numeric-array edits through native save/reload verification. |
| `walk` | Walk native objects and report reachability/reference structure. |
| `signatures` | Inspect Havok class signatures used by native files. |
| `paths` | Audit serialized object/reference paths. |
| `elements` | Inspect inline array/struct elements and their serialization. |
| `nesting` | Inspect native object nesting/ownership relationships. |
| `objects` | List native objects, optionally filtering by Havok class. |
| `capacity` | Audit Havok array capacity/count serialization. |
| `qstransform` | Inspect `hkQsTransform` values and layout behavior. |
| `splinestats` | Summarize spline-compressed animation structure and error metrics. |
| `spline` | Decode and inspect spline-compressed animation data. |
| `savespline` | Exercise spline-animation edits through native save/reload verification. |
| `editframe` | Exercise decoded animation frame edits and verify the result. |
| `trim` | Exercise animation trimming and verify decoded output. |
| `retime` | Exercise animation retiming and verify decoded output. |
| `run` | Run the behaviour simulation harness and optionally send events. |
| `weights` | Inspect runtime generator/blend weights. |
| `cliptime` | Resolve clip timing from animation data beside a behaviour. |
| `cliptrim` | Audit clip crop/trim values against resolved animation duration. |
| `mesh` | Inspect NIF mesh geometry and skinning data. |
| `meshpng` | Render a skinned-mesh diagnostic image. |
| `lifecycle` | Run the native open/edit/save/reload/validate/render lifecycle gate. |
| `test` | Run the native/Havok regression harness used by BGS CI. |
| `defaults` | Compare game class-registration defaults with the shipped table. |
| `gltf` | Import a glTF animation, such as a Blender export, as a Fallout 4 animation. |

## Command-specific switches

The catalog exposes the switches currently parsed by `symrm`:

- `frames`: `--digest`, `--strict`
- `scale`: `--strict`
- `extract`: `--tree`
- `types`: `--members`
- `defaults`: `--write`
- `mapper`: `--recompute`, `--out <file>`, `--force`, `--data <Data folder or MO2 instance>`
- `check` and `chain`: `--data`, `--plugins`
- `motion`: `--data <Data folder or MO2 instance>`, plus an optional entry substring to dump matches
- `crash` and `sweep`: `--data`, `--mods`, `--profile`
- `diff`: `--data`, `--mods`, `--profile`

`symrm scale` and `symrm frames` list and count every file they cannot decode. By default they exit
`0` when the only problems were animation classes this reader does not decode or spline sub-formats
(rotation quantization format N) it refuses, and exit `1` for a read failure or a run that decoded
nothing. Both take `--strict` to exit `1` for the unsupported and refused files as well.

Commands not listed in that section currently use positional arguments only. Use `<command> --help`
rather than relying on this summary when scripting a command; the executable help is the canonical
usage/flags/example source.

## `mapper` write-back

`symrm mapper <skeleton.hkx>` reads the file's `hkaSkeletonMapper` objects, prints them, and rebuilds the
file through the writer without editing anything. The rebuild is a real one — the write path runs and the
result is re-read and checked against the model — and it reproduces the input byte for byte.

```bash
symrm mapper skeleton.hkx                              # describe + verified rebuild, writes nothing
symrm mapper skeleton.hkx --recompute --out fixed.hkx  # recompute simple rows and keep the result
symrm mapper x --data "/path/to/Data"                  # sweep every mapper-bearing skeleton the game data resolves
```

- `--recompute` recomputes each simple row's translation and rotation from the two skeletons' reference
  poses and reports the worst error against the stored values. Both numbers are printed; the translation
  error is also given relative to the larger skeleton's extent, because a large rig measures the same
  relative accuracy in larger units. It writes nothing on its own.
- `--out <file>` writes the result. Without `--force`, `symrm` refuses an `--out` that is the file being
  read or that already exists; with `--force` it overwrites. The write goes to a temporary file beside the
  target and is then moved into place, so a failure leaves the existing file alone. Overwriting the file
  that was read keeps the version that was read as `<name>.bak` beside it, and refuses a file that changed
  on disk since it was read.
- `--data <folder>` sweeps a game `Data` folder, or an MO2 instance: its `ModOrganizer.ini` game path,
  profile and enabled mod roots resolve the Data folder and the mod archives, exactly as the app's scans
  do. It builds one `GameData` for the folder and reads every mapper-bearing `CharacterAssets` skeleton
  the game data resolves — a candidate that a higher-priority copy hides is not read twice — verifies an
  unedited forced rebuild byte for byte, then exercises an add-then-remove of a simple row. A sweep writes
  nothing anywhere, so `--out` is refused in that mode.
- Unknown switches, or `--out`/`--data` without a value, print the usage above and exit 1.

## `ragdoll` value write-back

`symrm ragdoll <skeleton.hkx>` reads the file's `hknpRagdollData` ragdolls, prints their bodies, motions,
materials, constraints and capsule shapes, and rebuilds the file through the value writer without editing
anything. The rebuild is a real one — the write path runs, the result is re-read, and every field is
checked against the model — and it reproduces the input byte for byte.

```bash
symrm ragdoll skeleton.hkx                                     # describe + verified rebuild, writes nothing
symrm ragdoll skeleton.hkx --set mass:0=5 --out edited.hkx     # retune one body and keep the result
symrm ragdoll skeleton.hkx --set limit:3=-0.5,0.5 --set friction:3=2
symrm ragdoll multi.hkx --set ragdoll=1 --set material:0=0.4,0.3 --set restitution:0=0.2
symrm ragdoll --data "/path/to/Data"                           # sweep every ragdoll the game data resolves
```

`--set <field>:<index>=<values>` may be repeated. Every number is decimal and comma-separated:

| Field | Values | What it writes |
| --- | --- | --- |
| `mass:<body>=<mass>` | one number, > 0 | the body's `inverseMass` and its `inverseInertiaLocal` scaled by the mass ratio |
| `com:<body>=<x>,<y>,<z>` | three numbers | the body's motion's `centerOfMassWorld` |
| `capsule:<shape>=<ax>,<ay>,<az>,<bx>,<by>,<bz>` | six numbers | the shape's endpoints, its derived `7` vertices and `7` planes, and its derived mass properties; the direction must not change |
| `radius:<shape>=<radius>` | one number, > 0 | `convexRadius`, the derived vertices and planes rescaled about the endpoint each vertex belongs to, and the derived mass properties |
| `material:<material>=<dynamic>[,<static>]` | one or two numbers, >= 0 | `dynamicFriction` and, when given, `staticFriction` (half precision); one value sets both, and only the lane that changed is written |
| `restitution:<material>=<value>` | one number, >= 0 | the material's `restitution` (half precision) |
| `limit:<constraint>=<min>,<max>` | two numbers in `[-pi, pi]`, ordered | a ragdoll's twist limit only, or a limited hinge's angular limit; planes are not touched |
| `cone:<constraint>=<max>` | one number in `[0, pi]` | a ragdoll's cone limit |
| `plane:<constraint>=<min>,<max>` | two numbers in `[-pi, pi]`, ordered | a ragdoll's plane limit alone |
| `friction:<constraint>=<value>` | one number, >= 0 | the constraint's `angFriction.maxFrictionTorque` |

`body`, `material` and `constraint` index the selected ragdoll's rows; `shape` indexes the file's capsule
shapes. `--set ragdoll=<i>` chooses that ragdoll for every edit that follows: it is optional when the file
holds exactly one ragdoll, and a file with more than one refuses such an edit, naming how many it holds,
until the selector is given. A material shared by more than one body is refused unless `--allow-shared-material`
is passed, because changing it changes every body that names it. A capsule shape is refused unless
`--allow-shared-shape` is passed when more than one body names it, or when it shares the same
`hkRefCountedProperties` container or `hknpShapeMassProperties` object with another capsule shape, because
resizing it would change the mass properties that shape reads.

Every capsule in the shipped game carries `7` vertices, `7` planes, `5` faces and `23` indices derived from
its endpoints and radius, and `882` of the `933` vanilla capsules also carry a stored volume, centre of mass
and inertia. `capsule:` and `radius:` re-derive all of them in place as long as the capsule's direction does
not change (within `1e-6` rad): each vertex moves to the matching endpoint plus `radius/99` times its own
offset, each plane keeps its normal and takes the `d` its own vertices support, and the volume, centre of
mass and inertia are re-encoded while the stored mass and the undecoded `majorAxisSpace` are kept. A change
of direction, a capsule whose stored arrays do not match its endpoints and radius, and a capsule more than
one body names, or whose mass properties another capsule shape shares, without `--allow-shared-shape` are
refused. A cone is written only when its atom's
`angle measurement mode` is the one the game uses (`0`; planes use `1`), and the constraint is refused
otherwise.

The writer patches only the bytes of the fields it owns and refuses any other byte change, so bodies,
shapes and constraints are never added or removed, every other object in the file is left untouched, and
the fixup tables are never reordered. `collisionFilterInfo`, `qualityId`, motion ids, body flags,
positions, orientations, local frames, constraint frames and motors are never written.

- `--out <file>` writes the result. Without `--force`, `symrm` refuses an `--out` that is the file being
  read or that already exists. With `--force` on another file it overwrites it through a temporary file
  beside the target, so a failure leaves the existing file alone. When `--out` is the file being read,
  `--force` saves through the same checked replacement the rig window uses: the version that was read is
  kept beside it as `<name>.bak` and a file that changed on disk since it was read is refused.
- `--data <folder>` sweeps a game `Data` folder, or an MO2 instance, the same way the mapper sweep does.
  It reads every `skeleton*.hkx` the game data resolves, requires an unedited forced rebuild to reproduce
  the file byte for byte, then exercises a mass edit and a limit edit and re-reads each from the produced
  bytes. A sweep writes nothing anywhere, so `--out` is refused in that mode.
- Unknown switches, an unparsable `--set`, or `--out`/`--data` without a value, print the usage and exit 1.

Re-run on the vanilla install (`7` archives): `41` files carry an `hknpRagdollData`, all `41` with at
least one body, `154` more skeleton-named files carry none, `0` unreadable. `41 of 41` unedited forced
rebuilds were byte-identical, `41 of 41` mass edits and `40 of 41` limit edits read back as edited (one
rig has no limit-bearing constraint to edit at all), `0` failures, exit 0.

## Root motion corpus

`symrm motion <animation.hkx-or-dir>` reads one clip or recursively scans loose HKX files.
`symrm motion --data "<Fallout 4 folder>\Data"` discovers game archives
through GameData and reads HKX entries under `animations/` in every `*Animations*.ba2`
through Ba2/Radicle, without extracting or writing game data. Counts are archive entries,
including duplicate virtual paths across archives, not a load-order-resolved asset count.
Texture and unrelated archives are not opened; plugins and user mod profiles do not affect the sweep.

An optional entry substring narrows the sweep to the entries whose archive path contains it:

```bash
symrm motion --data "/path/to/Data"                  # count every animation entry in the archives
symrm motion --data "/path/to/Data" TurnLeft90       # dump each matching entry
symrm motion ./Animations/TurnLeft90.hkx             # dump one loose clip
```

With a substring each match is read and dumped rather than counted: the clip's decoded sample table
(one line per 16-byte sample: `x`, `y`, `z` and the raw angle in radians, plus `up`, `forward` and the
raw total distance), then the reference frame object, its `referenceFrameSamples` array header, the
local fixup target that holds the samples inline, the 16-byte stride, and every sample's raw bytes at
that target. An animation with no `extractedMotion`, a null one, a frame class without the field and an
array header with no local fixup are each noted and skipped instead of failing the run. The sweep ends
with the number of entries that matched and how many were dumped, and exits `1` when nothing matched or
any entry could not be read.

The report counts animations carrying supported samples (including stationary samples),
clips without samples, unsupported class/frameType kinds, and read failures. Maximum distance
is the sum of sample-to-sample translation lengths, not endpoint displacement. Final-angle
range is the stored final angle in degrees, not reduced modulo 360, and a non-finite angle is
left out of it. Unsupported kinds are counted separately by class and frameType. Empty sweeps
and read failures return exit code 1. Only an `hkaDefaultAnimatedReferenceFrame` is decoded, and
only for frameType `0` (what every one of the 15,193 shipped default frames uses) and frameType
`1` (`DEFAULT`, the same payload under the other enum tag); frameType `2` (`PARAMETRIC`), any
other value and every other reference-frame class are explicitly refused and are reported as
`<class>, frameType <n>`. Positive angles turn left about up (right-handed, Y-forward, Z-up).
Single-file output also shows duration, distance, turn, and representative samples.

## `gltf` animation import

`symrm gltf <animation.gltf-or-glb> <skeleton.hkx> <template.hkx> <output.hkx> [--animation <name>]` reads
one glTF animation and writes it as a Fallout 4 animation. The glTF must hold one skin, which names the
bones. A file with several animations needs `--animation <name>`; without it they are refused and listed by
name. Animated bones match the skeleton by exact name. Nothing is guessed: an animated bone the skeleton
does not have, or a bone whose parent differs from the skeleton's, refuses the import.

```bash
symrm gltf idle.glb skeleton.hkx template.hkx idle-out.hkx
```

- The template is an animation with a compressed class (spline or lossless). Its tracks may cover a subset
  of the skeleton's bones: the template's own track-to-bone map is kept, so the paired kill (94 tracks,
  CamTarget undriven) works against the 95-bone character skeleton. An animated bone the template has no
  track for is refused, by name.
- The root bone's travel and turn are written as the template's root motion, and the root's track stays in
  place. A moving root needs a template with a root-motion frame to carry it, and is refused without one. A
  stationary root over a template without one writes no motion.
- A template's annotations (events) are cleared, and the count is printed, so its events do not fire on the
  new clip. The glTF's own `extras.annotations` (a list of `{"time": seconds, "text": name}`) are written as
  the clip's events. An event outside the clip is refused, by name.
- Channels read LINEAR, STEP and CUBICSPLINE keys. Morph-weight channels and channels on nodes that are not
  bones are ignored, and the counts are printed.
- Skeleton bones with no animation hold the skeleton's reference pose. Animated bones whose glTF rest
  pose differs from the skeleton's are listed, because their values are read as glTF local transforms.
- glTF is Y-up in metres. The import uses the Blender glTF convention: glTF `-Z` is forward. The clip
  is resampled to at least 30 frames per second.
- The output is written through the template's animation, read back, and checked against the sampled
  pose within the encoder's budget (0.01 units, 0.001 radians, 0.0005 scale). If the default rotation
  encoding misses, the rotation is re-encoded with a tighter fit tolerance, still at format 1, the format
  the committed vanilla clips use. Only if that also misses is format 2 tried. A clip that still misses
  writes nothing.
- An existing output is refused without touching it.

Print counts show the frames, animated bones, reference-pose bones, ignored joints, rest-pose
differences, ignored channels, written tracks, the root motion, the cleared and written annotations, the
root-motion replacement, the rotation format used, and the worst written error.
