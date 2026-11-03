> Unverified in this repository: reference copy from NifSkope Wild Wasteland Edition. See README.md.

# FO4 HKX animation format contract (hkaSplineCompressedAnimation, hkaAnimationBinding, hkaSkeleton)

**Status: MEASURED, 2026-09-10, lane HKX1.** Every byte offset below is traced
to one of three authorities, named in the provenance table at the end:

* **REFL** -- the Havok `hkClass` reflection arrays read out of the FO4
  an older game build exe (each class's member table; `hkClassMember`
  = 0x28 bytes: name* +0x00, class* +0x08, enum* +0x10, type u8 +0x18,
  subtype u8 +0x19, cArraySize u16 +0x1a, flags u16 +0x1c, **offset u16
  +0x1e**), via `scratchpad/hkx1_20260910/hkclass_reflect.py`. The
  reflection names every SERIALISED member and its in-memory offset, which is
  also its packfile offset (a packfile is a memory image plus fixup tables).
* **DISASM** -- the engine's own decoder in the same exe, checked in Todd's
  treat (an older game build; notes under `scratchpad/hkx1_20260910/disasm/`). This is the only authority on the
  spline block layout, which reflection does not describe (it is an opaque
  `hkArray<hkUint8> data`).
* **XML** -- HKXPACK 0.1.6-beta's unpack of real FO4 clips (paths and hashes
  in the provenance table). HKXPACK names the members the same way the
  reflection does and prints `data` as decimal bytes.
* **CENSUS** -- `scratchpad/hkx1_20260910/census.py` over every `.hkx` in
  `Fallout4 - Animations.ba2` (29,716 files, 15,320 `.hkx`), reading the
  packfile bytes directly at the REFL offsets.

A claim with no tag is arithmetic on tagged claims.

---

## 1. The container: a Havok 2014 binary packfile

A FO4 animation `.hkx` is a `hk_2014.1.0-r1` binary packfile, 64-bit
little-endian, classversion 11 -- the same container as a `bhkPhysicsSystem`
blob, with one difference that the collision walker in `src/gl/hknpdecode.cpp`
does not handle (it hardcodes 0x40):

| offset | field | value in every FO4 animation file | authority |
|---|---|---|---|
| 0x00 | magic | `57 E0 E0 57 10 C0 C0 10` | XML unpacks; hkparse.py |
| 0x08 | userTag | 0 | hexdump |
| 0x0c | fileVersion | 11 | hexdump |
| 0x10 | layout: bytesInPointer, littleEndian, reusePaddingOpt, emptyBaseClassOpt | 8, 1, 0, 1 | hexdump |
| 0x14 | numSections | 3 | hexdump |
| 0x18 | contentsSectionIndex, contentsSectionOffset | 2, 0 | hexdump |
| 0x20 | contentsClassNameSectionIndex, contentsClassNameSectionOffset | 0, 0x4b (= "hkRootLevelContainer") | hexdump |
| 0x28 | contentsVersion char[16] | `hk_2014.1.0-r1\0\xff` | hexdump |
| 0x38 | flags | 0 | hexdump |
| 0x3c | maxpredicate u16 | 0x15 | hexdump |
| 0x3e | **predicateArraySizePlusPadding u16** | **0x10** | hexdump -- the section headers start at `0x40 + this`, i.e. **0x50**, not 0x40. A collision blob has 0 here, which is why the hknp walker never noticed. |

Section headers are 0x40 bytes each from `0x40 + predicateArraySizePlusPadding`:
`char tag[19+1]`, then int32 absoluteDataStart, localFixupsOffset,
globalFixupsOffset, virtualFixupsOffset, exportsOffset, importsOffset,
endOffset (all relative to absoluteDataStart), then 16 bytes of 0xFF. The
three sections are `__classnames__`, `__types__` (empty), `__data__`.

`__classnames__` is `[u32 signature][0x09][name\0]...`; a virtual fixup's third
int is the offset of the NAME (not the signature) within the section.

