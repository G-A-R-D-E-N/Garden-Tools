# Radicle code notes

Long-form explanations for Radicle source. Each entry is anchored to the
`<file>:<symbol>` it documents; the source keeps only single-line constraint
comments. Format notes live under `Radicle/docs/`.

## Previs, phase 1 (read side)

Phase 1 is native-only: no Creation Kit anywhere, on any machine. It reads the
precombine/previs containers, resolves the CELL layer, applies the section 1.3
consistency checks of `previs-generator-plan.md`, and reports which cells a load
order broke. Writing `.cdx`, `.csg` and `.uvd` files is later-phase work; native
Umbra tome writing was decided for phase 4, so no source comment may call it an
open decision.

### XCRI wire format

Anchors: `Radicle/src/previs/xcri.cpp:parse_xcri_bytes`,
`Radicle/src/previs/xcri.cpp:write_xcri_bytes`,
`Radicle/src/previs/xcri.cpp:decode_combined_index`.

Payload layout is `u32 mesh_count`, `u32 word_count`, then `mesh_count` little
endian `u32` OC ids, then `word_count` little endian `u32` words read as
`(REFR local id, OC id)` pairs. `word_count` is always even and the payload size
is always `8 + 4*mesh_count + 4*word_count`; both conditions are wire
invariants, checked before any index is used. This contradicts xEdit's
declaration of 8-byte pairs, so the parser owns the pair split and the CELL
layer only resolves each first word to a FormKey through the Environment
(`decode_combined_index`). Every vanilla payload parses and re-serializes to the
same bytes, including the 4,600 Fallout4 cells, so a future change to either
direction is caught by `tests/test_previs_real.cpp`.

Form version: no vanilla XCRI appears below form version `0x72`; the measured
set is entirely form version `0x83`. Bytes below the measured minimum must be
reported as a skip, never parsed and never condemned.

### Packed dates and staleness

Anchors: `Radicle/include/fo4/previs/xcri.hpp:PackedDate`,
`Radicle/src/previs/xcri.cpp:decode_packed_date`,
`Radicle/src/previs/xcri.cpp:is_precalc_stale`.

PCMB and VISI store a 16-bit packed date: year `2000 + (v >> 9)`, month
`(v >> 5) & 15`, day `v & 31`. The record header `version_control` field uses
the same encoding; measuring all 40,165 Fallout4 CELLs and their children gives
only plausible values (years 2000 to 2026, months 1 to 12, days 1 to 31), which
is the evidence for treating it as a date. A cell is stale when the maximum
`version_control` over the winning CELL record and its same-plugin, non-partial
children is newer than its PCMB or VISI date. Both sides zero means unknown and
never stale.

Staleness is reported with its own `outdated` severity, never as broken: the
rule comes from a third-party tool's prose rather than from engine code, so a
stale cell warns that restamping it is expected work, but it is not proof that
the shipped data is unusable.

### CDX container and index

Anchors: `Radicle/include/fo4/previs/cdx.hpp:CdxRow`,
`Radicle/src/previs/cdx.cpp:parse_cdx`,
`Radicle/src/previs/cdx.cpp:CdxIndex`.

`<plugin>.cdx` is `bcdx`, a `u32` row count, then 16-byte rows. A row is
`(word0, word1, word2, word3)`. `word1` is a full Form-ID whose high byte is the
master index in the `.cdx` plugin's own master list: on exterior rows it names a
WRLD, on interior rows an interior CELL. Exterior `word0` packs
`(x & 0xFFF) << 20 | (y & 0xFFF) << 8 | kind`, with `x` and `y` signed 12-bit
grid coordinates; interior rows carry `kind` in the low byte and a zero grid.
Interior kinds are `0x80` and above (measured `128, 129, 131, 132, 134, 135`);
exterior kinds are below `0x80` (measured `0, 1, 2, 3, 4, 6, 7`). The row table
is sorted ascending by `(word1, word0)` in every shipped file, and the last two
words are opaque: their meaning is not established, so a writer must preserve
them verbatim.

