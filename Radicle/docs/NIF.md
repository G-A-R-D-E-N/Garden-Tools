# Fallout 4 NIF reader core

Scope of `fo4::nif`: header, block-type table, block sizes, string table, root list,
NiNode-family child lists, shape-to-global transforms, billboard-node mode, and
geometry for `BSTriShape`, `BSSubIndexTriShape`, `BSMeshLODTriShape` and
`BSDynamicTriShape`. A shape also decodes its bitangents, its skin weights and bone
indices, its bones with their bind transforms, its `BSLightingShaderProperty`
+material path and its `BSShaderTextureSet` slots. `include/fo4/nif/shader.hpp` adds
the whole `BSLightingShaderProperty` field set with its per material-type tail, the
`BSEffectShaderProperty` field set and `NiAlphaProperty`; see the shader sections
below. Not in scope: JSON, collision decoding, and the fragment and water shader
property types.
`include/fo4/nif/collision.hpp` adds the raw collision payloads with the
`bhkNPCollisionObject` that owns each system, that object's flags, its system
reference, Body ID and target node; see
`docs/code-notes/Radicle.md`, `read_collision_blobs`.
`include/fo4/c_api_nif.h` exposes all of it to C, and
`bindings/csharp/Radicle.Interop/NifFile.cs` to C#.

Sources, the only fact sources for this reader:

- `F4LODGEN/src/core/nif.cpp@06db2894:<line>` (abbreviated `F4LODGEN`). The commit
  pins every citation, because F4LODGEN is being removed from main.
