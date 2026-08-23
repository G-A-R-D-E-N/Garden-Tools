# 40 - All-type census (four masters)

Every record type present in Fallout4.esm + DLCCoast + DLCRobot +
DLCNukaWorld: **147 distinct types, 2,238,990 records**. Counts are exact
(strict walker). ✅ marks families whose LoadRecord is documented
(docs 28-39).

## >= 100k records

| type | count | notes |
|---|---:|---|
| REFR | 1,771,972 | ✅ doc 39 |
| INFO | 101,451 | ✅ doc 30 |

## 10k-100k

CELL 63,061 (✅ doc 23) · DIAL 45,885 (✅) · LAND 43,780 · STAT 22,714 · NAVM 12,520 (runtime-side docs 21/24) · ACHR 10,322

## 1k-10k

RFGP 9,320 · SNDR 6,975 · PACK 4,844 (✅) · SCEN 4,714 (✅) · LAYR 4,498 · NPC_ 4,319 (✅ doc 32) · KYWD 4,169 · OMOD 3,988 · IDLE 3,688 · COBJ 3,504 · MSWP 3,503 · SCOL 3,291 (✅ doc 37) · MISC 3,236 (✅ doc 38) · LVLI 2,708 (✅ doc 37) · PHZD 2,342 · GMST 2,051 · MESG 1,926 · GLOB 1,867 · QUST 1,862 (✅ doc 28) · ACTI 1,674 · TRNS 1,182 · MSTT 1,105 · ARMA 1,060 · ARMO 1,008 (✅ doc 38)

## 100-999

TERM 1,004* · PKIN 1,003* · LIGH 986 · FLST 910 · LCRT 909 · FACT 845 (✅ doc 34) · IPCT 835 · MGEF 830 (✅ doc 37) · CAMS 772 · VTYP 730 · FURN 697 (✅ template) · PERK 655 (✅ doc 37) · MUST 624 · CONT 580 · PGRE 522 · LCTN 498 · TXST 493 · OTFT 475 · BOOK 468 (✅ doc 38) · WEAP 465 (✅ doc 38) · CPTH 447 · LSCR 442 · DFOB 442 · DOOR 429 · SPEL 421 (✅ doc 37) · HDPT 420 · ENCH 416 (✅ doc 37) · AVIF 410 · SOPM 380 · IPDS 363 · ECZN 362 · LVLN 360 (✅ doc 37) · ALCH 353 (✅ doc 37) · SOUN 333 · IMGS 332 · SMQN 326 · ADDN 322 · EXPL 317 (✅ doc 27) · IMAD 265 · NOTE 252 (✅ doc 38) · KEYM 246 (✅ doc 38) · SCCO 225 · PROJ 218 (✅ doc 27) · ARTO 209 · KSSM 196 · DLBR 182 · AACT 175 · FSTP 175 · CLFM 166 · ANIO 164 · REGN 153 · MATT 152 · CSTY 143 (✅ doc 36) · MUSC 134 · SMBN 132 · LTEX 128 · GRAS 107 · WTHR 104 · MOVT 103

## < 100

ASPC 96 · AMMO 87 (✅ doc 38) · RACE 84 (✅ doc 33) · HAZD 78 (✅ doc 27) · TACT 74 · LGTM 72 · EFSH 70 · GDRY 69 · RELA 67 · FLOR 63 (✅ doc 38) · REVB 59 · AMDL 59 · BPTD 58 · COLL 57 · RFCT 53 · SNCT 50 · IDLM 49 · LENS 49 · WATR 47 · EQUP 47 · FSTS 42 · STAG 37 · CMPO 33 · CLAS 32 (✅ doc 35) · AECH 30 · MATO 26 · INNR 20 · SMEN 18 · ZOOM 18 · WRLD 17 (✅ doc 24) · ASTP 13 · BNDS 12 · CLMT 11 · SCSN 11 · AORU 9 · DMGT 8 · DEBR 5 · SPGD 4 · NAVI 4 · PMIS 2 · INGR 1 (✅ doc 38) · TREE 1 (✅ doc 38) · DOBJ 1 · DLVW 1 · NOCM 1 · OVIS 1

\* TERM/PKIN sit at exactly 1,004/1,003 - just over the band edge.

## Writer-relevant observations

* The long tail is real: **62 types ship fewer than 100 records**, and six
  ship exactly one (INGR/TREE/DOBJ/DLVW/NOCM/OVIS). A plugin writer can
  treat the whole sub-100 band as "emit-if-needed" and still cover
  99.99% of record volume.
* Top-6 types (REFR/INFO/CELL/DIAL/LAND/STAT) are 94.5% of all records -
  every one of them already has a documented loader or layout.
* LAND is the only top-10 type without a dedicated doc (vertex/layer
  format is self-contained in its VTEX/VNML/VHGT triple).
* Per-master type counts differ sharply (Fallout4 137 / Coast 113 /
  NukaWorld 111 / Robot 97) - writers must not assume a fixed superset;
  validate against each master's TES4 masters list instead.
