> Unverified in this repository: reference copy from NifSkope Wild Wasteland Edition. See README.md.

# FO4 HKX animation WRITE contract (hkaInterleavedUncompressedAnimation)

**Status: MEASURED and GATED, 2026-09-10, lane HKX5.** `src/hkxwrite.{h,cpp}`
implements exactly this page. It is the inverse of the READ contract,
`docs/HKX_ANIMATION_FORMAT.md` (lane HKX1); everything that page settles —
the packfile container, the section table, the fixup tables, the
`hkaAnimationBinding`, the extracted motion — is not repeated here, only what a
WRITER has to decide.

**Nothing on this page has been loaded by Fallout 4.** Every claim below is
either read out of an older game build exe, read back out of a file we wrote, or
produced by HKXPACK. The one unmeasured step is the game itself; the flight is
in `scratchpad/lane_hkx5_report.md` section 8.

Authorities, as `docs/HKX_ANIMATION_FORMAT.md` uses them: **REFL** (the exe's
`hkClass` reflection), **DISASM** (the exe's own code, an older game build, checked in Todd's treat),
**XML** (HKXPACK 0.1.6-beta), **CENSUS** (the whole animations BA2), and
**RT** — read back out of a file this writer produced, by the independent
decoder `scratchpad/hkx5_20260910/interleaved_decode.py` and by HKXPACK.

---

## 1. Why interleaved, and why that is enough

Lane HKXCLASS measured the class registry of the shipped exe (skill
`ww-hkx-animation` section 9, `scratchpad/hkxclass_20260910/`):

* `hkaInterleavedUncompressedAnimation` is a **registered, fully implemented**
  class — it has reflection, `objectSize` 0x58, a vtable, a slot in the
  game's built-in class registry, and HKXPACK signature `0xa5eff3f2`. [REFL]
* The loader **dispatches on the class NAME**, never on `hkaAnimation::type`
  and never on a signature: the post-load fix-up hashes the class name
  string and calls the per-class finisher, which writes the vtable pointer
  in. A scan of the whole game code for a test of the `type` field found
  none. [DISASM]
* After load, sampling is a virtual call through the animation's vtable
  (slot 4). The interleaved and spline vtables have identical 14-slot
  layouts. [DISASM]

So a clip of this class is loaded and sampled by the same code path a shipped
spline clip is. No FO4 file uses it (CENSUS: 13,514 spline + 856 lossless, and
nothing else in 15,320 `.hkx`), which is why the game flight is still owed.

The cost is size: **48 bytes per bone per frame**, no compression. `jog.hkx`
is 12,288 bytes as a spline clip and 109,376 bytes written interleaved (95
tracks × 23 frames). A 6,149-frame clip on 95 tracks would be 28 MB. The
writer states the number in `HkxWriteReport::transformBytes`.

## 2. The object set

Six objects in `__data__`, in this order (RT: identical to what HKXPACK's
`pack` produces for the same content, and to the object order of the shipped
`jog.hkx`):

| # | class | signature | why |
|---|---|---|---|
| 1 | `hkRootLevelContainer` | `0x2772c11e` | the root; 2 named variants |
| 2 | `hkaAnimationContainer` | `0x26859f4c` | 0 skeletons, 1 animation, 1 binding |
| 3 | `hkaInterleavedUncompressedAnimation` | `0xa5eff3f2` | the clip |
| 4 | `hkaDefaultAnimatedReferenceFrame` | `0x60f8e0b8` | the extracted motion; **omitted** when the clip has none, as 715 shipped clips do |
| 5 | `hkaAnimationBinding` | `0x0faf9150` | track → bone |
| 6 | `hkMemoryResourceContainer` | `0x1de13a73` | the empty "Resource Data" variant every shipped clip carries and the Mixamo fixture does not |

`__classnames__` carries the four Havok meta classes first — `hkClass`
`0x33d42383`, `hkClassMember` `0xb0efa719`, `hkClassEnum` `0x8a3609cf`,
`hkClassEnumItem` `0xce6f8a6c` — then exactly the classes above that are
actually used, then 0xFF padding to 16. The header's
`contentsClassNameSectionOffset` is the offset of the **name** (not the
signature) of `hkRootLevelContainer` inside that section. [XML, RT]

## 3. The interleaved animation object

`objectSize` 0x58, laid out at the reflection's offsets [REFL, the class
plus its `hkaAnimation` base]:

