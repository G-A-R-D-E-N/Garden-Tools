# BA2 support

Radicle reads, writes and extracts `BTDX/GNRL` general resources and
`BTDX/DX10` textures at versions 1, 7 and 8. DX10 extraction prepends a DDS
header reconstructed from the stored texture metadata only when the entry path
ends in `.dds` and the extracted payload does not already start with `DDS `;
any other entry is returned as stored, its chunks decompressed and
concatenated with no header added
(`src/archive/ba2_texture.cpp:extract_ba2_texture_entry`). Other versions and
content tags are rejected.

## Evidence

The initial compatibility oracle was the PluginEditTool archive codec in
`PluginEditTool/FO4RecordEditor.Core/Services/Archives/Ba2Codec.cs` and its
tests. Radicle reimplements the format independently in C++.

`tools/corpus/ba2_measure.py` is an independent, read-only measurement of the
installed Fallout 4 Data folder; re-run it with `FO4_DATA_DIR` set. It reads
only archive headers and entry/chunk tables. It reads fixed-size header, entry
and chunk records, and checks the 24-byte headers, chunk-header-size fields,
`0xBAADF00D` sentinels, payload bounds, the name-table offset within the file
and table end before the name-table offset, prints the totals below and
exits non-zero if any check fails. An archive that cannot be opened or parsed, or
that fails a check, is reported as a problem and left out of the totals. It is a
local tool and is not run by CI.
Counts cover all 79 installed root-level `.ba2` archives of the AE 1.11.240.0 build:

| Version | Content | Archives | Entries | Chunks |
|---|---|---:|---:|---:|
| 1 | GNRL | 12 | 383153 | 383153 |
| 1 | DX10 | 4 | 8735 | 21344 |
| 7 | GNRL | 1 | 6112 | 6112 |
| 7 | DX10 | 11 | 32335 | 77030 |
| 8 | GNRL | 29 | 76224 | 76224 |
| 8 | DX10 | 22 | 1011 | 3251 |

With `--first-chunk` the script also reads the first chunk of each archive at
its stored offset and size; compressed chunks decompressed with zlib framing,
and all 79 decoded sizes matched. This is extraction sampling, not an all-payload extraction audit.
The measured v7/v8 tables use the same field widths as v1; no additional
version-dependent fields were needed in these files. This does not establish
support for unmeasured versions or layouts.

### Per release

In-scope archives are those listed in each release's `scope.json`: the master and cc plugin archives and the `Fallout4 - *.ba2` archives. Counts are archive headers only.

| Release | Archives | v1 | v7 | v8 |
|---|---:|---:|---:|---:|
| OG 1.10.163.0 | 49 | 16 | 12 | 21 |
| NG 1.10.984.0 | 53 | 17 | 12 | 24 |
| AE 1.11.240.0 | 55 | 16 | 12 | 27 |

NG leaves out `DLCworkshop03 - Voices_en.ba2` (51,327,791 bytes; the OG and AE copies are 51,750,623 bytes). Its name table runs past the end of the file, so it is treated as an incomplete download; its header reports v1, which is why the raw NG v1 count is 18.

- `Fallout4 - Meshes.ba2` and `Fallout4 - Sounds.ba2` are v8 on OG and AE and v1 on NG. NG Meshes has 42416 entries (OG and AE 42426) and NG Sounds has 8872 (OG and AE 8869).
- `Fallout4 - TexturesPatch.ba2` (v8 DX10, 7 entries) is present on OG and AE and absent on NG.
- `Fallout4 - MeshesExtra.ba2` (v1, 125837 entries) is the same size on all three releases.
- No archive on any release has a version other than 1, 7 or 8.

`tests/test_ba2_versions_real.cpp:Ba2NamedRealData` pins these named fixtures:

| Version | Content | Filename | Entries |
|---|---|---|---:|
| 1 | GNRL | Fallout4 - Nvflex.ba2 | 32 |
| 1 | DX10 | DLCworkshop02 - Textures.ba2 | 171 |
| 7 | GNRL | DLCRobot - Voices_en.ba2 | 6112 |
| 7 | DX10 | DLCworkshop01 - Textures.ba2 | 32 |
| 8 | GNRL | ccBGSFO4001-PipBoy(Black) - Main.ba2 | 41 |
| 8 | DX10 | ccBGSFO4001-PipBoy(Black) - Textures.ba2 | 4 |

