# 48 - NAVM / MUST / SCCO: the last mid-volume types

Closes out everything above the sub-50 tail. Decompiles in
`ghidra-dump/decompile-dtier-navm.txt`.

## 1. LoadRecords

| type | class | LoadRecord | size | literal tags |
|---|---|---|---|---|
| NAVM | NavMesh | **`FUN_140efe470`** | 920 B | `EDID MNAM NVNM NNAM ONAM` + template family |
| MUST | BGSMusicTrackFormWrapper | `FUN_140bb16d0` | 534 B | `CNAM EDID` (track data via hooks: ANAM CTDA CITC FLTV DNAM SNAM) |
| SCCO | (no RTTI class resolved) | - | - | census-only: `EDID QNAM XNAM SNAM VNAM` |

## 2. NAVM structure

* The CK-side loader is small because all geometry lives in one
  **NVNM** blob per mesh (12,512 of 12,520 records carry exactly one;
  sizes 143 B .. 242,836 B).
* `MNAM` (mesh header) and `NVNM` ship as a pair on every navmesh;
  `NNAM`/`ONAM` (extra connections) only on the 95 exterior-doorway
  meshes that link islands.
* Deep vertex/triangle/edge/cover parsing of NVNM is runtime-side and
  already covered by docs 21/24; the CK loader just hands the blob to
  the shared navmesh reader.

## 3. SCCO grammar (census, no loader)

225 records: `EDID`+`QNAM` on all, `XNAM`(1,552 total) component
entries, `SNAM`(776)/`VNAM`(450) optional per-entry attachments.
Internal data record - no vtable found, consistent with RFGP.

With this doc, every record type shipping more than ~50 instances in
the four masters has either a located LoadRecord or (for the two
internal-data types RFGP/SCCO) a documented no-loader finding.
