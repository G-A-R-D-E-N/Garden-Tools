# Format notes

Confidence: `confirmed` / `strong` / `probable` / `speculative`.

## Record header (24 bytes)

`confirmed` against CommonLibF4 `FORM` (`sizeof == 0x18`) and a real ESL/ESP hex dump.

| Offset | Size | Field |
|-------:|-----:|-------|
| 0x00 | 4 | signature |
| 0x04 | 4 | data size (payload only) |
| 0x08 | 4 | flags |
| 0x0C | 4 | FormID |
| 0x10 | 2 | timestamp / VC info |
| 0x12 | 2 | version control |
| 0x14 | 2 | form version |
| 0x16 | 2 | VC version |

Fallout 4 form version observed: **131** (`confirmed`, Heather patch + `ccBGSFO4096-AS_Enclave.esl`).

## Group header (24 bytes)

`strong`. Size includes the header. Children occupy `group_size - 24`. Nested groups are legal (WRLD/CELL).

| Offset | Size | Field |
|-------:|-----:|-------|
| 0x00 | 4 | `GRUP` |
| 0x04 | 4 | group size including header |
| 0x08 | 4 | label (top-group signature, FormID, or grid) |
| 0x0C | 4 | group type (int32) |
| 0x10 | 2 | timestamp |
| 0x12 | 2 | VC |
| 0x14 | 2 | form version |
| 0x16 | 2 | unknown |

Group type names (`top=0` ... `cell_visible_distant_children=10`) are `probable` from xEdit/UESP. The integer is stored; names are only for display.

## Subrecords

`confirmed` by walking real plugins.

```text
char[4] signature
uint16  size
bytes   data
```

`XXXX`: the next subrecord's true size is a `uint32` stored in the `XXXX` payload. The following subrecord still has a `uint16` size field (often 0). `confirmed` by implementation tests; engine `TESFile::GetChunkData` / `actualChunkSize` exists (OG).

## TES4 / HEDR

CommonLibF4 `FILE_HEADER` is 12 bytes: `float version`, `uint32 formCount`, `uint32 nextFormID`. `confirmed`.

Observed FO4 HEDR version: **1.0**. `confirmed` on Heather and an official ESL.

HEDR `formCount` counts major records excluding TES4, plus every GRUP including nested groups.

- Heather: 4 records + 3 groups = 7. `confirmed`
- `DLCworkshop01.esm`: 1463 records + 65 groups = 1528. `confirmed`

`MAST` is a zstring plugin name. `DATA` is 8 bytes (file size, often zero in small patches). Masters come in MAST+DATA pairs.

Other TES4 subrecords preserved generically: `CNAM`, `SNAM`, `ONAM`, `INTV`, `INCC`, plus anything unknown.

## TES4 flags (file header)

CommonLibF4 `TESFile::RecordFlag` (`strong` oracle; matches a real ESL of `0x281`):

| Bit | Value | Name |
|----:|------:|------|
| 0 | 0x1 | Master |
| 7 | 0x80 | Delocalized / localized |
| 9 | 0x200 | SmallFile (light) |

The official ESL dump was `0x281 = Master | 0x80 | SmallFile`. Whether bit 7 is "localized" vs "delocalized" is still named both ways in sources. Treat the bit as present; do not rename it in the writer.

Record compression bit `1 << 18` (`0x40000`) is `strong` community/Mutagen/xEdit. `TESFile` has `decompressedFormBuffer`. Not yet verified against a named CommonLib enum.

Deleted record bit `1 << 5` is `strong` (`TESForm::IsDeleted`).

## Compressed records

Payload starts with `uint32 uncompressed_size`, then zlib (window bits 15, with raw-deflate fallback).

CK strings confirm inflate/deflate (`confirmed` that the compressor is zlib). The 4-byte uncompressed-size prefix is `strong` (Mutagen `Decompression.cs` + CommonLib `decompressedFormBuffer`). Runtime function is `TESFile::DecompressCurrentForm` (OG RVA `0x132500`, 566 bytes), not yet decompiled. Synthetic zlib round-trip is tested. Raw-deflate fallback stays until that decompile.

## Record type enum