Fixup tables in `__data__` (0xFF-padded to 16): **local** `(src, dst)` int32
pairs = intra-section pointer patches (every hkArray's data pointer, every
hkStringPtr); **global** `(src, sectionIndex, dst)` = pointers to other
OBJECTS (`hkaAnimationContainer::animations[i]`, `binding->animation`,
`animation->extractedMotion`); **virtual** `(objOffset, sectionIndex,
classNameOffset)` = the objects and their classes. An `hkArray<T>` member is
16 bytes: `T* data` +0 (patched by a local fixup; absent = empty), `int size`
+8, `int capacityAndFlags` +0xc (0x80000000 = do-not-free). An `hkStringPtr`
is 8 bytes, a local fixup to the NUL-terminated string.

The root: `hkRootLevelContainer::namedVariants[]` (0x20-byte entries:
`hkStringPtr name`, `hkStringPtr className`, `void* variant` global fixup).
A clip has two: "Merged Animation Container" -> `hkaAnimationContainer`, and
"Resource Data" -> `hkMemoryResourceContainer` (empty). `skeleton.hkx` has
five: the container, `hknpPhysicsSceneData`, `hknpRagdollData`, and two
`hkaSkeletonMapper`. [XML]

### hkaAnimationContainer [REFL `hkaAnimationContainerClass_Members`]

| off | member | type |
|---|---|---|
| +0x10 | skeletons | hkArray<hkaSkeleton*> (global fixups into the payload) |
| +0x20 | animations | hkArray<hkaAnimation*> |
| +0x30 | bindings | hkArray<hkaAnimationBinding*> |
| +0x40 | attachments | hkArray<hkaBoneAttachment*> |
| +0x50 | skins | hkArray<hkaMeshBinding*> |

A clip has 0 skeletons, 1 animation, 1 binding (CENSUS: 13,514 of 13,514
spline clips carry exactly one `hkaSplineCompressedAnimation`; no file carries
two). `skeleton.hkx` has 2 skeletons ("Root", 95 bones; "Ragdoll_NPC COM", 18
bones), 0 animations.

---

## 2. hkaSkeleton [REFL `hkaSkeletonClass_Members`, `hkaBoneClass_Members`]

| off | member | type | notes |
|---|---|---|---|
| +0x00 | (hkReferencedObject: vtable, memSizeAndRefCount) | 16 bytes | not serialised |
| +0x10 | name | hkStringPtr | "Root" on the player skeleton |
| +0x18 | parentIndices | hkArray<hkInt16> | -1 = root. HKXPACK prints it UNSIGNED: `65535` |
| +0x28 | bones | hkArray<hkaBone> | hkaBone = 16 bytes: `hkStringPtr name` +0, `hkBool lockTranslation` +8 |
| +0x38 | referencePose | hkArray<hkQsTransform> | 48 bytes each: translation xyz + w, rotation **xyzw**, scale xyz + w. THE BIND POSE, bone-local. |
| +0x48 | referenceFloats | hkArray<hkReal> | empty on the player |
| +0x58 | floatSlots | hkArray<hkStringPtr> | empty |
| +0x68 | localFrames | hkArray<LocalFrameOnBone> | 16 bytes: `hkLocalFrame* localFrame` +0, `hkInt16 boneIndex` +8; empty |
| +0x78 | partitions | hkArray<Partition> | 16 bytes: `hkStringPtr name` +0, `hkInt16 startBoneIndex` +8, `hkInt16 numBones` +0xa; empty on the player (the wt-fixfirst RE, `hkb-pose-apply.md` s25, saw the same null) |

This agrees with the offsets `src/gl/hknpdecode.cpp` already uses for the
ragdoll's copy (+0x18 / +0x28 / +0x38) and with the constructor read-off in
`E:\Projects\Fo4CommunityShaders\wt-fixfirst\docs\RE\hkb-pose-apply.md`
(+0x18 parentIndices, +0x28 bones, +0x38 referencePose).

