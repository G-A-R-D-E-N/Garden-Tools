# Load order files

Radicle reads the plugin list (`plugins.txt` or `loadorder.txt`, from the game AppData folder or a mod manager profile), the `Fallout4.ccc` list, and assembles the loaded-file list that the engine builds from them.

- Native: `fo4::loadorder` in `Radicle/include/fo4/loadorder/load_order_files.hpp`, sources in `Radicle/src/loadorder/`.
- C ABI: `Radicle/include/fo4/c_api_loadorder.h`, implemented in `Radicle/src/c_api_loadorder.cpp` (`radicle_plugin_list_read`, `radicle_ccc_read`, `radicle_load_order_assemble`). ABI 40. A file that cannot be opened or read, and a data directory that cannot be listed, return `RADICLE_STATUS_IO_ERROR`.
- C#: `Radicle.Interop.LoadOrderFiles` in `Radicle/bindings/csharp/Radicle.Interop/LoadOrder.cs`.

Evidence record: `RE-Work/subsystems/radicle-re-garden-tools/light-plugin-index-389-28/README.md` (RE repo commit 6916afb). The rules below follow its sections 2, 4.3, 4.4, 4.5 and its boundary-case list.

## Evidence status

| Build | Status in the record | Used for |
|---|---|---|
| OG 1.10.163 | Verified statically (bytes and decompile) | Official table of 7 names |
| AE 1.11.240 | Verified statically (bytes and decompile) | Official table of 8 names |
| NG 1.10.984 | Decompile-only; no byte verification, no unpacked image | Same rules as OG by decompile; no separate claim |

`OfficialTable::og_ng` is the OG table and the NG decompile table. `OfficialTable::ae` adds `DLCDummy10thAnniversary.esm`. The installed-data test picks the AE table for 1.11.x builds and the OG/NG table otherwise.

## Plugin list

- UTF-8, with or without a BOM; CRLF or LF line endings.
- Lines starting with `#` are skipped. Lines shorter than 2 characters after the line ending is removed are skipped.
- A leading `*` marks the entry active and is stripped before the duplicate test. A bare name is listed but not active.
- Exact duplicates (after `*` is stripped) are dropped and reported as `duplicate_entry`; the first occurrence keeps its active flag.
- File order is kept. Each entry records its 1-based physical line number.

## Fallout4.ccc

One name per line. A UTF-8 BOM is stripped, whitespace around each name is trimmed, blank lines are skipped, and file order is kept. The engine lower-cases the names; Radicle matches names case-insensitively instead, so the stored text is not lower-cased.

## Assembly

Stage one, the file list:

- Every `*.esm`, `*.esl` or `*.esp` directly in the data directory with a non-zero size. Extensions are matched case-insensitively. Zero-byte files are ignored.
- Master-flag files come first, then the rest. Master-flag means the TES4 master flag (0x1) or an `.esm`/`.esl` extension. Light means flag 0x200 or an `.esl` extension.
- Within each group: last-write time ascending, then case-insensitive name. The record says the engine's tie order follows directory enumeration; the name tie-break is a Radicle choice.
- Only each file's TES4 record is read (the 24-byte record header and its data). A file whose TES4 record cannot be read is dropped and reported as `unreadable_header`. A TES4 data size above 16 MiB counts as unreadable and is rejected before the data is read.

Stage two, the vector, in this order, skipping files already placed:

1. Official names present in the file list, in table order.
2. `Fallout4.ccc` names present, in file order.
3. `local_80`, master-flag files: plugin-list names (all lines, active or not) present in the file list whose every MAST name is present, in plugin-list order.
4. All remaining master-flag files, in stage-one order.
5. `local_80`, non-master files.
6. All remaining non-master files.

Master hoist, run only when the cycle check below finds no cycle: walk the vector. For a master-flag file F at position `pos`, scan its MAST names in MAST order. Each present master M at or after `pos` is moved to `pos`, ahead of F and of the masters moved earlier in the same scan, so independent masters end in reverse MAST order. The walk does not advance after a move, so the moved masters are processed next. Masters already earlier stay where they are. A plain `.esp` dependent is never reordered.

Cycle refusal: before the hoist, Radicle runs a depth-first search over the present master-flag files, following edges only to present master-flag masters. A cycle, including a file that lists itself, is reported as `hoist_cycle` naming the cycle's files, sets `failed`, and skips the hoist. The hoist keeps a backstop of `n*n + n` moves, where `n` is the vector length. The record says the engine has no cycle guard; past the backstop the hoist reports `hoist_cycle`, sets `failed`, and stops instead of looping.

Checked state (the startup run of the engine):

- Official and `Fallout4.ccc` names present are checked.
- Active plugin-list names in `local_80` are checked. An active plugin whose masters are not all present is never checked and is not a failure.
- `Fallout4.esm` is checked when present. This stands in for the sTestFile fallback.
- Each checked file's masters become checked, recursively. A missing master of a checked file fails the assembly (`missing_master`). A master whose HEDR version is above 1.0 fails it (`master_too_new`). The record describes the recursion's failure path as "result becomes the return"; Radicle treats any failure in the recursion as a whole-assembly failure.
- A checked file whose own HEDR version is above 1.0 is unchecked and reported as `version_unchecked`. This is not a failure. The comparison is against the float 1.0 exactly.

## Findings

| Code | Meaning | Failure |
|---|---|---|
| `missing_file` | Listed in the plugin list or `Fallout4.ccc` but not in Data (zero-byte files count as missing) | No |
| `duplicate_entry` | Plugin-list line repeating an earlier entry | No |
| `unreadable_header` | TES4 record unreadable; file dropped | No |
| `missing_master` | Checked file lists a master that is not in Data | Yes |
| `version_unchecked` | Checked file with HEDR version above 1.0 | No |
| `hoist_cycle` | Masters form a cycle, or the master hoist exceeded its move cap | Yes |
| `master_too_new` | Master of a checked file has HEDR version above 1.0 | Yes |

`failed` is returned with the assembled entries. Entries show the state reached when the assembly stopped.

## Out of scope

- The order of files that are not listed anywhere (the "remaining files" tie order) is a Radicle choice, documented above.
- `DLCList.txt`.
- sTestFile INI settings. Radicle always applies the `Fallout4.esm` fallback and does not read the INI.
- Mod-manager virtual file systems. Radicle reads the real directory or profile files given to it.
- The runtime index table. `build_runtime_form_id_context` in `Radicle/src/plugin/load_order.cpp` is unchanged, and callers supply it.
- External capacity limits (launcher, CK, plugin list writer). The record leaves these unresolved.
- Runtime consequences of an ESP master listed after its dependent. The record leaves these unresolved; Radicle keeps the plugin-list order for ESP dependents.
- Two files with the same name in different directories. Only the data directory is read.

## Tests

- `Radicle/tests/test_load_order_files.cpp` covers the parsing rules, both tables, ccc ordering, hoisting, light and master flags, checked rules, too-new versions, zero-byte files, unreadable headers and the cycle refusal, using synthetic plugins.
- `Radicle/tests/test_c_api_loadorder.cpp` covers the C ABI: argument checks, the unknown table, IO errors with cleared outputs, and the happy path freed with `radicle_free_string_array`.
- `Radicle/tests/test_load_order_real.cpp` runs on installed data when `FO4_DATA_DIR` is set. It assembles the installed Data with an empty plugin list and the game root's `Fallout4.ccc`, and checks that `Fallout4.esm` is first, the installed official plugins follow in table order, and the present `Fallout4.ccc` entries follow in file order.
- `Radicle.Interop.Tests/LoadOrderTests.cs` covers the C ABI through the C# wrapper.
