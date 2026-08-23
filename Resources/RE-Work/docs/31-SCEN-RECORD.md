# 31 - BGSScene (SCEN): LoadRecord + shipped census

Completes the S-tier set (docs 28-30 pattern). Source anchor:
`bgsscene.cpp` (`STR 0x302dd10`, `code\shared\tesforms\character\`).
Raw dump: `ghidra-dump/decompile-bgsscene.txt`.

## 1. LoadRecord

**`FUN_140a20ae0`** (RVA 0xa20ae0, 2112 B) - 15 read-helper calls,
31 literal tags:

`ACTV ALID ANAM BLID BNAM CNAM CRIS CTDA DNAM EDID FNAM HNAM INAM KSIZ
LNAM NEXT NNAM OBLT OBML OBND OBTE PBLT PNAM PTRN QTRN SCQS STCP TNAM
VMAD VNAM XNAM`

(`FUN_140a1fe90`, 684 B, is a small class helper.)

## 2. Shipped census

**4,714 records across the four masters, 58 types** - the richest record
of the S-tier. 10 types ship without inline dispatch literals (the
scene-action computed-tag family): `NAM0 SNAM ENAM WNAM DATA DMAX DMIN
HTID PTOP QTOP`.

Frequencies: `ANAM` 49,146 (scene actions - many per record); `HNAM`
41,846 paired exactly with `NEXT`; `ALID` 30,755; `INAM` 29,287; `SNAM`
25,747; `ENAM` 24,573; `WNAM` 20,923; `FNAM` 18,423; `DATA DMAX`
15,529; `DMIN` 13,268; `HTID` 6,901; `PTOP/QTOP` ~3.6k.

## 3. S-tier complete

QUST (doc 28), DIAL/INFO (doc 29), PACK (doc 30), SCEN (this doc).
A-tier next per roadmap: NPC_, RACE, CSTY, FACT, CLAS.
