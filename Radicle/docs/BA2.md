# BA2 support

Radicle's archive code currently implements the Fallout 4 version-1 `BTDX/GNRL` general-resource layout only. It intentionally rejects DX10 and next-generation v7/v8 archives until those layouts are verified separately.

## Evidence

The compatibility oracle for the initial implementation is the existing PluginEditTool archive codec in `PluginEditTool/FO4RecordEditor.Core/Services/Archives/Ba2Codec.cs` and its tests. Radicle reimplements the format independently in C++; it does not copy the C# implementation.

The proven GNRL layout used here is:

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

## Current API

`fo4/archive/ba2.hpp` exposes:

- read from bytes or file
- write to bytes or file
- stored and compressed chunk construction
- entry extraction with multi-chunk concatenation
- path lookup that treats slash direction and ASCII case equivalently

The low-level model preserves name/extension/directory hashes and the one-byte entry metadata field so a read/write round trip does not discard fields that Radicle does not interpret yet.

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

- `DX10` texture BA2 records/chunks
- BA2 versions 7 or 8
- BSA archives
- path-addressed PluginEditTool operations on a GNRL archive with no string table

These cases return explicit errors instead of interpreting unknown fields using version-1 assumptions. See the separate next-generation/DX10 roadmap issue before expanding these layouts.

## Tests

- `Radicle/tests/test_ba2.cpp`
- `PluginEditTool/native/tests/unit/RadicleArchiveBackendTests.cpp`

The Radicle core was also compiled in isolation with C++23, `-Wall -Wextra -Wpedantic -Werror`, and zlib while the branch CI path was being established.