- `BGS/src/Nif/NifFile.cs:<function>` (abbreviated `NifFile.cs`).
- `BGS/src/Nif/NifShape.cs:<function>` (abbreviated `NifShape.cs`).
- `nif.xml` from [niftools/nifxml](https://github.com/niftools/nifxml), the `develop`
  and `master` revisions (abbreviated `nif.xml`). Object, struct and field names are
  cited as written there. `#BS_FO4#` is `#BSVER# == 130` and `#BS_GTE_130#` holds at
  130, so both are true for this reader.
- The installed vanilla data, measured by `tests/test_nif_corpus.cpp`; the counts
  quoted below are that run.
- `PluginBrreakdown@5a58943:<map file>:<address>`, the Elric, Max and Fallout4
  function maps in `2026-10-30-nif-pluginbreakdown-extract.md` (abbreviated
  `PluginBrreakdown`). Its offsets such as `+0x30` are in-memory object offsets, not
  file offsets, so only the order of its reads and its version checks are used. Names
  it marks with a trailing `?` are guesses and are not treated as facts.
- `G-A-R-D-E-N/commonlibf4@22b0dbd:<path>` (abbreviated `commonlibf4`). Nomad owns it
  and approved copying its enums, flag tables, structs and bit helpers into Radicle
  (2026-10-30); credit is in `THIRD_PARTY_NOTICES`. Only the file-side parts are
  taken: field order, enum values and bit meanings. Its runtime parts — `REL::`
  relocations, vtables, RTTI ids, in-memory pointers and padding — are dropped,
  because this reader reads files and never game memory. Its offsets are in-memory
  unless a struct is itself a serialised Havok layout.

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
- `BSValueNode` trailing fields, after the children list: an i32 value, then one
  flags byte. All 1,766 base-game and DLC blocks have exactly those five bytes. The reader keeps the
  i32 as the node's `value` when at least four bytes remain, so a shorter block has no
  value and is not a parse error. The value equals the first number in the block's name, and 1,762 of the 1,766 base-game and DLC values match an ADDN DATA index in the seven masters. The flags byte is not read; its meaning is unknown.

Node-type allowlist (`src/nif/nif.cpp:k_node_types`): `NiNode`, `BSFadeNode`,
`BSMultiBoundNode`, `BSOrderedNode`, `BSLeafAnimNode`, `BSTreeNode`,
`NiBillboardNode`, `NiSwitchNode`, `NiLODNode`, `BSValueNode`, `BSRangeNode`,
`BSDebrisNode`, `BSBlastNode`, `BSDamageStage`, `NiSortAdjustNode`. Only
`BSMultiBoundNode` is pinned by `F4LODGEN@06db2894:166`. `BSValueNode` is settled
by the shipped-data scan in the code notes. The other thirteen are unverified and
listed in Parity-pending. Any other block type, including unknown `*Node`
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
bit 7 land data (`HasLandData` in `NifShape.cs` line 47; no vertex bytes, because
`NifShape.cs:Layout` lines 143-162 leaves it out of the stride),
bit 8 tail slot (`HasEyeData` in `NifShape.cs` line 48; 4 bytes at the end,
meaning not pinned), bit 10 full precision.

Data size must equal `vertex count * stride + triangle count * 6`
(`NifShape.cs:ReadShape` lines 99-102). Data size 0, vertex count 0 or stride 0
yields an empty shape, not an error (`src/nif/geometry.cpp:read_shape`).
`NifShape.cs:ReadShape` line 95 returns null for the same three conditions.

Vertex attribute offsets (`NifShape.cs:Layout` lines 143-162):

- Position: 16 bytes at full precision (three f32 plus one 4-byte value, not
  decoded; settled by the vanilla corpus, parity item 15), 8 bytes at half
  precision (three binary16 values plus two bytes; `NifShape.cs:ReadShape`
  lines 118-121).
- UV, UV2, normal, tangent and colour: 4 bytes each, in that order after the
  position (`NifShape.cs:Layout` lines 147-152).
- Skin weights 8 bytes and indices 4 bytes, after the colour
  (`NifShape.cs:Layout` lines 154-155). The weights and bone indices are decoded by
  `src/nif/geometry.cpp:read_vertex`; the bone list and bind transforms are read by
  `src/nif/skin.cpp:read_bones` (see Skin below).
- Tail slot (bit 8): 4 bytes after the skin block, at the end of the vertex
  (byte 36 in the vanilla stride-40 layout, flags `0x17b`; the descriptor's
  offset nibble at bits 36-39 says 9, which is 36 bytes). Not decoded. Its
  meaning is not pinned. In vanilla, the FaceGen FaceGeom heads and
  `maleeyes.nif` hold zero there and `maleeyes_facebones.nif` holds 1.0f.

The attribute bytes must equal the stride exactly
(`src/nif/geometry.cpp:read_shape`). `NifShape.cs:Layout` applies the same
check to skinned shapes only. Both that check and
`src/nif/verify.cpp:descriptor_fault` measure the attributes with
`src/nif/vertex_desc.cpp:vertex_bytes_for`, so the reader and the verifier
cannot disagree about a flag's width.
Each triangle index must be less than the vertex count
(`NifShape.cs:ReadShape` lines 135-137).

Binary16 decode: `NifFile.cs:Half` uses `BitConverter.ToHalf`. This reader has
its own decoder (`src/nif/geometry.cpp:half_to_float`) covering zero, subnormals,
infinity and NaN.

## Vertex attributes, bitangents and skin weights

`nif.xml:BSVertexData` covers `#SSE# #FO4# #F76#`, so it is the FO4 layout, and
`src/nif/geometry.cpp:read_vertex` follows its order:

1. Position: three floats at full precision (descriptor bit 10), or three binary16
   at half precision.
2. Bitangent X: one float at full precision, one `hfloat` at half precision. This is
   the slot the reader used to skip.
3. UV: two binary16 (`HalfTexCoord`), gated by bit 1.
4. Normal: `ByteVector3`, decoded as `b / 255 * 2 - 1`, gated by bit 3.
5. Bitangent Y: one `normbyte`, decoded the same way, present with the normal.
6. Tangent: `ByteVector3`, which `nif.xml` reads only when bits 3 and 4
   (`0x18 == 0x18`) are both set.
7. Bitangent Z: one `normbyte`, present with the tangent.
8. Vertex colours: `ByteColor4`, decoded as `b / 255`, gated by bit 5.
9. Bone weights: four binary16, then four bone indices, both gated by bit 6.
10. Eye data: one float, gated by bit 8. Still read past.

The bitangent is stored, not reconstructed: every component is on disk.
`Shape::bitangents` holds three floats per vertex when bits 3 and 4 are both set
and is empty otherwise, so it never holds a partial vertex.

Checked against 8,737 vanilla vertices that carry normals and tangents: the mean
decoded bitangent length is 0.99987, the mean `|normal . bitangent|` is 0.0045,
and `bitangent . (normal x tangent)` is 1.00026, positive for 8,290 of them. The
447 with a negative product are degenerate normal and tangent pairs.

`Shape::weights` holds four floats per vertex and `Shape::bone_indices` four bytes
per vertex, both empty when bit 6 is clear. Corpus: 23,595 shapes are skinned and
every one holds exactly `4 * vertex count` of each; every one of the 169,498 bone
indices found addresses a bone inside its shape's list.

## Skin

`nif.xml:BSSkin::Instance` is a skeleton-root pointer, a `BSSkin::BoneData`
reference, a bone count, that many `NiNode` pointers, then a scale count and
`Vector3` scales the reader does not read. A bone's name is the referenced node's
NiAVObject name, or empty when the reference or its string index does not resolve,
which is what `NifFile.cs:NameOf` does.

`nif.xml:BSSkin::BoneData` is a bone count and then one `nif.xml:BSSkinBoneTrans`
per bone, 68 bytes each: a `NiBound` (three floats and a radius), a `Matrix33`
rotation, a `Vector3` translation and a scale float, in that order.
`NifShape.cs:ReadSkin` reads the same fields in the same order and builds the same
matrix, so a bone's bind transform is the stored rotation scaled by the stored
scale with the translation in the fourth row. When the data reference is absent or
names another block type, the reader keeps the names and leaves the transforms at
their defaults, as that method does. A count that differs from the bone count is a
`parse_error` (`src/nif/skin.cpp:read_bones`).

Cross-checked against `NifShape.cs` outside this repository on four vanilla meshes
(`MaleBody.nif`, `FemaleBody.nif`, `1stPersonFemaleBody.nif`, `RadRoach.nif`): all
eight skinned shapes agree on the skinned verdict, every bone name, every bind
matrix, every weight and every bone index.

## Shader property and texture set

`nif.xml` does not fully pin `BSLightingShaderProperty` at BS 130. Its `stopcond`
names a `Name` field the revision never declares, and its shader-type field is
gated on a `Shader Type` field that `nif.xml` gives NiObjectNET only below BS 130.
The order below is what the vanilla data shows, and it fits every one of the
1,111,752 shapes in the corpus that carry a material; every one of those names ends
in `.bgsm`:

1. `Shader Type` u32, then `Name`, an i32 string-table index, then the `NiObjectNET`
   extra-data list and controller in `nif.xml` order.
2. `Shader Flags 1` u32 and `Shader Flags 2` u32.
3. UV offset and UV scale, two floats each.
4. Texture set, an i32 `BSShaderTextureSet` reference.
5. Emissive colour (three floats), emissive multiple, root material (an i32 string
   index), texture clamp mode, alpha, refraction strength, smoothness, specular
   colour (three floats), specular strength, subsurface rolloff, rimlight power,
   backlight power (present when rimlight power is `FLT_MAX`), grayscale to palette
   scale, fresnel power, and the six-float wetness block.
6. A shader-type tail: at type 1 an environment map scale and two bytes, at type 5 a
   three-float skin tint colour and a float, at type 6 a three-float hair tint colour.

`read_shader` stops after the texture set reference, which is all `Shape` needs. The
full field set is read by `src/nif/shader_lighting.cpp:read_lighting_shader`, whose
order the next section pins.

`NifShape.cs` reads neither of these blocks; the material path in step 1 is the
`Name` of the shader property, which is the BGSM or BGEM when the shape uses a
material file and empty when it does not.

`nif.xml:BSShaderTextureSet` is a slot count and then one sized string per slot,
empty for an unused slot. Corpus: 1,152,037 shapes carry a texture set, 11,520,370
slots in total, of which 3,740,219 are non-empty - 3,740,160 ending in `.dds`, 53 in
`.tga`, 2 in `.dd` and 4 with no extension. Those are vanilla data, passed through
unchanged.

## Lighting shader fields

`nif.xml:BSLightingShaderProperty` is the FO4 field order, and the extract confirms it
read by read. `src/nif/shader_lighting.cpp:read_lighting_shader` reads it in this
order and records the offset of every field in `LightingFieldOffsets`, so the writer
can edit a field in place without re-deriving the layout. The material type ids are
named from `commonlibf4`'s `BSShaderMaterial::Feature` (0 default through 20
dismemberment), which agrees with `nif.xml`'s `BSLightingShaderType` on every number;
the two only word id 14 differently, `Sparkle Snow` against multi-index snow, and both
describe the same four floats the block stores there.

