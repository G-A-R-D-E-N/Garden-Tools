# Format notes

Confidence: `confirmed` / `strong` / `probable` / `speculative`.

## Record header (24 bytes)

`confirmed` against CommonLibF4 `FORM` (`sizeof == 0x18`) and a real ESL/ESP hex dump.

| Offset | Size | Field |
|-------:|-----:|-------|
| 0x00 | 4 | signature |
| 0x04 | 4 | data size (payload only) |
| 0x08 | 4 | flags |
| 0x0C | 4 | FormID |
| 0x10 | 2 | timestamp / VC info |
| 0x12 | 2 | version control |
| 0x14 | 2 | form version |
| 0x16 | 2 | VC version |

Fallout 4 form version observed: **131** (`confirmed`, Heather patch + `ccBGSFO4096-AS_Enclave.esl`; the seven official masters and the installed cc Creation ESLs on AE 1.11.240.0, user copies not counted, see TES4 / HEDR).

### Form version census: AE 1.11.240.0, 2026-10-12

Measured with `Radicle/tools/corpus/form_versions.py` over the install's Data folder: the seven official masters plus every `cc*.esm`/`cc*.esl` Creation present. User mod plugins are not included. `Fallout4.exe` reports 1.11.240.0. The script walks TES4, GRUP and record headers only and never decompresses. The form version is the `uint16` at record offset 0x14. `Min`/`Max` cover every record of the signature across the census plugins. `<131` counts records below 131 per master (`-` means the signature is absent from that master). TES4 is 131 in every plugin, and no ESL has a record below 131.

RACE: all 110 official RACE records are at form version 131 (Fallout4.esm 45, DLCRobot.esm 8, DLCworkshop01.esm 23, DLCCoast.esm 15, DLCworkshop02.esm 2, DLCworkshop03.esm 1, DLCNukaWorld.esm 16). The cc ESLs add 2 RACE records in ccBGSFO4046-TesCan.esl (OG, NG, AE) and 3 in ccOTMFO4001-Remnants.esl (NG, AE), all at form version 131, for totals of 112 (OG), 115 (NG) and 115 (AE).

Records below 131 are common, so 131 is not the only AE form version. The table lists every signature with a record below 131.

