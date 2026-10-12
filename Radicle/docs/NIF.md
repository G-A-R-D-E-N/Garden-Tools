# Fallout 4 NIF reader core

Scope of `fo4::nif`: header, block-type table, block sizes, string table, root list,
NiNode-family child lists, shape-to-global transforms, billboard-node mode, and
geometry for `BSTriShape`, `BSSubIndexTriShape`, `BSMeshLODTriShape` and
`BSDynamicTriShape`. Not in scope: JSON, C ABI, shader, alpha and texture
properties, skin data, collision.

Sources, the only fact sources for this reader:

- `F4LODGEN/src/core/nif.cpp@06db2894:<line>` (abbreviated `F4LODGEN`). The commit
  pins every citation, because F4LODGEN is being removed from main.
- `BGS/src/Nif/NifFile.cs:<function>` (abbreviated `NifFile.cs`).
- `BGS/src/Nif/NifShape.cs:<function>` (abbreviated `NifShape.cs`).

Facts the sources do not pin are in the Parity-pending list at the end. The code
follows the stated default, and the corpus run (`tests/test_nif_corpus.cpp`)
decides.

## Stream gate

- Header line is terminated by LF, at most 1024 bytes, and starts with
  `Gamebryo File Format` (`NifFile.cs:ParseCore` lines 58-64).
- Version `0x14020007` (20.2.0.7): `F4LODGEN@06db2894:13` and `:368-373`
  (`is_fallout4`).
- User version 12 and BS version 130: `F4LODGEN@06db2894:368-373`.
- Endian 1: `F4LODGEN@06db2894:404`; `NifFile.cs:ParseCore` lines 68-69.
- BS version floor: `NifFile.cs:ParseCore` lines 74-77 accept any value >= 130.
  This reader requires exactly 130 (see Parity-pending).

## Header, tables and root list

Order after the header line (all little-endian):

1. `version` u32, `endian` u8, `user_version` u32, `block count` u32, `bs_version` u32
   (`F4LODGEN@06db2894:390-411`; `NifFile.cs:ParseCore` lines 67-72).
2. Four export-info strings, each a u8 length then bytes
   (`F4LODGEN@06db2894:413-425`; `NifFile.cs:ParseCore` lines 79-83). Contents are
   skipped.
3. Block-type count u16, then each type as a sized string (u32 length + bytes)
   (`F4LODGEN@06db2894:427-442`; `NifFile.cs:ParseCore` lines 85-89).
4. One u16 type index per block (`F4LODGEN@06db2894:444-453`;
   `NifFile.cs:ParseCore` lines 91-97).
5. One u32 size per block (`F4LODGEN@06db2894:455-462`; `NifFile.cs:ParseCore`
   lines 99-101).
6. String count u32, maximum string length u32, then each string as a sized
   string (`F4LODGEN@06db2894:464-484`; `NifFile.cs:ParseCore` lines 103-111). A
   nonzero maximum length is enforced on each string.
7. Group count u32, then `count * 4` bytes of group table
   (`NifFile.cs:ParseCore` lines 113-114). `F4LODGEN@06db2894:490-495` reads one u32
   and ignores groups. See Parity-pending.
8. Block data: block `i` starts where block `i-1` ends
   (`F4LODGEN@06db2894:497-512`; `NifFile.cs:ParseCore` lines 116-122). The block
   sizes must sum to the data region, and the footer starts right after.
9. Footer: root count u32, then `root_count` i32 references
   (`F4LODGEN@06db2894:514-546`). Every root must be in range, and the footer must
   end exactly at the file end (`F4LODGEN@06db2894:548-551`).

String references: an i32 name of `-1` means none (empty string). Any other
negative value or an index at or past the string count is a `parse_error`.

Count checks compare `count > remaining / N` before any multiplication or reserve,
so an oversized count cannot overflow.

## NiAVObject prefix

Shared by nodes and shapes (`detail::read_av_header` in `src/nif/nif.cpp`):

- Name i32 (`F4LODGEN@06db2894:182`; `NifFile.cs:NameOf` reads the first u32 of a
  block and indexes `Strings`).
- Extra-data count u32, then that many i32 references
  (`NifShape.cs:ReadShape` lines 68-69; `F4LODGEN@06db2894:187-192`).
- Controller i32 (`NifShape.cs:ReadShape` line 70; `F4LODGEN@06db2894:194-198`).
- Flags u32 (`NifShape.cs:ReadShape` line 71). `F4LODGEN@06db2894:200-211` reads
  the same four bytes as two u16 when user version >= 11 and BS version >= 26.
- Translation: three f32 (`NifShape.cs:ReadShape` lines 73-75).
- Rotation: nine f32, 36 bytes (`NifShape.cs:ReadShape` line 76;
  `F4LODGEN@06db2894:213-216` skips the 52-byte transform).
- Scale: one f32 (`NifShape.cs:ReadShape` lines 77-78).

`Transform` is stored as `t[3]`, `r[9]` and `s` in the order read.

## NiNode family

- Collision reference i32 after the transform (`F4LODGEN@06db2894:229-233`;
  `NifShape.cs:ReadShape` line 80 skips it).