CK `formEnumString[]` (CK 1.11.137.0, `.data` RVA `0x3b9a540`) and CommonLibF4 `ENUM_FORM_ID` agree: 159 entries, index 0 `NONE`, 1 `TES4`, 2 `GRUP`, 17 `RACE`, 63 `CELL`, 74 `WRLD`. `confirmed`.

CKRE `record-definition-table.txt` **class names are not trustworthy**. It maps `GMST` to `BGSKeyword`. Confirmed factory registrations and CommonLib comments disagree with that join. Use the 4CC list, not the dumped class column.

CKRE also claims `RACE` is absent from the file-load table. RACE records exist in `Fallout4.esm`. Treat "RACE is not file-loadable" as **disputed**. The generic reader still reads `RACE`.

## CELL / LAND (runtime)

OG 1.10.163, `ReverseEngineering/Organized/1.10.163/README-Landscape.md`, `confirmed`:

- Cell size 4096 (`cellX << 12`)
- VHGT is `0x448` bytes: float Offset + 33x33 signed deltas + 3 pad
- Height units = accumulated delta * 8
- REFR positions are world-absolute; do not add cell origin

Typed LAND decode is deferred.

## CTDA (conditions)

xEdit FO4 `wbConditions` and FO4IDE `ReadSingleCtdaParameterOne` agree the payload is 32 bytes:

| Offset | Field |
|-------:|-------|
| 0 | type (uint8) |
| 1-3 | unused |
| 4 | comparison value (float, or GLOB FormID if flagged) |
| 8 | function (uint16) |
| 10 | unused |
| 12 | parameter 1 (FormID or int, function-dependent) |
| 16 | parameter 2 |
| 20 | run-on (uint32); 2 = Reference |
| 24 | reference FormID when run-on is Reference |
| 28 | parameter 3 |

`GetBaseValue` is function 277; parameter 1 is an ActorValue FormID (`confirmed` via xEdit `ptActorValue` + FO4IDE regression expecting `0x02000812` at offset 12).

Comparison-value-as-global is not remapped yet.

## FormID-bearing subrecords (first schema slice)

Whole-subrecord FormIDs: `NAME`, `XLCN`, `XOWN`, `XEZN`, `PTRN`.
Arrays: `KWDA` (4-byte FormIDs).
Function-gated: `CTDA` as above.

Unknown subrecords are left untouched on master remap.

## VMAD (Papyrus)

FO4 default: version 5 or 6, object format 2. Strings are uint16 length + UTF-8.

Object v2: unused uint16, alias int16, FormID.
Object v1: FormID, alias int16, unused uint16.

Property types 1 / 11 / 7 / 17 contain objects. Type 3 int is not a FormID even if the bits look like one.

QUST extras after scripts: bind byte, fragment count, optional fragment script, fragments, then aliases (object + nested version/format/scripts).

Truncated VMAD is skipped for remap rather than aborting the plugin write.

## TESFile (runtime plugin IO)

OG 1.10.163 named API, `confirmed` as existing (bodies not all decompiled in this pass):

`OpenTES`, `CloseTES`, `CreateTES`, `LoadTESInfo`, `NextForm`, `NextChunk`, `GetChunkData`, `GetChunkSize`, `StartGroup` / `EndGroup`, `GetMasterList`, `AddMaster`, `RemoveMaster`, `GenIndexTable`, `AdjustFormIDFileIndex`, `GetCompileIndex`, `SetSmallCompileIndex`, `GetSmallFile` / `SetSmallFile`, `CheckValidIDForSmallFile`.

`AdjustFormIDFileIndex` is the engine function that maps plugin-relative FormIDs onto runtime compile indexes. The replacement library must do the equivalent using `FormKey`, not by editing high bytes ad hoc.

## Exterior CELL block / sub-block GRUP labels

Measured, not inferred. Every exterior CELL in `Fallout4.esm` (38,965) and
`DLCCoast.esm` (17,917) was walked, its enclosing block (GRUP type 4) and
sub-block (type 5) labels recorded, and compared against its own XCLC grid
coordinates.

```
block    label = (int16 floor(y / 32), int16 floor(x / 32))   bytes 0..1 = y, 2..3 = x
subblock label = (int16 floor(y /  8), int16 floor(x /  8))   bytes 0..1 = y, 2..3 = x
```