| Sig | Min | Max | Fallout4 | DLCRobot | DLCworkshop01 | DLCCoast | DLCworkshop02 | DLCworkshop03 | DLCNukaWorld | cc Creations total below 131 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| AACT | 15 | 131 | 173 | 0 | - | - | - | - | 0 | 0 |
| ACHR | 54 | 131 | 3800 | 0 | - | 0 | - | 0 | 0 | 0 |
| ACTI | 54 | 131 | 431 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| ADDN | 72 | 131 | 226 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| AECH | 100 | 131 | 12 | - | - | - | - | 0 | 0 | 0 |
| AMDL | 83 | 131 | 51 | 0 | - | 0 | 0 | - | 0 | 0 |
| ANIO | 109 | 131 | 3 | 0 | - | 0 | - | 0 | 0 | 0 |
| AORU | 109 | 131 | 6 | - | - | 0 | - | - | - | 0 |
| ARMA | 64 | 131 | 44 | 0 | - | 0 | 0 | 0 | 0 | 0 |
| ARTO | 57 | 131 | 90 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| ASPC | 126 | 131 | 65 | - | - | 0 | - | 0 | 0 | 0 |
| ASTP | 75 | 75 | 13 | - | - | - | - | - | - | 0 |
| CAMS | 59 | 131 | 655 | - | - | - | - | - | - | 0 |
| CELL | 40 | 131 | 31173 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| CLMT | 40 | 131 | 6 | - | - | 0 | - | - | 0 | 0 |
| COBJ | 76 | 131 | 247 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| CONT | 121 | 131 | 24 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| CPTH | 42 | 131 | 295 | - | - | - | - | - | - | 0 |
| CSTY | 95 | 131 | 106 | 0 | - | 0 | - | - | 0 | 0 |
| DFOB | 45 | 131 | 328 | 0 | - | 0 | 0 | 0 | 0 | 0 |
| DIAL | 42 | 131 | 34320 | 0 | 0 | 0 | - | 0 | 0 | 0 |
| DLBR | 42 | 131 | 127 | 0 | 0 | 0 | - | 0 | 0 | 0 |
| DLVW | 54 | 54 | 1 | - | - | - | - | - | - | 0 |
| DMGT | 63 | 129 | 8 | - | - | - | - | - | - | 0 |
| DOOR | 115 | 131 | 4 | 0 | - | 0 | 0 | 0 | 0 | 0 |
| ECZN | 42 | 131 | 195 | 0 | 0 | 0 | - | 0 | 0 | 0 |
| EFSH | 63 | 131 | 39 | 0 | - | 0 | - | - | - | 0 |
| ENCH | 54 | 131 | 59 | 0 | - | 0 | - | - | 0 | 0 |
| EQUP | 19 | 131 | 38 | 0 | - | - | - | - | - | 0 |
| EXPL | 63 | 131 | 116 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| FACT | 40 | 131 | 446 | 0 | 0 | 0 | - | 0 | 0 | 0 |
| FLST | 30 | 131 | 312 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| FSTP | 18 | 131 | 131 | 0 | - | 0 | - | - | 0 | 0 |
| FSTS | 55 | 131 | 29 | 0 | - | 0 | - | - | 0 | 0 |
| FURN | 74 | 131 | 188 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| GDRY | 122 | 131 | 61 | - | - | 0 | - | - | 0 | 0 |
| GLOB | 18 | 131 | 1245 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| GMST | 15 | 131 | 759 | 0 | - | - | - | - | 0 | 0 |
| HAZD | 94 | 131 | 4 | 0 | - | 0 | - | - | 0 | 0 |
| IDLE | 43 | 131 | 2731 | 0 | 0 | 0 | - | 0 | 0 | 0 |
| IDLM | 74 | 131 | 36 | 0 | 0 | 0 | - | 0 | 0 | 0 |
| IMAD | 109 | 131 | 219 | 0 | - | 0 | 0 | 0 | 0 | 0 |
| IMGS | 15 | 131 | 283 | - | - | 0 | - | 0 | 0 | 0 |
| INFO | 45 | 131 | 7564 | 0 | 0 | 0 | - | 0 | 0 | 0 |
| IPCT | 40 | 131 | 489 | 0 | - | 0 | 0 | - | 0 | 0 |
| IPDS | 45 | 131 | 239 | 0 | - | 0 | 0 | - | 0 | 0 |
| KSSM | 63 | 131 | 75 | 0 | - | 0 | 0 | - | 0 | 0 |
| KYWD | 15 | 131 | 2750 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| LAND | 41 | 131 | 36171 | 0 | - | 0 | - | 0 | 0 | 0 |
| LAYR | 45 | 131 | 3795 | 0 | - | 0 | - | 0 | 0 | 0 |
| LCRT | 40 | 131 | 697 | 0 | - | 0 | - | 0 | 0 | 0 |
| LCTN | 106 | 131 | 2 | 0 | - | 0 | - | 0 | 0 | 0 |
| LENS | 119 | 131 | 30 | 0 | - | 0 | - | - | 0 | 0 |
| LGTM | 34 | 131 | 65 | 0 | - | 0 | - | - | 0 | 0 |
| LIGH | 63 | 131 | 752 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| LTEX | 42 | 131 | 101 | - | - | 0 | - | - | 0 | 0 |
| LVLI | 59 | 131 | 1729 | 0 | - | 0 | 0 | 0 | 0 | 0 |
| LVLN | 74 | 131 | 158 | 0 | 0 | 0 | - | 0 | 0 | 0 |
| MATO | 18 | 39 | 26 | - | - | - | - | - | - | 0 |
| MATT | 45 | 131 | 137 | - | - | 0 | 0 | - | 0 | 0 |
| MISC | 86 | 131 | 110 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| MOVT | 24 | 131 | 77 | 0 | - | 0 | - | - | 0 | 0 |
| MSTT | 99 | 131 | 197 | 0 | - | 0 | - | 0 | 0 | 0 |
| MSWP | 54 | 131 | 2250 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| MUSC | 116 | 131 | 57 | 0 | - | 0 | - | - | 0 | 0 |
| MUST | 26 | 131 | 501 | 0 | - | 0 | - | - | 0 | 0 |
| NPC_ | 127 | 131 | 992 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| OTFT | 63 | 131 | 369 | 0 | - | 0 | - | 0 | 0 | 0 |
| OVIS | 128 | 128 | 1 | - | - | - | - | - | - | 0 |
| PACK | 40 | 131 | 3175 | 0 | 0 | 0 | - | 0 | 0 | 0 |
| PGRE | 98 | 131 | 260 | - | - | 0 | - | - | 0 | 0 |
| PHZD | 55 | 131 | 790 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| PKIN | 42 | 131 | 864 | 0 | - | 0 | - | 0 | 0 | 0 |
| PMIS | 109 | 131 | 1 | - | - | 0 | - | - | - | 0 |
| PROJ | 99 | 131 | 63 | 0 | - | 0 | 0 | - | 0 | 0 |
| QUST | 45 | 131 | 472 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| REFR | 40 | 131 | 760930 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| REGN | 40 | 131 | 76 | - | - | 0 | - | - | 0 | 0 |
| RELA | 75 | 131 | 63 | - | - | 0 | - | - | - | 0 |
| REVB | 118 | 130 | 59 | - | - | - | - | - | - | 0 |
| RFCT | 45 | 131 | 41 | 0 | - | 0 | - | - | 0 | 0 |
| RFGP | 53 | 131 | 6029 | 0 | 0 | 0 | - | 0 | 0 | 0 |
| SCCO | 68 | 131 | 173 | - | - | 0 | - | 0 | 0 | 0 |
| SCEN | 57 | 131 | 2691 | 0 | - | 0 | - | 0 | 0 | 0 |
| SCOL | 71 | 131 | 9 | 0 | 0 | 0 | - | 0 | 0 | 0 |
| SCSN | 102 | 131 | 8 | - | - | - | - | - | - | 0 |
| SMBN | 77 | 131 | 57 | 0 | 0 | 0 | - | 0 | 0 | 0 |
| SMEN | 45 | 131 | 15 | - | - | 0 | - | - | - | 0 |
| SMQN | 73 | 131 | 173 | 0 | 0 | 0 | - | 0 | 0 | 0 |
| SNCT | 120 | 131 | 3 | - | - | - | - | - | - | 0 |
| SNDR | 35 | 131 | 3164 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| SOPM | 63 | 131 | 196 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| SOUN | 57 | 131 | 217 | 0 | 0 | 0 | - | - | 0 | 0 |
| SPGD | 124 | 131 | 3 | - | - | - | - | - | 0 | 0 |
| STAG | 109 | 131 | 15 | 0 | - | 0 | - | - | 0 | 0 |
| STAT | 25 | 131 | 1108 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| TACT | 118 | 131 | 3 | 0 | - | 0 | - | - | 0 | 0 |
| TREE | 126 | 126 | 1 | - | - | - | - | - | - | 0 |
| TRNS | 53 | 131 | 361 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| TXST | 60 | 131 | 351 | 0 | - | 0 | 0 | 0 | 0 | 0 |
| VTYP | 16 | 131 | 531 | 0 | - | 0 | - | 0 | 0 | 0 |
| WATR | 122 | 131 | 2 | - | - | 0 | - | - | 0 | 0 |
| WTHR | 40 | 131 | 54 | - | - | 0 | 0 | - | 0 | 0 |
| ZOOM | 109 | 131 | 13 | 0 | - | 0 | - | - | - | 0 |