The player skeleton ("Root", `Meshes\Actors\Character\CharacterAssets\skeleton.hkx`
inside `Fallout4 - Animations.ba2`, NOT in the unpacked corpus): 95 bones,
parent array `65535 0 1 2 3 4 2 6 7 1 9 10 11 12 11 14 15 16 17 18 11 20 ...`,
bone 0 "Root", 1 "COM", 2 "Pelvis", 3 "LLeg_Thigh" ... 25 "PipboyBone" ...
94 is the last. `lockTranslation` is false on every player bone. [XML]

The XML prints a reference pose row as
`(tx ty tz tw)(qx qy qz qw)(sx sy sz sw)`, e.g. COM =
`(1.09e-5 4.10e-5 68.911 0.0)(1.09e-7 -0.70711 4.29e-7 0.70711)(1.0 1.0 1.0 0.0)`.
The `w` of translation and scale is padding (0 or 1, meaningless).

---

## 3. hkaAnimation base + hkaSplineCompressedAnimation

### hkaAnimation [REFL]

| off | member | type | value in FO4 clips |
|---|---|---|---|
| +0x10 | type | hkEnum<AnimationType, int32> | 3 = `HK_SPLINE_COMPRESSED_ANIMATION` (enum: 0 UNKNOWN, 1 INTERLEAVED, 2 MIRRORED, 3 SPLINE_COMPRESSED, 4 QUANTIZED_COMPRESSED, 5 PREDICTIVE_COMPRESSED, 6 REFERENCE_POSE -- REFL) |
| +0x14 | duration | float | seconds; `= (numFrames-1) * frameDuration` (CENSUS: 0 of 13,514 differ by > 1e-3) |
| +0x18 | numberOfTransformTracks | int32 | 94 or 95 on the player (95 = with the "Camera"-side 95th bone) |
| +0x1c | numberOfFloatTracks | int32 | 0 in 13,510 of 13,514; 1 in 4 furniture/animobject clips |
| +0x20 | extractedMotion | hkaAnimatedReferenceFrame* (global fixup) | `hkaDefaultAnimatedReferenceFrame` in 12,799, null in 715 |
| +0x28 | annotationTracks | hkArray<hkaAnnotationTrack> | one per transform track; hkaAnnotationTrack = 0x18 bytes: `hkStringPtr trackName` +0 (empty on FO4), `hkArray<Annotation> annotations` +8; Annotation = 16 bytes: `float time` +0, `hkStringPtr text` +8. 43,521 annotations in 8,652 tracks, e.g. "FootLeft"/"SyncLeft" at 0.0333 s on track 0 of JogForward. |

### hkaSplineCompressedAnimation [REFL]

