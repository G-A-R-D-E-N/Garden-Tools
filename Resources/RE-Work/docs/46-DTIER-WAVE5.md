# 46 - D-tier wave 5: the 50-130 band (twenty types)

LTEX / GRAS / WTHR / ASPC / TACT / LGTM / EFSH / GDRY / RELA / REVB /
AMDL / BPTD / COLL / RFCT / SNCT / IDLM / LENS / WATR / EQUP / FSTS.
Decompiles in `ghidra-dump/decompile-dtier-wave5.txt`.

## 1. LoadRecord locations

| type | class | LoadRecord | size | literal tags |
|---|---|---|---|---|
| LTEX | TESLandTexture | `FUN_140d100b0` | 823 B | `GNAM HNAM INAM MNAM SNAM TNAM` |
| GRAS | TESGrass | `FUN_140c43fd0` | 551 B | `DATA` + model family |
| WTHR | TESWeather | **`FUN_140e31050`** | **4251 B** | 42 tags - largest data-type body: `DALC/EALC` color pairs, `NAM0..NAM4/OAM3` cloud layers, `IMSP`, `WGDR`, `PNAM/QNAM/RNAM/SNAM/TNAM/UNAM/VNAM/WNAM` |
| ASPC | BGSAcousticSpace | `FUN_140b75be0` | 680 B | `BNAM CNAM RDAT WNAM XTRI SNAM TTCP` |
| TACT | BGSTalkingActivator | `FUN_140c28840` | **159 B** | stub (`VNAM`) - rest via base hooks |
| LGTM | BGSLightingTemplate | `FUN_140cab450` | 645 B | `DATA DALC WGDR` |
| EFSH | TESEffectShader | `FUN_140c31c80` | 743 B | `DATA DNAM NAM7 NAM8 WMAD` + model |
| GDRY | BGSGodRays | `FUN_140461e70` | 427 B | bare `DATA EDID` |
| RELA | BGSRelationship | `FUN_140a1e030` | 508 B | bare `DATA EDID` |
| REVB | BGSReverbParameters | `FUN_140bb6660` | 584 B | `ANAM DATA` |
| AMDL | BGSAimModel | `FUN_140445290` | 419 B | bare `DNAM` |
| BPTD | BGSBodyPartData | `FUN_14097f280` | 1108 B | `BPNN BPTN PNAM QNAM RAGA` + model |
| COLL | BGSCollisionLayer | `FUN_140b819c0` | 1226 B | `BNAM..MNAM INTV DESC` - color+friction set |
| RFCT | BGSReferenceEffect | `FUN_140cc7690` | 816 B | `DATA` |
| SNCT | BGSSoundCategory | `FUN_140bb8eb0` | 832 B | `CNAM FNAM FULL MNAM..WNAM` chain |
| IDLM | BGSIdleMarker | `FUN_1409b5780` | 1059 B | `IDLA IDLC IDLF IDLT KSIZ PNAM QNAM NODT` |
| LENS | BGSLensFlare | `FUN_14045a420` | 771 B | `CNAM DNAM LFSP` |
| WATR | TESWaterForm | `FUN_140e2da10` | 1251 B | 29 incl `NAM0..NAM4/OAM3` (shared layer scheme with WTHR), `ANAM/BANM? no: ANAM BNAM FNAM GNAM INAM` |
| EQUP | BGSEquipSlot | `FUN_1409ab0a0` | 922 B | `ANAM DATA PNAM` (parent slot) |
| FSTS | BGSFootstepSet | `FUN_140b8d5f0` | 1003 B | `DATA XCNT` |

## 2. Writer notes

* WTHR and WATR share the `NAM0-NAM4 + OAM3` layer-slot convention.
* The DALC/EALC ambient-color pair appears in both WTHR and LGTM.
* TACT joins ACTI in the stub-loader club (159 B) - everything rides
  inherited hooks.
* After this wave every type >= 50 vanilla records has a documented
  LoadRecord. Remaining undocumented: ~30 types with <= 47 records,
  most singletons or degenerate two-tag forms - document strictly on
  demand.
