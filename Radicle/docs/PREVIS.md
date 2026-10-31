# Previs read side (Phase 1)

Native-only. No CreationKit dependency, no Creation Kit run anywhere. Native
Umbra tome writing is Phase 4 and is not an open decision. Patch-plugin-first
and skip-and-report live in `include/fo4/previs/detect.hpp` (plan D10/D11).

## Layouts (measured on vanilla)

- CDX: `bcdx` + u32 count + 16-byte rows `(word0, word1, word2, word3)`,
  ascending by `(word1, word0)` in all six files. `word1` is the full
  Form-ID of a WRLD on exterior rows (Commonwealth `0x3C`) or of an interior
  CELL; its high byte is the master index in that plugin's own master list
  (a DLC's own records are `01`). Exterior `word0` is
  `(x & 0xFFF) << 20 | (y & 0xFFF) << 8 | kind` with signed 12-bit grid
  coordinates (Commonwealth grid x -35..30, y -39..28); interior rows keep
  the kind in the low byte and a zero grid. Interior kinds are `128, 129,
  131, 132, 134, 135`; exterior kinds are `0, 1, 2, 3, 4, 6, 7`. `word2` and
  `word3` are opaque. `Fallout4.cdx` is 1,460,027 rows (8 + 1460027 * 16
  bytes); the six files total 2,413,992 rows. Rows collapse onto a cell key
  of `(form, word0 >> 8)`, so one lookup answers a cell instead of scanning
  1.46M rows.
- CSG: `bcsg` + u32 rows + u32 chunks + chunk table `(compressed size,
  absolute offset)` + 20-byte row table + payloads. Chunks are contiguous
  from 12 + 8*chunks + 20*rows and end exactly at EOF in all six files; each
  starts `78 9c` and inflates to 65536 bytes except a smaller final chunk
  (`DLCworkshop01 - Geometry.csg` has one chunk of 22872 bytes). Fallout4's
  first entry is `(53818, 702140)`: size 53818 at offset 702140; the file has
  32,370 rows and 6,841 chunks. The row table has no visible cell id and its
  meaning is unestablished, so it stays opaque. Only `bcsg` is accepted: the
  clean-mode `.psg` twin has no file in the measured data, so a `.psg`-shaped
  file fails closed until its layout is measured.
- UVD: u32 magic `0xD6000012`, u32 CRC-32C over bytes [8, end), banner
  C string at +0xBC (Umbra 3.3.17), file size multiple of 16 and at least
  `0x100`. Read and validate only; native tome writing is Phase 4. CRC is
  standard CRC-32C (Castagnoli, init/FXOR 0xFFFFFFFF); the real-data test
  pins this against every vanilla `.uvd` in the BA2 archives and must fail if
  the assumption is wrong. The parsed view borrows the file image (no second
  body copy). There is no loose `Data\Vis` on the measured install: all 1,422
  tomes ship in six General BA2 archives (`MeshesExtra` 966, Coast 246,
  Nuka 183, Robot 15, workshop03 9, workshop01 3) and names may be uppercase.
- XCRI: u32 meshes, u32 reference words, meshes x u32 OC ids, words x
  u32 words as (REFR local id, OC id) pairs. Size is 8 + 4*M + 4*W with
  W even. Contradicts xEdit's 8-byte-pair declaration; all vanilla payloads
  match the word-pair form: Fallout4 4,600, DLCCoast 1,499, DLCNukaWorld
  1,172, DLCRobot 116, DLCworkshop03 73, DLCworkshop01 3. No vanilla XCRI is
  below form version `0x72` (the measured cells are all `0x83`). One parser
  owns it (`parse_xcri_bytes`); the CELL layer (`decode_combined_index`) only
  resolves links through the Environment.
- OC NIF: `<cell %08X>_<id %08X>_OC.NIF` under `Meshes\PreCombined\`, with a
  plugin-named subfolder for a non-base plugin and no subfolder for the base
  game. The cell field is the low 24 bits of the cell Form-ID, so lookups are
  keyed on `(plugin, low24 cell)` and never on a bare local id. The six
  archives hold 176,569 of them.

## Tome and CSG lookup (measured on vanilla)

A cell's tome is named after its RVIS root cell, in
`Vis\<root owner plugin>\<root low 24 bits %08X>.uvd`. The root is resolved
through the winning CELL record's master context, so the folder is the
Form-ID owner, not the plugin that holds the winning overridden version (a
DLCCoast override of a Fallout4 cell still resolves to `Vis\Fallout4.esm\`).
When RVIS is absent the cell is its own root. Tome names must be searched
case-insensitively across every archive and loose file: DLC archives also
ship `Vis\Fallout4.esm\` tomes (Coast 38, Nuka 17, Robot 13, workshop03 8), so
a lookup limited to one folder produces false misses. The 20-byte CSG row
table has no cell id, so "this cell has CSG rows" is derived from the OC NIF
set; a cell with at least one OC NIF is the only observable evidence that it
has geometry in a `<plugin> - Geometry.csg`.

## Detection (section 1.3)

Broken cells: RVIS set without VISI; VISI set but its named tome is missing;
a tome that fails its CRC or structure check; XCRI with no CDX row for the
cell (exterior uses the WRLD plus XCLC grid, interior the CELL); XCRI with no
CSG rows for the cell; an XCRI OC id with no matching `_OC.NIF`; a malformed
XCRI payload; a CELL that fails to decode. Legal in vanilla, so never broken:
PCMB without XCRI (Fallout4 2,321 cells; they have no CDX rows), XCRI without
PCMB (Fallout4 19 cells), and VISI without RVIS (526 cells). A VISI cell
without RVIS whose cell-named tome is absent is a skip, not a verdict: the
cell-named convention is measured but not engine-verified (D11).

A `false` presence flag means "verified absent", never "not checked": each
index answer carries a `*_checked` flag, and a needed-but-unchecked index
becomes a skip with `skip_unsupported` instead of a false broken verdict. A
`.psg` file makes the CSG answer unverifiable and therefore a skip. Staleness
is its own `outdated` severity, never broken: the packed date lives in the
record header's timestamp field (offset 0x10), not in the `version_control`
field (offset 0x12, which decodes to non-dates). Measured on all 40,165
Fallout4 CELLs: PCMB stale 356 and VISI stale 471 using the timestamp field,
versus 67 and 67 using `version_control`; the same rule fires on the other
masters in the same way, so the timestamp reading is the one vanilla
supports. Staleness compares the maximum timestamp over the winning CELL and
its same-plugin, non-partial children with the PCMB or VISI date (zero means
unknown on both sides).

`diagnose_load_order` builds these inputs for every winning CELL from a
loaded Environment plus caller-supplied index answers: per-plugin CDX, the
plugin's CSG presence, the OC NIF set and a tome lookup that keeps all file
system work in `PrevisData`. A CELL that fails to decode is reported broken
with the decode error instead of aborting the scan.

## Load-order report gate (vanilla AE 1.11.240.0)

`diagnose_load_order` over the six masters reports 62,393 winning CELLs. The
pinned expectation in `testdata/previs/vanilla_1_11_240_broken.tsv` is 370
broken cells, which are true vanilla gaps, not false verdicts:

- 14 cells have XCRI but no CDX row: Fallout4 3, DLCCoast 4, DLCNukaWorld 7.
- 18 cells carry 538 XCRI OC ids with no `_OC.NIF`: Fallout4 3 ids,
  DLCCoast 8, DLCNukaWorld 527.
- 18 cells have no CSG geometry as a consequence (the same three plus the
  same five and nine cells).
- 359 cells in 65 cluster roots have VISI plus RVIS but no tome: Fallout4 3
  (1 root), DLCCoast 94 (17 roots), DLCNukaWorld 262 (47 roots). Those
  clusters ship no `.uvd` at all, and their own cluster cells share the gap.

Outdated is separate: PCMB stale occurrences 343 / 94 / 3 / 142 / 65 / 114 and
VISI stale 458 / 4 / 3 / 66 / 0 / 175 for Fallout4, DLCRobot, DLCworkshop01,
DLCCoast, DLCworkshop03, DLCNukaWorld. 207 VISI cells without RVIS are
reported as unsupported skips, and every other cell is answered by a checked
index (zero other skips). These numbers are measured from the shipped files;
they are not produced by the code under test.

## Driver, ABI and panel

`PrevisData::load` scans a Data folder: `<plugin>.cdx`, `<plugin> -
Geometry.csg`, every General BA2 (entry names only) and the loose `Vis` and
`Meshes\PreCombined` trees. Archives are indexed through the streaming
`Ba2Reader` and a tome is extracted only when a cell asks for it, because
`Fallout4 - MeshesExtra.ba2` is 1.3 GB and eagerly keeping it resident is not
affordable; the CSG files are read in full because the structure check needs
the whole image. Tome answers are cached per root and the CRC check reads all
1,422 shipped tomes on a cold scan.

The C ABI (`include/fo4/c_api_previs.h`, ABI 98) exposes the report as an
opaque handle with summary, per-cell and per-issue accessors; strings are
borrowed until the report is destroyed and the report borrows its
`radicle_env`. The FO4IDE RPC target `previs` (`Scan`, `GetDataFolder`) hands
the same report to the `PrevisPanel` as JSON. The RPC path loads the saved
on-disk load order, so unsaved editor edits are not part of a previs scan.
All verdict logic stays in Radicle C++; the panel only renders.
