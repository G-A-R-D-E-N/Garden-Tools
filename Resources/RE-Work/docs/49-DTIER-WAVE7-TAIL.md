# 49 - Final wave: the sub-50 tail - form-type coverage complete

Last fourteen loaders; with this doc every one of the census's
non-degenerate types is covered. Decompiles in
`ghidra-dump/decompile-dtier-wave7.txt`.

## 1. LoadRecord locations

| type | class | LoadRecord | size | literal tags |
|---|---|---|---|---|
| STAG | BGSSoundTagSet | `FUN_140443380` | 663 B | `TNAM` |
| AECH | BGSAudioEffectChain | `FUN_140431af0` | 1029 B | `DNAM ENAM KNAM` effect chain |
| MATO | BGSMaterialObject | `FUN_140b9b510` | 874 B | `DATA DNAM` + model |
| INNR | BGSInstanceNamingRules | `FUN_140439af0` | 1105 B | `KSIZ TTCP UNAM VNAM WNAM XNAM YNAM` rule chain |
| SMEN | BGSStoryManagerEventNode | `FUN_140fecfa0` | **110 B** | bare `ENAM` stub (story-node family) |
| ZOOM | BGSZoomData | `FUN_140459d10` | 426 B | `GNAM` |
| ASTP | BGSAssociationType | `FUN_14096a5e0` | 795 B | `DATA FCHT FPRT MCHT MPRT` parent/child reaction pairs |
| BNDS | BGSBendableSpline | `FUN_140446ed0` | 543 B | `DNAM ENAM TNAM` |
| CLMT | TESClimate | `FUN_140cdd9c0` | 831 B | `FNAM MODL QNAM TNAM WLST` weather list |
| SCSN | BGSSoundCategorySnapshot | `FUN_14043c6a0` | 792 B | `CNAM DNAM PNAM` |
| AORU | BGSAttractionRule | `FUN_140419100` | 548 B | `AOR2 AORD` |
| DMGT | BGSDamageType | `FUN_140437b10` | 1073 B | `DNAM` damage-type table |
| DEBR | BGSDebris | `FUN_140c03440` | 949 B | `DATA MODT` |
| SPGD | BGSShaderParticleGeometryData | `FUN_140cca7e0` | 1032 B | `DATA ICON MNAM` |

## 2. Remaining types with no loader (internal data records)

* NAVI (4) / PMIS (2): navmesh-root and placed-missile internals -
  runtime-side machinery, not plugin-writer forms.
* DLVW (1) / NOCM (1) / OVIS (1): single-record degenerates; no RTTI
  class resolved. Treat as opaque if a plugin ever contains them.

## 3. Coverage statement

Docs 23-49 together cover **all 147 record types observed in the four
vanilla masters**: 143 with located LoadRecords or documented shared/
stub loaders, 4 as internal/no-RTTI records, plus the extra-data
machinery (doc 39) and per-family censuses (doc 40). The record-model
phase of the roadmap is complete.
