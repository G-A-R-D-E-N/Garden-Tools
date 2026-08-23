# 36 - TESCombatStyle (CSTY): LoadRecord + shipped census

Completes A-tier. `ck_search_source_anchor("combatstyle.cpp")` +
`ck_decompile` end-to-end.

## 1. LoadRecord

**`FUN_140c2de90`** (RVA 0xc2de90, 636 B) - phase-gated on reader
state `'S'`; 16 tags with fixed payload sizes:

| tag | size | notes |
|---|---|---|
| `DATA` | 4 | |
| `CSRA` | 4 | |
| `CSFD` | 32 | dispatch-only: never ships in vanilla |
| `CSEM` | 40 | dispatch-only: never ships in vanilla |
| `CSGD` | 48 | on every record |
| `CDSCR` | 44 | |
| `CSLR` | **variable** | 12 / 16 / 20 B in shipped files - version-branched in the writer's past; readers must accept all three |
| `CSCV` | 4 | on every record |

Plus base saves `EDID VMAD OBND OBML OBTE PTRN STCP`.

Three types ship via computed tags outside the literal set:
`CSME CSFL CSCR` (each on all 143 records).

## 2. Shipped census

**143 records across the four masters**, zero unknown types.
Mandatory: `EDID DATA CSRA CSGD CSCV CSME CSFL CSCR`.
