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
only archive headers and entry/chunk tables, checks the 24-byte headers,
entry/chunk widths, chunk-header-size fields, `0xBAADF00D` sentinels, payload
bounds and table end before the name-table offset, prints the totals below and
exits non-zero if any check fails. It is a local tool and is not run by CI.
Counts cover all 79 installed root-level `.ba2` archives:

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
mip range. They use `FO4_DATA_DIR` and skip cleanly when it or a named file is
missing, so they are skipped on CI. No game archives or extracted payloads are committed.

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
follows the DDS header rule above. Archive serialization preserves modeled
metadata and content, but does not promise original padding or byte-identical
archive files.

## PluginEditTool integration

`RadicleArchiveBackend` implements the existing native `IArchiveBackend` read contract for:

- list/filter
- extract one
- extract selected
- extract matching entries
- compare archives by decompressed content

The legacy `NullArchiveBackend` construction used by the current native host is a compatibility adapter to `RadicleArchiveBackend`, allowing the existing host setup and React archive contract to remain unchanged while the implementation moves to Radicle.

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