Version gates against the census (official masters):

| Gate (source) | Signature | Min seen | Below gate, Fallout4.esm | Below gate, other official masters |
|---|---|---:|---:|---:|
| 30 (`src/environment/world.cpp:k_ambient_specular_form_version`) | CELL, LGTM | 40 / 34 | 0 | 0 |
| 31 (`include/fo4/forms/weather.hpp:weather_color_groups`) | WTHR | 40 | 0 | 0 |
| 35 (`src/forms/weather.cpp:k_weather_legacy_cloud_form_version`) | WTHR | 40 | 0 | 0 |
| 37 (`include/fo4/forms/weather.hpp:weather_color_groups`) | WTHR | 40 | 0 | 0 |
| 111 (`include/fo4/forms/weather.hpp:k_weather_eight_times_form_version`) | WTHR | 40 | 4 | 0 |
| 119 (`include/fo4/forms/weather.hpp:weather_data_size`) | WTHR | 40 | 6 | 0 |
| 120 (`include/fo4/forms/weather.hpp:weather_fnam_size`) | WTHR | 40 | 8 | 0 |
| 130 (`include/fo4/forms/weather.hpp:weather_unam_size`) | WTHR | 40 | 50 | 0 |
| 82 (`src/forms/quest_objectives.cpp:k_qsta_keyword_form_version`) | QUST | 45 | 28 | 0 |
| 109 (`src/graph/reachability.cpp:k_race_weights_form_version`) | RACE | 131 | 0 | 0 |
| 128 (`src/environment/navmesh.cpp`, `src/plugin/navmesh_writer.cpp`, `src/schema/form_link_mixed.cpp`) | NAVM | 131 | 0 | 0 |
| 131 (`src/forms/race.cpp:k_current_form_version`) | RACE | 131 | 0 | 0 |
| 152 (`src/schema/form_link_mixed.cpp:k_damage_curve_form_version`, 12-byte DAMA layout) | DAMA | absent | 0 | 0 |

Record types below 131 that have no gate in this list: VTYP (16 to 128), DMGT (63 to 129), GDRY (122 to 128), MATO (18 to 39), REVB (118 to 130), SPGD (124 to 128), TREE (126), OVIS (128). The census records them only.

### Releases: OG 1.10.163.0, NG 1.10.984.0, AE 1.11.240.0

Read-only measurement of the three Data folders on 2026-10-12. The SHA-256 columns show the first 16 hex digits. Each release covers its official masters and the cc ESLs listed in `Fallout4.ccc`.

| Release | Fallout4.exe | Fallout4.esm | DLCRobot.esm | DLCworkshop01.esm | TES4 form version | TES4 flags | HEDR versions |
|---|---|---|---|---|---|---|---|
| OG 1.10.163.0 | 1.10.163.0 | 7b4c60ad47bf60e8 | 116ecb0e1139273d | 6d8b555676a8afcb | 131 | 0x81 / 0x281 | 1.0, 0.95 |
| NG 1.10.984.0 | 1.10.984.0 | b7dc7c22328258f1 | ecb5cb1ed0bf5252 | 04870da9fd017f90 | 131 | 0x81 / 0x281 | 1.0, 0.95 |
| AE 1.11.240.0 | 1.11.240.0 | a3fa1e66d75f80f3 | 465b904603caf18c | 9e645faa8620b5e2 | 131 | 0x81 / 0x281 | 1.0, 0.95 |

DLCCoast.esm, DLCworkshop02.esm, DLCworkshop03.esm and DLCNukaWorld.esm have the same SHA-256 prefix on all three releases. Of the cc ESLs that OG has, OG and AE match except `ccBGSFO4046-TesCan.esl`, and NG differs from both for all six. `ccFSVFO4007-Halloween.esl`, `ccOTMFO4001-Remnants.esl` and `ccSBJFO4003-Grenade.esl` are listed in `Fallout4.ccc` only on NG and AE; the OG install has the files but its `Fallout4.ccc` omits them, so they are out of the OG scope. They also differ by SHA between NG and AE. The only gate that throws is RACE exact-131, and every RACE record is 131 on OG, NG and AE (112, 115 and 115 records), so no release reaches a throwing gate. The per-signature minimum and maximum form versions are identical on OG, NG and AE for all 138 signatures. The non-throwing branch counts in the gate table were measured on AE only.

## Group header (24 bytes)

`strong`. Size includes the header. Children occupy `group_size - 24`. Nested groups are legal (WRLD/CELL).

| Offset | Size | Field |
|-------:|-----:|-------|
| 0x00 | 4 | `GRUP` |
| 0x04 | 4 | group size including header |
| 0x08 | 4 | label (top-group signature, FormID, or grid) |
| 0x0C | 4 | group type (int32) |
| 0x10 | 2 | timestamp |
| 0x12 | 2 | VC |
| 0x14 | 2 | form version |
| 0x16 | 2 | unknown |

Group type names (`top=0` ... `cell_visible_distant_children=10`) are `probable` from xEdit/UESP. The integer is stored; names are only for display.

## Subrecords

`confirmed` by walking real plugins.

```text
char[4] signature
uint16  size
bytes   data
```

`XXXX`: the next subrecord's true size is a `uint32` stored in the `XXXX` payload. The following subrecord still has a `uint16` size field (often 0). `confirmed` by implementation tests; engine `TESFile::GetChunkData` / `actualChunkSize` exists (OG).

## TES4 / HEDR

CommonLibF4 `FILE_HEADER` is 12 bytes: `float version`, `uint32 formCount`, `uint32 nextFormID`. `confirmed`.

Observed HEDR version, measured on AE 1.11.240.0 only (no OG 1.10.163 or NG 1.10.98x install has been measured):

