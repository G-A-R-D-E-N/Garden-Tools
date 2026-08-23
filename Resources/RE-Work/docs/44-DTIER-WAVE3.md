# 44 - D-tier wave 3: CPTH / LSCR / DFOB / HDPT / AVIF / SOPM / IPDS / ECZN / LCTN / OTFT

Continues docs 42/43. Decompiles in `ghidra-dump/decompile-dtier-wave3.txt`.

## 1. LoadRecord locations

| type | class | LoadRecord | size | literal tags |
|---|---|---|---|---|
| CPTH | BGSCameraPath | `FUN_140b77730` | 814 B | 14 (`ANAM CTDA DATA SNAM FDID`) |
| LSCR | TESLoadScreen | `FUN_140bd64c0` | 1301 B | 18 incl `MOD2 NNAM ONAM TNAM WMAD ZNAM` |
| DFOB | BGSDefaultObject | `FUN_140438d20` | 440 B | bare: `DATA EDID` |
| HDPT | BGSHeadPart | `FUN_1409aee30` | 1302 B | 28 incl `HNAM EATA NAM0 NAM1 PNAM RNAM` |
| AVIF | ActorValueInfo | `FUN_140734e70` | 941 B | 19 incl `ANAM AVFL BVFL NAM0 NAM1 WMAD` |
| SOPM | BGSSoundOutput | `FUN_140bc01e0` | 1255 B | 20 incl `ATTN ENAM FNAM GNAM MNAM NAM1` |
| IPDS | BGSImpactDataSet | `FUN_140b93890` | 758 B | near-bare: `EDID PNAM` (sound list) |
| ECZN | BGSEncounterZone | `FUN_140ca5580` | 530 B | bare: `DATA EDID` |
| LCTN | BGSLocation | **`FUN_140cb1380`** | **3164 B** | 32 - biggest data-type body: worldspace refs (`NAM0 NAM1`), actor/creature/vendor encounter sets (`ACPR ACUN ACSR ACEC BCPR BCUN LCEC LCUN MCEC RCPR`), music (`MNAM`), parent chain (`PNAM`), `KSIZ/KWDA` keywords |
| OTFT | BGSOutfit | `FUN_140c0e6e0` | 891 B | bare: `EDID INAM` (item list) |

## 2. Writer notes

* Three of these are effectively two-tag records (DFOB, ECZN, OTFT,
  plus IPDS at three) - emit-and-go.
* LCTN's ACUN-family subrecords are parallel arrays keyed by encounter
  type; count them from the census before writing (they interleave with
  actor-level BCPR/BCUN).
* HDPT shares the head-part model family with ARMO's second model set
  (`MODL/MODS/MODT`) and carries `EATA` (armor type link) like ARMO.
* AVIF's `AVFL/BVFL` are the calc-child/parent form lists; `WMAD`
  appears here and in LSCR/IPCT - a shared conditional-window block.
