# PreVis `.scene` Format (decompiled writer) + `.uvd` runtime sibling (real-file byte parse)

> Companion to [09-PRECOMBINED-PREVIS.md](09-PRECOMBINED-PREVIS.md) — closes the
> open item *"The `.scene` binary format is undocumented — byte-parse a real
> `Data\Vis\Scenes\*.scene` next."*

## 0. The short version

The CK's PreVis generator (`FUN_1401181210`, doc 09) writes per-cell scene files to
`Data\Vis\Scenes\` and serializes them through a generic *document → stream* writer
(`FUN_141815c90`) whose byte layout is fully recovered:

```
u32 0xBECAFABE            magic
u32 0x0000000F            version
u32 0x00000006            sub-version
u32 count0                + count0 × int32
u32 0 ; elementA          × countA      (tag-terminated section A)
u32 1 ; elementB          × countB      (tag-terminated section B)
u32 4 ; u32 countC ; elementC × countC  (section C, count-prefixed)
```

`.scene` is an **editor-only intermediate**: a survey of every archive on this machine
(game `Data\*.ba2`, the 384-archive Fallen World modlist, PRP 2.3 GB, Rebuild Collection)
found **zero** shipped `.scene` files. What ships — in `Fallout4 - MeshesExtra.ba2` and
the DLC/PRP archives — is the compiled runtime sibling `.uvd`
(`Vis\<plugin>\<formid8>.UVD`), a *different* format (magic `0xD6000012`) that never
contains the `0xBECAFABE` magic. Real `.uvd` samples extracted from the game are
byte-parsed in §3; generating a real `.scene` requires the CK (see §4).

---

## 1. Where scenes come from (pipeline recap, doc 09)

- Per-cell generator `FUN_1401181210` emits:
  - exterior: `Data\Vis\Scenes\<Worldspace>_<Plugin>_%+02i_%+02i.scene`
  - interior: `Data\Vis\Scenes\Interior_<CellName>.scene`
- The path is backslash-normalized, every directory is created
  (`CreateDirectoryA` walk), then the scene object is handed to the writer wrapper
  `FUN_14181ae70`, then the document serializer `FUN_141815c90`.
- The paths are recorded per-cell so the runtime can stream the per-cell visibility
  data. The runtime-facing artifact is the `.uvd` sibling (see §3).

## 2. The decompiled writer (byte layout, decompile-confirmed)

### 2.1 `FUN_14181ae70` — writer wrapper

```
undefined1 FUN_14181ae70(undefined8 *doc, undefined8 path, undefined8 p3, undefined8 p4)
{
  thunk_FUN_142cdce20(&stream, path, 2, p4, 0xfffffffffffffffe);   // open output stream
  stream.vft = &PTR_LAB_143218158;                                 // stream vtable
  uVar1 = thunk_FUN_141815c90(*doc, &stream);                      // serialize document
  thunk_FUN_142cdcd40(&stream);                                    // close/destroy
  return uVar1;
}
```

### 2.2 `FUN_141815c90` — document serializer (the format)

Writes through the stream's vtable slot +8 (`Write(ptr, 4)` → `iVar == 4` guards
every step; any short write clears the health flag `param_2+1`).

| # | field | source | size |
|---|---|---|---|
| 1 | magic `0xBECAFABE` | `local_res8[0] = -0x41103502` | u32 |
| 2 | `0x0000000F` | literal `0xf` | u32 |
| 3 | `0x00000006` | literal `6` | u32 |
| 4 | `count0` | `*(int *)(doc + 0x64)` | u32 |
| 5 | `count0` × `int32` | array `*(doc + 0x58)`, stride 4 | count0 × u32 |
| 6 | `u32 0 ; elementA` × `countA` | `countA = *(int *)(doc + 0x14)`, array `*(doc + 8)`, element writer `FUN_141813f80` | — |
| 7 | `u32 1 ; elementB` × `countB` | `countB = *(int *)(doc + 0x2c)`, array `*(doc + 0x20)`, element writer `FUN_141819d10` | — |
| 8 | `u32 4` | section-C terminator | u32 |
| 9 | `countC` | `*(int *)(doc + 0x44)` | u32 |
| 10 | `elementC` × `countC` | array `*(doc + 0x38)`, element writer `FUN_141814120` | — |

Notes:

- Sections A and B are **tag-terminated**: each element is preceded by a one-u32 tag
  (0 or 1) written immediately before its serialization, so a reader loops
  `tag = read u32; if 0 → elementA; if 1 → elementB; if 4 → countC + elementC…; else stop`.
- Section C is **count-prefixed** (no per-element tag), matching its `countC` field.
- Section A also performs **deduplication** during write: an identity hash set
  (`local_58`, `FUN_141818840`) stores already-written elements and re-uses their
  indices (`local_40`/`lStack_48` index table, 0x18-byte records), i.e. elementA
  indices can be *shared/referenced* — the reader must accept index references, not
  just fresh elements.
- `elementB` (FUN_141819d10) likewise consults the dedup table
  (`lVar2 = param_2[2]`, hash buckets at `+8`, 0x18-byte records at `+0x10`, key =
  u64 at `obj+0xe`) before writing its payload.

### 2.3 Element serializers

**Element A — `FUN_141813f80`** (confirmed writes):
```
u32 countA2      = *(int *)(obj + 0x14)
u32 1
u32 0xc
[then the sub-array; Ghidra loses the loop body — the 0xc suggests a fixed
 12-field sub-record repeated countA2 times]
