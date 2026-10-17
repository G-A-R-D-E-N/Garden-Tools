# Third party notices

Radicle's own source is covered by the repository licence. Software written by others and shipped
alongside it, or ported into it, is listed below with its own licence. Fallout 4 and its file formats
are Bethesda's; nothing here is derived from Bethesda's code or ships their assets.

## bcdec

- Upstream: https://github.com/iOrange/bcdec
- Licence: MIT / Unlicense (dual), by Sergii Kudlai.

`src/texture/bcn.cpp` is the bcdec BC1-BC7 block decoder, ported from bcdec.h v0.97. It is the same
decoder Godot ships as `modules/bcdec` and the same one PluginEditTool carries at
`PluginEditTool/native/src/textures/BcnDecoder.cpp`; the code moved here unchanged apart from its
namespace, its two public entry points (`can_decode_block_format` and `decode_block_format`) and a
thrown error instead of a returned one. Two things are carried across verbatim because they are fixed
spec data that cannot be derived: the 64 two-subset and 64 three-subset BC7 partition tables, and the
per-mode endpoint bit widths. BC6H (HDR) is not ported.

The port was checked against DirectXTex's own conversion of the same files, pixel for pixel, over 484
real mod textures covering every format: BC2, BC3, BC5, BC7 and the uncompressed layouts match
exactly, and BC1 and BC4 differ by at most 1 in the rounding of a rare interpolated value. That
measurement was made for the PluginEditTool port; `tests/test_dds.cpp` pins the formats it covers.

PluginEditTool still holds its own copy. Pointing it at `fo4::texture` is a follow-up and is not part
of the change that created this file.
