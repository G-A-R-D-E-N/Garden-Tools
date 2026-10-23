# DDS decode

Scope of `fo4::texture`: read a DDS container's header, size its mips, and decode one
mip level into RGBA8. Not in scope: encoding, PNG output, cube or array slice
selection (slice 0 is decoded), and BC6H.

`src/texture/bcn.cpp` is a port of [bcdec.h](https://github.com/iOrange/bcdec) v0.97
by Sergii Kudlai, taken from PluginEditTool where it is
`PluginEditTool/native/src/textures/BcnDecoder.cpp`. It moved unchanged apart from
its namespace, its two entry points (`can_decode_block_format` and
`decode_block_format`) and a thrown `dds_error` in place of a returned one, so the
two can still be compared side by side. `THIRD_PARTY_NOTICES.md` carries the
licence and the port's provenance. PluginEditTool still holds its own copy;
repointing it at `fo4::texture` is a follow-up and is not part of this change.

## Container

`src/texture/dds_codec.cpp:read_dds` reads `DDS `, a `DDS_HEADER` of 124 bytes, and
either the DX10 extension or a legacy pixel format. It accepts:

- A DX10 header (`DX10` FourCC) whose DXGI format fits the one byte a BA2 entry
  stores, with the array size, the cube flag from `DDS_RESOURCE_MISC_TEXTURECUBE`
  and the depth from `DDS_HEADER_DXT10`.
- A legacy FourCC: `DXT1`, `DXT2`, `DXT3`, `DXT4`, `DXT5`, `ATI1`, `BC4U`, `BC4S`,
  `ATI2`, `BC5U`, `BC5S`, `A16B16G16R16F` and `A32B32G32R32F`.
- A legacy uncompressed mask pair, which is mapped to a DXGI format from the
  bit depth and the four channel masks, plus luminance-only and alpha-only 8-bit
  layouts.

The format table is 40 entries; `lookup_format` returns the DXGI code, its name,
its bytes per 4x4 block and its bytes per pixel. An unknown format is
`UnsupportedFormat`, not a silent guess.

## Decode

`src/texture/dds.cpp:decode_dds_rgba` decodes exactly one mip. The surface starts at
`data_offset + sum of the earlier mips` and is `mip_size` bytes, so an out-of-range
mip index and a file that ends early are both `InvalidDds`. A file whose header says
zero mips is treated as one.

- Block formats: BC1 (70-72), BC2 (73-75), BC3 (76-78), BC4 (79, 80), BC5 (82, 83) and
  BC7 (97-99) through `decode_block_format`. BC4_SNORM (81) and BC5_SNORM (84) are
  accepted by `read_dds` and `radicle_dds_info` but not decodable: decode returns a
  parse error. BC6H is rejected: it is HDR and the port does not implement it.
- Uncompressed: R8G8B8A8 (28, 29), B8G8R8A8 (87, 91), B8G8R8X8 (88, 93), R8G8 (49),
  R8 (61, luminance), A8 (65), B5G6R5 (85), B5G5R5A1 (86) and B4G4R4A4 (115).
- `reconstruct_z` rewrites the blue channel from red and green as
  `z = sqrt(max(0, 1 - x*x - y*y))`, mapping `b / 255 * 2 - 1` each way, which is what
  a BC5 normal map needs. It is applied after the decode and only for unsigned BC5
  (82, 83); any other format throws `std::invalid_argument`, which
  `radicle_dds_decode_rgba` reports as `RADICLE_STATUS_INVALID_ARGUMENT`.

## Evidence

`tests/test_dds.cpp` pins the header parse for a legacy `DXT1` header and a DX10
header built by `build_dds_header`, the mip sizes, the BC1 white and three-colour
transparent blocks, a BC3 alpha block, BC4 and BC5 channel mapping, a synthetic BC7
mode-0 block, the uncompressed layouts, the BC5 Z reconstruction and the mip
selection. The block vectors are the same ones PluginEditTool's own
`tests/unit/TextureCodecTests.cpp` asserts.

The pixel output was checked against DirectXTex for the PluginEditTool port over 484
real mod textures covering every format: BC2, BC3, BC5, BC7 and the uncompressed
layouts matched exactly, and BC1 and BC4 differed by at most 1 in the rounding of a
rare interpolated value. That measurement was not repeated here; this module is the
same decoder, and `tests/test_dds.cpp` pins the formats it covers.

## Format-specific behaviour

- `reconstruct_z` is refused unless the DXGI format is an unsigned BC5 normal map,
  `BC5_TYPELESS` (82) or `BC5_UNORM` (83). It raises `std::invalid_argument`, which
  the C ABI maps to `RADICLE_STATUS_INVALID_ARGUMENT`. `BC5_SNORM` (84) is refused
  too: its channels are signed, and the reconstruction takes a square root of
  `1 - x*x - y*y`, which only means anything for the unsigned mapping. Every other
  format, the uncompressed layouts included, is refused as well, so a caller that
  asks for a reconstructed Z always gets one or an error, never a silent blue
  channel from an unrelated layout.
- The container reader refuses an array size above 65535 and, for a volume
  texture, a depth above 65535: those are the 16-bit counts a BA2 entry stores.
- `DdsInfo::payload_size()` multiplies unsigned and checks the product, so a
  header claiming 65535 mips of 65535x65535 times a 65535-slice array is
  `InvalidDds` instead of a wrapped byte count. The largest accepted payload is
  bounded by `INT64_MAX`.

## C ABI thread safety

- `radicle_dds_decode_rgba` and `radicle_dds_info` are pure functions of their
  input bytes: no state is kept between calls, so they are safe from several
  threads, and two calls never share a buffer.
- A returned RGBA buffer is owned by the caller and released with
  `radicle_free_bytes`; it is not read or written by the library after it is
  returned.

## Ported block layouts

The ported `src/texture/bcn.cpp` keeps two things verbatim because they are fixed
spec data that cannot be derived: the 64 two-subset and 64 three-subset BC7
endpoint/partition tables, byte-identical to the C# port in PluginEditTool, and the
per-mode endpoint bit widths. Its block shapes are the BC ones: a BC1-style colour
block is two 565 endpoints plus sixteen 2-bit indices, a BC4-style single-channel
block is two 8-bit endpoints plus sixteen 3-bit indices, and the BC7 path reads and
writes 16-byte blocks through an LSB-first bit reader and writer.
