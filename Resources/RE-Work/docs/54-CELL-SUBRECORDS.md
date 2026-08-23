# 54 - CELL subrecord reference

Measured across all four masters (63,061 CELLs; sizes are exact and
universal unless noted). Companion to docs 23 (walker/DATA) and 53
(child blocks).

## 1. Universal tags (every CELL)

| tag | size | content |
|---|---|---|
| DATA | 2 | flags; **bit 0 = interior** |
| XCLW | 4 | parent worldspace formID - present on interiors too |
| LTMP | 4 | light template formID |

## 2. Exterior-only tags

* `XCLC` (12 B, on all 61,655): grid coordinates + the trailing
  Force-Hide-Land byte + 3 pad (doc 23 §XCLC).
* `MHDT` (**fixed 1028 B**, 12,661 cells): multibounds/lighting blob -
  copy-through, never synthesize.
* `XCLR` (variable 4/8/16): region list - leading u32 count followed by
  count x u32 region formIDs (size = 4+4n observed).
* `VISI` (2) / `RVIS` (4): precomputed cell visibility pairs.
* `TVDT` (4, only 2 records): occlusion data pointer.

## 3. Interior-heavy tags

* `XCLL` (on all interiors): full lighting block.
* `XCIM` (396): imagespace ref. `XCAS` (335): acoustic space.
* `XCMO` (332): music type.

## 4. Shared optional tags

| tag | size | meaning |
|---|---|---|
| EDID | var | **absent on ~98% of exterior cells** - auto-generated coordinate cells are anonymous; only hand-named cells carry it |
| FULL | var | display name (localized rules apply) |
| XCRI | 20..2116 | precombined refs, doc 25 §6 wire format (7,387 total, all compressed records) |
| XPRI | 4/8/12 | precombined visibility |
| XEZN | 4 | encounter zone |
| XLCN | 4 | location |
| XCWT | 4 | water type |
| XCCM | 4 | climate (exterior) |
| XOWN | 12 | owner + rank + flags |
| XRNK | 4 | owner rank |
| XILW / XILL | 16 / 4 | inherited water/environment |
| XGDR | 4 | god-rays override |
| XWCN / XWCU | 4 / 48 | unknown water extras (1 each; doc 23) |

## 5. Writer rules

* Minimum viable exterior cell: `DATA XCLC XCLW LTMP` (+EDID optional!).
* Minimum interior: `DATA XCLL LTMP XCLW`.
* Never emit MHDT by hand - copy from a donor or omit.
* XCRI only when shipping precombined data; use the doc 25 layout
  exactly.