Rows are not per cell: Fallout4.cdx has 1,460,027 rows for roughly 4,300
precombined cells, because each cell gets one row per resource. The detection
surface therefore needs one lookup per cell, not a scan per cell, so `CdxIndex`
builds a hash set keyed on `(form, grid)` where `grid` is `word0 >> 8` (zero for
interior rows); duplicate resource rows collapse into the same key. Lookup must
never scan 1.46M rows per cell.

### CSG container

Anchors: `Radicle/include/fo4/previs/csg.hpp:CsgFile`,
`Radicle/src/previs/csg.cpp:parse_csg`,
`Radicle/src/previs/csg.cpp:validate_csg`,
`Radicle/src/previs/csg.cpp:decompress_csg_chunk`,
`Radicle/src/previs/csg.cpp:write_csg`.

`<plugin> - Geometry.csg` is `bcsg`, `u32 row_count`, `u32 chunk_count`, then
`chunk_count` 8-byte chunk entries, then `row_count` 20-byte rows, then the
compressed chunk payloads. A chunk entry is `(compressed size, absolute offset)`
- that order is the opposite of the naive guess and was wrong in an earlier
draft of this code. Chunks are contiguous from `12 + 8*chunks + 20*rows` and end
exactly at EOF in all six shipped files, every chunk starts with the zlib header
`78 9c`, and every chunk inflates to 65,536 bytes except a smaller final chunk
(the single `DLCworkshop01 - Geometry.csg` chunk inflates to 22,872 bytes). The
20-byte row table is opaque: it has no visible cell id, so its contents are
preserved as raw bytes and never interpreted. `.psg` (the clean-mode twin) does
not exist in vanilla data and its layout is unmeasured, so a `.psg`-shaped file
must fail closed instead of being guessed at.

The inflation cap must be 65,536 bytes and not a multiple-sized guess: a
`size * 1032 + 64` bound would allocate about 55 MB per chunk, which on
`Fallout4 - Geometry.csg` is hundreds of gigabytes of allocation across the
file. The final chunk may be smaller, so the cap is an upper bound checked by
the inflater returning `Z_STREAM_END`, not an exact-size requirement.

### UVD tomes

Anchors: `Radicle/include/fo4/previs/uvd.hpp:UvdFile`,
`Radicle/src/previs/uvd.cpp:parse_uvd`,
`Radicle/src/previs/uvd.cpp:validate_uvd`,
`Radicle/src/previs/uvd.cpp:crc32c`.

A visibility tome starts with magic `0xD6000012`, a `u32` CRC-32C at offset 4,
a NUL-terminated Umbra banner at offset `0xBC` (for example
`T 512.0 ... 3.3.17 ...`), is a multiple of 16 bytes, and is at least `0x100`
bytes. The CRC covers bytes `[8, end)` and uses standard CRC-32C (Castagnoli
polynomial, init and xorout `0xFFFFFFFF`). This CRC variant is an assumption
pinned by the real-data test over all 1,422 shipped tomes; the shipped data is
the evidence, and a mismatch means the variant is wrong.

Tomes are not loose on a vanilla install: all 1,422 ship inside BA2 archives
(`Fallout4 - MeshesExtra.ba2` 966, `DLCCoast - Main.ba2` 246,
`DLCNukaWorld - Main.ba2` 183, `DLCRobot - Main.ba2` 15,
`DLCworkshop03 - Main.ba2` 9, `DLCworkshop01 - Main.ba2` 3). Names are stored
in mixed case, so matching is case-insensitive. Archives are the only source;
the reader indexes entry names and extracts one tome at a time, because eagerly
loading the archive keeps its full 1.3 GB resident per archive.

A parsed tome borrows the input image and copies nothing: validation needs the
whole image only while the caller keeps it alive.

### OC NIF naming

Anchors: `Radicle/src/previs/previs_data.cpp:PrevisData::load`,
`Radicle/include/fo4/previs/previs_data.hpp:OcNifIndex`.

