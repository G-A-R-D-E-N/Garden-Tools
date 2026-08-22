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