The tests compare streaming and model extraction of the first entry, rebuild
that entry into an archive, and check identical extracted content and DDS
bytes after re-reading. The rebuilt entry must keep its name, extension and
directory hashes; for DX10 it must also keep its texture info and each chunk's
mip range. They run only when `FO4_DATA_DIR` is set; in CI
the `fo4-data` runner supplies it to the `native-windows-real-data` job of `native-ci`; both `native-windows-tests` passes clear it. The
Linux job passes `FO4_DATA_DIR_LINUX`, which is currently unset, so the tests skip
there. They skip when `FO4_DATA_DIR` or a named archive is missing. A present
archive whose version, kind or entry count differs from the table above fails the
test (`ASSERT_EQ` in `tests/test_ba2_versions_real.cpp`) rather than skipping. No game archives or extracted payloads are committed.

The measured GNRL layout at versions 1, 7 and 8 is:

- Header, 24 bytes:
  - `BTDX` magic, u32
  - version, u32
  - `GNRL` tag, u32
  - file count, u32
  - string-table offset, u64
- General entry header, 16 bytes:
  - name hash, u32
  - extension hash, u32
  - directory hash, u32
  - unknown metadata byte, u8
  - chunk count, u8
  - chunk-header-size field, u16; expected value `0x10`
- Each GNRL chunk record, 20 bytes:
  - payload offset, u64
  - compressed size, u32; zero means stored/uncompressed
  - decompressed size, u32
  - sentinel `0xBAADF00D`, u32
- Optional string table:
  - one u16 byte length followed by raw path bytes for each entry, in entry order

Compressed general chunks use zlib framing. Chunk payload offsets are honored individually; the reader does not assume payloads are contiguous or immediately follow the table.

The measured DX10 layout uses the same 24-byte archive header with a `DX10`
tag. Its 24-byte entry header starts with the same hashes and metadata as
GNRL, with chunk-header-size `0x18`, followed by height and width (u16 each),
mip count and DXGI format (u8 each), and two metadata bytes exposed as flags
and tile mode. Each 24-byte chunk stores offset (u64), compressed and
decompressed sizes (u32 each), first and last mip (u16 each), and the
`0xBAADF00D` sentinel. Texture chunks use the same stored/zlib size convention.
Flags bit 0 is read as the cubemap bit when the DDS header is reconstructed
(`src/archive/ba2_texture.cpp:dds_header_for`). The other flag bits and the
tile mode are preserved on round trip but not interpreted.

## DDS header rule

`src/archive/ba2_texture.cpp:dds_header_for` writes the same header as xEdit's
DDS builder for a `.dds` DX10 entry (`Core/wbBSArchive.pas:2195-2211` calls
`TwbDDS.SetUpHeader`, `Core/wbDDS.pas:574-786`; pinned xEdit source 93cc0bc5):

- Formats in `TwbDDS.DXGI_DX9` (`wbDDS.pas:249-265`) get the classic 128-byte
  header with no DX10 extension (`wbBSArchive.pas:2199`). BC1 (71) is `DXT1`,
  BC2 (74) `DXT3`, BC3 (77) `DXT5`, BC4U/BC4S (80/81) `BC4U`/`BC4S`, BC5U/BC5S
  (83/84) `BC5U`/`BC5S`. R8G8B8A8 (28), B8G8R8A8 (87), B8G8R8X8 (88), B5G6R5 (85),
  B5G5R5A1 (86), R8G8 (49), A8 (65) and R8 (61) get pixel-format masks
  (`wbDDS.pas:675-756`).
- Every other format gets the 148-byte header: `DX10` FourCC, DXGI format, and
  the DX10 extension (`wbDDS.pas:767-773`).
- DX9-set flags: `DDSD_LINEARSIZE` (0x80000) for BC formats, `DDSD_PITCH` (0x8)
  for uncompressed (`wbDDS.pas:612-756`). DX9 pitch or linear size: BC1 and BC4
  `width*height/2`, BC2, BC3 and BC5 `width*height`, uncompressed
  `width*bytes per pixel`. DX10-path flags follow the per-format list below.
- Caps: `DDSCAPS_TEXTURE`; `DDSCAPS_MIPMAP|DDSCAPS_COMPLEX` when mips > 1;
  `DDSCAPS_COMPLEX` for any cubemap, including one mip; caps2 `0xFE00` for a
  cubemap (`wbDDS.pas:593-606`).

The DX10 path follows xEdit's per-format table for `dwPitchOrLinearSize` and the
size flag (`Core/wbDDS.pas:610-764`, pinned 93cc0bc5), with 32-bit unsigned
arithmetic (`dwWidth`/`dwHeight` are Cardinal). Non-DX9 formats:

- `DDSD_LINEARSIZE`: BC1_UNORM_SRGB (72) `width*height div 2` (`wbDDS.pas:653-658`);
  BC2/BC3 SRGB (75, 78), BC6H UF16/SF16 (95, 96) and BC7 UNORM/UNORM_SRGB (98, 99)
  `width*height` (`wbDDS.pas:659-666`).
