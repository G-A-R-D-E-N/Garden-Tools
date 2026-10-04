# NAVM and NAVI stored read surface

Compatibility evidence is pinned to TES5Edit/TES5Edit commit
`93cc0bc5a1251936c3c7859eee3150eda12a62d7`, `Core/wbDefinitionsFO4.pas`:
`wbNVNM` lines 4360–4482, NAVM 7212–7238 and NAVI 7240–7350.
The existing checked grammar is `src/schema/form_link_mixed.cpp`:
`navmesh_mixed_formid_offsets`. It validates every counted section and the
complete payload before the new read surface exposes fields.

NVNM version 15 starts with version, pathing-cell CRC, world link and a
four-byte parent union: CELL link when world is null, signed grid Y/X otherwise.
Counted vertices have three f32 values (12 bytes). Counted triangles are 21
bytes: three u16 vertex indices, three signed i16 edge values, f32 height,
one unknown byte and two raw u16 flag words. Edge links are 11 bytes (u32
type, NAVM link, signed i16 triangle, one edge byte); doors are 10 bytes
(signed i16 triangle, u32 CRC, REFR link). The remaining cover, cover-map,
waypoint and grid sections are shape-validated and returned as opaque bytes.
No runtime meaning or valid range is inferred for flags, edges or height.

NAVI NVER is 15 in this corpus. Each NVMI stores NAVM link, raw flags,
approximate vec3 location, f32 preferred percentage, counted NAVM edge and
preferred links, and counted CRC/REFR door links. Optional island geometry
is validated and returned opaque. The final 12 bytes hold pathing CRC and
the same world/parent union. NVPP and NVSI remain available as raw subrecords.
Unknown versions and malformed or duplicate scalar fields fail closed;
absent geometry in a deleted NAVM remains absent.

The independent 2026-10-04 installed-data measurement found:

| Master | NAVM records | NVNM geometry | NAVI records |
|---|---:|---:|---:|
| Fallout4.esm | 7894 | 7894 | 1 |
| DLCRobot.esm | 85 | 84 | 1 |
| DLCworkshop01.esm | 8 | 8 | 1 |
| DLCCoast.esm | 2623 | 2623 | 1 |
| DLCworkshop02.esm | 1 | 1 | 1 |
| DLCworkshop03.esm | 37 | 37 | 1 |
| DLCNukaWorld.esm | 1918 | 1911 | 1 |
| Total | 12566 | 12558 | 7 |

Every NVNM payload was version 15 and fit the complete counted grammar,
with zero size/count/trailing-byte failures. This is a layout measurement,
not a successful run of the new decoder or a pathfinding acceptance test.
`test_navmesh_real.cpp` checks every record through native and C ABI reads
and unchanged whole-master byte identity when FO4_DATA_DIR is supplied.
CI results are the validation source; no local build is claimed.

The NAVI survey found 12,568 NVMI entries (7799/106/25/2645/23/37/1933
in the master order above), seven NVPP payloads and no NVSI. Every NVMI
and NVPP matched the existing checked grammar, with zero failures.

The private CK note `CreationKit/records/51-NVNM-HEADER.md` at
Creation_Kit_Research commit `9e55a42ab29810a2aaca13967da9e1b91f62c514`
contains reader/decompile verification stamps and several superseded
hypotheses. Its final section 8 reports a runtime/disk divergence. The
current shipped disk measurement above does not corroborate that section's
extra-table interpretation. Reconcile the exact CK build, stream adapter
and shipped payload before deriving runtime semantics or enabling mutation.
This read surface follows the measured stored layout only.

GardenEngine main `10ea81f1590b06dad962ff86721c0a55f00ef173` has vocabulary
entries but no NAVM/NAVI geometry consumer. Issue #146 must supply that
consumer before a real GardenEngine probe can exercise these getters.