| off | member | type | value / law |
|---|---|---|---|
| +0x38 | numFrames | int32 | 2 (a 1-frame pose is stored as 2 frames), 23, 311, ... up to 6,149 |
| +0x3c | numBlocks | int32 | 1 in 12,877 clips; 2 in 553; up to 25 (Magnolia's songs) |
| +0x40 | maxFramesPerBlock | int32 | **256 in every FO4 clip** (CENSUS) |
| +0x44 | maskAndQuantizationSize | int32 | `= 4 * (numberOfTransformTracks + numberOfFloatTracks)` (CENSUS: 0 violations); the byte count of the mask table at the head of every block |
| +0x48 | blockDuration | float | `= (maxFramesPerBlock - 1) * frameDuration` (CENSUS: 0 violations) -- 8.5 s at 30 fps |
| +0x4c | blockInverseDuration | float | 1 / blockDuration |
| +0x50 | frameDuration | float | 1/30 s on every player clip seen; a 2-frame pose clip stores duration/1 (0.3333 for a 0.3333 s pose) |
| +0x58 | blockOffsets | hkArray<hkUint32> | one per block: offset of the block's mask table within `data` (block 0 = 0; two-block clip: `0 56016`) |
| +0x68 | floatBlockOffsets | hkArray<hkUint32> | one per block: offset of the float-track data **relative to that block's blockOffset** (`56004 12248` on the two-block clip; block 1 = 56016 + 12248 = 68264 of 68272 bytes) |
| +0x78 | transformOffsets | hkArray<hkUint32> | EMPTY in every FO4 clip (CENSUS); when present it is per (block, track) random-access offsets read by the engine's per-track sampler at `transformOffsets[block * numberOfTransformTracks + track]` [DISASM] |
| +0x88 | floatOffsets | hkArray<hkUint32> | EMPTY in every FO4 clip |
| +0x98 | data | hkArray<hkUint8> | the blocks, section 4 |
| +0xa8 | endian | int32 | 0 = little in every FO4 clip |

The track-compression enums (`RotationQuantization`, `ScalarQuantization`) [REFL]:

| RotationQuantization | value | bytes per quaternion | FO4 use (CENSUS, per track per block) |
|---|---|---|---|
| POLAR32 | 0 | 4 | 0 |
| **THREECOMP40** | **1** | **5** | **1,173,390** |
| **THREECOMP48** | **2** | **6** | **118,436** |
| THREECOMP24 | 3 | 3 | 0 |
| STRAIGHT16 | 4 | 2 | 0 |
| UNCOMPRESSED | 5 | 16 | 0 |

| ScalarQuantization | value | bytes per component | FO4 use |
|---|---|---|---|
| BITS8 | 0 | 1 | 0 (positions and scales alike) |
| **BITS16** | **1** | **2** | **1,291,826** |

So a FO4 reader needs THREECOMP40, THREECOMP48 and BITS16. The exe carries
decoders for the other five (the 16/24/32-bit quaternion unpackers and the
8-bit curve reader); they are documented as present and NOT decoded here (a reader must refuse them
by name, not guess).

---

## 4. The block layout [DISASM]

The engine's read path samples a block, then per track reads the
translation, rotation and scale curves (each specialised on its quantization:
Q = ScalarQuantization value, R = RotationQuantization value), which read the
knots, the control points and the packed quaternions, and evaluate the spline
at degree 1, 2 or 3.

### 4.1 Block addressing

The engine's block-address helper returns
`data + blockOffsets[block] + (off & 0x7fffffff)`, and the block sampler calls
it twice: with
`off = maskAndQuantizationSize` for the TRACK DATA pointer and with
`off = 0x80000000` for the MASK pointer. Hence:

```
block b:   masks = data + blockOffsets[b]                       (4 bytes x numberOfTransformTracks, then the float-track masks)
           track data = masks + maskAndQuantizationSize          (no alignment step: 376 = 4*94 on the T-pose clip, 380 = 4*95 on JogForward)
           float data = data + blockOffsets[b] + floatBlockOffsets[b]
```

### 4.2 Time -> block and local frame [DISASM]

```
lastFrame  = getNumOriginalFrames() - 1                    (= numFrames - 1)
f          = time * lastFrame / duration                   (clamped >= 0)
frame      = trunc(f);  frac = f - frame
if frame > lastFrame: frame = lastFrame; frac = 1.0
block      = clamp(frame / (maxFramesPerBlock - 1), 0, numBlocks - 1)     (unsigned div)
local      = frame - block * (maxFramesPerBlock - 1)
u_seconds  = (local + frac) * frameDuration
u_frames   = trunc(u_seconds * blockInverseDuration * (maxFramesPerBlock - 1))   (the u8 compared against the knot bytes)
```

Blocks therefore hold `maxFramesPerBlock` frames with **one frame of overlap**:
frame 255 is the last control point of block 0 and the first of block 1 (block
stride 255 frames, blockDuration = 255 * frameDuration). A per-frame decoder
evaluates frame F in block `min(F div 255, numBlocks-1)` at local
`F - 255 * block`, and a decoder that evaluates frame 255 in BOTH blocks must
get the same pose (this is gate (g) below). The knots and the spline parameter
are in the same units, so evaluating with integer local-frame knots at
`u = local` is the engine's evaluation up to float rounding (the engine
multiplies both by frameDuration).

