# 42 - D-tier wave 1: STAT / KYWD / OMOD / COBJ / MSWP / SNDR / IDLE / ACTI / LAYR / RFGP / ACHR

Loaders via vtable-slot walk (doc 37 method); decompiles in
`ghidra-dump/decompile-dtier-wave1.txt`; censuses = four masters.

## 1. LoadRecord locations

| type | class | LoadRecord | size | literal tags |
|---|---|---|---|---|
| STAT | TESObjectSTAT | `FUN_140c80790` | 989 B | 22 incl `MNAM PRPS` |
| OMOD | BGSMod::Attachment::Mod | `FUN_14044b730` | 1697 B | 28 incl `DATA LNAM MNAM NAM1 EESC` |
| COBJ | BGSConstructibleObject | `FUN_140bfcc10` | 844 B | 22 incl `INTV FVPA CNAM ANAM` |
| MSWP | BGSMaterialSwap | `FUN_140440b90` | 994 B | 14 (`BNAM SNAM FNAM CNAM` + base) |
| SNDR | BGSSoundDescriptorForm | `FUN_140bbda40` | 588 B | 12 literal - descriptor fields via component hooks |
| IDLE | TESIdleForm | `FUN_140a74ba0` | 1459 B | 18 incl `ANAM BNAM GNAM ENAM CTDA` |
| KYWD | BGSKeyword | `FUN_1409b6ff0` | 809 B | 15 incl `CNAM TNAM DNAM` |
| LAYR | BGSLayer | `FUN_140462f80` | 458 B | 10 - near-bare form (`EDID PNAM` + template family) |
| ACTI | TESObjectACTI | `FUN_140c5a100` | **129 B** | stub - all tags via base-class hook virtuals |
| RFGP | (no RTTI class resolved) | - | - | pure data record: `RNAM NNAM PNAM` only |
| ACHR | Actor (TESObjectREFR subclass) | shared REFR machinery (doc 39) | - | reference grammar: `NAME DATA` + X-extras |

## 2. Censuses (records / observed types)

STAT 22,714/11 · ACHR 10,322/10+ · RFGP 9,320/3 · SNDR 6,975/11 ·
LAYR 4,498/2 · KYWD 4,169/7 · OMOD 3,988/11+ · COBJ 3,504/11+ ·
MSWP 3,503/6

## 3. Structural notes

* STAT: `OBND DNAM EDID` on effectively all records; model family
  MODL/MODT ~all; `MNAM` (material swap link) on 3,943; `PRPS`
  property array on 909.
* MSWP: `BNAM`(target) + `SNAM`(swap entries) ship as exact pairs -
  3,478 each across masters (validator contract added).
* COBJ: `INTV` (created quantity) is the only universal tag
  (3504/3504; u32 or u16 sizes both occur); `CNAM` on all but five
  minimal scrap recipes - vanilla even ships an EDID/DESC/INTV-only
  record, so treat everything beyond INTV as optional.
* SNDR: `CNAM EDID` universal; the descriptor block
  (ANAM/BNAM/GNAM/LNAM/ONAM) rides on component hooks, not literals.
* KYWD: `CNAM TNAM` on nearly all (4,137/4,122 of 4,169); `DATA`
  appears on only 17 legacy records.
* LAYR: `PNAM` (parent layer) on 1,526 - a plain hierarchy record.
* RFGP: no vtable found under any tried RTTI name - likely an
  internal data record outside the normal TESForm vtable pattern;
  grammar is trivial (RNAM/NNAM/PNAM).