- **0.95**: `DLCworkshop02.esm`, `DLCworkshop03.esm`, `DLCCoast.esm`, `DLCNukaWorld.esm`.
- **1.0**: `Fallout4.esm`, `DLCRobot.esm`, `DLCworkshop01.esm`, and every installed cc Creation ESL.

Both 0.95 and 1.0 occur in the official masters, so 1.0 is not the only FO4 value. The 0.95 rule for hardcoded ranges comes from xEdit, not from the engine (see `ESL_FORMIDS.md`).

AE 1.11.240.0 observations: TES4 form version **131** in the seven official masters and nine cc ESLs; TES4 flags **0x81** on the official ESMs and **0x281** on the cc ESLs. User plugins outside that set, such as `Synthesis.esm` (flags 0x01) and `WorkbenchFixerPatch.esp` (flags 0x00), are not measured.

HEDR `formCount` counts major records excluding TES4, plus every GRUP including nested groups.

- Heather: 4 records + 3 groups = 7. `confirmed`
- `DLCworkshop01.esm`: 1463 records + 65 groups = 1528. `confirmed`

`MAST` is a zstring plugin name. `DATA` is 8 bytes (file size, often zero in small patches). Masters come in MAST+DATA pairs.

`CNAM` (author) and `SNAM` (description) are read as zstrings and set by `set_plugin_author` and `set_plugin_description`. Other TES4 subrecords preserved generically: `ONAM`, `INTV`, `INCC`, plus anything unknown.

## TES4 flags (file header)

CommonLibF4 `TESFile::RecordFlag` (`strong` oracle; matches a real ESL of `0x281`):

| Bit | Value | Name |
|----:|------:|------|
| 0 | 0x1 | Master |
| 7 | 0x80 | Delocalized / localized |
| 9 | 0x200 | SmallFile (light) |

The official ESL dump was `0x281 = Master | 0x80 | SmallFile`. Whether bit 7 is "localized" vs "delocalized" is still named both ways in sources. Treat the bit as present; do not rename it in the writer.

Record compression bit `1 << 18` (`0x40000`) is `strong` community/Mutagen/xEdit. `TESFile` has `decompressedFormBuffer`. Not yet verified against a named CommonLib enum.

Deleted record bit `1 << 5` is `strong` (`TESForm::IsDeleted`).

## Compressed records

Payload starts with `uint32 uncompressed_size`, then a zlib-wrapped stream (window bits 15). Raw-deflate or gzip framing, a failed inflate, a declared size past the caps, or inflated bytes that do not parse as subrecords make a non-TES4 record load as undecodable with its raw bytes kept and written back unchanged (GT-205, `src/plugin/reader.cpp:read_record`); a TES4 record still fails the plugin. The zero-size early return is unchanged.

OG/AE decompressor and init-wrapper records establish the prefix and zlib framing statically; synthetic undecodable-record and round-trip tests cover Radicle's policy (`tests/test_undecodable_record.cpp`, `tests/test_compression.cpp`). See `COMPRESSION_EVIDENCE.md` for per-build verification, remaining runtime questions and corpus scope. Untouched compressed records preserve their source bytes; rebuilt records emit zlib.

## Record type enum

CK `formEnumString[]` (CK 1.11.137.0, `.data` RVA `0x3b9a540`) and CommonLibF4 `ENUM_FORM_ID` agree: 159 entries, index 0 `NONE`, 1 `TES4`, 2 `GRUP`, 17 `RACE`, 63 `CELL`, 74 `WRLD`. `confirmed`.

CKRE `record-definition-table.txt` **class names are not trustworthy**. It maps `GMST` to `BGSKeyword`. Confirmed factory registrations and CommonLib comments disagree with that join. Use the 4CC list, not the dumped class column.

CKRE also claims `RACE` is absent from the file-load table. RACE records exist in `Fallout4.esm`. Treat "RACE is not file-loadable" as **disputed**. The generic reader still reads `RACE`.

## CELL / LAND (runtime)

OG 1.10.163, `ReverseEngineering/Organized/1.10.163/README-Landscape.md`, `confirmed`:

- Cell size 4096 (`cellX << 12`)
- VHGT is `0x448` bytes: float Offset + 33x33 signed deltas + 3 pad
- Height units = accumulated delta * 8
- REFR positions are world-absolute; do not add cell origin

Typed LAND decode is deferred.

## CTDA (conditions)

xEdit FO4 `wbConditions` and FO4IDE `ReadSingleCtdaParameterOne` agree the payload is 32 bytes:

| Offset | Field |
|-------:|-------|
| 0 | type (uint8) |
| 1-3 | unused |
| 4 | comparison value (float, or GLOB FormID if flagged) |
| 8 | function (uint16) |
| 10 | unused |
| 12 | parameter 1 (FormID or int, function-dependent) |
| 16 | parameter 2 |
| 20 | run-on (uint32); 2 = Reference |
| 24 | reference FormID when run-on is Reference |
| 28 | parameter 3 |

`GetBaseValue` is function 277; parameter 1 is an ActorValue FormID (`confirmed` via xEdit `ptActorValue` + FO4IDE regression expecting `0x02000812` at offset 12).

Comparison-value-as-global is not remapped yet.

## FormID-bearing subrecords (first schema slice)

Whole-subrecord FormIDs: `NAME`, `XLCN`, `XOWN`, `XEZN`, `PTRN`.
Arrays: `KWDA` (4-byte FormIDs).
Function-gated: `CTDA` as above.

Unknown subrecords are left untouched on master remap.

## VMAD (Papyrus)

FO4 default: version 5 or 6, object format 2. Strings are uint16 length + UTF-8.

Object v2: unused uint16, alias int16, FormID.
Object v1: FormID, alias int16, unused uint16.

Property types 1 / 11 / 7 / 17 contain objects. Type 3 int is not a FormID even if the bits look like one.

QUST extras after scripts: bind byte, fragment count, optional fragment script, fragments, then aliases (object + nested version/format/scripts). Each fragment stores `Stage` (uint16) and `StageIndex` (int32). Object format 2 stores an alias ID (int16) before the FormID, so those extras bind Papyrus to stage identity and alias IDs.

