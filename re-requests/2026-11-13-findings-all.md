# Findings: LAND VNML recompute, plus the navmesh relocate answers

Compiled 2026-11-13. Everything below is static analysis (Ghidra 12.1 headless plus a byte scan of the PE) and one offline replay against `Fallout4.esm`. No game and no Creation Kit was driven; no Radicle code was changed.

| Input | Version | Identity |
|---|---|---|
| `CreationKit.exe` (local Steam) | 1.11.246.0 | Ghidra project `CKGhidra-CKRE`, program `CreationKit.exe` |
| `Fallout4.exe` old gen | 1.10.163 | corpus build 1, sha `e8cb7c3d...ccbee1`, project `fwgh_proj_1.10.163_unpacked` |
| `Fallout4.exe` AE | 1.11.240 | corpus build 3, sha `05d15840...f23f2b`, project `fwgh_proj_1.11.240_unpacked` |
| `Fallout4.esm` (vanilla data) | Steam data | 37,020 LAND records |

---

# Part 1 - the Creation Kit's edit-time VNML recompute

## 1.1 Where the code is

| RVA | Role |
|---|---|
| `0xd0f530` | **the recompute**: per quadrant, per triangle, accumulate, normalise, write |
| `0xd00820` | one-time build of the static tables (512-triangle list, quadrant origins) |
| `0xd004c0` | vertex position for `(quadrant, index)` |
| `0xd005b0` | read one stored normal (signed bytes to float, scale `1/127`) |
| `0xd006f0` | write one stored normal (float to signed bytes, scale `127`) |
| `0x24d00c0` | normalise 289 float3 vectors, stride 12 (shared math helper) |
| `0xd0f240` | pre-save wrapper; called by the LAND writer `0xd07350` |
| `0xd09d60`, `0xd0b430` | landscape brush operations that also call the recompute |

Call sites of `0xd0f530` (E8 rel32 scan): `0xd0b1bd` in `FUN_140d09d60`, `0xd0c5af` in `FUN_140d0b430`, and twice in `FUN_140d0f240` (`0xd0f32a`, `0xd0f344`). So the normals are rebuilt on a height edit **and** again immediately before a save. That is why a hand-edited file and a CK-saved file can differ even when the heights match.

## 1.2 Storage on the object

```
LAND_data  = *(TESObjectLAND + 0x48)
heights    : float[4][289] at LAND_data + 0x20     (quadrant-major, q*289 + i)
height base: float         at LAND_data + 0x6410   (added to every height)
normals    : int8[4][289][3] at LAND_data + 0x55EC (0x4860 + 0xD8C)
```

Note the two views of the same data: the object holds **four overlapping 17x17 arrays** (1156 vertices), while the **file** holds one **33x33** grid. A vanilla `VNML` subrecord is exactly **3267 bytes** (`33*33*3`), not `4*289*3 = 3468`. Checked on all 1,921 LAND records scanned: every one is 3267.

## 1.3 The formula

Positions, per quadrant `q` and index `i` in `0..288`:

```
base_x[4] = {-2048.0, 0.0, -2048.0, 0.0}   // default INI mode; a second INI-driven path exists
base_y[4] = {-2048.0, -2048.0, 0.0, 0.0}
SPACING   = 128.0                          // world units per grid step

x = (i % 17) * 128.0 + base_x[q]
y = (i / 17) * 128.0 + base_y[q]
z = heights[q*289 + i] + *(float*)(LAND_data + 0x6410)
```

Quadrant `q` reads indices `x = 0..16` or `16..32`, `y = 0..16` or `16..32` (offset `((q&1)*16, (q>>1)*16)`), so `q1 = +x`, `q2 = +y`, `q3 = +x+y`.

Per triangle, the three face expressions exactly as compiled (P0, P1, P2 are the three vertices in list order):

```
f16 = (P2.y - P1.y)*(P1.x - P0.x) - (P2.x - P1.x)*(P1.y - P0.y)   // = +cross_z
f15 = (P2.z - P1.z)*(P1.y - P0.y) - (P2.y - P1.y)*(P1.z - P0.z)   // = +cross_x
f17 = (P2.x - P1.x)*(P1.z - P0.z) - (P2.z - P1.z)*(P1.x - P0.x)   // = +cross_y
```

