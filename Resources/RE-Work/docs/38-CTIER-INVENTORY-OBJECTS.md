# 38 - C-tier: WEAP / ARMO / AMMO / MISC / BOOK / NOTE / KEYM / INGR / FLOR / TREE

Loaders located by the doc 37 vtable-slot walk; tag sets extracted from the
decompiled bodies (`ghidra-dump/decompile-ctier-inventory.txt`). Census =
four masters.

## 1. LoadRecord locations

| type | class | LoadRecord | size | literal tags |
|---|---|---|---|---|
| WEAP | TESObjectWEAP | `FUN_140c8ade0` | 3756 B | 67 - largest in the game so far |
| ARMO | TESObjectARMO | `FUN_140c67980` | 2924 B | 57 |
| MISC **and** KEYM | TESObjectMISC | `FUN_140c7c860` | 1475 B | 33 - TESKey shares this vtable slot verbatim |
| BOOK | TESObjectBOOK | `FUN_140c6f3a0` | 1750 B | 34 |
| AMMO | TESAmmo | `FUN_140c2a5a0` | 1392 B | 35 |
| NOTE | BGSNote | `FUN_1409ecf70` | 1196 B | 21 |
| FLOR | TESFlora | `FUN_140c36930` | 248 B | 3 (`PFIG PFPC SNAM`) |
| TREE | TESObjectTREE | `FUN_140c83530` | 915 B | 22 |
| INGR | IngredientItem | **shared MagicItem loader** `FUN_140e76b30` (doc 37) | 784 B | subclass tags via vt+0x598 hook |

Shared-loader findings:

* KEYM is a pure alias of the MISC loader - no separate function exists;
  writers emit identical grammar for both.
* INGR joins SPEL/ALCH/ENCH on the MagicItem base loader; its specific
  tags arrive through the subclass hook virtual.
* FLOR's own body is a stub for produce settings (`PFIG`/`PFPC`/`SNAM`);
  its model/DATA tags flow from inherited base-class hooks (same pattern
  as SPEL/ALCH/ENCH).

## 2. Censuses

| type | records | types |
|---|---:|---:|
| WEAP | 465 | 39 |
| ARMO | 1,008 | 32 |
| AMMO | 87 | 17 |
| MISC | 3,236 | 21 |
| BOOK | 468 | 18 |
| NOTE | 252 | 13 |
| KEYM | 246 | 10 |
| INGR | 1 | 10 |
| FLOR | 63 | 22 |
| TREE | 1 | 6 |

## 3. Structural notes for writers

* The destruction block family (`DEST` + repeated `DSTD`, plus `DAMC`
  stage counters) recurs across WEAP/ARMO/AMMO/MISC/BOOK - one shared
  reader shape, five consumers.
* ARMO carries a second model set: `MOD2`+`MO2S/MO2T/MO2C` and `ICO2`/
  `MIC2`/`JCO2` alongside the primary `MODL` family, plus `BOD2` (body
  slots), `BMCT`, `EATA`.
* WEAP's long tail is mostly per-modular-part data (`NAM7/8/9`, `OAM9`,
  `WAMD/WMAD/WZMD`, `MASE/NASE`, `MO4*`/`NO4C/NODS` second model set,
  `CRDT`, `APGD/BPGD` aim models, `APPR/AVMG/BVMG` attach-point/value
  modifiers, `INRD/JNRD`).
* AMMO adds `NAM1/NAM2` (projectile + consumable links) and `ESTD`
  alongside the common `DEST/DSTD`.
* BOOK has `CNAM` (description token) + `INAM` icon pair + `EESC`;
  NOTE keeps `PNAM/QNAM(+QTRN)` image blocks and `SNAM`.
* TREE ships exactly 1 record (Fallout4.esm) - 6 tags only; FLOR is
  similarly tiny at 63 records despite 22 observed types.