Truncated VMAD is skipped for remap rather than aborting the plugin write.

## TESFile (runtime plugin IO)

OG 1.10.163 named API, `confirmed` as existing (bodies not all decompiled in this pass):

`OpenTES`, `CloseTES`, `CreateTES`, `LoadTESInfo`, `NextForm`, `NextChunk`, `GetChunkData`, `GetChunkSize`, `StartGroup` / `EndGroup`, `GetMasterList`, `AddMaster`, `RemoveMaster`, `GenIndexTable`, `AdjustFormIDFileIndex`, `GetCompileIndex`, `SetSmallCompileIndex`, `GetSmallFile` / `SetSmallFile`, `CheckValidIDForSmallFile`.

`AdjustFormIDFileIndex` is the engine function that maps plugin-relative FormIDs onto runtime compile indexes. The replacement library must do the equivalent using `FormKey`, not by editing high bytes ad hoc.

## Exterior CELL block / sub-block GRUP labels

Measured, not inferred. Every exterior CELL in `Fallout4.esm` (38,965) and
`DLCCoast.esm` (17,917) was walked, its enclosing block (GRUP type 4) and
sub-block (type 5) labels recorded, and compared against its own XCLC grid
coordinates.

```
block    label = (int16 floor(y / 32), int16 floor(x / 32))   bytes 0..1 = y, 2..3 = x
subblock label = (int16 floor(y /  8), int16 floor(x /  8))   bytes 0..1 = y, 2..3 = x
```

- **Y is stored first.** `(floor(y/d), floor(x/d))` matched 38,965/38,965 on
  Fallout4.esm; the transposed form matched only 6,932 (block) and 1,850
  (sub-block) — exactly the diagonal cases where x and y bucket the same.
- **Division is mathematical floor, not C truncation toward zero.** 29,343 of
  the Fallout4.esm cells have a negative coordinate; floor matched all of them,
  truncation matched 585 (block) and 2,568 (sub-block) — only the cases that
  divide evenly, where the two agree.
- DLCCoast.esm reproduces both formulas 17,917/17,917 as an independent
  worldspace.

This is the calculation #71 flags as "do not guess". `exterior_cell_grid_layout`
and `exterior_grid_group_label_u32` in `fo4/forms/cell.hpp` implement it, and
`tests/test_exterior_cell_grid_real.cpp` re-checks it against the shipped
masters when `FO4_DATA_DIR` is set (it skips unless `FO4_DATA_DIR` is set: CI
takes it from the variable `FO4_DATA_DIR_LINUX` on Linux and from the `fo4-data` runner's environment on Windows).

## Localized string subrecords in shipped masters

Every Bethesda master sets TES4 flag `0x80`: `Fallout4.esm` header flags are
`0x0081`. In a localized plugin a string subrecord does not hold inline text —
it holds a **four-byte string ID** into the `.STRINGS`/`.DLSTRINGS`/`.ILSTRINGS`
tables. All 45 `RACE` records in `Fallout4.esm` carry a `DESC` of exactly 4
bytes.

This is handled by `fo4/schema/localized_fields.hpp`. A string field decodes to
a `LocalizedString` carrying **either** a four-byte string ID (localized plugin,
lstring field) **or** inline text, whichever the file used; a localized payload
that is not four bytes has neither, and its raw bytes are kept. Decoding performs no
I/O and picks no language, so a write-back reproduces the original bytes;
resolve an ID through `fo4::localization` when display text is wanted.

`RACE` is wired. The remaining typed families still read strings as inline text
and need the same treatment. Related: #29 (bit 7 localized vs delocalized
semantics).

## RACE BodyData INDX

`INDX` inside a RACE BodyData block is a `uint32` body-part index, not an empty
separator, and it is **not** derivable from list position: of the 90 gender
blocks in `Fallout4.esm`, 17 are not a plain `0..n-1` run — they begin
`0, 0, 1, 2, …`. The value is decoded and written back verbatim rather than
recomputed.

### Which fields are localized

Measured on `Fallout4.esm` (localized, TES4 `0x0081`), payload size per
(record, subrecord) pair:

| Field | Always 4 bytes | Never 4 bytes |
|---|---|---|
| `FULL` | ARMO 974, WEAP 864, MISC 2043, NPC_ 1944, QUST 708, CELL 319, RACE 40 | — |
| `DESC` | ARMO 688, WEAP 252, RACE 45 | — |
| `EDID` | — | ARMO 688, WEAP 252, RACE 45, ARMA 739, CELL 1736 |
| `DNAM` | — | ARMA 739, WEAP 252, NPC_ 3015, QUST 1336 |

`FULL` and `DESC` are 4 bytes in every record that carries them, with zero
inline instances — those are the safe lstring set to start from. `EDID` is
never localized.

**Payload size alone cannot decide this.** `NPC_ CNAM` is 4 bytes in all 3,015
instances but it is a Class FormID, not a string ID; `QUST CNAM` in a stage is
a log-entry lstring. The same signature means different things in different
records, so lstring-ness has to be declared per (record, subrecord) pair in
`schema/fallout4/fields.json` rather than inferred from the bytes. Anything
that guesses from size will silently turn FormIDs into string IDs.

### ARMO FULL is not a unique field

`DESC` is exactly one per record across all 688 shipped ARMO. `FULL` is not:
counts per record run 0, 1, 2, 3, 4, 6, 7, 8, 9, 14, 16 and 20, because `OBTE`
object-template blocks carry their own `FULL` name overrides. In 11 records a
`FULL` appears only after `OBTE`.

A unique-subrecord lookup for ARMO `FULL` would therefore throw on 71 of 688
shipped records, and a first-match lookup would silently return a template
override instead of the armour's name. `FULL` stays unmodelled on ARMO until
the `OBTE` template block is parsed; `DESC` is modelled now.

