# 39 - REFR LoadRecord + the extra-data reader (XCRI/ExtraCombinedRefs confirmed)

## 1. TESObjectREFR::LoadRecord

`FUN_140d65f60` (vftable slot 24, `.?AVTESObjectREFR@@` @ vftable 0x30a1988).

* Handles `EDID`, `NAME`, `DATA` (24 B: 6 floats position+rotation then
  u16 flags + pad), `VMAD`, `OBND/OBTE/OBML`, `XCLC`-family scale `XSCL`,
  `MNAM`/`ONAM`/`XRMR` and routes **every other X-tag into the generic
  extra-data reader** `FUN_1407d8100` with the record's `BSExtraDataList`.
* Template-flag branch (`DATA` bit?): when the record is a template
  consumer it reads only NAME-family tags and skips the extra-data loop.
* Missing base object triggers "Missing/Invalid base object ... This
  reference will be deleted." and marks the load failed.
* `XSCL` = f32, multiplied by game constants and range-checked against a
  max ("Scale mismatch" assert).

## 2. BSExtraDataList::Load - FUN_1407d8100 (extradatalist.cpp)

One giant switch over the subrecord 4CC. Confirmed cases include:
`XNBC`(0x4d4e5458→"XTNM"? see note), `XLKR`, `XCZC`+`XCZA` pair,
`XLCT`(0x434f4c58 "XCOL"?), `XSPC/XPSX`, `XCAM`, `XIS2`, `XAPP`,
`XRGB/XLIB/XGLB`, `XPOA/XROD/XPRD/XBSR`, `XPOD`, `XDCR/XTRP/XPRM/XSED`,
`XGCN/XRGD/XLOD`, `XNDP/XWPN`, `XMRC/XMCX/XMCC/XLCM/XLTW/XLTV/XTEL/XLRT/
XILL/XLSP`, `XEZN/XHCG/XLGH/XIBU/XIRC/XPGI/XPMI/XPTN/XNWV/XOWN/XPOC/
XPDD/XPCl/...`, `XTRT/XRTM`, `SCHR/SCDA` family via XLCM...

Note: tag strings above are decoded from switch constants; byte order was
calibrated against in-function assert text ("Expected XCZA chunk after
XCZC chunk.") so decodes are exact.

### XCRI -> ExtraCombinedRefs (type 0xC6) construction site

Constant `0x49524358`. Payload prefix `[formID u32][count u32]`; the
object is built inline when not already present:

| offset | type | meaning |
|---|---|---|
| +0x00 | ptr | `ExtraCombinedRefs::vftable` |
| +0x08 | qword | 0 |
| +0x10 | u16 | 0 |
| +0x12 | u8 | extra-type tag **0xC6** |
| +0x14..0x23 | - | unused by ctor |
| +0x24 | u32 | hash capacity (next pow2 >= count, shift cap 0x1f) |
| +0x28 | u32 | capacity copy |
| +0x2C | u32 | capacity copy |
| +0x30 | ptr | static empty-node sentinel (&DAT_143b87b88) |
| +0x38 | u32 | 0 |
| +0x48 | ptr | table buffer (capacity x 16-B entries, zeroed pairs) |
| +0x50 | - | subobject A (ctor'd; BSTArray machinery) |
| +0x60 | - | subobject B (heap-allocator BSTArray; insert keyed by **3-byte formID** truncated from the payload's first u32, value size 4 B) |

Total size 0x68. If an instance already exists the loader clears both
arrays and rebuilds. This supersedes doc 25's provisional field map and
its wrong allocator attribution.

## 3. Shipment reality — CORRECTED 2026-08-23

The original claim here ("zero XCRI/XCRF/XCRC/XCRD in the four masters")
was an artifact of **raw byte-scanning compressed records**: CELLs are
DEFLATE-compressed, hiding their subrecord strings from `bytes.count`.
Parsed (decompressed) census says:

* **XCRI ships 7,387 times** - all on **CELL** records (not REFR), all
  inside compressed records.
* XCZA: 2 (REFR); XCZC: 1 (REFR). XCRF/XCRC/XCRD: still zero.

So ExtraCombinedRefs wire data DOES ship in vanilla cells and doc 25's
§6 grammar is verifiable against shipped bytes after decompression -
no user-generated CombinedObjects.esp required.
