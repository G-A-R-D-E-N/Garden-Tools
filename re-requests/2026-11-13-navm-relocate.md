# RE request: engine behaviour for a navmesh moved to another grid (what plugin data cannot tell)

Date: 2026-11-13
Requested by: Radicle `relocate_exterior_cell` and `relocate_cell_navigation` (PR https://github.com/G-A-R-D-E-N/GardenTools/pull/1252, branch `nomad/cell-exterior-relocate`)
Executable version and hash: unknown (TODO-verify). No executable was opened; everything below comes from the shipped master plugin data.

## Settled from data (no RE needed; numbers in `Radicle/docs/code-notes/Radicle.md`, `cell_relocate_nav.cpp:relocate_cell_navigation`)

- NVNM x/y, waypoint, grid-bound fields and the parent grid are world-absolute and equal the CELL XCLC (7,113 of 7,113).
- NAVI NVMI: navmesh FormID, parent crc/world/grid (equal to the NVNM header, 7,799 of 7,799), location (mean of the NVNM vertices, 7,799 of 7,799), island bounds and vertices (world-absolute), edge lists A and B (A plus B equals the type 0 and 2 NVNM link targets, 7,799 of 7,799), doors (equal to the NVNM doors).
- NVNM external links (type 0 portals) are reciprocal: 269,276 of 269,278 have the exact reverse link; neighbours of a cell are all reachable from the cell's own link FormIDs (16,047 of 16,047 over 2,995 cells).
- Unlinked edges are stored as slot 0xFFFF with the extra bit clear; the boundary bit 16 + i is set on 554,692 and clear on 393,965 of them.
- Every shipped DLC NAVI is a partial override of the same FormID; the NVPP lists are identical across the masters tested (four of six DLCs).

## Open questions (unverified, TODO-verify by RE; plugin data cannot answer them)

1. Does the engine read NVMI entries from every plugin's NAVI override and merge them by navmesh, or does it use only the winning NAVI? Does it rebuild NVMI (location, edge lists) at load, so that a patch NAVI holding only the touched entries is enough?
2. After the move, an NVPP preferred-path list still names the moved navmesh (415 navmeshes appear in the 196 lists). Does a path through a navmesh whose portal links were removed break pathing, crash, or get ignored? Is there a no-path encoding (shipped data has no empty list)?
3. Does the engine need the boundary bit 16 + i set on a slot whose portal link was removed, or is a clear bit (the state the slot had as a portal) accepted?
4. Door `XTEL` on a door in another cell that teleports into the moved cell stores a destination position and rotation. Is the position resolved from the destination door reference at runtime, or used as stored?

## Answer format needed

For each question: "must rewrite", "ignored or rebuilt by the engine", or "crashes or misbehaves if stale", with function or address evidence and the engine version (OG 1.10.163, NG 1.10.984, AE 1.11.240). For "must rewrite", the exact fields and formula.

## What the answer unblocks

Whether `relocate_cell_navigation` may report a moved cell with a navmesh as consistent: questions 1 to 3 decide whether NVPP rewriting or full NAVI merging is needed; question 4 decides the teleport fixup.
