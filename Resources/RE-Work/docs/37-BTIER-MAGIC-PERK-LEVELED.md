# 37 - B-tier: SPEL / ALCH / ENCH / MGEF / PERK / LVLI / LVLN / SCOL

Census-first pass over the magic/perk/leveled tier. LoadRecords pinned for
MGEF and PERK; the rest have censuses and pinned RTTI, loader locations
pending. All counts = four masters (Fallout4 + DLCCoast + DLCRobot +
DLCNukaWorld). Raw dump: `ghidra-dump/decompile-btier-magic.txt`.

## 1. LoadRecord locations (all resolved 2026-08-22)

Found via vftable slot walk: LoadRecord/SaveRecord live at fixed slots
(24/27 in the doc 23 numbering) of every form class's vftable, so one
known class calibrates the slot for all others (`FindVftable.java`).

| type | class | LoadRecord | SaveRecord (slot 27) |
|---|---|---|---|
| SPEL / ALCH / ENCH | SpellItem / AlchemyItem / EnchantmentItem | **shared**: `FUN_140e76b30` (magicitem.cpp, 784 B) | `FUN_140e79f80` |
| LVLI / LVLN | TESLevItem | **`FUN_140c47480`** (teslevitem.cpp, 1318 B) | `FUN_1408db250` |
| MGEF | EffectSetting | `FUN_140e66e40` (2225 B) - 19 literal tags | - |
| PERK | BGSPerk | `FUN_1409f15b0` (1346 B) - 20 literal tags | - |

MagicItem shared-loader notes:

* Literal tags only `FULL EDID VMAD OBND OBTE OBML PTRN STCP KSIZ` plus
  the atomic effect block: `EFID` and interleaved `CTDA` both route into
  `FUN_140e771f0`.
* Subclass-specific tags (`SPIT` for SPEL, `ENIT/DNAM/DATA` for
  ALCH/ENCH) never appear here: after every subrecord the loader calls a
  **subclass hook virtual (vt+0x598)**, plus post-hooks vt+0x570/0x5c0/
  0x5a0 - that is where each derived class consumes its own fields.
* `KSIZ` triggers the keyword-array reader and warns on version skew
  ("Warnings were encountered loading keyword data").

TESLevItem loader notes (phase-gated on reader state '8'):

* Tags: `EDID OBND VMAD LVLG LVLD(1B) LVLF(1B) LVLM(1B) LLCT LVLO LLCK
  COED ONAM LLOF`.
* `LVLO` is a 12-byte entry `[level u16][formID u32][count u16][flag u8
  + pad]`, with the trailing flag byte gated on file form-version >= 0x45.
* `LLCT` allocates the entry array (n x 0x18 B); `COED` patches the last
  entry when present.

## 2. Censuses

| type | records | types | notes |
|---|---:|---:|---|
| SPEL | 421 | 10 | `EFID/EFIT` effect pairs 813 each; `SPIT` on all; `CTDA` 623 |
| ALCH | 353 | 20 | `EFID/EFIT` 1,262 pairs; `ENIT DATA DNAM` mandatory; `KSIZ/KWDA` 343 |
| ENCH | 416 | 7 | smallest: `EFID/EFIT` 614, `ENIT` on all, `CTDA` 295 |
| MGEF | 830 | 9 | `EDID DATA SNDD DNAM` mandatory; `VMAD` 244; `KWDA/KSIZ` 208 |
| PERK | 655 | 18 | entry structure `PRKE`(990)+`PRKF`(990) with `EPFT/EPFB/EPFD` payloads (823/823/796) and `PRKC` conditions 773; `CTDA` 1,800 |
| LVLI | 2,708 | 12 | `LVLD LVLF OBND EDID` mandatory; `LVLO` entries 12,252; `LLCT` count 2,701; `LVLM` 2,643 |
| LVLN | 360 | 11 | same grammar as LVLI (`LVLO` 1,862) |
| SCOL | 3,291 | 11 | `ONAM/DATA` pairs 21,374 (!); `MODL/MODT` 3,290; `FLTR` 2,697 |

Structural rules for writers:

* Effect blocks are atomic `EFID`+`EFIT` pairs - always equal counts within
  a record, CTDA conditions interleaved between pairs.
* Leveled lists (LVLI/LVLN share the grammar): header `LVLD`(chance)+
  `LVLF`(flags)+`LLCT`(entry count), then exactly LLCT × `LVLO`.
* SCOL's `ONAM`+`DATA` run in strict 1:1 pairs (21,374 each across 3,291
  records) - one combined-object reference per pair.
* LVSP ships zero records in vanilla but the class exists
  (`teslevspell.cpp`, FUN_140c483a0).
