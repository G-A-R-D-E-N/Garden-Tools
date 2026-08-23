# 28 - BGSQuest (QUST): LoadRecord/SaveRecord + shipped census

Phase 5 S-tier start (ROADMAP next-step #3). First form type outside the
CELL/WRLD/LAND/REFR core to get the full treatment.

## 1. Locating the class (tesquest.cpp)

`__FILE__` anchor: `STR 0x303a500 =
e:\buildagent\...\code\shared\tesforms\character\tesquest.cpp`. Five
functions reference it:

| function | RVA | size | role |
|---|---|---|---|
| **`FUN_140abdaf0`** | 0xabdaf0 | 2904 | **LoadRecord** - gated on reader phase `'P'`, 16 calls to the shared subrecord read helper `FUN_14087e870`, 45 literal 4CC dispatch comparisons |
| `FUN_140abe690` | 0xabe690 | 2286 | **SaveRecord** (high confidence: assert-dense, form lookups `FUN_1408be010`, zero read helpers) |
| `FUN_140abf5a0` | 0xabf5a0 | 177 | small helper (assert + single call) |
| `FUN_140ac09d0` | 0xac09d0 | 85 | small helper |
| `FUN_140ac0a40` | 0xac0a40 | 1300 | member/condition helper (CTDA-adjacent call pattern) |

Raw dump: `ghidra-dump/decompile-tesquest-cluster.txt`.

## 2. LoadRecord static dispatch (45 tags)

`ALCS ALLS ALST ANAM BNAM CNAM CTDA DATA DNAM EATA EDID ENAM FLTR FNAM FULL
GNAM INDX LNAM NAM2 NEXT OBLT OBML OBND OBTE PBND PTRN QNAM QOBJ QSDT QSTA
QTGL RNAM RTGL SCDA SCHR SCRO SCRV SCTX SCVR SLSD SNAM STCP TCHR VMAD XNAM`

Plus base-class saves (`OBND PTRN STCP VMAD` and template `OBTE/OBML/OBLT`)
and the inline script block (`SCHR SCDA SCTX SCVR SCRO SCRV SLSD`).

## 3. Shipped census (Fallout4 + DLCCoast + DLCRobot + DLCNukaWorld)

**1,862 QUST records**, 59 distinct subrecord types, parsed with
`tools/plugin_parse.py`.

Dispatch tags **never present** in any shipped master (21): `BNAM DATA EATA
OBLT OBML OBND OBTE PBND PTRN QNAM RNAM RTGL SCDA SCHR SCRO SCRV SCTX SCVR
SLSD STCP TCHR`. Two takeaways: quests attach scripts exclusively through
`VMAD` (the inline SCRH* block is dead weight in FO4 files), and the
TES4-era `DATA`/`QNAM`/`RNAM` fields are gone from the CK's write path.

Types present in files **but dispatched via computed tags/helpers** rather
than inline literals (35, all alias/display family): `ALID ALED ALUA ALEQ
ALEA ALFL ALPC ALFR ALFA ALRT ALFC ALFV ALCO ALCA ALCL ALLA ALDN ALFE ALFD
ALMI ALSP ALFI ALCC ALNA ALNT ALDI ECOR CIS1 CIS2 KSIZ KWDA COCT CNTO NNAM
VTCK`.

Top frequencies (records carrying them): `FNAM` 17,946; `ALID`/`ALED`
16,099; `ALST`/`VTCK` 15,144 (exact 1:1 pairing - every alias name block
carries a voice-type slot); `CTDA` 14,903; `QSDT` 13,296; `INDX` 12,775;
`NAM2` 12,237; then `ALPC ALNA ALNT ALFA ALRT QSTA ALUA ALCO ALCA ALCL`.
Record-mandatory set: `EDID` 1862/1862; `DNAM NEXT ANAM` 1855;
`QOBJ NNAM` 1847; `VMAD` 1327.

## 4. Open items

- Confirm the SaveRecord writer-helper chain (expected analogue of CELL's
  `FUN_1408c7710`) by decompiling `FUN_140abe690`'s callees.
- Map the computed-tag alias handlers to their 4CCs (they sit behind the
  `FUN_1402c9670`/`FUN_1407bb4e0` helpers in LoadRecord).
- DIAL/INFO/PACK/SCEN remain untouched (next S-tier targets).
