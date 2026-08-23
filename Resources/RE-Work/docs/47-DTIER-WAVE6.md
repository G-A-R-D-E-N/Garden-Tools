# 47 - D-tier wave 6: the missed mid-volume types

Coverage audit against doc 40 caught types the earlier waves skipped:
GMST / MESG / GLOB / TRNS / ARMA / PKIN / MOVT / FSTP / MUSC / SMBN /
LCRT / AACT / PHZD. Decompiles in
`ghidra-dump/decompile-dtier-wave6.txt`.

## 1. LoadRecord locations

| type | class | LoadRecord | size | literal tags |
|---|---|---|---|---|
| GMST | TESGlobal | `FUN_140bd5a40` | 446 B | `EDID FLTV` only - string-valued globals have **no inline value subrecord**; their text rides the localization path (see doc 26 §2a) |
| MESG | BGSMessage | `FUN_140ba2e20` | 1091 B | `DNAM FULL DESC INAM ITXT CTDA JNAM NNAM QNAM SNAM TNAM` |
| GLOB | = TESGlobal, same loader as GMST |
| TRNS | BGSTransform | `FUN_140444930` | 436 B | bare `DATA EDID` |
| ARMA | TESObjectARMA | **`FUN_140c5ed40`** | **2144 B / 48 tags** - five model sets (`MODL MOD2..MOD5`, each with S/T/C + F variants like `MO2F MO3F MO4F MO5F`), `BOD2` slots, `BSMB/BSMP/BSMS/CSMB` bone-scaling blocks, `NAM0-NAM3`, `SNDD TNDD` destruction |
| PKIN | BGSPackIn | `FUN_140c10a70` | 527 B | `CNAM DNAM FLTR` |
| LCRT | BGSLocationRefType | **shared KYWD loader** `FUN_1409b6ff0` | - | keyword-family alias (like KEYM=MISC) |
| AACT | BGSAction | **shared KYWD loader** `FUN_1409b6ff0` | - | same |
| MOVT | BGSMovementType | `FUN_1409e5710` | 1277 B | `SPED JNAM KNAM LNAM MNAM` speed table |
| FSTP | BGSFootstep | `FUN_140b8c820` | 581 B | `ANAM DATA` |
| MUSC | BGSMusicType | `FUN_140bb3b90` | 898 B | `FNAM PNAM QNAM TNAM WNAM` track chain |
| SMBN | BGSStoryManagerBranchNode | `FUN_140ff2b20` | 609 B | story-node family `CITC DNAM ENAM HNAM INAM MNAM PNAM QNAM SNAM XNAM YNAM` |
| PHZD | BGSHazard (placed instance) | `FUN_140c0a710` | - | placed-reference family (REFR machinery, doc 39) |

## 2. Writer notes

* The KYWD loader now has three aliases: KYWD itself, LCRT, AACT -
  all keyword-like simple forms.
* ARMA is the heaviest non-REFR record after WEAP; its per-model-set
  F-subrecords are first-person variants and must stay paired with the
  set's S/T/C trio.
* Story-manager nodes form a family (SMQN/SMBN/SMEN share grammar);
  SMEN/ZOOM/etc. remain in the sub-20 tail.