### 4.3 The 4-byte track mask [DISASM]

```
byte 0  quantization:   bits 0-1  = ScalarQuantization for translation
                        bits 2-5  = RotationQuantization
                        bits 6-7  = ScalarQuantization for scale
byte 1  translation mask
byte 2  rotation mask
byte 3  scale mask
```

Every FO4 track has byte 0 = 0x45 (16-bit, THREECOMP40, 16-bit) or 0x49
(16-bit, THREECOMP48, 16-bit). [CENSUS]

Vector masks (translation, scale) are read PER AXIS by the curve reader: bit 0/1/2 = X/Y/Z is STATIC (one float stored), bit
4/5/6 = X/Y/Z is a SPLINE channel, neither = the default (0 for translation, 1
for scale: the scale sampler passes (1,1,1,1) as the default, the
translation sampler passes zero). A track may
mix static and spline axes (`posMask=0x34` = static Z + spline Y: 13,958 such
tracks in the CENSUS). Bits 3 and 7 are unused for vectors.

The rotation mask is read WHOLE by the quaternion curve reader:
any bit of 0xF0 set -> a quaternion SPLINE; else any bit of 0x0F set -> ONE
static packed quaternion; else identity `(0,0,0,1)` and no bytes. The
individual bits carry no meaning to the reader (the compressor sets them to
the non-identity components; FO4 masks such as 0xC3 and 0xE1 mix both nibbles
and decode as splines).

### 4.4 A transform track's bytes, in order [DISASM]

For each transform track t = 0 .. numberOfTransformTracks-1, from
`track data` onward, with `p` the running pointer:

**Translation**:
* if mask byte 1 == 0: nothing is read, the result is (0,0,0).
* else the curve is read:
  1. if `mask & 0xF0`: the knots: `u16 n` (= number of control
     points - 1; the u16 is called "numItems"), `u8 degree`, then
     `n + degree + 2` knot bytes (frame numbers within the block, 0-255)
     (`p += n + degree + 2`).
  2. **align p up to 4** -- unconditionally, spline or not.
  3. for axis in X, Y, Z: if the static bit: `float value`; else if the spline
     bit: `float min, float max`; else nothing.
  4. if any spline bit: **align p up to 2** for Q=BITS16
     (the 8-bit path also rounds to 2), then `(n + 1)` control
     points, each = one u16 (or u8) per SPLINE axis in X, Y, Z order.
     `value = min + q * (1/65535) * (max - min)`
     (1.5259022e-05 = 1/65535; the 8-bit constant is
     1/255). `p += (n + 1) * 2 * (#spline axes)`.
* then **align p up to 4**.

**Rotation** (R=1 or R=2):
* the quaternion curve:
  * if `mask & 0xF0`: the knots as above, then the packed quaternions:
    for THREECOMP48 **align p up to 2** first; for
    THREECOMP40 no alignment; then `(n + 1)` packed
    quaternions of 5 (R=1) or 6 (R=2) bytes; `p += (n + 1) * size`.
  * else if `mask & 0x0F`: one packed quaternion (THREECOMP48: align 2 first);
    `p += 5` or `6`.
  * else: identity, nothing read.
* then **align p up to 4**.

**Scale**: exactly the translation law with default (1,1,1) [mask byte 3 ==
0 -> (1,1,1), nothing read].

The T-pose clip (`Furniture\Tpose\PoseA_Idle1.hkx`, 94 tracks, 2 frames)
walks exactly like this: masks 0..375; track 1 (COM, pos 0x05 = static X and
Z, rot 0x0A = static) = floats at 376 and 380, 5 quaternion bytes at 384,
pad to 392; track 3 (LLeg_Thigh, pos 0x04, rot 0x0F) = float at 392, 5 bytes
at 396, pad to 404; ... and the walk ends at floatBlockOffsets[0] = 1440 =
the data length. [XML + this law; gate (f)]