Measured top-level order is `EDID OBND PTRN FULL … KSIZ KWDA DESC INDX …`.

## QUST stage / log-entry grammar

Measured on `Fallout4.esm`: 1,336 QUST records, 8,680 stages, 9,058 log
entries. The stage array runs from the first `INDX` up to the first `QOBJ`
(objectives) or `ANAM` (alias array):

```
INDX  4 bytes: u16 stage index, u8 flags, u8 unknown
  QSDT  1 byte flags          -- one log entry
    CNAM  log text, LOCALIZED -- 4 bytes in all 921 shipped instances
    NAM2  note text, inline   -- NUL-terminated, 99 distinct lengths
    CTDA / CIS2               -- per-entry conditions
```

Payload sizes are fixed: `INDX` is 4 bytes in all 8,680 stages, `QSDT` is 1
byte in all 9,058 entries. Observed `QSDT` flag values are `0` (8,649), `1`
(267, completes the quest) and `2` (142, fails it).

Three things worth pinning:

- **`NAM0` does not occur.** Zero of 8,680 stages carry it, despite it being a
  documented Skyrim stage field. It is not modelled.
- **Log entries per stage are not always one:** 0 (63 stages), 1 (8,378), and
  2–9 (239). Both the empty and the multi-entry shapes must be representable.
- **The fourth `INDX` byte is not always zero** (observed 0–10+), so it is
  preserved rather than rebuilt.

`INDX` is also used by the alias array, which is why the walk has to stop at
`QOBJ`/`ANAM` rather than scanning the whole record.

## Exterior CELL DATA and XCLC

Measured across `Fallout4.esm`, `DLCCoast.esm` and `DLCNukaWorld.esm`.

**`DATA` is 2 bytes on every CELL.** For the 61,543 exterior cells (those
carrying `XCLC`), bit 1 is set on **61,542** — the sole exception is one cell
with `0x0000`. Observed values are `0x0002` (61,252), `0x0042` (237, plus Hand
Changed) and `0x000A` (53, plus No LOD Water). All 1,376 interior cells set
bit 0.

An all-clear `DATA` therefore describes a record shape that occurs once in the
entire shipped corpus, so `create_exterior_cell` defaults to `0x0002` and
refuses the interior bit outright.

**`XCLC` is exterior-only and 12 bytes** in all 38,970 `Fallout4.esm` instances
that carry it; the 1,195 CELLs without one are interiors, which use `XCLL`
instead (CKRE doc 23: `FUN_140d18490` gates access on the interior flag in both
LoadRecord and SaveRecord).

The 4 bytes after x/y are **one meaningful byte plus three bytes of
uninitialised padding**, not a `u32`. Measured over 61,543 exterior cells in
`Fallout4.esm` + DLCCoast + DLCNukaWorld:

| Offset 8 (Force-Hide-Land quads, bits 0–3) | count |
|---|---|
| `0x00` | 61,485 (99.91%) |
| `0x0F` — all four quads hidden | 50 |
| `0xFF`, `0x44`, `0x22`, `0x88` | 1 each |

Bytes 9–11 are junk the CK does not clear: only 96 distinct values across all
61,543 cells, clustering in large batches (29,389 share `71a376`), which is the
signature of reused allocation rather than data. Reading the field as a `u32`
yields 103 distinct "flag" values and would surface padding as spurious
high flag bits — xEdit's `u32 wbFlags Quad 1..4` is over-wide for this reason.

This is consistent with the CK's own LoadRecord (`FUN_140d146d0`, XCLC branch),
which takes **byte 8 into `cell+0x1E` and skips three padding bytes**.

`create_exterior_cell` writes all four bytes zero: the quad flags clear, and no
fabricated padding.

## QUST objective / target grammar

Measured on `Fallout4.esm`: 1,198 objectives, 1,500 targets. The array runs
from the first `QOBJ` up to `ANAM`:

```
QOBJ  u16 objective index            (2 bytes, all 1,198)
  FNAM  u32 objective flags          (4 bytes, all 1,198)
  NNAM  display text, LOCALIZED      (4 bytes, all 1,198)
  QSTA  12 bytes, all 1,500          -- one target
          i32 alias id     @0
          u32 target flags @4        (0 in 1,479; also 1, 2, 4)
          u32 keyword FormID @8      (zero in 1,498 of 1,500)
    CTDA / CIS1 / CIS2  per-target conditions
```

Targets per objective: 0 (66 objectives), 1 (950), and 2–19 (182). Both the
empty and multi-target shapes must be representable.

**`QSTA`'s keyword is a FormID inside a mixed-type payload** and needs a
structured FormID slot, otherwise a master remap leaves it pointing at whatever
record now occupies the old index. Only 2 of 1,500 shipped targets carry one,
so the corruption is easy to miss —
`QuestObjectives.TargetKeywordIsRemappedWithTheMasterTable` pins it; with the
slot removed, a `Beta.esm` keyword silently becomes an `Alpha.esm` one.

`QOBJ` and `NNAM` are also used by the alias array, so the walk stops at `ANAM`.

## PROJ / EXPL / HAZD

Measured across `Fallout4.esm` + DLCCoast/DLCNukaWorld/DLCRobot: 218 PROJ, 317
EXPL, 78 HAZD. Every subrecord in all three families has max cardinality 1.

```
PROJ  EDID OBND [FULL] [MODL MODT] [DEST DSTD [DMDL DMDT] DSTF] DATA DNAM NAM1 [NAM2] VNAM
EXPL  EDID OBND [FULL] [MODL MODT] [EITM] [MNAM] DATA
HAZD  EDID OBND [FULL] MODL MODT [MNAM] DNAM
```