Combined-object meshes are named `<cell %08X>_<id %08X>_OC.NIF` below
`Meshes\PreCombined\`. Cells owned by a plugin other than the base game put
them in a plugin-named subfolder (`Meshes\PreCombined\DLCCoast.esm\...`); base
game cells have no subfolder. The cell field is the low 24 bits of the cell
Form-ID and the id field is the XCRI OC id. Because the folder names the plugin
that owns the cell rather than the plugin that generated it, lookups are keyed
on `(plugin, low24 cell)` and never on a bare low-24 id: two plugins routinely
use the same local id. Measuring the six archives shows every OC NIF folder
matches only cells owned by that plugin (Fallout4.esm 4,682 / 4,682 cells,
DLCCoast.esm 1,217 / 1,217, DLCRobot.esm 4 / 4, DLCNukaWorld.esm 1,012 /
1,012, DLCworkshop01.esm 3 / 3, DLCworkshop03.esm 1 / 1).

### Tome and CSG lookup rules

Anchors: `Radicle/src/previs/previs_data.cpp:PrevisData::load`,
`Radicle/include/fo4/previs/previs_data.hpp:TomeIndex`,
`Radicle/include/fo4/previs/detect.hpp:LoadOrderIndexes`.

A cell's tome is named after its RVIS root cell, not after the cell itself.
The root is resolved through the winning CELL record's own plugin context, so
the file is `Vis\<root owner plugin>\<root local 24 bits %08X>.uvd`; the
filename carries the low 24 bits only, and the folder disambiguates the plugin.
When RVIS is absent the cell is its own root, so the lookup is
`Vis\<cell owner plugin>\<cell local 24 bits>.uvd`. Tome names must be searched
case-insensitively across every archive and loose file: DLC archives ship
`Vis\Fallout4.esm\` tomes as well as their own (Coast 38, Nuka 17, Robot 13,
workshop03 8), so a lookup limited to the plugin's own folder produces false
misses. A measured counter-example explains why the folder comes from the
Form-ID owner and not from the plugin that holds the winning version: a DLCCoast
override of a Fallout4 cell still resolves its tome to `Vis\Fallout4.esm\`.

The 20-byte CSG row table has no cell id, so "this cell has CSG rows" can only
be derived from the OC NIF set: a cell with at least one OC NIF is the only
observable evidence that a cell has geometry in a `<plugin> - Geometry.csg`.
That is a documented approximation of a container whose row meaning is
unestablished, not a guess about the row layout. The derived set is keyed on
`(plugin, low24 cell)` for the same collision reason as the NIF check.

### Detection rules

Anchors: `Radicle/src/previs/detect.cpp:diagnose_cell`,
`Radicle/src/previs/detect.cpp:diagnose_load_order`,
`Radicle/include/fo4/previs/detect.hpp:CellPrevisInput`.

Broken cells: RVIS set without VISI; VISI set without the named tome; a tome
that fails its CRC or structure check; XCRI with no CDX row for the cell; XCRI
with no CSG rows for the cell; an XCRI OC id with no `_OC.NIF` in the archive
set; a malformed XCRI payload. Legal in vanilla, so not broken: PCMB without
XCRI (Fallout4 2,321 cells, and those cells have no CDX rows), XCRI without
PCMB (Fallout4 19 cells), and VISI without RVIS (Fallout4 455 cells; the cell
is its own root).

Absent presence flags are only trusted when the corresponding index answered:
each input carries a `*_checked` flag, and a needed but unchecked index becomes
a skip with `skip_unsupported` instead of a false broken verdict. A plugin
without a `.cdx` is not unchecked - a Data folder scan proves absence - so an
XCRI cell there is broken, not skipped. A `.psg` file makes the CSG answer
unverifiable and therefore a skip, because its layout is unmeasured.

`diagnose_load_order` walks the winning CELL of every FormKey in a loaded
Environment and builds each input from the record data plus caller-supplied
index answers, so all file system work (archives, tomes, tome extraction)
stays in `PrevisData`. A CELL whose decode fails is reported broken with the
decode error instead of aborting the scan. Index answers take the winning
record's plugin and the cell/world FormKeys, never bare local ids, because a
load order resolves FormKeys across plugins.
