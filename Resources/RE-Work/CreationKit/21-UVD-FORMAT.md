# 21 — The `.uvd` PreVis Scene Format (magic `0xD6000012`)

Status: **confirmed against real samples** (`ghidra-dump/scene-samples/*.UVD`) and the writer decompiles.
Scope: header + framing only. The payload (tile/portal/gate tables after `0x150`) is mapped by offset but not yet decoded.

## The compiler chain

The CK compiles PreVis scenes into per-cell `.uvd` files through a three-function
pipeline (all staged in `ghidra-dump/`):

```
FUN_14188e570                      PreVis export driver
  └─ FUN_1418802d0                 "tome" builder / orchestrator (UVD-TOME)
       ├─ builds version string  "... - %d.%d.%d F %d %d OG %g"  (engine ver 3.3.17)
       ├─ thunk_FUN_14187b340      per-cell scene compiler  (UVD-WRITER)
       │    ├─ header: memset 0x150, magic 0xd6000012 at +0
       │    ├─ payload: tile data, 16-byte aligned
       │    ├─ thunk_FUN_141883c60  assemble final buffer (header + payload)
       │    ├─ thunk_FUN_1418f37d0  CRC-32C over [8, 8+size)
       │    └─ puVar12[1] = crc     store CRC at +4
       └─ thunk_FUN_141876f40      disk writer / depthmap cache (UVD-CACHEWRITER)
            ├─ "_depthmaps.umbracache" cache path
            └─ same framing: size@+8, flags@+0x10, CRC recomputed at +4
```

The file name comes from `Data\Vis\%s\%08X.uvd` (`.rdata 0x14309da60`) — the
`%s` is the plugin name (`Fallout4.esm`), `%08X` is the cell/area FormID — used by
`FUN_140d276b0` / `FUN_140d27750` / `FUN_140d27800` (path builders, `UVD-PATHFNS`)
and the big orchestrator `FUN_140d1a5e0` (`UVD-ORCH`).

The `FUN_14182d570` function writes the *prefix* of the version string with
format `T %.1f SO %.1f SH %.3f BF %.0f F %x CS %.1f` (`.rdata 0x143219c48`);
`FUN_1418802d0` appends ` - %d.%d.%d F %d %d OG %g`.

## Framing (first 8 bytes) — verified

| off | size | value | notes |
|-----|------|-------|-------|
| +0x00 | u32 | `0xD6000012` | magic, written as `local_1b8[0]` |
| +0x04 | u32 | CRC-32C | **Castagnoli** poly `0x82F63B78`, covers `[8, 8+size)` (i.e. everything after the magic+CRC), computed with the CRC field zeroed |
| +0x08 | u32 | `size` | total file size; also the byte length the CRC covers (offset of the run = +8, length = value at +8) |

`FUN_1418f37d0` is a table-driven CRC-32 with the Castagnoli polynomial. The
writer computes it *after* assembling the buffer and storing `puVar12[1] = crc`.
The cache writer does the same dance: zero `+4`, compute, store `+4`, and even has
a retry for the reserved CRC value `0xD15AB5ED` (`-0x2ea54e13`): set flag at
`+0x14c`, zero the CRC, recompute.

Verified against all three samples:

```
Vis_Fallout4.esm_0000C2E9.UVD  crc=0x77d6097b  size=0x28d850 (2676816 = file size)  CRC32C MATCH
Vis_Fallout4.esm_0000C346.UVD  crc=0xaf42eec1  size=0x1eb7d0 (2013136 = file size)  CRC32C MATCH
Vis_Fallout4.esm_0000E7FC.UVD  crc=0x4a28e5fc  size=0x2842a0 (2638496 = file size)  CRC32C MATCH
```

## Header (offsets +0x0c .. +0x14f) — field-by-field against samples

All three samples agree on layout. Values below are sample A
(`0000C2E9`), with B/C ranges where they differ.

