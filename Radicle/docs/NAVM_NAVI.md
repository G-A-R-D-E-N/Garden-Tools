# NAVM and NAVI stored read surface

Compatibility evidence is pinned to TES5Edit/TES5Edit commit
`93cc0bc5a1251936c3c7859eee3150eda12a62d7`, `Core/wbDefinitionsFO4.pas`:
`wbNVNM` lines 4360–4482, NAVM 7212–7238 and NAVI 7240–7332.
The existing checked grammar is `src/schema/form_link_mixed.cpp`:
`navmesh_mixed_formid_offsets`. It validates every counted section and the
complete payload before the new read surface exposes fields.

NVNM version 15 starts with version, pathing-cell factory key, world link and a
four-byte parent union: CELL link when world is null, signed grid Y/X otherwise.
Counted vertices have three f32 values (12 bytes). Counted triangles are 21
bytes: three u16 vertex indices, three signed i16 edge values, f32 clearance,
one traversal byte and one raw u32 flags word. Edge links are 11 bytes (u32
type, NAVM link, signed i16 triangle, one edge byte); doors are 10 bytes
(signed i16 triangle, u32 CRC, REFR link). The remaining cover, cover-map,
waypoint and grid sections are shape-validated and returned as opaque bytes.
Only numeric traversal/flag values are exposed; unknown bit names remain
unassigned. Triangles gain this width at form version 0x3A; extra-edge records
gain their byte only above 0x7F. The current decoder requires both modern
layouts, so form versions below 128 fail closed.

NAVI NVER is 15 in this corpus. Each NVMI stores NAVM link, raw flags,
approximate vec3 location, f32 preferred percentage, counted NAVM edge and
preferred links, and counted CRC/REFR door links. Optional island geometry
is validated and returned opaque. The final 12 bytes hold pathing CRC and
the same world/parent union. NVPP decodes through `decode_navmesh_paths` (below); NVSI remains a raw subrecord.
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
not a successful run of the new decoder or a pathfinding acceptance test. The shipped navmesh counts above are identical on OG 1.10.163, NG 1.10.984 and AE 1.11.240.
`test_navmesh_real.cpp` checks every record through native and C ABI reads
and unchanged whole-master byte identity when FO4_DATA_DIR is supplied.
Local and CI results are recorded separately for each published head.

The NAVI survey found 12,568 NVMI entries (7799/106/25/2645/23/37/1933
in the master order above), seven NVPP payloads and no NVSI. Every NVMI
and NVPP matched the existing checked grammar, with zero failures.

The newer [reader reconciliation](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/6916afbcbec8ab09ecc0dfd30661502d0064f4f7/RE-Work/subsystems/radicle-re-garden-tools/navigation-401-339/README.md)
withdraws the older CK note's extra-triangle-table interpretation. Sections
3–4 stamp the OG triangle and extra-edge readers at 0x14250FCD0 and
0x14250FFB0, with independently matched AE/CK readers. Legacy triangles
are 16 bytes below form version 0x3A; the extra-edge byte appears from 0x80.
Flag bits 0–2 identify extra-edge indices. The complete modern grammar
matches all 12,558 payloads. Cover edge fields and the cover map, the door portal and waypoint layouts, and the NVPP index formula are recorded under "Flags, links and NVPP" (RE-012) with inferred labels. Triangle bits 4 and 9 and a few NG routine counterparts remain unresolved; flag and traversal values stay raw rather than acquiring inferred names. Pathfinding remains outside stored-data authoring.

## Flags, links and NVPP (RE #401, comments 7878, 9765, 9785, 10005)

Sources: GARDEN-TOOLS #401 comments 7878, 9765, 9785 and 10005, #520 comment 13148 (RE-012), and the evidence record at `re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research` commit `6916afbcbec8ab09ecc0dfd30661502d0064f4f7`, folder `RE-Work/subsystems/radicle-re-garden-tools/navigation-401-339`. Verification here is static (OG 1.10.163 decompile as primary, NG 1.10.984 for routine matching, CK 1.11.137 for the NVPP gate, AE and NG readers, the seven installed masters). Nothing was run at runtime.

