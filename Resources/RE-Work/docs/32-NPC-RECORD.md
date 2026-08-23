# 32 - TESNPC (NPC_): LoadRecord + shipped census

A-tier start (docs 28-31 pattern). Source anchor: `tesnpc.cpp`
(`STR 0x3037550`, `code\shared\tesforms\character\`). Raw dump:
`ghidra-dump/decompile-tesnpc.txt`.

## 1. LoadRecord

**`FUN_140a89910`** (RVA 0xa89910, **7935 B**) - by far the largest
LoadRecord documented: **55 read-helper calls, 100 literal tags**:

`ACBS AIDT ANAM APGD APPR ATKD ATKR ATKS ATKT ATKW ATTX BCLF BNAM BPPR
BTKT BTKW CCNT CNAM CNTO COCT COED CRIF CS2H CSCR DAMC DATA DEST DNAM
DNTO DOFT DPLT DS2H DSTD EATA ECOR EDID ENAM FCPL FMIN FMRI FMRS FNAM
FTST FTYP FULL GNAM GWOR HCLF HEAD INAM KSIZ LTPC LTPT MRSV MSDK MSDV
MWGT MXWT NAM4 NAM6 NAM8 NAM9 NSDK NTRM OAM4 OBLT OBML OBND OBTE OCOR
PBTE PFRN PKID PNAM PRKZ PRPS PTRN QKID QNAM RCLR RNAM SCLR SHRT SNAM
SOFT SPCT SPLO SPOR STCP TEND TETI THRT TPCT TPLT TPTA VMAD VTCK WMAD
WNAM ZNAM`

## 2. Shipped census

**4,319 records across the four masters, 79 types.** Mandatory set:
`EDID OBND ACBS RNAM PRPS AIDT CNAM DATA DNAM NAM4 NAM5 NAM6` on all
4,319, plus `PNAM` (face/base texture pair?) on 23,165 occurrences.

12 types ship outside the literal dispatch - computed-tag helpers again:
`NAM5` (all 4,319), `KWDA` 2,902, `PRKR` (perk ranks) 2,244, and the
template family `OBTS`/`STOP` 432 / `OBTF` 140 (matching doc 26's NPC_
template counts), plus `CS2D/CS2E/CS2F/CS2K`, `ATKE`, `DSTF`.

Top frequencies: `FMRI/FMRS` 55,237 each (faction rank/rank-response
pairs), `PNAM` 23,165, `TETI/TEND` 12,502 each (tint-layer pairs),
`CNTO` 11,390, `SNAM` 8,908, `PRKR` 4,357.

## 3. Verification note

Runtime-side names cited across docs 27-32 are cross-checked against the
workspace `f4-re-mcp` index (`re-index.sqlite`: symbols, address library,
OG/AE map, name-correction audit trail). Example:
`TESFile::CheckValidIDForSmallFile` resolves to RVA 0x130560 sizeof 10 on
1.10.163 - matching doc 26 section 4's corrected table.