### 4.5 The NURBS evaluation [DISASM]

The knot reader finds the knot span for the integer local frame `u` (a u8):
if `u >= knots[n+1]` span = n; else if `u <= knots[0]` span
= degree; else binary search for `k` with `knots[k] <= u < knots[k+1]`
(lo = degree, hi = n+1). Only the control points
`span-degree .. span` are read (start = `(span - degree) * stride`) and the
evaluation is de Boor's
algorithm on those `degree+1` points with the knots `span-degree+1 ..
span+degree`. FO4 clips use degree 1 (every spline seen; the T-pose and
JogForward headers read `n=22, degree=1, knots 0 0 1 2 ... 22 22`) and the
reader handles degree 1..3 as the engine does (the engine has no degree-4
path).

The quaternion spline is de Boor on the four raw components of the unpacked
control quaternions (the same evaluation), then the result is used as-is by
the engine (no normalisation in the rotation sampler; the decoder normalises before handing out a rotation,
and reports the pre-normalisation length as a diagnostic). With degree 1 this
is component-wise LERP between the two nearest control quaternions -- and at
an integer frame that is exactly one control point, so per-frame decoding
never interpolates at all on a degree-1 clip.

### 4.6 Packed quaternions [DISASM]

**THREECOMP40** (5 bytes, read as a 40-bit little-endian integer `v`):

```
a = (v      ) & 0xFFF          (lanes built from bytes {0,1}, {1,2}>>4, {3,4})
b = (v >> 12) & 0xFFF
c = (v >> 24) & 0xFFF
missing = (v >> 36) & 3        (byte 4 & 0x30)
negate  = (v >> 38) & 1        (byte 4 & 0x40)
x_i = (a - 2047) * 0.00034543566    (constants 2047.0 and 0.000345435663 = (sqrt(2)/2)/2047)
d   = sqrt(max(0, 1 - x0^2 - x1^2 - x2^2))    (engine: a fast reciprocal-sqrt approximation with a 12-bit mantissa; the decoder uses exact sqrt, the difference is < 2.5e-4 in the component and < 0.03 deg)
if negate: d = -d
result quaternion (x, y, z, w) = the three decoded values in order with `d` inserted at index `missing`
      missing=0 -> (d, a, b, c);  1 -> (a, d, b, c);  2 -> (a, b, d, c);  3 -> (a, b, c, d)
```

Byte 4's bit 7 (bit 39 of `v`) is not read by the decoder.

**THREECOMP48** (3 little-endian u16 `w0, w1, w2`):

```
a = w0 & 0x7FFF;  b = w1 & 0x7FFF;  c = w2 & 0x7FFF
missing = (w0 >> 15) | ((w1 >> 15) << 1)
negate  = w2 >> 15
x_i = (a - 16383) * 4.3161006e-05             (constants 16383.0 and 4.31610060e-05 = (sqrt(2)/2)/16383)
d as above; insertion as above
```

Quaternion component order is Havok's (x, y, z, w) throughout; NifSkope's
`Quat` is (w, x, y, z) -- the reader converts at the boundary, as
`hknpdecode.cpp` does for the reference pose.

---

## 5. hkaAnimationBinding [REFL]

| off | member | type | FO4 |
|---|---|---|---|
| +0x10 | originalSkeletonName | hkStringPtr | "Root" (12,732), "Dogmeat_Root" (495), "Base01" (102), "PipboyRoot" (14), ... |
| +0x18 | animation | hkaAnimation* (global fixup) | the clip |
| +0x20 | transformTrackToBoneIndices | hkArray<hkInt16> | track i drives bone `[i]` of the skeleton named above. Identity `0..n-1` in 13,322 clips, a permutation/subset in 192 -- the reader MUST apply it. |
| +0x30 | floatTrackToFloatSlotIndices | hkArray<hkInt16> | empty except the 4 float-track clips |
| +0x40 | partitionIndices | hkArray<hkInt16> | empty in every FO4 clip |
| +0x50 | blendHint | hkEnum<BlendHint, int8> | 0 NORMAL (13,255), 1 ADDITIVE_DEPRECATED (29), 2 ADDITIVE (230) [REFL]. An additive clip's decoded transforms are deltas, not poses; the reader carries the hint and the playback lane decides. |