With `d1 = P1 - P0` and `d2 = P2 - P1`, the triple `(f15, f17, f16)` is `(cross_x, cross_y, cross_z)` of `d1 × d2` (same as the `+cross_*` annotations on the face expressions above; not `(-cross.x, -cross.y, +cross.z)`). Flat terrain collapses it to `(0, 0, +)`, which is what vanilla flat cells store (`0,0,127`); there is no further negation.

Full loop:

```
for q in 0..3:
    acc[289] = {0,0,0}
    for t in 0..511:                              // 512 triangles per quadrant
        (A, B, C) = tri[t]
        f = (f15, f17, f16)                       // above
        L = f15*f15 + f17*f17 + f16*f16           // SSE order: ((x*x)+(y*y))+(z*z)
        s = 1.0f / L                              // literal DIVSS; see 1.6
        acc[A] += f * s ; acc[B] += f * s ; acc[C] += f * s
    normalise_289(acc, stride 12)                 // 0x24d00c0
    for i in 0..288:
        int8[i].x = (char)(int)(acc[i].x * 127.0f)  // truncation toward zero
        int8[i].y = (char)(int)(acc[i].y * 127.0f)
        int8[i].z = (char)(int)(acc[i].z * 127.0f)
```

Constants read out of the image: `127.0f` at `0x303d528`; `1/127` as `0x3C010204` (0.0078740157) at `0x30ac608`; the helper's `DIVSS` numerator `1.0f` at `0x2f9cfd4`; accumulator reset `0.0` at `0x7443e10/e14/e18`.

## 1.4 The triangle list (built in `0xd00820`)

512 triangles over the 17x17 index grid. For cell `(r, c)` in `0..15`, `v = 17*r + c + 1`, and index `(x, y)` means `x = i%17`, `y = i/17`:

```
if ((c ^ r) & 1) == 0:
    tris = { (c+1,r+1), (c,r+1), (c,r) } , { (c,r), (c+1,r), (c+1,r+1) }
else:
    tris = { (c,r+1), (c,r), (c+1,r) } , { (c+1,r), (c+1,r+1), (c,r+1) }
```

The checkerboard diagonal is the whole point: it changes which triangles touch a vertex, so a fixed per-vertex stencil cannot reproduce it.

## 1.5 Validation against shipped data

`.probe/ck_vnml_replay.py` (in the Garden-Tools workspace) decodes vanilla LAND `Fallout4.esm 0000F540` with the chained `VHGT` layout, runs this triangle list and these expressions over the four quadrants, normalises with IEEE `sqrt`, truncates with `127.0f`, and diffs against the stored `VNML`:

| quadrant | exact bytes (interior vertices) | mean miss on the rest |
|---|---|---|
| 0 | 179 / 256 | 3.1 |
| 1 | 187 / 256 | 2.7 |
| 2 | 175 / 256 | 2.3 |
| 3 | 185 / 256 | 3.0 |

Total across quadrants: **726 / 1024 exact (71%)**, mean miss 2.75 units out of 127 (about 1.5 degrees). Two controls show the test discriminates rather than agreeing by luck:

- negating the triple (`-f15, -f17, -f16`) gives **0 / 1024** at a mean miss of about 250 units, so the sign convention is pinned by measurement;
- a single-running-sum `VHGT` decode (the wrong layout) gives absurd slopes and a mean miss around 128.

The residual is not formula error: a shipped record only carries `VHGT` deltas quantised to 8 units, while the CK computed from the unquantised floats still in `LAND_data + 0x20`. Feeding the CK's own floats would remove it.

## 1.6 What is still open (and the cheap way to close it)

