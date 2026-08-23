# bgsterrainwatermeshbuilder.cpp — decompiled (CK 1.11.137, OG image)

Closes ROADMAP Phase 4 item "Decompile bgsterrainwatermeshbuilder.cpp body".
Raw artifacts: `ghidra-dump/decompile-FUN_140f8ec20-watermeshbuilder.txt`,
`ghidra-dump/decompile-watermeshbuilder-cluster.txt`.

## Address correction

The roadmap's range `0xf76000-0xf83fff` is stale for this image — it lands in
pathing code (`PathingRequest*` strings). The real file members were located
by their `__FILE__` assert string
(`STR 0x30f5820 = e:\buildagent\...\distantterrain\bgsterrainwatermeshbuilder.cpp`):

| function | RVA | size | role |
|---|---|---|---|
| `FUN_140f8e1b0` | 0xf8e1b0 | 1436 | per-cell sheet build from a cell list |
| `FUN_140f8e760` | 0xf8e760 | 1207 | worldspace walk variant (follows parent-worldspace chain via the +0x19 has-parent byte) |
| **`FUN_140f8ec20`** | 0xf8ec20 | 1195 | **core**: one subdivided water sheet + triangle emission |
| `FUN_140f8f0e0` | 0xf8f0e0 | 191 | thin wrapper: iterate cell array, caller-supplied subdivision |

## Core routine (FUN_140f8ec20)

```c
FUN_140f8ec20(builder* this, void* meshTarget, float* waterHeight,
              int cellOffX, int cellOffY, uint subdivisions)
```

* Extent: `cellsPerSide = *(s32*)(this+0xC)`; world span = `cellsPerSide << 12`
  (cells -> units, 4096 u/cell). Builder origin cell at `*(s16*)(this+8)/(+0xA)`.
* Vertex spacing: `DAT_142f8beb4 / subdivisions`; positions are
  `(cellOff + i*spacing) * DAT_142f95154`, asserted `0 <= p <= span`
  (assert sites = .cpp lines 0x145/0x146).
* Vertices are `BSHEAttribute<NiPoint3>` payloads (0x18 B: vtbl, x@8, y@C,
  z@10); each vertex object (0x78 B) comes from the target's allocator
  (vtbl+0x90) and is registered under the hash-tag `"pos"` (`0x736F70`) in an
  attribute map; missing tag inserts into a std::list (0x3FFFFF cap check).
* **Z = `*waterHeight` on every vertex** - the sheet is flat; height arrives
  wholesale from the WRLD water-height chain (XCLW feed documented in doc 23
  section 10b.2 / xLODGEN_TERRAIN_INTERNALS WaterDelta context).
* Triangulation: per quad, indices `(r*n+c, r*n+c+1, (r+1)*n+c)` and
  `(r*n+c+1, (r+1)*n+c+1, (r+1)*n+c)` emitted through
  `thunk_FUN_142662490(target, vA, vB, vC, 0)`.

## Wrappers

* Both entry wrappers resolve each CELL's grid coords via
  `FUN_140045d1d` (X) and `thunk_FUN_140d196a0` (Y), subtract the builder
  origin, then call the core with **subdivisions = 1** - shipped LOD water is
  one quad per cell.
* `FUN_140f8e1b0` allocates a 0xC0-byte mesh node per cell
  (`thunk_FUN_142663140(mem, 0x200000)`); empty builds (vertex count 0) are
  freed immediately.
* `FUN_140f8e760` additionally walks the worldspace parent chain
  (+0x19 has-parent byte, doc 23 section 10b WNAM semantics) collecting cells
  from parent worlds too.

## Subsystem map (bonus, from the same string-xref sweep)

`__FILE__` strings pin every other `distantterrain` source file to functions:

| source file | functions (RVA) |
|---|---|
| bgsterraindataprocessing.cpp | 0x323560, 0x323ee0, 0x324960, 0x325c80, 0x326210, 0x3268b0 |
| bgsterraintexturebuilder.cpp | 0xf892f0 |
| bgsterrainnode.{h,cpp,inl} | 0xf8af80..0xf8ce90 (16), 0xfc4b60..0xfc79b0 (18), 0xfd04f0 |
| bgsterrainupdatemanager.cpp | 0xf8af80, 0xf8b380, 0xf8b610, 0xf8b850, 0xf8bca0, 0xf8bee0, 0xf8c140 |
| bgsterrainvisibilitydata.cpp | 0xf8d210, 0xf8d2b0 |
| bgsterrainobjectblockbuilder.cpp | 0xf93620 (8015 B) |
| bgsterrainobjecttextureatlasbuilder.cpp | 0xf9e9d0, 0xfa0560 |
| bgsdistantobjectblock.cpp | 0xfa2aa0..0xfa7100 (12) |
| bgsdistanttreeblock.cpp | 0xfacd50..0xfaf8a0 (13) |
| bgsterrainchunk.cpp | 0xfb7440..0xfbc4b0 (8) |
| bgsterrainmanager.cpp | 0xfc0720, 0xfc0fd0, 0xfc11f0, 0xfc2000, 0xfc2390 |
| bgsterrainutils.cpp | 0xfcdcd0, 0xfce200 |
| bgsterrainvisibilityfinder.cpp | 0xfcf770 |
| bgsterrainvisibilitydatadb.cpp | 0x103a3c0, 0x103a7c0 |

Tools added: `scripts/DecompileRange.java`, `scripts/XrefsTo.java`,
`scripts/StrSubstringXrefs.java` (headless, project `/tmp/ckre-ghidra CKRE`,
program `CreationKit.exe`).
