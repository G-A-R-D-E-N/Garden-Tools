# 35 - TESClass (CLAS): LoadRecord + shipped census

A-tier continuation. `ck_search_source_anchor("tesclass.cpp")` +
`ck_decompile` end-to-end, no local pass needed.

## 1. LoadRecord

**`FUN_140a69ba0`** (RVA 0xa69ba0, 646 B) - 11 tags, phase-gated on
reader state 0xd:

`FULL EDID DATA DESC VMAD OBND OBTE OBML PTRN STCP PRPS`

Reader notes:

- `DATA` is size-branched: exactly **36 B** takes the modern read
  (flags + stat-table block at +0xa4), anything else falls back to a
  legacy 8-byte read.
- `PRPS` (property list) routes through the shared property helper
  `FUN_14036e3c0`.
- Base-class saves: `OBND OBTE OBML PTRN STCP VMAD`; `DESC` via the
  description helper.

## 2. Shipped census

**32 records across the four masters.** Every record carries
`EDID FULL? DESC PRPS DATA`; `DATA` is always the **8-byte legacy
shape in shipped files** - the 36-byte branch is CK-write-only so far.
No unknown types; nothing outside the dispatch.