1. **The per-face divisor.** The decompiled step divides by `L` (the squared length), and the disassembly confirms a `DIVSS` of `1.0f` by `L` at `0x24d015c` inside `0x24d00c0`. A division by the squared length is not a normalisation, so either the incoming vector is already near unit length or the real weight lives elsewhere. Measured both ways on `0000F540`: normalising each face gives 726/1024 exact (mean miss 2.8); summing raw cross products gives 677/1024 (mean miss 2.5). On this one record they are close, because the quantisation noise is bigger than the difference. That is a 49-byte gap inside noise, and a larger sample (`Radicle/docs/code-notes/radicle-forms.md`, interior rows: per-face unit 38.1 % against raw sums 31.2 %) favours per-face normalisation, which is what `Radicle/src/forms/land_normals.cpp:compute_land_normals` does. **Recommendation for `edit_land_heights`: keep per-face normalisation until the live CK round trip below settles the divisor**; raw sums (area weighting, no per-face division) are the alternative to test there.
2. **The sqrt table.** `DAT_147445730` is a pointer, not data: `FUN_1424d0070` runs `DAT_147445730 = FUN_14248c220()`, and `FUN_14248c220` allocates `0x400` bytes and fills 256 entries with `SQRT(asfloat(0x3f800000 | (i<<16))) & 0x7fffff` (low half) and `SQRT(asfloat(0x40000000 | (i<<16))) & 0x7fffff` (high half) - mantissas of `sqrt` over `[1,4)`. It is therefore reproducible in code, not a missing constant. Index selection in `0x24d00c0`: `idx = mantissa>>16` for odd exponents, `0x80 + (mantissa>>16)` for even ones; reconstruction is `asfloat(0x3f800000 | table[idx])`.
3. **Seam ownership.** The object keeps four overlapping arrays and the save collapses them into 33x33; which quadrant wins at internal index 16 is the writer's business and was not pinned. Every vertex the replay compared is interior to its quadrant.

The definitive test for 1 and 2 is one live CK round trip: set a quadrant's heights, save, diff the resulting `VNML` against the port. That is the only way to compare against unquantised floats.

## 1.7 Why it matters: the CK does not do what Radicle approximates

- The per-face weighting is unresolved; see section 1.6. A per-vertex 4- or 8-neighbour average cannot be assumed bit-identical to a triangle-based calculation.
- The CK works from **float heights**, not decoded `VHGT`. Any path that regenerates normals from decoded quantised heights is already off by up to half a height quantum per vertex.
- The CK recomputes **immediately before saving** (`0xd0f240` called from `0xd07350`), so "the CK will fix my normals on load" and "the CK will fix them on save" are both misleading: it recomputes from its own arrays only, for the record it is saving.

---

# Part 2 - the runtime questions (LAND)

Answered in `2026-11-13-land-normals.md`; summarised here because reading `0xd0f530` confirms them.

| Question | Verdict | Evidence |
|---|---|---|
| 2 Does the engine read stored `VNML` or rebuild from `VHGT`? | **must rewrite** - the runtime loads the stored normals and quadrant-remaps them; it never rebuilds from heights | OG `FUN_1403a09e0`: `VHGT` compare at `0x3a0b9b`, `VNML` at `0x3a0bc2`, `VCLR` at `0x3a0bf4`; normals land at `LAND_data + 0x55EC`, colours at `+0x4860` (`0x4860 + 0xD8C == 0x55EC`); AE counterpart at `0x4b23f4` / `0x4b7075` with the same sequence |
| 3 Do LOD generators read `VNML` or `VHGT`? | **partial** - xEdit core never reads `VNML` (marked benign); xLODGen and the CK LOD build were not examined | xEdit `wbDefinitionsCommon.pas` `wbLandNormals` |
| 4 Does the CK fix neighbouring cells' border normals on save? | **ignored** - it writes one LAND from its own arrays, with no neighbour fix-up | `0xd07350` writes only the record it is saving; the recompute is per object (`0xd0f530` takes one LAND) |

The practical consequence: stale or approximated normals are **not** corrected by anything downstream - not by the runtime, not by xEdit, not by the CK on load. They persist until something recomputes that object, and only the CK's own edit or save path does.

---

# Part 3 - navmesh relocate (`2026-11-13-navm-relocate.md`)

| Question | Verdict | Evidence |
|---|---|---|
| 1 Does the engine merge `NVMI` per navmesh, or use only the winning `NAVI`? | **ignored or rebuilt by the engine** - entries are merged one navmesh at a time into a single map, so a patch `NAVI` carrying only the moved navmesh is sufficient | OG `FUN_14074db8b` (NAVI chunk load; `NVMI` at `0x74dc09`, `NVPP` at `0x74de6f`): `new(0x60)` then deserialise, key on the entry's `NAVM` link, map lookup, insert if absent, replace that one entry if present. AE `0x7823e6` carries the same sequence with different jump displacements |
| 2 Does a stale `NVPP` break pathing, crash, or get ignored? | **open** - `NVPP` is parsed into the same info map, but the consumer that indexes the preferred-path lists was not decompiled | `0x74de6f`; index formula recorded in `Radicle/docs/NAVM_NAVI.md` |
| 3 Boundary bit `16+i` on a slot whose portal was removed | **open** | extension point: `RE-012` edge/portal reader, bits 16-18 at `[+0x10]` against `0x10000/0x20000/0x40000` |
| 4 Door `XTEL` destination: resolved from the reference or used as stored? | **open** | `XTEL` did not appear in the OG tag scan; needs the same immediate-scan treatment |