1. `Shader Type` u32, the material type id; then the `NiObjectNET` prefix (name, extra
data list, controller).
2. `Shader Flags 1` u32 and `Shader Flags 2` u32 - one 64-bit flag word
   (`PluginBrreakdown@5a58943:Elric_BSShader/BSShader.dll function map.txt:0x10001c3a`: the
   engine writes a 64-bit flag word; the extract's `GetFlagString` answers below 0x40,
   so bit 32 is the first bit of word 2). Word 2 is present only from stream version
   0x45 (`PluginBrreakdown@5a58943:Max_BGS_Libs_BSShader/BSShader.dll function map.txt:0x1800046d8`).
3. `UV Offset` and `UV Scale`, two floats each, for stream versions above 0x2b
   (`PluginBrreakdown@5a58943:Elric_BSShader/BSShader.dll function map.txt:0x10002284`).
4. `Texture Set` i32 reference, then `Emissive Color` (three floats) and
   `Emissive Multiple`.
5. `Root Material`, a string-table index, when the stream version is above 0x7c
   (`PluginBrreakdown@5a58943:FO4_Fallout4/Fallout4.exe function map.txt:0x142178bf0`).
6. `Texture Clamp Mode` u32, `Alpha`, `Refraction Strength`, `Smoothness` (the engine
   clamps it to 0-1, which is nif.xml's range). FO4 has no `Glossiness` field: nif.xml
   gates it on `#NI_BS_LT_FO4#`.
7. `Specular Color` (three floats), `Specular Strength`, `Subsurface Rolloff`,
   `Rimlight Power`, then `Backlight Power` only when `Rimlight Power` is the `FLT_MAX`
   placeholder (`PluginBrreakdown@5a58943:FO4_Fallout4/Fallout4.exe function map.txt:0x1421ce880`
   reads the next float only when the previous one equals `FLT_MAX`).
8. `Grayscale to Palette Scale` for stream versions above 0x79, `Fresnel Power`, and
   `Wetness`, six floats in `nif.xml:BSSPWetnessParams` order for stream versions
   above 0x7b (`PluginBrreakdown@5a58943:FO4_Fallout4/Fallout4.exe function map.txt:0x1421ce630`
   writes six).
9. The material tail for the type id, below.

FO4 is stream version 130 (0x82), so every gate above (0x2b, 0x45, 0x79, 0x7b, 0x7c)
takes its present branch; the gates are still coded because they change the offsets.

Material type ids and their tails (`src/nif/shader_lighting.cpp:read_tail`):

| id | nif.xml class (extract name) | tail |
|---|---|---|
| 0 | default | none |
| 1 | EnvMap | `Environment Map Scale` float, plus two bytes from version 0x7c |
| 5 | SkinTint | three-float tint and its alpha float |
| 6 | HairTint | three-float tint |
| 7 | ParallaxOcc | `Max Passes` and `Scale` |
| 11 | MultiLayerParallax | four floats, plus a fifth above version 0x4a |
| 14 | Snow | four floats (`Sparkle Parameters`, CK calls it snow) |
| 16 | Eye | cubemap scale, left and right reflection centres |

The ids are `nif.xml`'s shader-type conditions; 1, 5 and 6 also match the extract's
read (`...:0x1800016d1` picks the lighting material class from the type id, and
`...:0x1421cf7c0` / `0x1421d1980` / `0x1421d1b80` read the envmap, skin tint and hair
tint tails). 7, 11, 14 and 16 rest on `nif.xml` alone, so the type ids are parity
items 27 and 28.

Flag names: `src/nif/shader.cpp:shader_flag_name` holds 64 names in nif.xml bit
order, taken from `nif.xml:Fallout4ShaderPropertyFlags1` and
`nif.xml:Fallout4ShaderPropertyFlags2`. The extract's table starts at `specular` for
index 0 and stops below 0x40
(`PluginBrreakdown@5a58943:Max_BGS_Libs_BSShader/BSShader.dll function map.txt:0x180006ce4`),
so the bit order agrees; the reader keeps nif.xml's capitalisation rather than the
engine's, because nif.xml is where the names are written down.

## Effect shader property

`src/nif/shader_effect.cpp:read_effect_shader` reads
`nif.xml:BSEffectShaderProperty` in field order: the `NiObjectNET` prefix, the two flag
words, UV offset and scale, `Source Texture` (a sized string), the four clamp bytes
(`Texture Clamp Mode`, `Lighting Influence`, `Env Map Min LOD`, `Unused Byte`), the
four falloff floats, `Base Color`, `Base Color Scale`, `Soft Falloff Depth`, then the
greyscale texture, and from BS 130 the environment map, normal and environment mask
textures and `Environment Map Scale`. That is five texture paths, which is the five
slots the extract loads
(`PluginBrreakdown@5a58943:Elric_BSShader/BSShader.dll function map.txt:0x10002991`),
and `EffectShader::textures` holds them in that order, slot 0 being the source
path the field also keeps separately.

## Alpha property

`src/nif/shader.cpp:read_alpha` reads `nif.xml:NiAlphaProperty`: the `NiObjectNET`
prefix, then `Flags` as a u16 and `Threshold` as one byte
(`PluginBrreakdown@5a58943:FO4_Fallout4/Fallout4.exe function map.txt:0x1416cf580`, the
same two fields in the same order). `AlphaProperty::flags_at`, `threshold_at` and
`end` give the writer the three offsets it edits. `AlphaProperty::name` is the
`NiObjectNET` name, read for display only.

## Describe layer

`src/nif/describe.cpp`, `src/nif/verify.cpp` and `src/nif/tree.cpp` hold what a caller
reports; each returns plain structs from `include/fo4/nif/describe.hpp` and formats
nothing itself.

- `inspect_nif` gives the shapes as counts, the material path, the texture slots and
  whether tangents exist. `has_bsx` and `has_collision` are block-type prefixes, so
  they follow the block table and not the graph.
- `geometry_nif` carries the vertex, index, normal and UV data itself, the non-empty
  texture slots with their index, and the billboard mode from `billboard_mode`.
- `verify_nif` returns the checks in a fixed order, because a caller prints them in
  that order: `header parses`, `each block is listed under one parent only`,
  `no editor markers`, `shapes parse`, `vertex descriptor matches the data`,
  `shapes have vertices and triangles`, `normals match the vertex count`,
  `UVs match the vertex count`, `shader and skin reads are clean`,
  `texture paths are relative`. The header check is the caller's: a file that does not
  parse never reaches `verify_nif`. `shapes parse` names the block and its fault
  rather than failing the whole report, so a broken shape still leaves the other
  checks meaningful.
- `vertex descriptor matches the data` reads each shape's `BSVertexDesc`
  (`src/nif/vertex_desc.cpp:read_vertex_desc`) and requires the stored attribute
  offsets to be the cumulative attribute widths and the size nibble to cover exactly
  those widths. `nif.xml:BSVertexDesc` puts attribute N's offset in bits 4N+4, four
  bits holding the byte offset over four, so attribute 0's field is `Dynamic Vertex
  Size` and is not an offset: the position slot is always first, at 8 bytes or 16
  when `0x400` is set, and the check starts measuring from its width. The reader takes
  the attribute widths from the flag word (`src/nif/tree.cpp` and
  `src/nif/vertex_desc.cpp:vertex_bytes_for`, the one function this check and the
  reader both measure with), and `nif.xml:BSVertexData` is what says
  the binormal and tangent take four bytes each.
- `no editor markers` fails when a `BSXFlags` block has bit 5 set
  (`nif.xml:BSXFlags`; `PluginBrreakdown@5a58943:include/RE/B/BSXFlags.h`), the bit
  the engine's `is_editor_marker` reads. `BSXFlags` is a `NiIntegerExtraData`, so
  the block is a u32 name index and then the u32 flag word. This is the only one of
  the four `OptimizeOptions` passes the sources pin; the other three are in
  Parity-pending.

`describe_nif` returns one entry per block, in block order, each with a `group`, a
`cat` and the fields that block owns. Every block appears: a node from
`NifFile::nodes()`, a shape from `read_shape`, a `bhk*` block with its collision
fields, a shader, alpha, texture-set, skin or `BSXFlags` block with its own readers,
and anything else with nothing but its size. A block whose reader throws keeps its
entry and carries a `readError` field with the fault, so one bad block cannot empty
the tree.

The groups are `Nodes`, `Shapes`, `Collision` and `Extra`. Those four are the editor's
fixed list, so a block with no group of its own (a shader, a texture set, an alpha
property) is in `Extra` and is told apart by its `cat`. Field keys are stable:
the writer accepts the same key for the same block, which is why the per-type shader
fields keep `nif.xml`'s names (`Max Passes` is `parallaxMaxPasses`, `Sparkle
Parameters` splits into `sparkleParameters` and `sparkleW` because the editor has no
four-component widget). A shape also reports its vertex descriptor as `vertexSize`,
`vertexAttributes` and `fullPrecision`, which is the decoded `BSVertexDesc` flag word
rather than the raw `uint64`; an alpha property reports the decoded blend and test
names; and `BSXFlags` reports `flags` and `editorMarker` from
`src/nif/bsx.cpp:is_editor_marker`.

The flags word is decoded once, into `AlphaProperty::blend_enabled` (bit 0),
`source_blend` (bits 1-4), `destination_blend` (bits 5-8), `test_enabled` (bit 9) and
`test_function` (bits 10-12, the position from `nif.xml`) with the
`AlphaBlendMode` and `AlphaTestFunction` enums
(`commonlibf4`, `include/RE/N/NiAlphaProperty.h`: the blend values 0 one through 10
source-alpha-saturate, the test values 0 always through 7 never, and the 0xEC default
word). `alpha_blend_name` and `alpha_test_name` give an editor the labels; both answer
`Unknown` past the last value rather than indexing the table. The C ABI carries the
same five decoded values in `radicle_nif_alpha_property`.

## Editor markers

`src/nif/bsx.cpp:read_bsx_flags` reads a `BSXFlags` block as a u32 name index and then
the u32 flag word, because the type inherits `NiIntegerExtraData`
(`nif.xml:BSXFlags`). `is_editor_marker` tests bit 5, the `bEditorMarker` bit
(`nif.xml:BSXFlags`; `commonlibf4`, `include/RE/B/BSXFlags.h`; the same bit the engine
reports as `is_editor_marker` in `PluginBrreakdown@5a58943:include/RE/B/BSXFlags.h`).
Bit 5 is also the one `OptimizeOptions` pass these sources pin; the describe layer
below turns it into a verify check.

`fix_nif` clears bit 5 on every `BSXFlags` block (`src/nif/fix_nif.cpp:clear_editor_marker`) and keeps the other bits, so a repaired file passes that check.

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

- No JSON: the describe layer returns structs and every caller formats its own output.
- `BSLightingShaderProperty`, `BSEffectShaderProperty` and `NiAlphaProperty` are read;
  the fragment and water shader property types are stored as references and read for
  nothing.
- Trailing bytes inside a block are not validated: `read_shader`, which a shape uses,
  stops after the texture set reference and never looks at what follows. The full
  readers record where they stop (`LightingFieldOffsets::end`, `EffectFieldOffsets::end`,
  `AlphaProperty::end`) so a caller can check it; `tests/test_nif_corpus.cpp` requires
  every vanilla shader block to end exactly at its declared size.
- `Shape::alpha_ref` stores the alpha property reference; a shape does not read it.
- UV2 values are read past and discarded; `Shape` has no field for them.
- The vertex tail slot (bit 8, `Eye Data` in `nif.xml`) is read past.

## Parity-pending

Not pinned by the sources when the item was written. The code follows the default in each
case; the corpus run (`tests/test_nif_corpus.cpp`) and a later RE or CK check decide.
Numbers do not move: an item that has since been settled says so and stays for the record.

1. Rotation storage order: row-major 3x3 applied to column vectors
   (`p' = s * R * p + t`). Default used. Sources read and skip the nine floats.
   SETTLED against vanilla data: composing `skeleton.nif` with this reading and
   comparing it with the `skeleton.hkx` reference pose (composed with the same
   row-vector `Matrix4x4.CreateScale * CreateFromQuaternion * CreateTranslation`
   product BGS's `AnimationPose.ReferencePose` uses, an independent oracle that
   agrees to 1e-4) gives a maximum position error of 0.0063 units and a mean of
   0.0016 over the 74 HKX bones with a node name in the NIF, with the identity
   axis mapping as the best of all 48 signed permutations. The row-vector reading
   of the same floats gives a maximum error of 138.4 over the same pairs, so the
   stored matrix is applied as `R * p`. 49 of the 74 agree within 1e-3; the rest
   differ only in the last decimals of a float, and 8 of the 74 are exact.
2. Composition convention for `shape_to_global` (see above). Default used.
3. Node-type allowlist (`src/nif/nif.cpp:k_node_types`): `NiNode` and the other
   fourteen names. Only `BSMultiBoundNode` is pinned, by `F4LODGEN@06db2894:166`.
   `NiNode` is not pinned by any source; its prefix is assumed to match the
   `BSMultiBoundNode` prefix. `BSValueNode` is settled (see the NiNode family
   section). The other twelve names are unverified too. The corpus decides. Unknown `*Node` types such as `BSFurnitureMarkerNode` are skipped
   as opaque sized blocks, not rejected.
4. Billboard: the type name `NiBillboardNode` and a u16 mode after the children
   list. No source covers billboards.
5. Null (-1) child references are skipped. `F4LODGEN@06db2894:286-291` rejects them.
6. `BSDynamicTriShape`: no source pins it. It is read with the `BSTriShape` layout.
   Any type-specific trailing fields are not read.
7. `BSSubIndexTriShape` and `BSMeshLODTriShape`: `NifShape.cs:Shapes` reads them
   with the `BSTriShape` layout. Per-type trailing fields are not read.
8. `shader_ref` is settled: `nif.xml:BSTriShape` lists Bounding Sphere, Skin,
   Shader Property and Alpha Property in that order, and the 16 bytes are the bounding
   sphere. `alpha_ref` points at a `NiAlphaProperty`, which `read_alpha` reads and the
   shape reports as its alpha fields.
9. Meaning of the vertex-descriptor flag bits beyond their positions. Only the bit
   positions are pinned.
10. Normal and tangent encoding is settled by `nif.xml:BSVertexData`: three bytes
    mapped to `b / 255 * 2 - 1`, and the fourth byte is Bitangent Y or Z (see the
    vertex section). `NifShape.cs` does not decode them.
11. Colour encoding: four bytes mapped to `b / 255`. Not pinned.
12. UV encoding: two binary16 values, as the 4-byte width suggests. Not pinned.
13. The bytes after a position are settled: Bitangent X, one float at full precision
    and one `hfloat` at half precision (`nif.xml:BSVertexData`).
14. Bitangents are settled: stored, not reconstructed, and read as described in the
    vertex section.
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
24. The tangent is read whenever bit 4 is set, while `nif.xml:BSVertexData` reads it
    only when bits 3 and 4 are both set. Both readings consume four bytes, so the
    stride check cannot distinguish them, and no vanilla shape carries bit 4 without
    bit 3. The bitangent follows `nif.xml` and needs both.
25. The leading u32 of `BSLightingShaderProperty` is read as `Shader Type` because the
    shader-type tail follows it. `nif.xml`'s version condition for that NiObjectNET
    field excludes BS 130, so the corpus decides; all 1,111,752 material-bearing
    shapes read a `.bgsm` name at the offset the field implies. The extract settles it:
    `PluginBrreakdown@5a58943:FO4_Fallout4/Fallout4.exe function map.txt:0x142178bf0`
    creates the material from an id read before the base property data.
26. `Grayscale to Palette Scale` and `Wetness` gates: `nif.xml` says `#BS_GTE_130#`
    for both, the extract says above stream version 0x79 and 0x7b
    (`PluginBrreakdown@5a58943:FO4_Fallout4/Fallout4.exe function map.txt:0x1421ce880`).
    The reader follows the extract's two thresholds, which are looser than nif.xml's;
    at 130 every reading agrees, and no tighter file is read, so the conflict cannot
    show. The same disagreement exists for the effect shader's environment map, normal
    and environment mask textures and its scale: `nif.xml` gates them on `#BS_GTE_130#`,
    the extract on stream versions 0x6c and 0x6e
    (`...:0x142176f30`), and the reader follows nif.xml there.
27. Material type ids 7 (ParallaxOcc), 11 (MultiLayerParallax), 14 (Snow) and 16 (Eye)
    come from `nif.xml`'s shader-type conditions alone. The extract names the classes
    and their tails but its type ids only appear for 1, 5 and 6, so those four ids are
    unconfirmed until a vanilla mesh or a CK check shows one.
28. The two bytes after `Environment Map Scale` are read as `Use Screen Space
    Reflections` and `Wetness Control: Use SSR` (nif.xml booleans). The extract calls
    them two single bytes at `+0xd4` and `+0xd5`, which pins the width and not the
    meaning.
29. `Glossiness` is not read: v1.x of this reader never did, `nif.xml` gates it on
    `#NI_BS_LT_FO4#`, and the FO4 base material has `Smoothness` in its place. The
    extract's clamp of the float at `+0x88` to 0-1 matches `Smoothness`, not
    `Glossiness` (`...:0x1421ce880`).
30. Flag names are `nif.xml`'s, and their 64 bit positions cross-check against
    `commonlibf4`'s `EShaderPropertyFlag` (bits 0-31 then 32-63, the same split). The
    two agree on every bit position but four of the labels differ: bit 13 is
    `RefractionClamp` in `commonlibf4` and `Non_Projective_Shadows` in `nif.xml`, bit
    20 `LocalMapClear` against `Localmap_Hide_Secret`, bit 21 `FaceGenRGBTint` against
    `Skin_Tint`, and bit 58 `InvertedFadePattern` against `Gradient_Remap`. The reader
    keeps the `nif.xml` spelling because `nif.xml` is where the names are written down
    and an editor's `shaderFlags` edit has to match the table exactly; the engine
    spellings are recorded here instead. The strings are documentation for an editor,
    not data the game reads.
31. The engine's loader clears flag bits 25 (tessellate) and 47 (dismemberment) when
    the stream version is below 0x6d (`commonlibf4`, `BSShaderProperty::Load`).
    `read_lighting_shader` reports the stored word instead of a post-load one, and the
    strict gate at 130 never reaches the rule. A reader for older streams decides.
32. `OptimizeOptions::RemoveDuplicateProperties`: no source says what counts as a
    duplicate property, so nothing is checked or removed. A pass that guessed would
    delete blocks the game still links. Default: no check.
33. `OptimizeOptions::RemoveBadTextureProperties`: the only invalid texture path these
    sources pin is an absolute one, which `texture paths are relative` already
    reports. Whether an empty or unmatched slot counts as bad is not pinned, so it is
    not reported. Default: no further check.
34. `OptimizeOptions::ClearUnusedMaterialValues`: no source lists which stored
    material values a given shader type ignores. `nif.xml` keeps every field with its
    default for every type, and the engine overrides them from the `.bgsm`, so
    clearing them cannot be told apart from a real value. Default: no check, and
    `fix_nif` leaves the words alone.
35. Effect shader texture slot order: `nif.xml:BSEffectShaderProperty` stores
    `Greyscale Texture`, then `Env Map Texture`, then `Normal Texture`, then `Env Mask
    Texture`, and `read_effect_shader` follows that order. The extract's
    `BSEffectShaderMaterial` field list names its five paths source, greyscale,
    environment, environment mask, normal
    (`PluginBrreakdown@5a58943:include/RE/B/BSEffectShaderMaterial.h`), which swaps the
    last two. Those in-memory fields are filled by the loader in the order the file
    stores them, so the file order decides and the reader follows `nif.xml`. Slots 3
    and 4 therefore keep 3 = normal and 4 = env mask; the vanilla corpus is the check.

## Scene graph

`read_nif` keeps every node block it understands in `NifFile::nodes()` (the C ABI
exposes them through `radicle_nif_node_*`, the C# binding as `NifFile.Nodes`).
A `Node` carries its block index, its block type, its `NiObjectNET` name, its
parent as a node-list index (-1 for a root), its children as the block indices
the file stores (shapes included), and its local `NiAVObject` transform.

The node prefix follows `nif.xml` for BS 130 and user version 12, in this order,
and it is the same prefix the shapes read:

1. `NiObjectNET`: `Name` (an i32 string-table index, -1 for none), then
   `Num Extra Data List` and the `Extra Data List Ref` entries (since 10.0.1.0;
   `Extra Data Ref` is `until 4.2.2.0` and so never appears), then `Controller Ref`.
   `Shader Type` is a `NiObjectNET` field for 20.2.0.7, but the installed data only
   carries it on `BSLightingShaderProperty` blocks, so a node's first field is the
   name - proven by every node block ending exactly at its declared size.
2. `NiAVObject`: `Flags` as a u32 (`#BSVER# #GT# 26`, so 130 takes the u32 branch),
   `Translation` (three floats), `Rotation` (`Matrix33`, nine floats),
   `Scale` (one float), then `Collision Object Ref`.
3. `NiNode`: `Num Children` u32 and the child references. `Effects` is gated on
   `#NI_BS_LT_FO4#`, which excludes this reader, so FO4 has no effects list here -
   settled by the corpus: all 896,993 node blocks of the 226,177 vanilla NIFs parse
   with the children list last, in block order.

`read_children` skips negative child references and range-checks the rest against
the block count, so a wrong node type cannot silently produce a fake child edge.

### The node-type allowlist

`src/nif/nif.cpp:k_node_types` is the same fifteen names as before, and the
vanilla corpus now proves it covers everything that has an `NiNode`-shaped
prefix. Histogram over 226,177 NIFs (Fallout 4 1.10.163, `FO4_DATA_DIR` set to
the stock Data folder): `NiNode` 893,570, `BSValueNode` 1,783, `NiBillboardNode`
709, `BSLeafAnimNode` 349, `BSOrderedNode` 235, `BSBlastNode` 148,
`BSDamageStage` 121, `NiSwitchNode` 61, `BSMultiBoundNode` 16, `BSTreeNode` 1.
No `BSFadeNode`, `NiLODNode`, `BSRangeNode`, `BSDebrisNode` or `NiSortAdjustNode`
occurs in a vanilla `.nif` (they appear in the LOD `.bto`/`.btr` block types,
which this corpus does not read).

The only vanilla block whose name contains `Node` and is not a node is
`BSFurnitureMarkerNode`, and it is not `NiNode`-derived: `nif.xml` gives it
`inherit="BSFurnitureMarker"`, and its bytes agree - a u32 marker count where a
node has its name index. Reading it as a node would take that count for a name,
so it stays an opaque sized block, and `radicle_nif_node_for_block` returns -1 for
it. The corpus counts it and prints it as `not a node despite the name`.

### Node hierarchy against the HKX reference pose

Vanilla `Meshes\Actors\Character\CharacterAssets\skeleton.nif` has 129 node
blocks and 74 names in common with the 95 bones of the sibling `skeleton.hkx`.
Of the 72 common bones whose HKX parent is also in the NIF, 71 name the same
parent node. The one difference is `CamTarget`, whose NIF parent is
`CamTargetParent` while the HKX parents it to `Root`; that is a property of the
two shipped assets, not a reading difference.

The 45 `*_skin` helper bones `MaleBody.nif` skins to all exist as nodes in BOTH
files: 45 of them are nodes of `skeleton.nif` (which carries 129 nodes) and the
same 45 are 45 of the 59 nodes of `MaleBody.nif`. Each reaches an HKX bone in one
to three hops, for example `Belly_skin -> SPINE1 -> COM`,
`Chest_skin -> Chest`, `UpperBelly_skin -> SPINE2 -> SPINE1 -> COM` and
`RArm_ShoulderFat_skin -> RArm_Collarbone`.

`BSSkin::BoneData`'s stored per-bone transform is NOT the helper's global
transform: for the 45 helpers the global transform built from the node hierarchy
(which matches the HKX reference pose to 0.0063) differs from the stored bind by
up to 120.5 in its largest element, and from the stored bind's inverse by up to
127.9. Whatever the stored `BSSkinBoneTrans` is relative to, it is not the
bone's place in the scene graph; the reader still exposes it verbatim.

### Shape parents

Every vanilla shape sits under a node: 1,206,190 of 1,206,190 shapes have a node
ancestor, and `shape_parent_node` (the C ABI's `radicle_nif_shape_parent_node`,
the C# `NifShape.ParentNodeIndex`/`ParentNode`) agrees with the block parent for
all of them. `shape_to_global` still excludes root nodes.

## Skin policy

- `BSSkin::BoneData` whose count disagrees with the skin's bone count is a
  `parse_error`, not a silent identity transform. An absent or mistyped data
  reference still yields names with default transforms, as `NifShape.cs:ReadSkin`
  does; that case has no transforms in the file to read.
- Bone indices are one byte per vertex, so a skin can only address its first 256
  bones: index 255 is the largest a shape can name, and a `BSSkin::Instance` with
  more bones leaves the rest unreachable. The reader refuses a file whose indices
  are past the end of the skin's bone list instead of reading a bone that is not
  there. The corpus has 0 out-of-range indices over 168,238 bones in 23,375
  skinned shapes.
- The 68-byte `BSSkinBoneTrans` this reader reads is the transform
  `NifShape.cs:ReadSkin` reads; the corpus cross-check against BGS's own reader
  is unchanged.

## Shader property failures

A `BSLightingShaderProperty` or `BSShaderTextureSet` that does not parse no longer
fails the whole file. `read_shape` catches the `parse_error`, leaves the material
path and the texture slots empty, and records the reason in `Shape::warning`,
which the C ABI exposes as `radicle_nif_shape_warning` and the binding as
`NifShape.Warning`. The geometry, the skin and the transform of that shape are
still decoded. Vanilla data never needs this: 0 of 1,206,190 shapes record a
warning, so no shipped file has a shader block this reader cannot follow.

## C ABI thread safety

- Every `radicle_nif_*` accessor is read-only on a handle and safe to call from
  several threads at once; the handle's node list and shape list are built once in
  `radicle_nif_read` and never mutated afterwards.
- `radicle_nif_destroy` must not race any other call on the same handle. Handles
  are independent of one another.
- `radicle_last_error` is thread-local and stays valid until the next Radicle C
  API call on the same thread, as `include/fo4/c_api.h` states.
- The same contract holds for `radicle_dds_*`: the functions are pure, and
  `fo4::texture` holds no state between calls.

## Parity-pending status

- Item 3 (node-type allowlist): settled by the corpus histogram above. The
  fifteen names are the node types vanilla data uses; `BSFurnitureMarkerNode` is
  deliberately outside the list, with its raw layout as evidence.
- Item 3 (`BSValueNode` layout): settled by the shipped-data scan. The 1,766 base-game
  and DLC blocks carry exactly five trailing bytes, and the first run of digits in
  each of the 1,800 block names equals the i32 value. The corpus test `NifCorpus.ReadsEveryVanillaNifAndHandlesGeometryTypes`
  checks the name rule on every value node it reads. A later whole-Data scan counted 1,800 blocks: 1,766 in base-game and DLC archives, 33 in Creation Club and 1 loose. The 1,783 in the histogram is from the earlier corpus run.
- Item 4 (`NiBillboardNode` mode as a u16 after the children list): still the
  reader's reading; 709 vanilla billboard nodes parse with it, and the corpus
  reads their children without a single failure.
- Item 20 (a shape with a zero data size, vertex count or stride yields an empty
  shape): unchanged.

## Node graph safety

The fifteen NiNode-derived types in `src/nif/nif.cpp:k_node_types` are NiNode, BSFadeNode, BSMultiBoundNode, BSOrderedNode, BSLeafAnimNode, BSTreeNode, NiBillboardNode, NiSwitchNode, NiLODNode, BSValueNode, BSRangeNode, BSDebrisNode, BSBlastNode, BSDamageStage and NiSortAdjustNode; the vanilla corpus uses ten of them.

`read_nif` refuses a file whose parent links form a loop, including a node that lists itself as its own child, with the parse_error `node parent chain contains a cycle`. The check walks each block's parent chain once with a three-state marker, so it is linear in the block count and never recurses; a 10,000-deep acyclic chain reads (`NifNodes.ReadsAVeryDeepAcyclicChain`). The managed `NifNode.WorldMatrix` composes its ancestors in a loop capped at the node count rather than by recursion, so a deep tree cannot overflow the stack and a loop that reached it anyway would throw instead of hanging.

A skinned shape whose skin instance lists no bones keeps its geometry, but its per-vertex weights and bone indices are dropped and the shape warning says `the skin lists no bones, so its weights were dropped`: indices with nothing to index are never handed to a consumer. Eight vanilla skinned shapes have no resolvable bone list.

## Edit writer

`include/fo4/nif/nif_write.hpp` edits a NIF through `apply_edits` (C++) or
`apply_edits_json` (the editor's path, `src/nif/edit_json.cpp`). `write_nif` with no
edits returns the input bytes unchanged, so a save that changes nothing changes nothing
(`tests/test_nif_write.cpp:ZeroEditRoundTripIsByteIdentical`).

Every edited field is written where the matching reader reports it: the writer calls
`read_lighting_shader`, `read_effect_shader`, `read_alpha`, `read_bsx_flags` and
`read_collision_object` and writes through their recorded offsets, so a field cannot
drift between the two sides. The one exception is `bhkNPCollisionObject`, whose layout is
fixed: `target` i32 at 0, `flags` u16 at 4, `system` i32 at 6 and `bodyId` u32 at 10, so
`src/nif/write_edits.cpp:apply_collision_edit` writes the two editable fields at the
constants 4 and 10 and calls the reader only to refuse a block that does not parse. A
shared `BSLightingShaderProperty`, `BSShaderTextureSet` or
`NiAlphaProperty` is copied before the edit and the owner's reference is repointed
(`src/nif/write_edits.cpp:shader_block`, `:shader_texture_set`, `:alpha_block`).

Shader flags are edited by name (`edit_json.cpp:read_edit`), so an unknown name is a
parse_error instead of a silently cleared bit. `bhkNPCollisionObject` exposes only its
flags and Body ID; the `bhkPhysicsSystem` payload is never rewritten
(`tests/test_nif_write_blocks.cpp:CollisionObjectFlagsAndBodyIdRoundTripAndKeepTheBlob`).

Its parity items extend the list above:

36. `NiAlphaProperty` source blend, destination blend and test function stay read-only.
    `nif.xml:NiAlphaProperty` stores them as bits of one flags word, and `commonlibf4`
    names the values, but no allowed source pins the engine's write path for them, so the
    writer changes them only through the whole `flags` word.
37. `smoothness` is the FO4 name; the writer also accepts `glossiness`, the older name the
    editor already sends, and both land on the same float. Whether the engine renames the
    field for BS 130 is nif.xml's business, not a second offset.
38. Below stream version 0x45 a shader flags edit naming a bit above 31 is refused rather
    than truncated, because the block has no second word to hold it.
39. `BSEffectShaderProperty` environment, normal and env-mask paths are refused below BS
    130, where `src/nif/shader_effect.cpp` does not read them.
40. `import_obj` with `box_collision` writes one static box: a `bhkNPCollisionObject` (block 5)
    and a `bhkPhysicsSystem` (block 6) whose packfile holds one `hknp` box body. The box is
    the mesh's axis-aligned bounds, centred and halved, with each half extent floored at
    0.01 m. Compressed mesh, compound shapes, constraints, ragdolls and dynamic bodies are not
    written, and a generated box has not been walked on in the game.
41. `import_obj` writes an OBJ `vt` v coordinate through unchanged. No allowed source
    states whether a NIF texture's v axis starts at the top or the bottom, and nothing
    here compares a written mesh against an authored one, so the orientation is unproven
    rather than chosen: the check is a NIF imported from a known OBJ and opened in the
    Creation Kit. The read side passes the stored pair through either way, and the
    round-trip test therefore cannot see this.