```

**Element C — `FUN_141814120`** (confirmed writes, flat struct):
```
u32 low64(obj+0x00)          // u64 value written low dword first
u32 high64(obj+0x00)
u32 (obj+0x01)               // packed struct — misaligned reads throughout
u32 low64(obj+0x0c)
u32 high64(obj+0x0c)
u32 (obj+0x14)
u32 (obj+0x03)
u32 (obj+0x1c)
u32 (obj+0x04)
u32 (obj+0x24)
u32 (obj+0x05)
u32 (obj+0x2c)
```
~11 u32s per element; two 64-bit values stored little-endian low-then-high, the rest
are small packed fields (offsets 0x03/0x04/0x05 are byte-granular).

**Element B — `FUN_141819d10`**: dedup-registering writer; the visible body is the
hash lookup; the payload write follows the `LAB_141819dab` join point (decompile
truncated there). Its key is the u64 at `obj+0xe`.

### 2.4 Document object field map (offsets used by the writer)

| offset | meaning |
|---|---|
| `doc+0x08` | elementA array (ptr array, stride 8) |
| `doc+0x14` | elementA count |
| `doc+0x20` | elementB array |
| `doc+0x2c` | elementB count |
| `doc+0x38` | elementC array |
| `doc+0x44` | elementC count |
| `doc+0x58` | int32 array |
| `doc+0x64` | int32 count |

## 3. Real-file byte parse: the shipped `.uvd` sibling

`.uvd` is what actually ships (editor output is compiled into it). Samples:

- Source: `Data\Fallout4 - MeshesExtra.ba2` — 966 files `Vis\Fallout4.esm\<formid8>.UVD`
- Also present in: `DLCCoast/DLCNukaWorld/DLCRobot/DLCworkshop01/DLCworkshop03 - Main.ba2`,
  PRP `prp - main.ba2` (1,165), Rebuild Collection (56).
- Extracted samples saved to `ghidra-dump/scene-samples/` (2.0–2.7 MB each).

### 3.1 Header (64 bytes, three samples)

```
0000C346.UVD  12 00 00 d6 c1 ee 42 af d0 b7 1e 00 00 00 00 44 00 00 00 00 00 00 08 c8 00 00 e0 c7 00 00 3c 46 00 00 f8 c7 00 00 c8 c7 00 00 8f 46 20 1c 00 00 50 01 00 00 00 00 37 01 00 00 c0 01 00 00
0000C2E9.UVD  12 00 00 d6 7b 09 d6 77 50 d8 28 00 00 00 00 44 00 00 00 00 00 00 08 c8 00 00 e0 c7 00 00 34 46 00 00 f8 c7 00 00 b0 c7 00 00 8b 46 e0 25 00 00 50 01 00 00 00 00 2f 01 00 00 c0 01 00 00
0000E7FC.UVD  12 00 00 d6 fc e5 28 4a a0 42 28 00 00 00 00 44 00 00 00 00 00 00 f8 c7 00 00 f8 c7 00 00 e0 45 00 00 e0 c7 00 00 e0 c7 00 00 86 46 e0 26 00 00 50 01 00 00 00 00 37 01 00 00 c0 01 00 00
```

### 3.2 Field decode — SUPERSEDED by doc 21

The definitive field-by-field decode (verified against the writer decompiles
`FUN_14187b340`/`FUN_1418802d0`/`FUN_141876f40` and CRC-reproduced on all three
samples) is now **`21-UVD-FORMAT.md`**. The table below is kept as the original
pre-writer parse history, with the important corrections noted:

- `+0x04` is **CRC-32C (Castagnoli `0x82F63B78`)** over `[8, 8+size)` — not a
  generic "hash/checksum". Verified: all three samples reproduce exactly.
- `+0x08` **is** the total file size (byte-exact: `0x1EB7D0` = 2,013,136 etc.) —
  the old "not byte-exact" reading was an arithmetic slip.
- `+0x0C` is **f32 512.0** — tile size **T** (the `0x44000000` u32 reading).
- The `+0x14` "count ≈51208" reading was **misaligned**: `+0x14..+0x28` are six
  f32 world-AABB corners (minX/minY/minZ/maxX/maxY/maxZ), e.g. C2E9 =
  (−139264, −102400, 11520) → (−126976, −90112, 17792).
- `+0x2C` is `nodeCount << 5 | flags` (C2E9: `0x25E0` → nodeCount 303); `+0x34`
  is `0x150` (header size), not 311 — the old grouping was one float off, exactly
  as the "misalignment" note suspected.
- `+0x38` = nodeCount (303/225/311), `+0x3C` = 448 tile stride (`0x1C0`), `+0x40`
  = target count, `+0x94` = 12 (constant).
- The version string at `+0xBC` is `T 512.0 SO 128.0 SH 16.000 BF 100 F 0 CS 0.0 - 3.3.17 F 1 0 OG 0`.

The file does **not** contain the `0xBECAFABE` `.scene` magic anywhere (scanned all
three samples) — `.uvd` is a distinct, independently written format.

## 4. How to obtain a real `.scene` (and the wine status)

- `.scene` is produced **only** by the CK's `-GeneratePreVisData` batch path
  (dispatcher bit `0x80000`, doc 13). No shipped or modded archive contains one
  (verified: base-game `Data\*.ba2`, 384 modlist archives, PRP, Rebuild Collection).
- The correct batch grammar is **space-separated after the colon** (doc 09):
  ```
  CreationKit.exe -GeneratePreVisData:Fallout4.esm clean all 1 2
  ```
  `clean|filtered`, scope `all|main|ints|other`, then `currentset totalsetcount`.
- Under wine the CK batch does boot (Xvfb + llvmpipe/DXVK); the first runs aborted at
  `MASTERFILE: Could not get file pointer for Fallout4.esm,clean,all,1,1, aborting
  automated process.` — the **comma** form passes the whole string as the plugin name.
  The space form above was staged but not re-run (per user direction to stop the wine
  attempts). If re-run, per-cell `Data\Vis\Scenes\*.scene` files appear under the
  scratch `Data` folder and can be byte-parsed against §2.
- Runtime `.uvd` samples for cross-referencing are already in
  `ghidra-dump/scene-samples/` (see §3).

## 5. Artifacts

| File | Contents |
|---|---|
| `ghidra-dump/scenewriter-fun14181ae70.log` | wrapper decompile |
| `ghidra-dump/scenewriter-fun141815c90.log` | document serializer (the format) |
| `ghidra-dump/scenewriter-element-1813f80.log` | element A writer |
| `ghidra-dump/scenewriter-element-1819d10.log` | element B writer (dedup) |
| `ghidra-dump/scenewriter-element-1814120.log` | element C writer |
| `ghidra-dump/previs-generator-0x1181210.log` | PreVis per-cell generator (scene naming, §1) |
| `ghidra-dump/scene-samples/Vis_Fallout4.esm_0000C2E9.UVD` | real shipped `.uvd` (2,676,816 B) |
| `ghidra-dump/scene-samples/Vis_Fallout4.esm_0000C346.UVD` | real shipped `.uvd` (2,013,136 B) |
| `ghidra-dump/scene-samples/Vis_Fallout4.esm_0000E7FC.UVD` | real shipped `.uvd` (2,638,496 B) |

## Open items

- ~~Re-tag the `.uvd` header tail as floats from +0x28~~ **done** — see `21-UVD-FORMAT.md`.
- ~~Confirm the `.uvd` semantics by locating its writer~~ **done** — `FUN_14187b340`
  (writer) → `FUN_1418802d0` (tome builder) → `FUN_141876f40` (disk/cache writer),
  documented with the full byte layout in `21-UVD-FORMAT.md`.
- Decode the `.uvd` payload tables (offsets `+0x44`/`+0x48`/`+0x50`, per-tile records).
- Element A's sub-array body and element B's payload write (Ghidra-truncated) — needs
  a disassembly pass at `0x1813f80`/`0x1819d10` tails.
- A real `.scene` byte parse (§4) once the CK batch can run.
