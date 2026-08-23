# 45 - D-tier wave 4: SOUN / REGN / ANIO / IMGS / IMAD / ADDN / MATT / SMQN / DLBR / ARTO / CLFM / KSSM

Continues docs 42-44; the 150-350-record band. Decompiles in
`ghidra-dump/decompile-dtier-wave4.txt`.

## 1. LoadRecord locations

| type | class | LoadRecord | size | literal tags |
|---|---|---|---|---|
| SOUN | TESSound | `FUN_140bf7c90` | 909 B | `EDID REPT SDSC` + template |
| REGN | TESRegion | **`FUN_1409571d0`** | 1248 B | 29 - the region-data array family: `RDAT RDGS RDLN RDMO RDMP RDOB RDOJ RDSA RDSD RLDM RPLD RPLI` + `RCLR/SCLR SDMO SDSD SLDM ANAM ICON FDID` |
| ANIO | TESObjectANIO | `FUN_140c5da60` | 764 B | model family + `BNAM FDID TTCP NODL` |
| IMGS | TESImageSpace | `FUN_140cdfa50` | 937 B | `DNAM ENAM HNAM CNAM TNAM TX00` - TX00 is the first of a numbered family |
| IMAD | TESImageSpaceModifier | **`FUN_140ce4ec0`** | **3453 B** | 27 - `NAM1..NAM6 OAM6` eye/color/dof modifier chains + lettered tail (`RNAM SNAM TNAM UNAM VNAM WNAM XNAM YNAM`) |
| ADDN | BGSAddonNode | `FUN_140bf9d70` | 868 B | model family + `DATA DNAM LNAM SNAM TTCP FDID` |
| MATT | BGSMaterialType | `FUN_140b9ea40` | 853 B | `ANAM BNAM CNAM FNAM HNAM MNAM PNAM` |
| SMQN | BGSStoryManagerQuestNode | `FUN_140ff4210` | 619 B | bare: `FNAM HNAM MNAM QNAM RNAM` |
| DLBR | BGSDialogueBranch | `FUN_140983290` | 792 B | `DNAM QNAM RNAM SNAM TNAM` |
| ARTO | BGSArtObject | `FUN_140bfbe90` | 758 B | model family + `DNAM KSIZ NODS PBND` |
| CLFM | BGSColorForm | `FUN_140b890e0` | 533 B | `CNAM FNAM FULL CTDA` |
| KSSM | BGSSoundKeywordMapping | `FUN_14043e3c0` | 1000 B | `DNAM ENAM KNAM LNAM RNAM TNAM VNAM` |

## 2. Writer notes

* REGN's RD* subrecords are a tagged union: each entry type has its own
  4CC and payload shape; writers should emit only the entry types they
  need (the loader switches per-4CC inside the loop).
* IMAD's NAM1-NAM6 are fixed-position modifier slots (missing slots
  omitted), with OAM6 as the odd one out.
* SCCO (225 recs) had no RTTI-resolvable class in this pass - like
  RFGP it looks like an internal data record; census grammar is
  trivial if needed later.
* With this wave, every type >= 150 vanilla records has its loader
  documented; what remains is the <150 long tail (mostly <= 50).
