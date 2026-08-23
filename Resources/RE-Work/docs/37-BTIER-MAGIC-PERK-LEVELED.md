# 37 - B-tier: SPEL / ALCH / ENCH / MGEF / PERK / LVLI / LVLN / SCOL

Census-first pass over the magic/perk/leveled tier. LoadRecords pinned for
MGEF and PERK; the rest have censuses and pinned RTTI, loader locations
pending. All counts = four masters (Fallout4 + DLCCoast + DLCRobot +
DLCNukaWorld). Raw dump: `ghidra-dump/decompile-btier-magic.txt`.

## 1. LoadRecord locations

| type | class | LoadRecord | status |
|---|---|---|---|
| MGEF | EffectSetting | **`FUN_140e66e40`** (0xe66e40, 2225 B) | ✅ 19 tags: `CTDA DATA DNAM EDID ESCE FULL KSIZ MDOB SNDD TNDD` + base (`OBLT OBML OBND OBTE PBTE PTRN QTRN STCP VMAD`) |
| PERK | BGSPerk | **`FUN_1409f15b0`** (0x9f15b0, 1346 B) | ✅ 20 tags: `CTDA DATA DESC EDID FDID FNAM FULL GULL ICON JCON NNAM PRKE SNAM` + base |
| ENCH | EnchantmentItem | candidate `FUN_140e72740` is a helper (0 tags) | 🔴 pending - no assert strings in its loader |
| SPEL | SpellItem | 🔴 pending | RTTI TypeDescriptor pinned via `.?AVSpellItem@@` |
| ALCH | AlchemyItem | 🔴 pending | RTTI pinned (`.?AVAlchemyItem@@`) |
| LVLI / LVLN | TESLeveledList | 🔴 pending | shared component file `formcomponents\tesleveledlist.cpp` pinned; `teslevspell.cpp` (LVSP) found at FUN_140c483a0 |

Hunt note: these loaders emit few/no assert strings, so the __FILE__-anchor
technique does not reach them; next step is relative-RTTI vftable walks
(see doc 25 correction for the method).

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
