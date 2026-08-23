# 30 - TESPackage (PACK): LoadRecord + shipped census

S-tier continuation (doc 28/29 pattern). Source anchors:
`package.cpp` (`STR 0x2ff6550`, `code\shared\ai\`) and
`bgspackagedata.cpp` (`STR 0x2fe9420`). Raw dump:
`ghidra-dump/decompile-package.txt`.

## 1. LoadRecord

**`FUN_140790640`** (RVA 0x790640, 2866 B) - 18 read-helper calls,
39 literal tags:

`CNAM CTDA EDID IDLD IDLF IDLN IDLT OBLT OBML OBND OBTE PBLT PBND PKCU
PKDD PKDT PKE2 PKFD PKPT PKW3 PLD2 PLDT POBA POCA POEA PSDT PT2A PTD2
PTDA PTDT PTRN QKDT QNAM QT2A QTDA RNAM STCP VMAD WMAD`

(`bgspackagedata.cpp` itself pins only one small function, 0x75ebb0 -
the package-data subclasses load through shared condition/item helpers.)

## 2. Shipped census

**4,844 records across the four masters, 31 types.**

Mandatory set (present on every record): `PKDT PSDT PKCU XNAM POBA POEA
POCA` (+ EDID). High-frequency: `UNAM` 48,874 / `ANAM` 47,247 / `CNAM`
34,444 (per-procedure fields - many per record), `PDTO` 14,865, `INAM`
14,532, `PKC2` 6,833, `CTDA` 6,045, `PLDT` 5,764, `PTDA` 5,309.

12 types ship without inline dispatch literals - same computed-tag helper
pattern as QUST/INFO (doc 28 s3, doc 29 s2): `UNAM ANAM PDTO INAM PKC2
XNAM PNAM BNAM CITC FNAM PRCB CIS2`.

## 3. S-tier status

QUST / DIAL / INFO / PACK documented; **SCEN** remains.