---

## 6. Extracted motion: hkaDefaultAnimatedReferenceFrame [REFL, DISASM]

| off | member | type | FO4 |
|---|---|---|---|
| +0x10 | frameType | hkEnum<int8> (hkaAnimatedReferenceFrame) | SERIALIZE_IGNORED; the class name says DEFAULT |
| +0x20 | up | hkVector4 | (0, 0, 1, 0) |
| +0x30 | forward | hkVector4 | (0, 1, 0, 0) |
| +0x40 | duration | float | = the animation's duration |
| +0x48 | referenceFrameSamples | hkArray<hkVector4> | numFrames samples: `(tx, ty, tz, angle)` |

Sampling the reference frame at `time`: `i = time / duration * (numSamples - 1)`, lerp
samples `i` and `i+1` component-wise (clamped to the first/last sample);
the output transform is translation = lerped xyz,
rotation = an axis-angle quaternion about `up` by the lerped w,
scale = (1,1,1). So sample `w` is the yaw about `up` in
radians, and the root motion of frame F is sample F. JogForward's 23 samples
run 0 -> 143.6 units in Y with w = 0; the T-pose's two samples are zero.

The extracted motion is SEPARATE from track 0 ("Root"): track 0's transform
is the Root bone's own local transform (identity on every clip seen), and the
reader hands the samples out as their own array for the playback lane to apply
to the scene root, which is what bungo's "root motion separate" asks.

---

## 7. What the reader refuses by name

* Not a packfile (magic), sections missing, fixup tables truncated.
* `hkaAnimationContainer` absent, or with 0 animations.
* An animation whose class is not `hkaSplineCompressedAnimation`: FO4 ships
  **856 `hkaLosslessCompressedAnimation`** files (CENSUS) -- refused as
  "lossless-compressed animation (class hkaLosslessCompressedAnimation): not
  decoded by this reader". Their names are in
  `scratchpad/hkx1_20260910/find_clips.txt`.
* `type != 3`, `maxFramesPerBlock < 2`, `numBlocks != blockOffsets.size`,
  `maskAndQuantizationSize != 4 * (tracks + floatTracks)`, a block offset
  outside `data`, `endian != 0`.
* A quantization the reader does not implement (POLAR32, THREECOMP24,
  STRAIGHT16, UNCOMPRESSED, BITS8), by enum name.
* A track walk that leaves the block (`p > floatBlockOffsets[b]` of that block,
  or past `data`), a knot count that leaves the block, a degree outside 1..3,
  a control-point count `n+1 > 256`.
* A binding whose `transformTrackToBoneIndices.size != numberOfTransformTracks`
  or whose bone index is outside the named skeleton (when a skeleton is given).

Every refusal is a sentence naming the field and the value.

---

## 8. What HKX2 (playback / mapping) receives

`HkxAnimClip` (src/hkxanim.h): `name` (file stem), `duration`, `frameDuration`,
`numFrames`, `blendHint`, `originalSkeletonName`, per track: bone index (from
the binding), annotations; per frame per track: `HkxTransform { translation
Vector3, rotation Quat (w,x,y,z), scale Vector3 }`; `rootMotion` = one
`(Vector3, yaw)` per frame or empty; and, when the file also carries skeletons
(`skeleton.hkx`), `HkxSkeleton { name, boneNames, parents, referencePose }`.
The bone NAMES a clip's tracks map to come from the skeleton the clip was
authored against (`originalSkeletonName`), so a clip alone gives INDICES;
HKX2 needs `skeleton.hkx` (this reader returns it) to turn them into names.