- Child count u32, then i32 references (`F4LODGEN@06db2894:235-249`).
- Effects list: absent for BS >= 130 (`F4LODGEN@06db2894:251-258`).
- `BSMultiBoundNode` trailing fields are multi-bound i32 and cull mode u32
  (`F4LODGEN@06db2894:260-268`). The reader does not read them.

Node-type allowlist (`src/nif/nif.cpp:k_node_types`): `NiNode`, `BSFadeNode`,
`BSMultiBoundNode`, `BSOrderedNode`, `BSLeafAnimNode`, `BSTreeNode`,
`NiBillboardNode`, `NiSwitchNode`, `NiLODNode`, `BSValueNode`, `BSRangeNode`,
`BSDebrisNode`, `BSBlastNode`, `BSDamageStage`, `NiSortAdjustNode`. Only
`BSMultiBoundNode` is pinned by `F4LODGEN@06db2894:166`. The rest are unverified
and listed in Parity-pending. Any other block type, including unknown `*Node`
types such as `BSFurnitureMarkerNode`, is an opaque sized block: it is skipped,
never rejected, and its children are not read.

Parent map: for each allowlisted block, every child reference becomes that block's
child, and each child takes the first parent seen (block order). Parent walks are
bounded by the block count; a walk that exceeds it is a `parse_error` (cycle
guard).

## Shapes

`BSTriShape` layout (`NifShape.cs:ReadShape` lines 62-93):

1. NiAVObject prefix (above).
2. Collision i32, then 16 bytes that are skipped (`NifShape.cs:ReadShape`
   lines 80-81).
3. Skin ref i32, then two more i32 that are read as `shader_ref` and `alpha_ref`
   (`NifShape.cs:ReadShape` lines 83-85 skip them; naming is pending).
4. Vertex descriptor u64, read as two u32 (`NifShape.cs:ReadShape` line 87;
   `NifShape.cs` uses `NifFile.U64`).
5. Triangle count u32, vertex count u16, data size u32 (`NifShape.cs:ReadShape`
   lines 88-90).
6. Vertex data (`vertex count * stride` bytes), then triangles as u16 triples
   (`NifShape.cs:ReadShape` lines 131-133).

Descriptor: stride is `(desc & 0xF) * 4` bytes; attribute flags are `desc >> 44`
(`NifShape.cs:ReadShape` lines 92-93). Flag bits (`NifShape.cs` lines 40-49):
bit 1 UV, bit 2 UV2, bit 3 normal, bit 4 tangent, bit 5 colour, bit 6 skin,
bit 8 tail slot (`HasEyeData` in `NifShape.cs` line 48; 4 bytes at the end,
meaning not pinned), bit 10 full precision.

Data size must equal `vertex count * stride + triangle count * 6`
(`NifShape.cs:ReadShape` lines 99-102). Data size 0, vertex count 0 or stride 0
yields an empty shape, not an error (`src/nif/geometry.cpp:read_shape`).
`NifShape.cs:ReadShape` line 95 returns null for the same three conditions.

Vertex attribute offsets (`NifShape.cs:Layout` lines 143-163):

- Position: 16 bytes at full precision (three f32 plus one 4-byte value, not
  decoded; settled by the vanilla corpus, parity item 15), 8 bytes at half
  precision (three binary16 values plus two bytes; `NifShape.cs:ReadShape`
  lines 118-121).
- UV, UV2, normal, tangent and colour: 4 bytes each, in that order after the
  position (`NifShape.cs:Layout` lines 147-152).
- Skin weights 8 bytes and indices 4 bytes, after the colour
  (`NifShape.cs:Layout` lines 154-155). Skin data is not decoded.
- Tail slot (bit 8): 4 bytes after the skin block, at the end of the vertex
  (byte 36 in the vanilla stride-40 layout, flags `0x17b`; the descriptor's
  offset nibble at bits 36-39 says 9, which is 36 bytes). Not decoded. Its
  meaning is not pinned. In vanilla, the FaceGen FaceGeom heads and
  `maleeyes.nif` hold zero there and `maleeyes_facebones.nif` holds 1.0f.

The attribute bytes must equal the stride exactly
(`src/nif/geometry.cpp:read_shape`). `NifShape.cs:Layout` applies the same
check to skinned shapes only.
Each triangle index must be less than the vertex count
(`NifShape.cs:ReadShape` lines 135-137).

Binary16 decode: `NifFile.cs:Half` uses `BitConverter.ToHalf`. This reader has
its own decoder (`src/nif/geometry.cpp:half_to_float`) covering zero, subnormals,
infinity and NaN.

## Transforms and billboards

- Shape-to-global composes the local transforms of the shape and its ancestors
  bottom-up. Root nodes are excluded, as the brief requires
  (`src/nif/nif.cpp:shape_to_global`). Composition: with `P` the parent and `A` the
  accumulated child transform, `t = P.s * (P.R * A.t) + P.t`, `s = P.s * A.s`,
  `R = P.R * A.R`. Scale is uniform (`NifShape.cs:ReadShape` line 77 reads one
  f32). The rotation storage order and composition are parity-pending.
