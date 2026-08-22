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

**OBTS body layout (validated 665/665 real payloads; every include formID
resolves to an OMOD record):**

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

- **The template block grammar:** `OBTE` (u32 combination count), then N
  combinations each `[OBTF (empty, optional)] [FULL (4B localized ID,
  optional)] OBTS`, then `STOP`. **OBTE's value equals the number of OBTS
  bodies** — verified on every record (0 mismatches across 153 records).
  NPC_/WEAP emit `OBTF FULL OBTS`; **ARMO emits `OBTS` with no FULL** — the
  CK reader (FUN_140350930) allocates N `BGSMod::Template::Item` objects and
  lets FUN_14034f9b0 consume the markers, so markers are per-record-type
  optional.
- **Every include formID resolves to an OMOD record** (2,367/2,367) — the
  template includes are Object Modifier records, not actor bases.
- xEdit's FO4 definitions agree on the field set and both row shapes; the
  **one divergence is the keyword count width** — xEdit declares it as a
  `-4` (u32) array marker, but real files use a **u8 at body+0x0F**. The
  7-byte include and 24-byte property rows match exactly.
- **Correction to the earlier draft:** the header is 16 bytes + keywords,
  not a fixed 18-byte header with counts at 0x00/0x01. The 18-byte bodies
  seen in the master are keyword-free 16-byte headers + 2 trailer bytes
  (min-rank/alt-levels), not a different header shape.
- OBTE/OBTS appear on NPC_, ARMO and WEAP records that actually use
  templates (153 records in Fallout4.esm: 60 NPC_, 63 ARMO, 30 WEAP,
  665 OBTS bodies total).

**Note on "blocks ARMO FULL":** the user's report that OBTE blocks reading
ARMO FULL is consistent with a template-boundary bug in a parser that stops
consuming subrecords at the first OBTE and never resumes. The CK itself reads
OBTE/OBTS in place (LoadRecord at FUN_14034f9b0) and continues with subsequent
subrecords; any consumer that stops is over-strict.

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

---

## 3. XCLC trailing dword — Force-Hide-Land ✅ resolved

Byte-parsed **9,741 real exterior CELL records** from Fallout4.esm (validated
subrecord walk, all cells 0x40000-compressed, decompressed with zlib).

**XCLC is exactly 12 bytes:**

```text
offset  size  field
------  ----  ------------------------------------------------------
0x00    4     s32  gridX
0x04    4     s32  gridY
0x08    4     u32  Force-Hide-Land flags (bits 0-3 = Quad 1..4)
```

- CK LoadRecord (FUN_140d146d0, XCLC branch) reads 12 bytes: two s32 into the
  cell grid coords, then **byte 8 → cell+0x1E** and 3 padding bytes skipped.
  SaveRecord (FUN_140d44ba0) writes the same 12-byte shape.
- xEdit FO4 definition matches exactly: `wbStruct(XCLC,'Grid',[X s32, Y s32,
  'Force Hide Land' u32 wbFlags Quad 1..4])`.
- **Distribution in the shipped master:** 9,727 cells `0x00000000`; **14 cells
  `0x0000000F`** (all four quads hidden). No partial-quad values exist in the
  vanilla file.
- The 14 all-hidden cells cluster in the low formID range (0x00000FCA …
  0x00000FE1) — the "no terrain LOD shown" cells.
- **Correction to earlier notes:** the constant `0x0071a376` seen in raw
  byte scans is a compressed-record false positive (zlib streams contain the
  same 4-byte runs), not a real field value. Only the validated subrecord walk
  is trustworthy.

---

## 4. Fallout4.exe RVA rebase — section delta check ✅ done (root cause identified)

Section headers of the unpacked `Fallout4.exe` (55,343,016 B, PE32+):

| section | virtAddr | rawPtr | rawPtr − virtAddr |
|---|---|---|---|
| .text    | 00001000 | 00000400 | **−0x0C00** |
| .interpr | 02435000 | 02434000 | −0x1000 |
| .rdata   | 02440000 | 0243e800 | −0x1800 |
| .data    | 02ed8000 | 02ed5c00 | −0x2400 |
| .pdata   | 03e76000 | 030e3000 | −0x0D93000 |
| _RDATA   | 040f5000 | 03362000 | −0x0D93000 |
| .rsrc    | 040f6000 | 03363000 | −0x0D93000 |
| .reloc   | 0419d000 | 03409800 | −0x0D93800 |
| .bind    | 04220000 | 0348bc00 | −0x0D94400 |

**Root cause of the 0x10 drift:** raw→RVA deltas are **not uniform** across
sections. The first four sections each have their own delta (−0xC00, −0x1000,
−0x1800, −0x2400), and the last five jump to a constant −0xD93000 family. Any
tool that assumes a single file-offset→RVA conversion (like
`CheckValidIDForSmallFile` sweeps) will be off by exactly the section-delta
difference at a section boundary — the "off by 0x10" in at least one region.

**Correct rebase rule:**
1. Locate the section whose `virtAddr ≤ RVA < virtAddr + max(vsize,rawSize)`.
2. fileOffset = RVA − virtAddr + rawPtr.
3. Never use one global `RVA − imageBase + delta` constant.

The `.bind`/`.reloc`/`.rsrc`/`_RDATA`/`.pdata` sections all share the
−0xD93000 family in this build, so the drift only bites at the .text/.interpr/
.rdata/.data transitions — consistent with "off by 0x10 in at least one
region".

---

## Open items

- Bit-7 runtime string semantics: decompile `BGSLocalizedStringDL` in
  Fallout4.exe (RVA 0x14014FAF0) — runtime RE track, issue #29.
- Whether the CK *editor* ever writes a non-zero Force-Hide-Land on save
  (the writer preserves byte 8 from cell+0x1E; vanilla is 0/0x0F only).
