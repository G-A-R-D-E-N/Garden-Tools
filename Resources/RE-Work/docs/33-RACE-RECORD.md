# 33 - TESRace (RACE): LoadRecord + shipped census

A-tier continuation (docs 28-32 pattern). Source anchor: `tesrace.cpp`
(`STR 0x303d050`, `code\shared\tesforms\character\`). Located via
`ck_search_source_anchor` in one call. Raw dump:
`ghidra-dump/decompile-tesrace.txt`.

## 1. LoadRecord

**`FUN_140aeff50`** (RVA 0xaeff50, **9613 B**) - the largest documented:
125 literal tags covering body data (`BOD2`), attack chains
(`ATKD/ATKR/ATKS/ATKT/ATKW`), sounds (`SADD SGNM SAPT SRAF STKD SAKD`
and the movement-sound family `RNMV SNMV WKMV TWMV FLMV SWMV` +
`PHWT PHTN PHKV QHKV` physics), morph system (`MSID MSM0 MSM1 MPP*
NPGN NSID MPGN MPGS MTYP`), hair/tint (`HCLF HNAM TINL TTGP TETI`
+ colour `NAME INDX SPED SPLO SPCT`), equipment/apparel (`APPR RPRF
RPRM DFTF DFTM BPT?/BMMP`), and base (`ACBS`-equivalent fields via
`DATA`, `DESC`, `VTCK WTCK`, `ANAM FNAM GNAM JNAM LNAM MNAM NNAM ONAM
QNAM UNAM VNAM WNAM XNAM ZNAM` name-family, `EATA HEAD IEAD INAM KSIZ
MLSI RBPC SRAC SRAF STCP TPLO UNWP WMAP FT* GTSF FTSF AHCF AHCM BHCM`).

Notable reader behaviour: `RBPC` (body part data) is version-branched -
payload < 0x4e bytes reads 32 legacy slots, otherwise 32 new-format
slots; `SRAC` triggers a race-sound registration side effect.

## 2. Shipped census

**84 records across the four masters (small population, extreme
variety): 101 distinct subrecord types.**

Mandatory-ish: model trio `MODL MODT MTNM` on all 84 (computed-tag
family), `EDID DATA ANAM?`; high-frequency: `SAPT` 20,349 (sound
descriptors), `SAKD` 7,796, `STKD` 5,743, `SGNM/SRAF` 3,799, `PHWT`
3,440 (physics weights), `NAME` 2,688, `TTET` 2,241, `ATKD/ATKE`
1,166 pairs.

Computed-tag families outside the literal dispatch (12):
`MODL MODT MTNM KWDA ATKE TETI TTET TTEC TTED TTGE TTEF CTDA`.

## 3. Tooling note

`ck_decompile` truncated at maxDecompileBytes (64 KB) on this function;
the MCP call located and identified it, and the untruncated tag set came
from a local `DecompileRange.java` pass. Candidate improvement: a range
or chunked mode for oversized functions.
