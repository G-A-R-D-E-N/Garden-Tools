# RE request: what the engine accepts from a generated previs tome (.uvd)

Requested 2026-11-13. For the RE machine. Nothing here needs a build on our side; every answer is read from the
engine or from a CK/engine run.

## Why this is needed

Our CK-free previs generator now writes a real Umbra tile-model tome instead of the old geometry-page layout. It is a
conservative tome: one open tile for the cell box, one cell that lists every object, six outside portals, no
occluders. The layout is proven only structurally: it round-trips byte for byte on all 2,587 retail tomes
(1,422 vanilla + 1,165 Previsibines Repair Pack), and the writer reproduces the four shipped fully open one-tile tomes
from their boxes, ids and `expand` values (the `expand` rule itself is a guess, question 3). Nothing proves the game accepts a tome we write, and several field values are guesses.

## Versions

| Item | Version | MD5 |
|---|---|---|
| `Fallout4.exe` (Steam, installed) | 1.11.240.0 | `c5791a0ce539465701c6e38fa465960c` |
| `CreationKit.exe` (from the CK control-surface notes) | 1.11.246.0 | `56c00500b33a6ccd84a667016b831be9` |

Tomes measured: `Fallout4.esm` and DLC vanilla (banner Umbra `3.3.17`), and the Previsibines Repair Pack
(`prp - main.ba2`, banner `3.3.22`).

## Links

- Issues: RE requests for previs generation, G-A-R-D-E-N/GardenTools#1093 (Umbra tome payload generation, part 1).
- PRs (stacked): #1263 patching and CI fixes; #1269 node tile model; #1271 C++ tile model; #1272 conservative writer.
- Our measured layout and every guess: `Radicle/docs/PREVIS.md`, sections "Tile model (measured)" and "Minimal tome (measured)".
- The RE material already on hand: `2026-11-13-ck-agent-control-surface/2026-11-13-previs.md` and
  `CK-CORPUS-CROSSREF.md` (neither is in this repo; UNVERIFIED from this checkout) (Umbra is statically linked in the CK; the tome is produced inside it).

## What is already known (do not re-derive)

- Container: magic `0xD6000012`, CRC-32C over `[8,end)` at `+4`, size at `+8`, banner at `0xBC`, header `0x14C` bytes.
- Tiles: box, cells (36 B), portals (16 B), planes, per-tile kd tree and leaf map, BSP; header holds objects (boxes, ids,
  optional distances), run lists A/B, gates, clusters, slot paths, LOD floats (`cbrt(volume)/512`), leaf-match rows.
- Opaque to us: cell list start/count semantics beyond the measured shapes, run list contents, gate and cluster-portal
  records, leaf-match row words, tile flag bits above bit 0, what tile `expand` (`+0x30`) selects, u16 AABB dequantisation.

## Questions (answer each separately)

1. **Single open tile.** Retail ships four tomes shaped exactly like ours (one leaf tile, one cell with the full
   `0..65535` box, six outside portals, no distance table, no gates). Does the runtime treat such a tome as
   "everything listed is visible"? What does the runtime do when the camera is outside the tile box, and outside the
   world box (`0x14`/`0x20`)?
2. **Object ids.** Header user ids (`0x50`) are reference form IDs (high byte `00`/`01`/`02`) or `0xFD`-prefixed
   combined-object ids on retail. How does the runtime map a tome id to a scene object? What happens for an id that has
   no matching object, and for a scene object that is not listed in the tome (visible, culled, or error)? Are ids
   matched by form ID after load-order remap, or by raw value?
3. **`expand` (tile `+0x30`).** What does it control, what values are legal, and does a value outside retail's
   observed set (16/32/64/128, plus 256 on the four fully open one-tile retail tomes) risk a load failure or a
   culling error? The conservative writer's longest-side guess clamps to 16..128 (`Radicle/src/previs/uvd_tile_build.cpp`,
   `tools/previs/uvd_tile_build.mjs`); that ceiling is below the 256 values those open tomes store, so the clamp is
   our guess, not a retail upper bound. The rule remains unverified.
4. **Cell object lists.** For a cell that lists K objects from list start 0 (run list A `(0, K)`, widths `1, bitlen(K)`),
   is K above 10 handled normally? Is there a hard limit (our own tests build tomes with up to 5,000 objects; retail's one-tile tomes list at most 10)?
5. **Distance table (`0x48`).** We omit it (retail omits it in 437 of 1,422 vanilla tomes). Does omission mean "no
   per-object distance cull" or a default distance?
6. **Load-time checks.** Which header fields and values does the loader validate (banner Umbra version string
   `3.3.17` vs `3.3.22`, the `0x14C` flag, `0xD15AB5ED` CRC retry seen in the CK cache writer, tile counts, alignment)?
   What is logged or returned when a tome is rejected, and what happens to previs for that cell?
7. **Version coupling.** Does the loader require `VISI`/`PCMB` dates or the cell's `RVIS` root to match the tome in any
   way that depends on tome contents (for example slot or object counts)?

## Answer format

For each question: a one-line answer (yes/no/value), the function and address in `Fallout4.exe` 1.11.240.0 (and the CK
address if it differs), a short excerpt or pseudocode, and which of our fields it constrains. If a question is cheaper to
settle by running the game than by reading code, a runtime test is acceptable: load a tome built by
`write_uvd_tome_tiled` (PR #1272) for one vanilla interior and one exterior cell and report the observed behaviour with
the log lines.

## What the answers unblock

- Whether the conservative writer can become the only generator and the legacy page writer and
  `patch_uvd_page`/`patch_page.mjs` can be deleted.
- The object id rule (Q2) and the `expand` rule (Q3), which are currently guesses.
- Whether a multi-tile or occluder-carrying writer is needed at all, or the open tile is enough for mod cells.
- #1093 part 1: what, if anything, still needs the Umbra codec.