Required in every shipped record: PROJ `EDID OBND DATA DNAM NAM1 VNAM`;
EXPL `EDID OBND DATA`; HAZD `EDID OBND MODL MODT DNAM` — note HAZD requires the
model where the other two do not. `FULL` is 4 bytes in every instance of all
three, so it is declared localized.

**PROJ `DATA` is a zero-length marker** in all 218 records, not a payload.
**EXPL `DATA` is not fixed-width**: 84 bytes (299), 56 (11) and 64 (3), so a
decoder must accept the shorter legacy shapes.

### Measured payload layouts

The disk layouts below are measured from 239 PROJ, 342 EXPL and 78 HAZD
records across `Fallout4.esm` and all six DLC masters, then re-checked on 1,187
PROJ / 867 EXPL / 55 HAZD records in the third-party corpus. CommonLibF4's
`BGSProjectileData` / `BGSExplosionData` / `BGSHazardData` supply the field
*names*; they do not supply the offsets, and in two places they do not even
supply the order.

Every field was validated three ways: FormID slots must resolve to a coherent
record type, floats must be finite and in a sane range, and enum/count fields
must have a small closed value set. Sizes are exact — the field lists below sum
to 93, 84 and 52 bytes with nothing left over.

#### `PROJ DNAM` — 93 bytes, fixed

| Offset | Field | Type |
|---|---|---|
| +0 | flags | u16 (xEdit `wbFlags`, bits `0x0001`-`0x2000`; xEdit Core/wbDefinitionsFO4.pas:7123-7138 @93cc0bc5) |
| +2 | type | u16 (xEdit `wbEnum`: 1 Missile, 2 Lobber, 4 Beam, 8 Flame, 0x10 Cone, 0x20 Barrier, 0x40 Arrow; default 1; xEdit Core/wbDefinitionsFO4.pas:7139-7147 @93cc0bc5) |
| +4 | gravity | float (−0.01 … 10) |
| +8 | speed | float (0 … 16000) |
| +12 | range | float (200 … 25000) |
| +16 | light | FormID → LIGH |
| +20 | muzzleFlashLight | FormID → LIGH |
| +24 | explosionProximity | float (0 … 200) |
| +28 | explosionTimer | float (0 … 5) |
| +32 | explosionType | FormID → EXPL |
| +36 | activeSoundLoop | FormID → SNDR |
| +40 | muzzleFlashDuration | float (0 … 2) |
| +44 | fadeOutTime | float (0 … 5) |
| +48 | force | float (0 … 1000) |
| +52 | countdownSound | FormID → SNDR |
| +56 | deactivateSound | FormID → SNDR |
| +60 | defaultWeaponSource | FormID → WEAP |
| +64 | coneSpread | float (0 … 5) |
| +68 | collisionRadius | float (0.01 … 50) |
| +72 | lifetime | float (0 … 100) |
| +76 | relaunchInterval | float (0.001 … 1) |
| +80 | decalData | FormID → TXST |
| +84 | collisionLayer | FormID → COLL |
| +88 | tracerFrequency | u8 (0, 1 or 4) |
| +89 | vatsProjectile | FormID → PROJ, **unaligned** |

**The tail is the reverse of the in-memory struct.** `BGSProjectileData` holds
`vatsProjectile` at `0x80` and `tracerFrequency` at `0x88`; on disk the byte
comes first and the FormID is the final four bytes, straddling the natural
alignment. A previous note in this file claimed `+88` was
`[tracerFrequency u8][3 bytes heap padding]` — that is wrong, and it left a
live FormID unremapped in 25 shipped and 453 corpus records.

#### `EXPL DATA` — 84 bytes, with truncated legacy shapes

| Offset | Field | Type |
|---|---|---|
| +0 | light | FormID → LIGH |
| +4 | sound1 | FormID → SNDR |
| +8 | sound2 | FormID → SNDR |
| +12 | impactDataSet | FormID → IPDS |
| +16 | impactPlacedObject | FormID → any placeable base |
| +20 | spawnProjectile | FormID → PROJ |
| +24 | force | float |
| +28 | damage | float |
| +32 | innerRadius | float |
| +36 | outerRadius | float |
| +40 | imageSpaceRadius | float |
| +44 | verticalOffsetMult | float (0 … 64, usually 0.3) |
| +48 | flags | u32 |
| +52 | soundLevel † | u32 (0 … 4) |
| +56 | placedObjectFadeDelay | float (0 … 500) |
| +60 | staggerMagnitude † | u32 (0 … 4) |
| +64 | spawnProjectileVector X | float |
| +68 | spawnProjectileVector Y | float |
| +72 | spawnProjectileVector Z | float |
| +76 | projectileSpread | float (0 … 180, degrees) |
| +80 | projectileCount | u32 (0 … 15) |

† **`soundLevel` and `staggerMagnitude` needed a separate discriminator.**
Both are `u32` with the identical observed domain 0…4, so field ranges alone
cannot tell them apart. Sound presence can: at `+52`, value 0 has a sound
descriptor in 26 of 26 records while value 2 has none in 27 of 38 — the
Loud/Silent poles of the sound-level enum. `+60` shows no relationship to
sound at all across its values and instead tracks damage, as a stagger field
should. The order matches `BGSExplosionData`, but it is now carried by the
data rather than by the struct. This is strong evidence, not proof; a check
against xEdit's `wbEXPL` or the CK would settle it outright.

**The field order is not `BGSExplosionData`'s.** In memory the projectile
vector, spread and count sit in the middle, before `force`; on disk they are
the tail. Reading the memory order off disk lands `projectileCount` on a float
and `flags` on `1.0f` — which is exactly how this was caught.

The truncated shapes are a clean prefix, one field group at a time: 52 bytes
ends after `flags`, 56 after `soundLevel`, 60 after `placedObjectFadeDelay`,
64 after `staggerMagnitude`, 84 is current. Observed counts across the masters:
52×3, 56×11, 60×1, 64×3, 84×324.

#### `HAZD DNAM` — 52 bytes, fixed