- **Y is stored first.** `(floor(y/d), floor(x/d))` matched 38,965/38,965 on
  Fallout4.esm; the transposed form matched only 6,932 (block) and 1,850
  (sub-block) — exactly the diagonal cases where x and y bucket the same.
- **Division is mathematical floor, not C truncation toward zero.** 29,343 of
  the Fallout4.esm cells have a negative coordinate; floor matched all of them,
  truncation matched 585 (block) and 2,568 (sub-block) — only the cases that
  divide evenly, where the two agree.
- DLCCoast.esm reproduces both formulas 17,917/17,917 as an independent
  worldspace.

This is the calculation #71 flags as "do not guess". `exterior_cell_grid_layout`
and `exterior_grid_group_label_u32` in `fo4/forms/cell.hpp` implement it, and
`tests/test_exterior_cell_grid_real.cpp` re-checks it against the shipped
masters when `FO4_DATA_DIR` is set (it skips in CI, which has no game files).

## Localized string subrecords in shipped masters

Every Bethesda master sets TES4 flag `0x80`: `Fallout4.esm` header flags are
`0x0081`. In a localized plugin a string subrecord does not hold inline text —
it holds a **four-byte string ID** into the `.STRINGS`/`.DLSTRINGS`/`.ILSTRINGS`
tables. All 45 `RACE` records in `Fallout4.esm` carry a `DESC` of exactly 4
bytes.

This is handled by `fo4/schema/localized_fields.hpp`. A string field decodes to
a `LocalizedString` carrying **either** a four-byte string ID (localized plugin,
lstring field) **or** inline text, whichever the file used. Decoding performs no
I/O and picks no language, so a write-back reproduces the original bytes;
resolve an ID through `fo4::localization` when display text is wanted.

`RACE` is wired. The remaining typed families still read strings as inline text
and need the same treatment. Related: #29 (bit 7 localized vs delocalized
semantics).

## RACE BodyData INDX

`INDX` inside a RACE BodyData block is a `uint32` body-part index, not an empty
separator, and it is **not** derivable from list position: of the 90 gender
blocks in `Fallout4.esm`, 17 are not a plain `0..n-1` run — they begin
`0, 0, 1, 2, …`. The value is decoded and written back verbatim rather than
recomputed.

### Which fields are localized

Measured on `Fallout4.esm` (localized, TES4 `0x0081`), payload size per
(record, subrecord) pair:

| Field | Always 4 bytes | Never 4 bytes |
|---|---|---|
| `FULL` | ARMO 974, WEAP 864, MISC 2043, NPC_ 1944, QUST 708, CELL 319, RACE 40 | — |
| `DESC` | ARMO 688, WEAP 252, RACE 45 | — |
| `EDID` | — | ARMO 688, WEAP 252, RACE 45, ARMA 739, CELL 1736 |
| `DNAM` | — | ARMA 739, WEAP 252, NPC_ 3015, QUST 1336 |

`FULL` and `DESC` are 4 bytes in every record that carries them, with zero
inline instances — those are the safe lstring set to start from. `EDID` is
never localized.

**Payload size alone cannot decide this.** `NPC_ CNAM` is 4 bytes in all 3,015
instances but it is a Class FormID, not a string ID; `QUST CNAM` in a stage is
a log-entry lstring. The same signature means different things in different
records, so lstring-ness has to be declared per (record, subrecord) pair in
`schema/fallout4/fields.json` rather than inferred from the bytes. Anything
that guesses from size will silently turn FormIDs into string IDs.

### ARMO FULL is not a unique field

`DESC` is exactly one per record across all 688 shipped ARMO. `FULL` is not:
counts per record run 0, 1, 2, 3, 4, 6, 7, 8, 9, 14, 16 and 20, because `OBTE`
object-template blocks carry their own `FULL` name overrides. In 11 records a
`FULL` appears only after `OBTE`.

A unique-subrecord lookup for ARMO `FULL` would therefore throw on 71 of 688
shipped records, and a first-match lookup would silently return a template
override instead of the armour's name. `FULL` stays unmodelled on ARMO until
the `OBTE` template block is parsed; `DESC` is modelled now.

Measured top-level order is `EDID OBND PTRN FULL … KSIZ KWDA DESC INDX …`.