- Triangle bits 0 to 2 are verified. When set, edge slot i indexes the extra-edge array, not the triangle array. `NavmeshTriangle::edge_is_extra` reports the bit, and `decode_navmesh` throws `WorldDecodeError` when a flagged index is at or past the extra-edge count. `test_navmesh_real.cpp` checks, for each official master, that the set bits 0 to 2 total the extra-edge record count.
- Bits 5, 8 and 13 to 15 are runtime-only and are never serialized. The writer refuses any flags-word change (`TriangleFlagChangesRefuseWithoutChangingBytes` and the 32-bit flip loop in `CAbiExportsValidateInputsAndRoundtrip`, both in `tests/test_navmesh_authoring.cpp`).
- Bits 16 to 18 are verified as reads: the triangle split tests `[+0x10]` against 0x10000, 0x20000 and 0x40000, one per edge. The clear mask is 0x70000, not 0x700. The label "boundary edge marker" is inferred. Bit 16 + i is set where edge i has no neighbor, with exceptions (187 flagged edges in 183 triangles of Fallout4.esm).
- Bit 10 is set on 1,181 of the 1,184 door triangles across the seven masters and on no other triangle surveyed. Fallout4.esm has 955 door triangles, none missing it; the 3 exceptions are all in DLCNukaWorld.esm. The label is empirical; the bit is tested and cleared at runtime (RE-012). The corpus test asserts 0 exceptions on Fallout4.esm.
- Bit 11 is set on 99.50% of shipped triangles (5,552,909 of 5,580,668 across the seven masters); the earlier bit 19 claim was wrong. Bit 11 is the "visited" mark of the island flood-fill (#520 comment 13394, RE-012, evidence commit 9c47a1c9, `RE-Work/subsystems/radicle-re-garden-tools/navigation-401-339/`). The CK 1.11.137 island pass (`0xeecc80`, reached from the navmesh check `0xeff370`) returns early if the triangle count is at most 1; otherwise it clears bit 11 on every triangle, floods from triangle 0 and from every triangle still clear, and the flood sets `0x800` and recurses over neighbor edges whose triangle flag bit e is clear. The runtime uses the same flood: OG `0x743eb0` sets it at `0x743f62`, and NG `0x729650` and AE `0x77d0d0` (Address Library id 2212257) do too. No code on OG, NG or AE clears bit 11 across the triangle array. Fallout4.esm alone has bit 11 on 99.993% of its triangles (3,664,597 of 3,664,866), which is the earlier 99.99% figure. Across the seven masters, 27,759 triangles in 116 navmeshes are clear (70 of them entirely clear, 42 of those 70 with a single triangle). Candidate causes are a pass not completed before save or triangles appended later; neither is traced. Fallout4.esm has 13 partly-clear navmeshes whose clear triangles form a contiguous suffix. Traversal is non-zero on 1,618,139 triangles. Bit 6 has five runtime readers (RE-012). Bits 4 and 9 not found (immediate-operand search only; bit 4 is set on 15 triangles, bit 9 on 50). Traversal bit 0x08 has a setter, a runtime command that sets and clears it (RE-012); the other traversal bit names are unresolved. Fallout4.esm triangle record 0x0003970A has bit 9 on 50 of its 1,146 triangles.
- Extra-edge type 0 is a cell portal. Types 1 and 2 also occur on disk and type 3 does not. A zero destination form ID is not followed, and portals are not assumed reciprocal: 2 of 269,278 Fallout4.esm portals lack a reverse link. The corpus test counts two type-0 records whose destination edge flag is clear.
- NVMI: the scalar read from disk is only the initial value. After a rebuild, OG 1.10.163 overwrites the runtime field with the float ratio count(traversal & 2) / triangle count (comment 10005). The NG NVMI reader is at 0x1D45860 and matches the AE reader byte for byte, 263 of 263 instructions (comments 9785 and 10005).
- NVMI flag 0x20 means the record has bounds. Flag 0x40 marks exactly the 14 NVPP anchors; its runtime role is not decoded. NVMI list B holds the preferred links. In the OG search, list A costs three times list B, so list B costs one third of list A. AE and NG were not examined for this.
- NVPP layout is verified: u32 N, then N lists of (u32 k, k form IDs), then u32 M, then M pairs (form ID, u32 value). `decode_navmesh_paths` returns the lists and anchors and fails closed on truncation or trailing bytes. Fallout4.esm has N = 196 and M = 14. The list region is identical across the masters and the 14 pairs differ, so NVPP blobs are not byte-identical across masters. The NVPP list index is (mapCapacity - freeCount) * value(src) + value(dst); the stride is the map's live entry count (14 in shipped data), not a literal (RE-012).
- Partly answered per #520 comment 13148 (RE-012). Verified: the NVPP index formula above, and the CK 1.11.137 gate: its first NVPP call site reads only when the navmesh version is >= 0xF, and the second site is the < 0xF path. This corrects comment 10005 item 6. Partly answered: a door portal's first u32 is CRC32("PathingDoor") = 0xE48B73F3 and its second u32 is a REFR form ID (961 of 961 resolve in Fallout4.esm). Waypoints are three f32, a u16 triangle index and a u32 flags word, loaded only when the version is >= 0xE; all 11,060 shipped waypoint flags are 0 and no flag consumer was found.
- Still open: 21,632 unexplained cover vertex entries; no OG writer found for cover fields 5 and 6; cover labels inferred; waypoint flag consumer not found; why 27,759 shipped triangles have bit 11 clear; bits 4 and 9 not found (bit 4 is set on 15 triangles, bit 9 on 50); NG: 25 OG/NG routine pairs matched, OG 0x24FA400 and 0x769E40 unmatched, NG bit-10 sites matched by count only.

Cover data (RE-012, partly answered): the cover edge word has fields 2 to 6. Field 2 is a chain flag with mask 0x80000000. Fields 3 and 4 are 3-bit codes in 8-unit steps, with lower and upper bounds. Fields 5 and 6 are one-bit flags mapped to result bits 0x8 and 0x10. These field labels are inferred, and no OG writer was found for fields 5 and 6. The cover map key is triangle * 4 + edge, and its value is an index into the cover array. In a cover word, bits 0 and 1 mark each u16 endpoint as a fraction times 1/65535 along the edge instead of a vertex index. 21,632 cover vertex entries remain unexplained (RE-012).

## Editing

| Capability | Coverage |
|---|---|
| READ/WRITE | Indexed NAVM vertices, triangles and door links through native/C ABI/C#; fixed NAVI info fields and NAVM links through W0 |
| ADD/REMOVE/CREATE | Refused for counted or coupled geometry and info structures; NAVM creation requires parent placement which the generic creator cannot supply |
| REMAP | Existing checked mixed-link grammar; numeric topology and opaque bytes preserved |
| OPAQUE | Unknown triangle traversal byte, cover/waypoint/grid tails, island bytes and unsupported layouts preserved or edits refused |

Edits preserve array sizes. Triangle vertex indices must fit the existing vertex array; the unknown traversal byte and complete raw flags word must remain identical. Vertex and height values must be finite. Door references must be non-null, use an existing master, and resolve to REFR when local. These are stored-data edits; no derived navigation rebuild is implied.

`RADICLE_ABI_VERSION` (`Radicle/include/fo4/c_api.h`) is pinned by `Radicle/tests/test_c_api_quest.cpp:CApiQuest.AbiVersionIsSeventyEight`; it was 45 when this section was written. Current validation and exact-head CI counts belong in the PR description. `tests/test_navmesh_real.cpp:expect_geometry` and `expect_info` check native/C value agreement, including opaque bytes and resolved keys. Untouched body records are byte-identical; TES4 checks allow the existing serializer's regenerated ONAM metadata and size markers.

The earlier GardenEngine audit at `10ea81f1590b06dad962ff86721c0a55f00ef173`
found vocabulary entries without a NAVM/NAVI geometry consumer. That audit
was not rerun during this follow-up. Current consumer adoption and the
GardenEngine #146 C ABI probe remain separate, unverified acceptance work.

## GardenEngine consumer probe

Copy `Radicle/probes/gardenengine/NavmeshProbe.cs` to `opencw/tools/NavmeshProbe.cs`
and `Radicle/probes/gardenengine/navmesh_probe.tscn` to `opencw/tools/navmesh_probe.tscn`.
Build GardenEngine with `RadicleSourceDir` set to this Radicle checkout so the probe
compiles against the `Radicle.Interop` binding at this PR head.

Run from the GardenEngine root with `FO4_DATA_DIR` set to the Fallout 4 `Data`
directory and `RADICLE_NATIVE_PATH` set to the built Radicle native library:

```
godot --headless --path opencw tools/navmesh_probe.tscn
```

Exit codes: 0 prints `navmesh_probe PASS` with a one-line JSON summary; 77 prints
`navmesh_probe SKIP: FO4_DATA_DIR unset`; 1 prints `navmesh_probe FAIL` with the exception.

The PASS JSON carries `NavmeshLinks`/`NavmeshUnresolved`, `WorldLinks`/`WorldUnresolved` and
`CellLinks`/`CellUnresolved`: present NVMI links per kind and how many did not resolve through `env.Winner`.

Assertions:

- Exactly one NAVI record exists in `Fallout4.esm` (`WinnersOf("NAVI")`), matching the
  installed-data table above, and `NavmeshInfos` decodes at least one NVMI entry.
- For each NVMI kind (navmesh, parent world, parent cell), counts present and unresolved links.
  The probe fails only if a kind has present links and none of them resolve; unresolved links are reported, not failed.
- The first NAVM whose `Navmesh` decodes with vertices and triangles greater than zero has every
  triangle vertex index below its vertex count, and its present parent world and cell links resolve.

No vanilla editor IDs are used: this repo cites none for NAVM or NAVI, so the probe walks
records by signature instead.

Not yet run; needs a GardenEngine checkout whose Radicle pin is this PR head (GardenEngine pins ABI 9 at 34d16d50).