| Offset | Field | Type |
|---|---|---|
| +0 | limit | u32 (0 … 20) |
| +4 | radius | float (0 … 351) |
| +8 | lifetime | float (0 … 30) |
| +12 | imageSpaceRadius | float (0 in every shipped record) |
| +16 | targetInterval | float (0.1 … 10) |
| +20 | flags | u32 — `BGSHazardFlags` |
| +24 | spell | FormID → SPEL or ENCH |
| +28 | light | FormID → LIGH |
| +32 | impactDataSet | FormID → IPDS |
| +36 | sound | FormID → SNDR |
| +40 | fullEffectRadius | float (0 … 187) |
| +44 | taperWeight | float (0 … 1) |
| +48 | taperCurve | float (0 … 1) |

HAZD is the one family whose disk layout matches the memory struct field for
field.

### FormID slots — all twenty, wired

Twenty slots across the three families, gated on each payload's own extent in
`struct_payload_formid_offsets`. Every one resolves to its stated type in all
659 shipped records and all 2,109 corpus records, with zero contradictions
(`StructPayloadFormLinksRealData`).

| Record | Subrecord | Offsets |
|---|---|---|
| PROJ | `DNAM` | +16 +20 +32 +36 +52 +56 +60 +80 +84 +89 |
| EXPL | `DATA` | +0 +4 +8 +12 +16 +20 |
| HAZD | `DNAM` | +24 +28 +32 +36 |

Nine of those twenty (`PROJ` +52 +56 +60 +80 +89, `EXPL` +20, `HAZD` +28 +32
+36) were unwired until the layouts above were measured — silent master-remap
corruption in the same class as the QSTA keyword bug.

Several slots are sparse enough that a single-file test proves nothing:
`HAZD DNAM +36` is non-zero in exactly one shipped record and it lives in
`DLCRobot.esm`; `PROJ DNAM +80` has one instance in `Fallout4.esm` and seven in
`DLCworkshop02.esm`. The test walks every master and DLC and fails a slot that
was never exercised.

`PROJ DNAM +84` is a real slot. It failed an earlier DLC master-index test only
because every vanilla `COLL` lives in `Fallout4.esm`, so no DLC reference to
one ever carries index `0x01` — absence of that signal is not evidence of
absence. The same reasoning retired the earlier "carries a DLC master index"
screen entirely: it rejects true slots whose targets are all first-party.

`DATA` and `DNAM` mean different things in different records, so these are
keyed on the record signature in `form_links.cpp` rather than the
signature-only table.

## ARMO FULL and the object-template boundary

`OBTE` starts the template block, which emits `[OBTF] [FULL] OBTS` per
combination — so a single ARMO can carry up to 20 `FULL` subrecords. **The
record's own name is the `FULL` that precedes `OBTE`.**

Verified across `Fallout4.esm` + 3 DLC masters, on every template-bearing
record type: **never more than one `FULL` before the boundary**, zero
exceptions in 5,792 records (ARMO 710 with / 298 without, WEAP 347/118,
NPC_ 1979/2340). After the boundary: 502 template `FULL`s on ARMO alone.

> RE-Work doc 26 states that "ARMO emits `OBTS` with no `FULL`". The shipped
> files disagree — ARMO emits `OBTF FULL OBTS` per combination exactly like
> NPC_ and WEAP. The boundary rule above is measured, not taken from the doc.

## Third-party plugin corpus

`ModCorpusRealData` sweeps every decoder over a directory tree of mod plugins
(`FO4_MOD_CORPUS`). The shipped masters are one authoring tool's output; mod
plugins come from the CK, xEdit and hand-rolled tooling and carry shapes
Bethesda never produces.

Current local result: **1,821 plugins parsed, 1,369 of them light**, 11,026
records decoded across QUST/RACE/ARMO/ARMA, with **one** defective plugin
(`ImprovedInstITO2Patch.esp`, 8 masters but records referencing index 10).

It found one real decoder defect: a third-party quest places `NAM2` before any
`QSDT`, which the stage decoder rejected outright. `QuestStage::leading_members`
now keeps those losslessly. Bethesda emits no such stage in 8,680.

## OBTE / OBTS object-template block

Validated against every shipped body: **3,297 OBTS payloads across ARMO (1,564),
WEAP (813), NPC_ (848) and FURN (72)** in `Fallout4.esm` + DLCCoast/NukaWorld/
Robot. The computed size matches the payload exactly in all 3,297, and
`OBTE`'s u32 equals the number of `OBTS` bodies in all 1,037 records carrying
one.

```text
OBTE  u32 combination count
  [OBTF] [FULL] OBTS   x N
STOP

OBTS body:
  0x00      u32  include count   I
  0x04      u32  property count  P
  0x08      u8   level min      0x09 pad
  0x0A      u8   level max      0x0B pad
  0x0C      s16  addon index
  0x0E      u8   default
  0x0F      u8   keyword count   K      <- u8, not the u32 xEdit declares
  0x10      4*K  keyword FormIDs
  0x10+4K   u8   min level for ranks, u8 alt levels per tier
  0x12+4K   7*I  include rows { u32 OMOD FormID, u8 attach, u8 optional, u8 dontUseAll }
            24*P property rows
```

Total size is `0x12 + 4K + 7I + 24P`.

**Every include FormID resolves to an OMOD record** — 7,342 of 7,342 in
`Fallout4.esm`, zero unresolved.

### These FormIDs need remapping

`OBTS` keyword and include FormIDs carry real master indices — across the three
DLC masters, 1,960 include and 41 keyword FormIDs use index `0x01` into
`Fallout4.esm`. They sit at offsets derived from the header counts, so no fixed
structured slot can express them; `form_links.cpp` walks the body instead and
fails closed when the counts do not describe the payload exactly.

`FURN` also carries templates, which is worth noting because the record-type
list usually given for this block is ARMO/WEAP/NPC_.