## QUST stage / log-entry grammar

Measured on `Fallout4.esm`: 1,336 QUST records, 8,680 stages, 9,058 log
entries. The stage array runs from the first `INDX` up to the first `QOBJ`
(objectives) or `ANAM` (alias array):

```
INDX  4 bytes: u16 stage index, u8 flags, u8 unknown
  QSDT  1 byte flags          -- one log entry
    CNAM  log text, LOCALIZED -- 4 bytes in all 921 shipped instances
    NAM2  note text, inline   -- NUL-terminated, 99 distinct lengths
    CTDA / CIS2               -- per-entry conditions
```

Payload sizes are fixed: `INDX` is 4 bytes in all 8,680 stages, `QSDT` is 1
byte in all 9,058 entries. Observed `QSDT` flag values are `0` (8,649), `1`
(267, completes the quest) and `2` (142, fails it).

Three things worth pinning:

- **`NAM0` does not occur.** Zero of 8,680 stages carry it, despite it being a
  documented Skyrim stage field. It is not modelled.
- **Log entries per stage are not always one:** 0 (63 stages), 1 (8,378), and
  2–9 (239). Both the empty and the multi-entry shapes must be representable.
- **The fourth `INDX` byte is not always zero** (observed 0–10+), so it is
  preserved rather than rebuilt.

`INDX` is also used by the alias array, which is why the walk has to stop at
`QOBJ`/`ANAM` rather than scanning the whole record.

## Exterior CELL DATA and XCLC

Measured across `Fallout4.esm`, `DLCCoast.esm` and `DLCNukaWorld.esm`.

**`DATA` is 2 bytes on every CELL.** For the 61,543 exterior cells (those
carrying `XCLC`), bit 1 is set on **61,542** — the sole exception is one cell
with `0x0000`. Observed values are `0x0002` (61,252), `0x0042` (237, plus Hand
Changed) and `0x000A` (53, plus No LOD Water). All 1,376 interior cells set
bit 0.

An all-clear `DATA` therefore describes a record shape that occurs once in the
entire shipped corpus, so `create_exterior_cell` defaults to `0x0002` and
refuses the interior bit outright.

**`XCLC` is exterior-only and 12 bytes** in all 38,970 `Fallout4.esm` instances
that carry it; the 1,195 CELLs without one are interiors, which use `XCLL`
instead (CKRE doc 23: `FUN_140d18490` gates access on the interior flag in both
LoadRecord and SaveRecord).

The 4 bytes after x/y are **one meaningful byte plus three bytes of
uninitialised padding**, not a `u32`. Measured over 61,543 exterior cells in
`Fallout4.esm` + DLCCoast + DLCNukaWorld:

| Offset 8 (Force-Hide-Land quads, bits 0–3) | count |
|---|---|
| `0x00` | 61,485 (99.91%) |
| `0x0F` — all four quads hidden | 50 |
| `0xFF`, `0x44`, `0x22`, `0x88` | 1 each |

Bytes 9–11 are junk the CK does not clear: only 96 distinct values across all
61,543 cells, clustering in large batches (29,389 share `71a376`), which is the
signature of reused allocation rather than data. Reading the field as a `u32`
yields 103 distinct "flag" values and would surface padding as spurious
high flag bits — xEdit's `u32 wbFlags Quad 1..4` is over-wide for this reason.

This is consistent with the CK's own LoadRecord (`FUN_140d146d0`, XCLC branch),
which takes **byte 8 into `cell+0x1E` and skips three padding bytes**.

`create_exterior_cell` writes all four bytes zero: the quad flags clear, and no
fabricated padding.

## QUST objective / target grammar

Measured on `Fallout4.esm`: 1,198 objectives, 1,500 targets. The array runs
from the first `QOBJ` up to `ANAM`:

```
QOBJ  u16 objective index            (2 bytes, all 1,198)
  FNAM  u32 objective flags          (4 bytes, all 1,198)
  NNAM  display text, LOCALIZED      (4 bytes, all 1,198)
  QSTA  12 bytes, all 1,500          -- one target
          i32 alias id     @0
          u32 target flags @4        (0 in 1,479; also 1, 2, 4)
          u32 keyword FormID @8      (zero in 1,498 of 1,500)
    CTDA / CIS1 / CIS2  per-target conditions
```