| off | size | sample A | B / C | meaning (from writer decompile) |
|-----|------|----------|-------|----------------------------------|
| +0x0c | f32 | `512.0` | const | tile size **T** (matches version string `T 512.0`) |
| +0x10 | u32 | `0` | const | flags (cache writer ORs in `1` or `2`; 0 in shipped files) |
| +0x14 | f32 | `-139264` | | world AABB minX |
| +0x18 | f32 | `-102400` | | world AABB minY |
| +0x1c | f32 | `11520` | | world AABB minZ |
| +0x20 | f32 | `-126976` | | world AABB maxX |
| +0x24 | f32 | `-90112` | | world AABB maxY |
| +0x28 | f32 | `17792` | | world AABB maxZ |
| +0x2c | u32 | `0x25e0` | `0x1c20`/`0x26e0` | `nodeCount << 5 \| flags` (`local_18c = iVar11 << 5`) |
| +0x30 | u32 | `0x150` | const | header size (336) |
| +0x34 | u32 | `0` | const | reserved |
| +0x38 | u32 | `303` | `225`/`311` | node/tile count (`nodeCount` = `+0x2c >> 5`) |
| +0x3c | u32 | `0x1c0` | `0x1a0`/`0x1c0` | tile-major stride (448) |
| +0x40 | u32 | `0x47a` | `0x2b0`/`0x340` | target count (1146) — also used by cache reader as list size |
| +0x44 | u32 | `0x1dbb0` | | relative offset into payload (cache reader: `param_2 + 0x44` = table base) |
| +0x48 | u32 | `0x24720` | | second payload offset / size |
| +0x4c | u32 | `0` | const | reserved |
| +0x50 | u32 | `0x2d660` | | third payload offset |
| +0x54 | u32 | `0x1a8ab` | `0x1a4aa`/`0x1a8aa` | near-constant (stride/hash) |
| +0x58 | u32 | `0x11d0` | `0xdb0`/`0x1240` | |
| +0x5c | u32 | `0x1ce0` | `0xe24`/`0x1619` | |
| +0x60 | u32 | `0x4b90` | `0x2840`/`0x3bb0` | |
| +0x64 | u32 | `0x284` | `0x1ed`/`0x275` | |
| +0x68 | u32 | `0` | const | gate count (`scene+0x17c`) — 0 in shipped files |
| +0x6c..+0x78 | u32 | `0` ×5 | const | reserved |
| +0x7c | u32 | `0x29e` | `0x1ae`/`0x282` | |
| +0x80 | u32 | `0x4fb0` | `0x2b30`/`0x3fb0` | |
| +0x84 | u32 | `0x8410` | `0x4cd0`/`0x71e0` | |
| +0x88 | u32 | `0xd10` | `0xa20`/`0xd60` | |
| +0x8c | u32 | `0x98` | `0x71`/`0x9c` | |
| +0x90 | u32 | `303` | `225`/`311` | `nodeCount` again |
| +0x94 | u32 | `12` | const | fixed count (12) |
| +0x98 | u32 | `0x680` | `0x530`/`0x6a0` | |
| +0x9c | u32 | `0x850` | `0x690`/`0x880` | |
| +0xa0 | u32 | `0x2e850` | `0x1f470`/`0x28ac0` | |
| +0xa4 | u32 | `0x281f90` | `0x1e3c10`/`0x278a20` | near payload end (file size − offset) |
| +0xa8 | u32 | `0x282910` | `0x1e4320`/`0x2793e0` | |
| +0xac | u32 | `0x92` | `0x70`/`0x83` | |
| +0xb0..+0xbb | — | `0` | const | padding |
| +0xbc | char[64] | version string | const | see below |
| +0x100..+0x14f | — | `0` | const | padding to 0x150 |

## Version string (offset +0xbc)

Decoded from the samples (identical across all three):

```
T 512.0 SO 128.0 SH 16.000 BF 100 F 0 CS 0.0 - 3.3.17 F 1 0 OG 0
```

- `T %.1f SO %.1f SH %.3f BF %.0f F %x CS %.1f` — prefix format
  (`.rdata 0x143219c48`, used by `FUN_14182d570`). T = tile size (matches the
  `+0x0c` float), SO/SH/BF/CS = compiler settings (portal "something" sizes /
  batch fraction / flags / cell size).
- ` - %d.%d.%d F %d %d OG %g` — suffix appended by `FUN_1418802d0`
  (`.rdata 0x14321b278`): `3.3.17` = engine build, `F 1 0` = two bool flags from
  `scene+0x54d/0x54e`, `OG 0` = float from `scene+0x53c`.

## Payload (offset +0x150 .. end)

Not yet decoded. Known structure facts from the writer:

- Total payload size is computed as
  `0x150 + Σ (tileSize + 0xF) & ~0xF` — every tile record is 16-byte aligned
  (`local_1b0` accumulator in `FUN_14187b340`).
- `+0x44` is a relative offset the depthmap-cache reader uses as a table base
  (`lVar12 = *(uint*)(param_2+0x44) + param_2`), iterating `scene+0x87c` items.
- Bytes at `+0x150` onward are high-entropy (looks compressed or hashed, not raw
  geometry).

## Notes / gotchas

- The same framing (magic/CRC/size) and much of the header is shared by the
  `_depthmaps.umbracache` cache file (`FUN_141876f40`), which additionally
  patches `+0x10` flags, `+0x13c/+0x140/+0x144/+0x148` (depthmap offsets), and
  `+0x14c` (reserved-CRC retry flag).
- CRC field is always computed with itself zeroed; reserved value `0xD15AB5ED`
  triggers a recompute with `+0x14c = 1`.
- `FUN_14190cd50` is the reader/consumer: it fills a header with defaults
  (`param_2[0] = 0xd6000012`, `param_2[3] = 0x7f7fffff` = FLT_MAX) before
  validating against disk data.
- The `Data\Vis\%s\%08X.uvd` family (`FUN_140d276b0` etc.) formats the path with
  `sprintf` into a 0x104-byte buffer and gates on record flags (`& 0x70000000`,
  checking `0x30000000`/`0x40000000`) before load — the exported-file reader.

## Verification

- `python3` CRC-32C (Castagnoli) reproduced the stored `+4` value on all three
  shipped `.UVD` samples (`ghidra-dump/scene-samples/`).
- Header field offsets were cross-checked field-by-field against the writer's
  stack writes in `FUN_14187b340` (e.g. AABB floats = `scene+0x88..0x9c`,
  `nodeCount<<5` = `scene+0x58c`).