| off | member | written |
|---|---|---|
| +0x00 | vtable, memSizeAndRefCount | 16 zero bytes (the loader fills the vtable) |
| +0x10 | `type` | **1** = `HK_INTERLEAVED_ANIMATION` |
| +0x14 | `duration` | `(numFrames - 1) * frameDuration`; the writer refuses a clip whose own `duration` differs by more than 1e-3 |
| +0x18 | `numberOfTransformTracks` | the clip's track count |
| +0x1c | `numberOfFloatTracks` | 0 (this writer writes transform tracks only, and refuses a clip with float tracks by name) |
| +0x20 | `extractedMotion` | global fixup to object 4, or null |
| +0x28 | `annotationTracks` | `hkArray<hkaAnnotationTrack>`, **always exactly `numberOfTransformTracks` entries**, `trackName` null (empty on every FO4 clip) |
| +0x38 | `transforms` | `hkArray<hkQsTransform>`, `numberOfTransformTracks * numFrames` elements |
| +0x48 | `floats` | empty |

### 3.1 THE ELEMENT ORDER IS FRAME-MAJOR — measured, not assumed

A one-track clip cannot tell `frame * tracks + track` from
`track * frames + frame`, and `scratchpad/hkxclass_20260910/interleaved.hkx`
(lane HKXCLASS's proof) has exactly one track. So the order was read out of the
engine's own per-track accessor for this class [DISASM]: it divides
`transforms.size` by `numberOfTransformTracks` to get the frame count, and
reads the element at `transforms.data + 48 * (frame * tracks + track)`.

Two facts follow, and the writer obeys both:

1. `transforms[frame * numberOfTransformTracks + track]`, stride 48.
2. **The engine derives the frame count by dividing** `transforms.size` by
   `numberOfTransformTracks` — so the array length must be an exact multiple,
   and the clip has no other place to state its frame count. The decoder
   refuses a file where it is not (gate (d), "transforms has N elements, not a
   whole multiple of the M transform tracks").

### 3.2 hkQsTransform

48 bytes: `translation` (x, y, z, **w**), `rotation` (x, y, z, w),
`scale` (x, y, z, **w**). The two padding `w` are written **0.0**, which is
what HKXPACK writes (RT: byte-compared against
`scratchpad/hkxclass_20260910/interleaved.hkx`). The quaternion is Havok's
(x, y, z, w); `HkxTransform::rotation` is NifSkope's `Quat` (w, x, y, z) and
the writer converts at the boundary, exactly as the reader does.

## 4. The binding, the motion, the annotations

**`hkaAnimationBinding`** [REFL] — `originalSkeletonName` +0x10 (the clip's
own, or overridden), `animation` +0x18 (global fixup), `transformTrackToBoneIndices`
+0x20 (**always written explicitly, one `hkInt16` per track**),
`floatTrackToFloatSlotIndices` +0x30 empty, `partitionIndices` +0x40 empty
(empty in every FO4 clip, CENSUS), `blendHint` +0x50 (`int8`: 0 NORMAL,
1 ADDITIVE_DEPRECATED, 2 ADDITIVE).

The index array is written even when it is the identity `0..n-1`, and that is
deliberate: lane FIXTURE measured that an **empty** `transformTrackToBoneIndices`
is the single reason FO4 tooling refuses the Mixamo clip. A clip that came in
with an empty binding therefore leaves this writer with a real one (gate (f)).

**`hkaDefaultAnimatedReferenceFrame`** [REFL] — `up` +0x20 and `forward` +0x30
as `hkVector4` (the fourth component 0), `duration` +0x40 equal to the
animation's, `referenceFrameSamples` +0x48 as `hkArray<hkVector4>` of exactly
`numFrames` entries, each `(tx, ty, tz, yaw-about-up-in-radians)`. Omitted
entirely — object, class name and the `extractedMotion` pointer — when the
clip has no root motion or `HkxWriteOptions::writeRootMotion` is false.

**`hkaAnnotationTrack`** is 0x18 bytes: `trackName` hkStringPtr +0 (null),
`annotations` hkArray +8; `Annotation` is 16 bytes, `time` float +0, `text`
hkStringPtr +8. One track per transform track always, because
`annotationTracks` is what the count is sized against.

## 5. Layout rules of the emitted packfile (route B)

Read off HKXPACK's own output and off the shipped `jog.hkx`, and reproduced:

* Header 0x50 bytes: magic `57 E0 E0 57 10 C0 C0 10`, `userTag` 0,
  `fileVersion` 11, layout `08 01 00 01`, `numSections` 3,
  `contentsSectionIndex` 2 / offset 0, `contentsClassNameSectionIndex` 0 /
  offset = the `hkRootLevelContainer` name, `contentsVersion`
  `"hk_2014.1.0-r1\0\xff"`, `flags` 0, `maxpredicate` 0x15,
  `predicateArraySizePlusPadding` **0x10**, then the predicate array
  `14 00 00 00` followed by twelve zero bytes. **Identical in the shipped
  `jog.hkx` and in HKXPACK's output** — both were dumped
  (`scratchpad/hkx5_20260910/dump_packfile.py`).
* Three 0x40-byte section headers at 0x50: a 20-byte tag (NUL-padded, **last
  byte 0xFF**), seven `int32` (absoluteDataStart, local, global, virtual,
  exports, imports, end), then 16 bytes of 0xFF.
  `__types__` is empty and shares `__data__`'s absolute start.
* Everything inside `__data__` — every object body, every array payload,
  every string — is aligned to **16**, and the gaps are **zero** bytes (the
  0xFF padding is only between the fixup tables and at the end of
  `__classnames__`).
* `hkArray<T>` is 16 bytes: `T*` (a local fixup, absent when the array is
  empty), `int size`, `int capacityAndFlags` = `size | 0x80000000`.
* Fixup tables follow the payload: local `(src, dst)` int32 pairs, global
  `(src, sectionIndex=2, dst)`, virtual `(objectOffset, sectionIndex=0,
  classNameOffset)`, each padded to 16 with 0xFF. `exports`, `imports` and
  `end` are all the end of the virtual table.

**CORRECTION to `docs/HKX_ANIMATION_FORMAT.md` section 1:** it says
`hkRootLevelContainer::namedVariants` has "0x20-byte entries". The stride is
**0x18** — `name` +0, `className` +8, `variant` +0x10. Measured on the shipped
`jog.hkx` and on HKXPACK's output alike: both files' local fixups run
`0x10→0x40, 0x18→0x60, 0x28→0x80, 0x30→0x90`, so the second entry's `name`
sits at +0x18 from the first's. (Private notes, 2026-09-10.)

## 6. Route A and route B agree

Route A writes HKXPACK XML and shells out to
`java -jar "<HKXPACK>/hkxpack-cli.jar" pack <in.xml> -o <out.hkx>`; route B
emits the packfile directly and needs no Java. Both produce a file of the
**same length** for the same clip (109,376 bytes for `jog`), and decoding both
gives the same clip to within the cost of route A's decimal text: over 2,185
transforms, **max |ΔT| 4.7e-10 units and max 1.10e-7 degrees** — 902 of 26,220
floats differ by about one ULP. Route B is exact (bit-identical floats to the
clip in hand) and is the default.

## 7. What the writer refuses, by name

0 or > 65535 transform tracks; fewer than 2 frames (Havok stores a one-frame
pose as two); a frame whose transform count is not the track count; a binding
whose size is not the track count; a bone index outside `hkInt16`; a
non-positive or non-finite `frameDuration`; a `duration` that is not
`(numFrames-1) * frameDuration` to 1e-3; a root-motion array that is not
`numFrames` long; any float track; an annotation list that is not per track; a
non-finite translation or scale; a rotation whose length is not 1 to 1e-3.
Route A additionally refuses a missing jar, a `java` that will not start, a
pack that exits non-zero (its stderr is quoted) and a pack that reports success
without writing a file.

## 8. Provenance

| source | sha256 (16) | bytes | role |
|---|---|---|---|
| Todd's treat: `Fallout4.exe` (an older game build) | `886d67fc955be02d` | — | REFL, DISASM; the build every claim on this page was checked on |
| `E:\Tools\Fallout 4\HKXPACK\hkxpack-cli.jar` 0.1.6-beta | `393abbbdac009624` | 2,962,934 | route A, and the layout that route B mirrors |
| `scratchpad/hkxclass_20260910/interleaved.hkx` | `4ad71f2d1d5de5d9` | 2,160 | lane HKXCLASS's one-track interleaved proof; the byte template |
| `scratchpad/hkx1_20260910/clips/jog.hkx` | `f7f74a00f23e87c6` | 12,288 | the shipped clip of round trip 1, and the vanilla layout reference |
| `fixtures/Running_To_Slide_And_Back_To_Running.hkx` | `957db497783a989f` | 40,000 | the Mixamo fixture, gate (f) |
| `src/hkxwrite.h` | `fd1a2cbb51dd1b05` | 4,334 | this contract's interface |
| `src/hkxwrite.cpp` | `13f0bf2eca5453d5` | 31,802 | this contract implemented |
| `scratchpad/hkx5_20260910/dump_packfile.py` | `fb685f5fcc97133e` | 4,197 | the structural dump behind sections 2 and 5 |
| `scratchpad/hkx5_20260910/interleaved_decode.py` | `62279896fc3deefd` | 10,688 | the independent decoder (written from this page's inputs, not from the writer) |
| `scratchpad/hkx5_20260910/make_flight_files.py` | see the report | — | the flight pair: the exact rewrite and the 45-degree head marker |
| `tests/spells/hkxwrite_gates.py` | `4ca1dc6bd7d8dac2` | 10,199 | the gate suite, 23/23 |

The DISASM claims were checked in Todd's treat (an older game build).