- `DDSD_PITCH`: R8G8B8A8 SRGB/UINT/SINT (29, 30, 32), B8G8R8A8 SRGB and B8G8R8X8 SRGB
  (91, 93) `width*4` (`wbDDS.pas:667-674`); R8G8 UINT/SINT (50, 52) `width*2`
  (`wbDDS.pas:731-736`); R8 UINT/SNORM/SINT (62-64) `width` (`wbDDS.pas:737-742`).
- Every other format, including the typeless ones: `DDSD_PITCH` with
  `(width * bits per pixel) shr 3`, where bits per pixel is `TwbDDS.GetBitsPerPixel`
  (`wbDDS.pas:788-928`, indexed by the DXGI number).

The DX9 set keeps its own formulas (`dx9_format`). Radicle's width and height are
16-bit, so the 32-bit products cannot wrap.

Radicle policy differences from xEdit, pending an owner decision (code unchanged):

- Non-`.dds` entries in a DX10 archive pass through unchanged; xEdit always
  prepends a header (`wbBSArchive.pas:2195-2210`).
- A missing name table gives `unnamed.dds`; xEdit builds
  `<dirhash>\<namehash>.<ext>` (`wbBSArchive.pas:1491-1497`).
- Zero width or height throws; xEdit does not check.
- Chunk-header size (must be 0x18) and the `0xBAADF00D` sentinel are enforced;
  xEdit reads and ignores them (`wbBSArchive.pas:1453`, `:1464`, `:1485`).
- A name-table offset past EOF throws; xEdit falls back to hash names
  (`wbBSArchive.pas:1491`).

## Current API

`fo4/archive/ba2.hpp` and `fo4/archive/ba2_texture.hpp` expose:

- read from bytes or file
- write to bytes or file
- stored and compressed chunk construction
- entry extraction with multi-chunk concatenation
- path lookup that treats slash direction and ASCII case equivalently

The low-level model preserves name/extension/directory hashes and the one-byte entry metadata field so a read/write round trip does not discard fields that Radicle does not interpret yet.

`fo4/archive/ba2_reader.hpp:Ba2Reader` indexes metadata without loading all
archive payloads and extracts entries on demand. Texture model extraction
follows the DDS header rule above. A DX10 entry with no stored path (no name table,
or an empty name) is extracted under the path `unnamed.dds`
(`src/archive/ba2_reader.cpp:Ba2Reader::extract`), so the rule applies: a DDS header
is prepended unless the payload already starts with `DDS `. GNRL entries are not
affected. Archive serialization preserves modeled
metadata and content, but does not promise original padding or byte-identical
archive files.

## PluginEditTool integration

`RadicleArchiveBackend` implements the existing native `IArchiveBackend` read contract for:

- list/filter
- extract one
- extract selected
- extract matching entries
- compare archives by decompressed content

The native host constructs `RadicleArchiveBackend` directly
(`PluginEditTool/native/apps/fo4ide/main.cpp`). `NullArchiveBackend`
(`PluginEditTool/native/include/fo4ide/archive/ArchiveService.hpp`) is a null object
whose `available()` returns false.

Extraction performs path validation before writing a batch: absolute paths, traversal components, Windows-invalid/device names, case-insensitive output collisions, and existing symbolic-link traversal are rejected. Writes use a temporary file and preserve an existing regular destination if replacement fails.

## Deliberate boundaries

Radicle does not currently claim support for:

- BA2 versions outside 1, 7 and 8, or content tags other than GNRL/DX10
- BSA archives
- in PluginEditTool, any non-empty GNRL or DX10 archive without a string
  table: the backend rejects it on open because its entries cannot be
  addressed by path
  (`PluginEditTool/native/src/archive/RadicleArchiveBackend.cpp`,
  `requireAddressable`)

These cases return explicit errors. Unknown entry metadata remains preserved;
support for further layouts needs its own evidence.

## Tests

- `Radicle/tests/test_ba2.cpp`
- `Radicle/tests/test_ba2_texture.cpp`
- `Radicle/tests/test_ba2_reader.cpp`
- `Radicle/tests/test_ba2_hardening.cpp`
- `Radicle/tests/test_ba2_versions_real.cpp`
- `Radicle/tests/test_ba2_real.cpp`
- `PluginEditTool/native/tests/unit/RadicleArchiveBackendTests.cpp`

The Radicle core was also compiled in isolation with C++23, `-Wall -Wextra -Wpedantic -Werror`, and zlib while the branch CI path was being established.
