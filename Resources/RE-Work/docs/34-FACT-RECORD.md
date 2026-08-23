# 34 - TESFaction (FACT): LoadRecord + shipped census

A-tier continuation. Located via `ck_search_source_anchor("tesfaction.cpp")`;
full untruncated decompile via `ck_decompile` (this function fits the 64 KB
cap - no local pass needed).

## 1. LoadRecord

**`FUN_140a6b680`** (RVA 0xa6b680, 1878 B) - 23 tags:

`STOL OBND CITC DATA CRVA VENC VMAD EDID JAIL VEND PLVD FULL OBML PTRN
RNAM FNAM INAM MNAM XNAM PLCN PBLT STCP CRGR WAIT JOUT VENV`

Reader notes:

- `DATA` is **version-branched**: payload < 9 bytes takes the legacy read
  path and also fills the +0xC flag word; >= 9 uses the current layout.
- Rank blocks are a linked chain: `RNAM` allocates a 0x48-byte rank node,
  then `MNAM`/`FNAM` (male/female titles) and `INAM` (icon) fill it -
  treat `RNAM MNAM FNAM INAM` as one logical rank unit when re-serialising.
- `XNAM` (inter-faction relations) appends to a separate list; `CRVA`
  (crime values) reads an array into +0x13.
- Vendor block: `VENC` (end hour) / `VEND` (end day) / `VENV` (12 B
  vendor table, on every faction) / `PLVD` / `WAIT` / `JOUT`.

## 2. Shipped census

**845 records across the four masters, just 13 types** - the cleanest
record so far: every shipped type is in the dispatch except one stray
`CTDA` (emitted by the shared `CITC` condition helper).

Mandatory: `EDID DATA CRVA VENV` on all 845. `XNAM` relations on 431
records (1,000 occurrences); `FULL` 238; `PLVD` 110; `VENC/VEND` ~108;
`CRGR` (crime groups) 31; `RNAM` ranks only 10 records; `CITC` exactly 1.
