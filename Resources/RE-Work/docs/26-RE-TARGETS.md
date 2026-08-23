# 26 — High-value RE Targets: OBTE, String Tables, XCLC, RVA Rebase

Four targets investigated from the runtime/plugin RE wishlist. Three are fully
resolved against real Fallout4.esm data; one (bit-7 string semantics) is
documented to the limit of what the CK binary can prove.

---

## 1. OBTE / OBTS — object-template block structure ✅ resolved

The template system on NPC_ (and other actor records) is a **two-subrecord
pair**, and the old understanding ("OBTE = template formID") was wrong:

| subrecord | size | content |
|---|---|---|
| `OBTE` | 4 B | u32 — the **combination count** (equals the number of `OBTS` bodies that follow; 0x00000001 on simple records) |
| `OBTS` | variable | the **BGSMod::Template block body** (`bgsmod_template.cpp`, FUN_14034f010 read / FUN_140359f50 write) |

**OBTS body layout (validated on all 2,430 Fallout4.esm payloads and all
3,297 payloads across Fallout4/DLCCoast/DLCRobot/DLCNukaWorld — 0 size-formula
mismatches; every include formID resolves to an OMOD record):**

```text
offset  size  field
------  ----  ------------------------------------------------------
0x00    4     u32 Include Count     (count of 7-byte include rows)
0x04    4     u32 Property Count    (count of 24-byte property rows)
0x08    1     u8  Level Min
0x09    1     u8  (unused)
0x0A    1     u8  Level Max
0x0B    1     u8  (unused)
0x0C    2     s16 Addon Index
0x0E    1     u8  Default
0x0F    1     u8  Keyword Count     (u8, NOT u32 -- verified on real files)
0x10   4*K   u32 keyword formIDs    (K == Keyword Count)
0x10+4K     1  u8  Min Level For Ranks
0x11+4K     1  u8  Alt Levels Per Tier
0x12+4K     7*I include rows        { u32 OMOD formID; u8 AttachPoint;
                                     u8 Optional; u8 DontUseAll }
       24*P property rows           { u8 ValueType; u8[3] unused;
                                     u8 FunctionType; u8[3] unused;
                                     u16 Property; u16 unused;
                                     u32 Value1; u32 Value2; f32 Step }
```

- **The template block grammar:** one `OBTE` (u32 combination count) per
  record, then N combinations each `[OBTF (empty, optional)] [FULL (4B
  localized ID, optional)] OBTS`, then `STOP`. **The OBTE u32 equals the
  number of OBTS bodies** — 0 mismatches on all 630 template-bearing records
  in Fallout4.esm (2,430 bodies) and all 1,037 across the four shipped
  masters (3,297 bodies). `OBTF` and `FULL` are optional independently:
  2,040 of the post-OBTE FULLs sit in `OBTF FULL` pairs, exactly 6 are bare
  `FULL OBTS`.