Next bounded targets for part 3, in order: the `NVPP` index consumer from the global info map at `0x1438cc980` (answers 2); the boundary-bit reader at `RE-012` (answers 3); `XTEL` in the runtime image (answers 4).

---

# Part 4 - method and tooling notes (for whoever repeats this)

- **Chunk tags are compare immediates.** Byte-scan the image for the little-endian tag (`VNML` = `0x4C4D4E56`, `VHGT` = `0x54474856`, `VCLR` = `0x524C4356`, `NVNM` = `0x4D4E564E`, `NVMI` = `0x494D564E`, `NVPP` = `0x5050564E`, `NAVM` = `0x4D56414E`), map each hit to its `.pdata` function, then decompile by RVA. This is far cheaper than string hunting and finds every reader and writer of a subrecord.
- **Static tables may be empty in the file.** The CK stores only `0x18b200` bytes of `.data` while `.data` is `0x447deb9` bytes virtually, so the triangle list and quadrant origins read as zeros from the PE. They are filled at run time: a raw-byte scan for RIP-relative references (`FindRipRefs.java`, matching any ModRM with `mod=00, rm=101` to a target RVA) finds the initialiser `0xd00820` immediately, and decompiling **that** gives the table contents in source form.
- **Pointers that look like arrays are often pointers.** `DAT_147445730` read as zeros and looked like missing data; it is assigned from a generator function at start-up, and the generator is fully decompilable. Always check for a function that stores to the address before concluding a table is unobtainable.
- **Ghidra's decompile of a software-pipelined loop lies about the divisor.** `0x24d00c0` renders as `1.0f / (x*x + y*y + z*z)` and reuses one variable for both the input exponent bits and the output sqrt bits. Only the disassembly (`DumpBytesAt` plus the `DIVSS`/`MULSS` listing) settles which value is divided by which.
- **Verify a recovered formula against shipped data before writing it down.** The replay caught two errors that read plausibly in prose: the sign convention (0/1024 when negated) and the axis assignment in the vertex index (x and y swapped gives near-horizontal normals). A doc-only derivation would have shipped both.
- **Tooling that did not work here, and the workaround.** The `f4-re-mcp` server fails in this workspace (`RE_MCP_FO4RE_DB_PATH` unset, Ghidra project paths from a previous machine, see `E:/RE-work/f4-re-mcp-setup/SETUP.md`), so analysis was done directly with `analyzeHeadless.bat`. `DecompileAt.java`'s `.pdata` parse throws (`Index 136 out of bounds for length 128`), so pass exact function starts taken from the corpus `functions` table. Stock scripts `FindRipRelativeDataRefs` and `FindDispXrefs` produce nothing on these `-noanalysis` projects; the local `FindRipRefs`, `FindCallsTo` and `FindTagXrefs` scripts in `.probe/ghidra-scripts` do.

## Evidence files

These files are not committed in this repository; the claims that rest on them are UNVERIFIED from this checkout.

| Path | Contents |
|---|---|
| `.probe/ck-d00820.txt` | decompile of the table initialiser (triangle list and quadrant origins) |
| `.probe/ck_tables.py` | PE reader for the static tables (proves they are zero in the file) |
| `.probe/ck_vnml_replay.py` | the replay of the recovered formula against vanilla LAND `0000F540` |
| `.probe/flat_vnml.py` | flat-cell scan that fixes the sign convention (`0,0,127`) |
| `.probe/ghidra-scripts/FindRipRefs.java` | byte-level RIP-relative reference scanner |
| `.probe/ghidra-scripts/FindCallsTo.java` | E8/E9 call-site scanner |
| `.probe/ck-norm-setters.txt`, `ck-norm-parts.txt`, `ck-landread.txt`, `dec-ck-d07350.log` | raw decompiles and logs behind the RVA table |
