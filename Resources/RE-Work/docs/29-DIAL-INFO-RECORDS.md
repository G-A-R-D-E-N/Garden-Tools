# 29 - TESTopic (DIAL) / TESTopicInfo (INFO): LoadRecord + shipped census

S-tier continuation (doc 28 pattern applied to the dialogue system).
Source anchors: `testopic.cpp` (`STR 0x3040c80`) and `testopicinfo.cpp`
(`STR 0x3042640`), both under `code\shared\tesforms\character\`.
Raw dumps: `ghidra-dump/decompile-testopic.txt`,
`ghidra-dump/decompile-testopicinfo.txt`.

## 1. DIAL - TESTopic

| function | RVA | size | role |
|---|---|---|---|
| **`FUN_140b38d50`** | 0xb38d50 | 1314 | **LoadRecord** - 8 read-helper calls |
| `FUN_140b39e10` | 0xb39e10 | 481 | helper (no tags) |
| `FUN_140b3a7d0` | 0xb3a7d0 | 543 | helper |
| `FUN_140b3aa60` | 0xb3aa60 | 424 | helper |

Dispatch: `BNAM CNAM DATA EDID FULL KNAM PNAM QNAM SNAM TNAM TIFC` plus
base-class saves (`OBND OBTE OBLT OBML PBND PTRN STCP VMAD`).

Shipped census: **45,885 records across the four masters, 9 types,
zero outside dispatch.** Every record carries `PNAM QNAM DATA SNAM TIFC`
(100% each); `BNAM` (branch) 1,669, `KNAM` 1,587; only 982 have `EDID`
and 955 `FULL` - most topics are identified by their PNAM/SNAM alone.
CNAM never ships in these masters.

## 2. INFO - TESTopicInfo

| function | RVA | size | role |
|---|---|---|---|
| **`FUN_140b57a50`** | 0xb57a50 | 2915 | **LoadRecord** - 27 read-helper calls, 48 literal tags |
| `FUN_140b57600` | 0xb57600 | 259 | small helper |

Static dispatch (48): `ALFA ANAM BNAM CTDA DATA DNAM EDID ENAM GNAM GREE
INAM INCC INTV IOVR JNCC LNAM MODQ NAM0 NAM1 NAM2 NAM3 NAM4 NAM9 OAM9 OBLT
OBML OBND OBTE ONAM PBND PNAM PTRN QSTI QTRN RNAM RSTI SNAM SRAF STCP TIQS
TNAM TPIC TRDA TRDT TSCE UIQS VMAD WZMD`.

Shipped census: **101,451 records, 32 types.** Only `CIS1` (48) / `CIS2`
(10,302) fall outside the static tag set - the same computed-tag condition-
string pair as QUST (doc 28 section 3), emitted by the shared CTDA
condition parser rather than inline literals.

Frequencies: `ENAM` 101,451 (every record); `NAM0` 101,441; `INAM`
100,688; the response-data quintuple `TRDA NAM1 NAM2 NAM3 NAM4` appears
exactly 95,851 times each (one atomic response block); `CTDA` 87,878;
`NAM9` 82,265; `GNAM` 29,516; `PNAM` 23,364; `RNAM` 16,250; `CIS2` 10,302.

Structural note for readers/writers: the fixed `NAM1..NAM4` run following
`TRDA` is a single logical unit - treat it as one response record when
re-serialising, not four independent fields.

## 3. Remaining S-tier

SCEN and PACK are untouched; their anchors should follow the same recipe
(`bgsscene*.cpp`, `packdata*.cpp` expected).
