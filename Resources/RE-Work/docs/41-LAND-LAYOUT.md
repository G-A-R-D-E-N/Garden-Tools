# 41 - LAND record layout

Measured from Fallout4.esm (37,020 LAND records; sampled + full-shape
scan). The only top-10 type without a dedicated doc until now.

## 1. Subrecord shapes

Every LAND carries, in order:

| subrecord | size | content |
|---|---|---|
| DATA | 4 | flags u32 (sample values 0x1d etc.) |
| VNML | 3267 | 33x33 grid x 3 s8 normals (row-major) |
| VHGT | 1096 | f32 height offset at +0, then 1089 s8 vertex deltas |
| VCLR | 3267 | optional; 1089 x RGB u8 vertex colors |
| BTXT/ATXT+VTXT | var | texture layers, see §2 |

Shape frequency (first 4,000 recs): DATA+VNML+VHGT only = 74.4%;
+VCLR = 1.5%; textured variants make up the rest. Geometry trio is
universal - a writer can emit it blind.

## 2. Texture blocks (per-quadrant)

Sequence is grouped: one **BTXT** per used quadrant (up to 4), each
followed by its alpha layers as repeating **[ATXT, VTXT]** pairs.

* `BTXT`/`ATXT` (8 B each): `[formID u32][quadrant u8][layer u8][pad u16]`.
  ATXT layer counts up from 0 within its quadrant; BTXT's layer byte is
  a constant 0xB4 in every vanilla sample (unknown, treat as reserved).
* `VTXT` (N x 8 B, **no header**): flat array sorted by
  `[u16 vertexIndex][u16 unknown][f32 opacity]`.
  * vertexIndex runs 0..288 (17x17 vertices per quadrant).
  * opacity spans [0, 1].
  * the middle u16 shows values like 45/0/16/-245/730 across records -
    suspected unused/uninitialized (same pattern family as doc 26's
    XCLC padding). Writers should emit 0; readers must ignore it.

Sample quadrant block: BTXT(LTEX 000C55FC, quad 0) -> ATXT(layer 0,
LTEX 0001D1D3) + 99-entry VTXT -> ATXT(1, 000BA560) + 103 entries ...

## 3. MPCD variant (34 vanilla records)

A minority shape replaces (or joins) the geometry trio with **MPCD**
blobs - always 3-4 per record, one per quadrant:

* Pure-MPCD records: `[DATA][MPCD x4]`, sizes vary per blob
  (measured range 2412..9196 B across 28 such records).
* Mixed records (6): full DATA/VNML/VHGT(+VCLR) trio AND four MPCD
  blobs appended after the texture blocks.
* Blob head contains an embedded toolchain string
  **"2014.1.0-r15"** plus leading bytes that look like a GUID/hash -
  i.e. precomputed binary data emitted by the CK landscape/physics
  pipeline, not hand-editable terrain. Writers should copy-or-skip,
  never synthesize.

## 4. Writer contract

* Emit DATA/VNML/VHGT always; VCLR only when coloring.
* Textures optional per quadrant; when emitting, group strictly
  BTXT -> (ATXT -> VTXT)+ and keep VTXT vertex indices ascending.
* All sizes above are exact; total record length is predictable from
  layer counts, so the validator can hard-check LAND sizes.
* Shape legality is permissive in practice: pure-geometry (74.4%),
  +VCLR, textured, MPCD-bearing (34), texture-only (59), and bare
  [DATA]-only (31) all occur in vanilla - the validator therefore only
  hard-checks subrecord SIZES when present, never presence.