- Billboard mode: the nearest ancestor of type `NiBillboardNode` gives the mode,
  read as a u16 right after its children list (`src/nif/nif.cpp:billboard_mode`).
  Returns -1 when there is none. The layout is parity-pending.

## Error policy

- Every read goes through `fo4::binary::Cursor`. Errors throw `fo4::parse_error`.
- Limits: file <= 512 MiB; blocks <= 1,000,000; strings <= 100,000; string
  length <= 1 MiB. Counts are checked as `count > remaining / N` before any reserve
  or multiplication.
- Block-local cursors: each allowlisted node or shape parses only inside its own
  block, so a wrong count cannot read into a neighbour block.
- `shape_to_global` and `billboard_mode` throw `std::out_of_range` for a block
  index past the end (the `.at()` accessors).

## Deliberate omissions

- No JSON, no C ABI, no shader or alpha or texture property decoding, no skin
  decoding, no collision.
- Trailing bytes inside a block are not validated.
- Shader, alpha and skin references are stored, not followed.
- UV2 values are read past and discarded; `Shape` has no field for them.
- The `bitangents` vector is always empty (see Parity-pending).

## Parity-pending

Not pinned by the sources. The code follows the default in each case; the corpus
run (`tests/test_nif_corpus.cpp`) and a later RE or CK check decide.

1. Rotation storage order: row-major 3x3 applied to column vectors
   (`p' = s * R * p + t`). Default used. Sources read and skip the nine floats.
2. Composition convention for `shape_to_global` (see above). Default used.
3. Node-type allowlist (`src/nif/nif.cpp:k_node_types`): `NiNode` and the other
   fourteen names. Only `BSMultiBoundNode` is pinned, by `F4LODGEN@06db2894:166`.
   `NiNode` is not pinned by any source; its prefix is assumed to match the
   `BSMultiBoundNode` prefix. The other thirteen names are unverified too. The
   corpus decides. Unknown `*Node` types such as `BSFurnitureMarkerNode` are skipped
   as opaque sized blocks, not rejected.
4. Billboard: the type name `NiBillboardNode` and a u16 mode after the children
   list. No source covers billboards.
5. Null (-1) child references are skipped. `F4LODGEN@06db2894:286-291` rejects them.
6. `BSDynamicTriShape`: no source pins it. It is read with the `BSTriShape` layout.
   Any type-specific trailing fields are not read.
7. `BSSubIndexTriShape` and `BSMeshLODTriShape`: `NifShape.cs:Shapes` reads them
   with the `BSTriShape` layout. Per-type trailing fields are not read.
8. Meaning of `shader_ref` and `alpha_ref` (the two refs after skin), and of the 16
   bytes after the collision ref. `NifShape.cs` skips them.
9. Meaning of the vertex-descriptor flag bits beyond their positions. Only the bit
   positions are pinned.
10. Normal and tangent encoding: three bytes mapped to `b / 255 * 2 - 1`, fourth byte
    unread. Not pinned; `NifShape.cs` does not decode them.
11. Colour encoding: four bytes mapped to `b / 255`. Not pinned.
12. UV encoding: two binary16 values, as the 4-byte width suggests. Not pinned.
13. Two bytes after a half-precision position are skipped. Their meaning (the
    bitangent X slot in common Gamebryo layouts) is not pinned.
14. Bitangents: left empty. The packing is not pinned.
15. Full-precision position slot width: 16 bytes. Settled by the vanilla corpus:
    descriptor 0x0041b00000650407 places UV at byte 16 (stride 28); decoding UV
    at 12 gives invalid UVs. `NifShape.cs:Layout` uses 16 as well.
16. Attribute bytes must equal the stride exactly. The reader rejects any stride
    that is larger than the attributes (padding), so the corpus decides whether
    padding occurs.
17. Group table: `NifFile.cs:ParseCore` skips `count * 4` bytes, while
    `F4LODGEN@06db2894:490-495` reads one u32 and ignores groups. Groups are assumed
    to be the skipped table.
18. Strict gate: only version 20.2.0.7, user version 12 and BS 130 are read
    (`F4LODGEN@06db2894:368-373`; `:420` for the FO4 fourth export string).
    `NifFile.cs:ParseCore` accepts any BS >= 130. The corpus run will show if any
    vanilla file differs.
19. Footer: trailing bytes after the root table are rejected
    (`F4LODGEN@06db2894:548-551`); `NifFile.cs:ParseCore` only requires four bytes.
    Rejection is the default.
20. Data size 0, vertex count 0 or stride 0 yields an empty shape.
    `NifShape.cs:ReadShape` line 95 returns null for these three conditions instead.
21. Position is read regardless of the `HasVertex` flag bit (`NifShape.cs:ReadShape`
    line 118 does the same).
22. Four export-info strings are skipped without validation
    (`F4LODGEN@06db2894:413-425`; `NifFile.cs:ParseCore` lines 79-83).
23. Shape name is read from the NiAVObject name (`NifShape.cs:ReadShape` line 106).