Targets per objective: 0 (66 objectives), 1 (950), and 2–19 (182). Both the
empty and multi-target shapes must be representable.

**`QSTA`'s keyword is a FormID inside a mixed-type payload** and needs a
structured FormID slot, otherwise a master remap leaves it pointing at whatever
record now occupies the old index. Only 2 of 1,500 shipped targets carry one,
so the corruption is easy to miss —
`QuestObjectives.TargetKeywordIsRemappedWithTheMasterTable` pins it; with the
slot removed, a `Beta.esm` keyword silently becomes an `Alpha.esm` one.

`QOBJ` and `NNAM` are also used by the alias array, so the walk stops at `ANAM`.

## PROJ / EXPL / HAZD

Measured across `Fallout4.esm` + DLCCoast/DLCNukaWorld/DLCRobot: 218 PROJ, 317
EXPL, 78 HAZD. Every subrecord in all three families has max cardinality 1.

```
PROJ  EDID OBND [FULL] [MODL MODT] [DEST DSTD [DMDL DMDT] DSTF] DATA DNAM NAM1 [NAM2] VNAM
EXPL  EDID OBND [FULL] [MODL MODT] [EITM] [MNAM] DATA
HAZD  EDID OBND [FULL] MODL MODT [MNAM] DNAM
```

Required in every shipped record: PROJ `EDID OBND DATA DNAM NAM1 VNAM`;
EXPL `EDID OBND DATA`; HAZD `EDID OBND MODL MODT DNAM` — note HAZD requires the
model where the other two do not. `FULL` is 4 bytes in every instance of all
three, so it is declared localized.

**PROJ `DATA` is a zero-length marker** in all 218 records, not a payload.
**EXPL `DATA` is not fixed-width**: 84 bytes (299), 56 (11) and 64 (3), so a
decoder must accept the shorter legacy shapes.

### Typed creation is blocked on the payload structs

`PROJ DNAM` (93 bytes), `EXPL DATA` (84) and `HAZD DNAM` (52) are unmodelled
and have no usable default: 199 distinct DNAM payloads across 218 PROJ records,
and only 13 of the 93 byte positions are constant. A creator cannot invent a
valid one, so these need their field layouts pinned before typed creation is
possible.

### Candidate FormID slots — NOT wired, needs confirmation

These payloads appear to carry FormIDs, and none of the three families has any
entry in the FormLink schema, which would mean a master remap leaves them
pointing at whatever record now occupies the old index — the same class of bug
as the QSTA keyword.

Two independent signals agree:

| Subrecord | Offset | Resolves to (Fallout4.esm) | Carries a DLC master index |
|---|---|---|---|
| `EXPL DATA` | +0 | LIGH 85/220 | yes |
| `EXPL DATA` | +4 | SNDR 165/220 | yes |
| `EXPL DATA` | +8 | SNDR 38/220 | yes |
| `EXPL DATA` | +12 | IPDS 70/220 | yes |
| `EXPL DATA` | +16 | EXPL/MSTT/HAZD 47/220 | yes |
| `PROJ DNAM` | +16 | LIGH 26/139 | yes |
| `PROJ DNAM` | +20 | LIGH 120/139 | yes |
| `PROJ DNAM` | +32 | EXPL 98/139 | yes |
| `PROJ DNAM` | +36 | SNDR 70/139 | yes |
| `HAZD DNAM` | +24 | SPEL 53 / ENCH 2 of 59 | yes |

The target types are semantically coherent (a light at a light slot, sounds at
sound slots), and in the DLCs those offsets carry index byte `0x01` pointing
into Fallout4.esm, which a float or small integer would not do systematically.
`PROJ DNAM +0` and `+84` and `EXPL DATA +52`/`+60` were rejected: they never
carry a DLC master index, so their Fallout4.esm matches are coincidence.

**These are deliberately not added to `structured_formids.inc`.** The evidence
is inferential, and a wrong offset would make master remapping corrupt a float
or integer field — worse than the current omission. Confirm against xEdit's
`wbEXPL`/`wbPROJ`/`wbHAZD` definitions or the CK before wiring them.