- **Template-block FULL placement (corrects the earlier "ARMO emits OBTS
  with no FULL" claim):** ARMO does emit FULL inside the template block —
  of the 562 FULLs in multi-FULL ARMO records on Fallout4.esm, 502 sit after
  OBTE as template-combination names. The real invariant, verified with zero
  exceptions across all 1,037 template-bearing ARMO/WEAP/NPC_/FURN records
  on the four masters: **at most one FULL before OBTE (the record's own
  name), and every FULL inside the block is immediately followed by OBTS.**
  NPC_ may also carry a second name FULL after STOP; that one is outside the
  block.
- **FURN also carries templates** — 65 records / 65 bodies in Fallout4.esm,
  72 records across the four masters.
- **Every include formID resolves to an OMOD record** (7,342/7,342 in
  Fallout4.esm; 10,615/10,615 across the four masters) — the template
  includes are Object Modifier records, not actor bases.
- xEdit's FO4 definitions agree on the field set and both row shapes; the
  **one divergence is the keyword count width** — xEdit declares it as a
  `-4` (u32) array marker, but real files use a **u8 at body+0x0F**
  (confirmed against all 3,297 four-master payloads). The 7-byte include and
  24-byte property rows match exactly.
- **Correction to the earlier draft:** the header is 16 bytes + keywords,
  not a fixed 18-byte header with counts at 0x00/0x01. The 18-byte bodies
  seen in the master are keyword-free 16-byte headers + 2 trailer bytes
  (min-rank/alt-levels), not a different header shape.
- Template-bearing records in Fallout4.esm: 630 total — 250 ARMO,
  202 NPC_, 113 WEAP, 65 FURN (2,430 OBTS bodies). Four-master totals:
  1,037 records / 3,297 bodies.

**Note on "blocks ARMO FULL":** the earlier reading of this report ("a parser
that stops at the first OBTE is over-strict") was itself wrong. The ~500
ARMO FULLs after OBTE are *real data* (template names), so any consumer that
stops consuming subrecords at the first OBTE silently loses them. The CK
reads OBTE/OBTS in place (LoadRecord at FUN_14034f9b0) and continues;
consumers must do the same.

---

## 2. String-table resolution — .STRINGS / .DLSTRINGS / .ILSTRINGS ✅ resolved (CK side)

**Which table a field resolves from is decided by the record reader, not by
bits in the string ID.**

Evidence from the CK:

- The three tables are separate singleton file objects. The .DLSTRINGS block
  loader is `FUN_140914f10` (the earlier doc's `0x140915000` is off by a
  thunk boundary) and it consults `DAT_143b9bf90` (DL table), with a
  **.STRINGS fallback** (`PTR_s__STRINGS_143b9be90`, FUN_14091a230) when the
  DL lookup misses (`LOCALIZATION: Lookup failed for ID: 0x%08X from
  persistent strings file: %s`).
- 39 callers dispatch into the loader via thunk `0x14000181b` — each record
  field's reader passes the appropriate table handle.
- The language-specific file path is derived from the TESFile's language key
  (`param_2 + 0x78`) — the loader builds the `_<Language>` path per file.

**On-disk IDs are raw u32s, no mask:** 507 real NPC_ FULL values from
Fallout4.esm are plain IDs (`0x00036763`, `0x00036425`, …) — **no
0x80000000 flag bit, no bit-7 semantics on disk**. The compiler writes IDs as
bare integers into subrecords.

**What bit 7 (0x80) is NOT:** it is not part of the record-embedded ID. The
ID space is globally unique across all three tables (doc `localized-string-
file-format.md` §4), so a bare ID resolves unambiguously once the field's
reader picks a table. The three tables are partitioned by the *writer's*
category routing (dialogue categories → DL, misc UI → IL, rest → ordinary),
not by ID arithmetic.

**Remaining runtime question (bit 7, issue #29):** if a runtime dump shows
bit 7 set on some IDs, that is an in-memory runtime encoding (e.g. a
pointer-vs-ID tag in `BGSLocalizedString`), not the file format. The CK side
cannot prove it; it needs the Fallout4.exe `BGSLocalizedStringDL` resolver
(runtime RE track). The runtime doc already lists `BGSLocalizedStringDL::
GetDescription` at RVA `0x14014FAF0` as a confirmed symbol — that is the
function to decompile to settle bit 7.

### 2a. Reader-side resolution recipe ✅ verified against shipped data (2026-08-22)

Empirical pass over all Fallout4.esm localized string fields plus the three
`Fallout4_en.*` tables (47-plugin localization survey on the local corpus;
`tools/plugin_parse.py`):

* **TES4 flag bit 7 (`0x80`) decides inline-text vs ID for every string
  subrecord in the file.** Bit clear → `FULL`/`DESC`/… payloads are inline
  NUL-terminated UTF-8 (`b'Slow Time Dispel\x00'`, length includes the NUL);
  a 4-byte payload under a plain plugin is *text*, never an ID. Bit set →
  payloads are u32 IDs. No per-record override exists.
* **ID `0x00000000` is the empty-string sentinel.** Every "unresolvable"
  DESC/FULL ID surveyed (AMMO/SPEL/WEAP/COBJ/… descriptions) was exactly 0;
  there are no dangling nonzero IDs in the shipped master.
* **Nonzero IDs resolve in exactly one table** (re-verified: `.STRINGS` ∩
  `.DLSTRINGS` = the two documented CC exceptions `0x36CB7`/`0x36CB9`; all
  other intersections empty). A reader may therefore probe tables in any
  fixed order; recommended `STRINGS → ILSTRINGS → DLSTRINGS`.
* **Writer routing, measured:** every `FULL`/`SHRT` field resolves from
  `.STRINGS` (including DIAL FULL titles — dialogue *text* goes to DL via
  INFO/TOPIC structures, not DIAL names); `DESC` resolves from
  `.DLSTRINGS` except LSCR (`DESC` → `.STRINGS`). Routing is a writer
  convention only — do not rely on it when reading.
* **Delocalized plugins:** none observed in the local corpus (23 plain/text,
  16 localized/id32, no plain-with-ID case). Rule: trust the bit; if a
  plain plugin carries 4-byte FULLs that look like IDs, they are still text.
* **EXCEPTION (2026-08-23, validator-discovered): `NNAM` is not a
  localized string.** DLCNukaWorld SNDR 01039631-33 ship NNAM as inline
  text (11-20 B) *inside a localized plugin* - so NNAM never enters the
  ID32 path regardless of bit 7. Writers must always emit NNAM inline;
  readers must treat it as raw bytes. (doc 50's SNAM is likewise a raw
  numeric block, not text.)

This unblocks Radicle's display-text resolution: store `(plugin, bit7,
payload)`; resolve at render time via the recipe above.

---

## 3. XCLC trailing field — Force-Hide-Land byte + padding ✅ resolved

**Population (strict GRUP-tree walk, `tools/validate_cell_save.py` +
`tools/plugin_parse.py`):** Fallout4.esm carries **40,165 CELL records —
38,970 exterior (XCLC-bearing) + 1,195 interior**. Only 7,276 of the
exterior cells are zlib-compressed; all 1,195 interiors are. The earlier
"9,741 exterior cells, all 0x40000-compressed" figure was an artifact of the
old scanner, not a property of the file — see the walker post-mortem below.

**XCLC is exactly 12 bytes:**

```text
offset  size  field
------  ----  ------------------------------------------------------
0x00    4     s32  gridX
0x04    4     s32  gridY
0x08    1     u8   Force-Hide-Land (bits 0-3 = Quad 1..4 hidden)
0x09    3     -    uninitialised padding (never cleared by the CK)
```

- The trailing field is **one meaningful byte plus three bytes of reused
  heap**, NOT a u32. CK LoadRecord (FUN_140d146d0, XCLC branch) reads 12
  bytes: two s32 into the cell grid coords, then **byte 8 → cell+0x1E** and
  the three padding bytes are skipped. SaveRecord (FUN_140d44ba0) writes the
  same 12-byte shape.
- xEdit's `wbStruct(XCLC,...[Force Hide Land u32 wbFlags Quad 1..4])` is
  over-wide for the same reason: reading the tail as u32 surfaces padding
  bytes as spurious high flag bits.
- **Measured distribution over all 61,543 exterior cells across
  Fallout4/DLCCoast/DLCNukaWorld:** byte 8 low nibble is `0x0` in 61,485
  cells and `0xF` in 50; four cells carry stray high-nibble-only values
  (`0xFF/0x44/0x22/0x88`, one each). Fallout4.esm alone: `0x00` ×38,928,
  `0x0F` ×38 (formIDs 0x00000FC6–0x00000FEE), plus one each of
  `0x11/0x22/0x44/0x88`. No genuine partial-quad mask exists in vanilla.
- **Padding evidence:** bytes 9–11 take only 20 distinct values across
  Fallout4.esm's 38,970 payloads (96 across the three masters), clustering
  in huge batches — 29,389 cells share `71 a3 76`, which is reused
  allocation, not data. Only 84 cells carry zero padding.
- Reading the tail as u32 therefore yields ~103 distinct "flag" values on
  Fallout4.esm; every value above `0x0F` is padding noise.

**Walker post-mortem (why earlier counts were wrong):** both previous
scanners byte-scanned for the `CELL` magic instead of walking GRUP extents.

1. The first variant advanced past *any* match by its unvalidated size
   field; one garbage match threw the cursor past EOF, ending the scan ~7%
   into the file (46 cells).
2. The replacement "validated" variant stepped the file on a fixed 4-byte
   stride believing record headers are 4-aligned. FO4 records are
   **unaligned** (subrecord = 6+len bytes), so it saw only ~25% of headers —
   that is where 10,027 = 9,741 + 286 came from, and why the sampled subset
   looked all-compressed: the stride happened to land in the compressed
   interior-cell region first.

Both are replaced by the strict sequential walk in `tools/plugin_parse.py`;
the "all cells compressed" note and every CELL-derived number in docs 23/26
measured before 2026-08-22 inherits the gap and has been re-measured.

---

## 4. Fallout4.exe RVA rebase — section-aware mapping ✅ resolved for OG 1.10.163

The unpacked OG 1.10.163 image used by the symbol dump is 65,319,936 B (PE32+).
Its section table is the authoritative source for converting a symbol RVA to a raw
file offset:

| section | virtAddr | rawPtr | rawPtr − virtAddr |
|---|---:|---:|---:|
| `.text` | `0x00001000` | `0x00000400` | `-0x0c00` |
| `.interpr` | `0x02c0c000` | `0x02c0b400` | `-0x0c00` |
| `.rdata` | `0x02c17000` | `0x02c15c00` | `-0x1400` |
| `.data` | `0x036cb000` | `0x036c9c00` | `-0x1400` |
| `.pdata` | `0x06737000` | `0x038cb600` | `-0x2e6ba00` |
| `.tls` | `0x06a8c000` | `0x03c20400` | `-0x2e6bc00` |
| `.rsrc` | `0x06a90000` | `0x03c23a00` | `-0x2e6c600` |
| `.reloc` | `0x06b37000` | `0x03cca200` | `-0x2e6ce00` |

**Why the old sweeps drifted:** raw→RVA deltas are **not uniform** across
sections, so a single global delta misreads bytes near section boundaries.
That class of error is real but it is *not* what caused the historic
`±0x10` confusion — see the build-mismatch note below.

**Correct rebase rule:**
1. Locate the section whose `virtAddr ≤ RVA < virtAddr + max(vsize,rawSize)`.
2. fileOffset = RVA − virtAddr + rawPtr.
3. Never use one global `RVA − imageBase + delta` constant.

For the OG 1.10.163 dump, `TESFile::CheckValidIDForSmallFile` is listed at
RVA `0x130560`. Section-aware mapping places it at raw file offset `0x12f960`,
where the bytes are:

```text
f7 c1 00 f0 ff 00 0f 94 c0 c3
```

That is the complete 10-byte body: `test ecx, 0x00fff000; sete al; ret`.
Therefore the exact predicate is:

```text
(formID & 0x00FFF000) == 0
```

A complete low-24-bit sweep finds 4,096 valid IDs (`0x000000..0x000fff`)
and 16,773,120 invalid IDs — independently confirming the light-plugin form
range `0x001..0xFFF`. The old “function not found” result was a raw
offset/RVA lookup failure, not a missing function. The reusable implementation
is `tools/rebase_runtime_symbols.py`; it maps every symbol through its
containing PE section and can reproduce the sweep with `--sweep`.

**The ±0x10 drift explained (2026-08-22):** two unpacked OG images circulate
in this workspace and they are *different builds*, not two unpacks of one
file:

| image | PE TimeDateStamp | build | `CheckValidIDForSmallFile` | `AdjustFormIDFileIndex` |
|---|---|---|---|---|
| `ReverseEngineering/Symbols/Fallout4.exe` (md5 `3cc9a3e0…`) | `0x5dac778e` | 1.10.155 | RVA `0x130550` / raw `0x12f950` | RVA `0x131f90` |
| `Symbols/1.10.163/Fallout4.exe.unpacked.exe` (md5 `fa847db9…`) | `0x5ddc40fa` | 1.10.163 | RVA `0x130560` / raw `0x12f960` | RVA `0x131fa0` |

Each address table matches its own build byte-for-byte (the predicate bytes
occur exactly once in each image, at the mapped offset). Reading the
**1.10.155 symbol table against the 1.10.163 image** (or the reverse) shifts
every address by ±0x10 and lands mid-instruction or on the next function's
prologue (`48 89 5c 24 08 …` at 155's raw `0x12f960`, padding at 163's raw
`0x12f950`) — that mixing is the entire story behind the "offset by 0x10"
symptom. Section-aware mapping remains mandatory; it just was never the
culprit. When quoting addresses, always name the build stamp alongside.

---

## Open items

- Bit-7 runtime string semantics: the CK and `StringFileInfo::ResolveID` paths
  are now pinned; the remaining runtime question is the in-memory field/tag
  convention around `BGSLocalizedStringDL`.
- Apply the same section-aware symbol sweep to AE 1.11.240 once its matching
  named-function dump is available.
- Whether the CK *editor* ever writes a non-zero Force-Hide-Land on save
  (the writer preserves byte 8 from cell+0x1E; vanilla is 0/0x0F only).
