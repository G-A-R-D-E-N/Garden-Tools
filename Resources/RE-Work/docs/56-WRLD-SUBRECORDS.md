# 56 - WRLD subrecord reference

All 17 worldspaces across the four masters; every tag measured.

## 1. Identity & hierarchy

| tag | size | content |
|---|---|---|
| EDID | var | editor ID (inline NUL text, always present) |
| FULL | 4 | localized name string-ID |
| WNAM | 4 | parent worldspace (child worlds only) |
| PNAM | 2 | unknown u16 (child worlds only, observed 0) |
| XEZN | 4 | encounter zone |
| ZNAM | 4 | music type |
| CNAM | 4 | climate |
| XL CN→XLCN | 4 | location |

## 2. Map extents & land defaults

* `NAM0` (8 B): **2×f32 minimum map X/Y** (e.g. -49152).
* `NAM9` (8 B): **2×f32 maximum map X/Y**.
* `MNAM` (16 B): 4×f32 land defaults (observed zeros on DLC worlds;
  Tamriel-class values elsewhere).
* `DNAM` (8 B): 2×f32 - second value is the **default land height**
  (128.0 / 450.0 observed).
* `ONAM` (16 B) + `NAMA` (4): scale factors, leading 1.0.
* `DATA` (1 B): flags byte (values 0x00/0x01/0x9b observed).

## 3. Icons

* `ICON` (33-37 B): **inline file-path text** - NOT localized despite
  being a string family member.
* `NAM2` / `NAM3` (4 each): u32 24 observed (LOD/map texture dims).
* `NAM4` (4): f32 (450.0 / 0).

## 4. Water

* `WLEV` exists in TWO forms: 4-byte f32 water level, **and 1089 B =
  33×33 grid (u16) water heightmap** for the world map.
* `XWEM` (58 B fixed, 9 records): weather-emitter array.

## 5. World-map reference data

* `RNAM`: repeated variable-size blocks, one per map cell region -
  `[originX u16][originY i16][count u32]` then `count × {formID u32,
  coordX u16, coordY i16}` (12 B/entry). Matches doc 24's grammar;
  re-measured here at block granularity (12,107 blocks total).

## 6. Terrain offsets

`OFST` + `CLSZ`: parallel arrays, byte-equal lengths (doc 24). Largest
seen: 80,064 B each on Far Harbor.

## 7. Writer rules

* Order matters visually but not structurally; vanilla order is:
  EDID → RNAM blocks → MHDT (if any) → identity/localization →
  NAM-family defaults → DATA → NAM0/NAM9 bounds → ZNAM/XWEM/WLEV →
  OFST/CLSZ last.
* Always emit NAM0/NAM9 pairs; readers derive the world map viewport
  from them.