**Measured against `skeleton.nif` (gate (b), `tests/spells/hkxanim_gates.py`):**
of the 95 animation bones, 78 have a NiNode of the same name in
`meshes\actors\character\CharacterAssets\skeleton.nif` -- four of them only
case-insensitively (`Head`/`HEAD`, `Spine1`/`SPINE1`, `Spine2`/`SPINE2`,
`Weapon`/`WEAPON`) -- and the 17 `Weapon*` bones (WeaponBolt, WeaponExtra1-3,
WeaponIKTarget{L,R}{,Mirror}, WeaponMagazine + Child1-5, WeaponOptics1-2,
WeaponTrigger) have NO node on the body skeleton (they live on the weapon
NIF). 55 NiNodes (`*_skin`, CamTargetParent, CharacterBumper, ...) are not
animation bones; the NIF interposes `CamTargetParent` between Root and
CamTarget. On the shared bones the reference pose and the NiNode bind pose
agree: parents by name on all 78, rotation to 2.7e-4 per matrix element --
the hkx quaternion (x,y,z,w) converted to a rotation matrix by the standard
formula equals the NiNode matrix DIRECTLY, not its transpose -- scale to
1.8e-4, translation to 1e-3 on 77 and 1.48e-3 on `Weapon`. So: match names
case-insensitively, expect partial matches, and hand the decoded `Quat` to
the NiNode as-is.

---

## 9. Provenance

| source | sha256 (16) | bytes | role |
|---|---|---|---|
| Todd's treat: `Fallout4.exe` (an older game build) | `886d67fc955be02d` | -- | REFL arrays, DISASM bodies; the build every claim above was checked on |
| `E:\Tools\Fallout 4\HKXPACK\hkxpack-cli.jar` 0.1.6-beta | `393abbbdac009624` | 2,962,934 | XML route |
| `Meshes\Actors\Character\CharacterAssets\skeleton.hkx` | `c6f795a7615cfa6d` | 43,296 | the player skeleton (from `Fallout4 - Animations.ba2`) |
| `Meshes\Actors\Character\Animations\Furniture\Tpose\PoseA_Idle1.hkx` | `07406db13fd1107a` | 7,792 | the T-pose clip, 94 tracks, 2 frames, all static |
| `Meshes\Actors\Character\Animations\MT\Neutral\JogForward.hkx` | `f7f74a00f23e87c6` | 12,288 | 95 tracks, 23 frames, splines, root motion, annotations |
| `Meshes\Actors\Character\Animations\MT\Neutral\TurnInPlaceLeft45_Fast.hkx` | `4adb7564ce5a0bad` | 14,192 | 31 frames, yaw root motion |
| `Meshes\Actors\Character\Animations\MT\Irritated\PoseA_Idle1.hkx` | -- | 79,600 | 311 frames, 2 blocks (the overlap gate) |
| `scratchpad/hkx1_20260910/hkclass_reflect.py` | -- | -- | the REFL reader; its output is reproduced in section 2, 3, 5, 6 |
| `scratchpad/hkx1_20260910/census.py`, `census_all.txt`, `census_rows.tsv` | -- | -- | CENSUS, 15,320 `.hkx`, 6 s |
| `scratchpad/hkx1_20260910/disasm/*.txt` | -- | -- | DISASM notes |
| `src/hkxanim.h` | `b606db5c8fbaf87b` | 4,692 | the reader (this contract implemented) |
| `src/hkxanim.cpp` | `dfb2347143dad6dc` | 38,319 | the reader (this contract implemented) |
| `tests/spells/hkxanim_decode.py` | `2e0011d380080899` | 27,559 | the independent decoder, written from this page |

The DISASM claims were checked in Todd's treat (an older game build).
The XML anchors are `<hkparam name="...">` element names, identical to the
reflection member names.
