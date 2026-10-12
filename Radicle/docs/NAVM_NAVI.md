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
matches all 12,558 payloads. Cover/waypoint semantics, several triangle bits
and NG behavior remain unresolved; those values stay raw rather than acquiring
inferred semantic names. Pathfinding remains outside stored-data authoring.

## Editing

| Capability | Coverage |
|---|---|
| READ/WRITE | Indexed NAVM vertices, triangles and door links through native/C ABI/C#; fixed NAVI info fields and NAVM links through W0 |
| ADD/REMOVE/CREATE | Refused for counted or coupled geometry and info structures; NAVM creation requires parent placement which the generic creator cannot supply |
| REMAP | Existing checked mixed-link grammar; numeric topology and opaque bytes preserved |
| OPAQUE | Unknown triangle traversal byte, cover/waypoint/grid tails, island bytes and unsupported layouts preserved or edits refused |

Edits preserve array sizes. Triangle vertex indices must fit the existing vertex array; the unknown traversal byte and complete raw flags word must remain identical. Vertex and height values must be finite. Door references must be non-null, use an existing master, and resolve to REFR when local. These are stored-data edits; no derived navigation rebuild is implied.

ABI 31 advances main's ABI 28 once while retaining main and published exports and layouts. A re-bump is expected if another additive ABI PR merges first. Current validation and exact-head CI counts belong in the PR description. `tests/test_navmesh_real.cpp:expect_geometry` and `expect_info` check native/C value agreement, including opaque bytes and resolved keys. Untouched body records are byte-identical; TES4 checks allow the existing serializer's regenerated ONAM metadata and size markers.

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
