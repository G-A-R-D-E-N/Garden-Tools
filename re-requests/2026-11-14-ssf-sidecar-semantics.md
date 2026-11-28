# RE request: engine semantics of `.ssf` sidecars and `BSSubIndexTriShape` segment user data

Date: 2026-11-14
Requested by: Radicle `ssf_shape_from_segments` (branch `nomad/sidecar-generator-coverage`, based on PR https://github.com/G-A-R-D-E-N/GardenTools/pull/1293 at 171fa7f2e)
Related: PR #1293 (removed the contradictory matched == examined assertion), `Radicle/tests/test_nif_sidecar_generate_real.cpp` (`NifSidecarGenerateReal.RegeneratesShippedOutfitSidecarsFromTheirSegmentTables`), `Radicle/src/nif/sidecar_generate.cpp:ssf_shape_from_segments`, notes in `docs/code-notes/radicle-ssf-sidecar.md`.
Executable version and hash: Fallout4.exe 1.11.240.0 (AE), MD5 `c5791a0ce539465701c6e38fa465960c`, SHA-256 `fdcef37ac1230af6d0b0050eb2142b139ef3a867b37b9211fb6edfcc646072f8`. No executable was opened for this request; every fact below was measured from the shipped `Fallout4 - *.ba2` and DLC archives (NIFs and the `.ssf` files beside them).

## What is answered from data (no RE needed)

Measured on every vanilla `BSSubIndexTriShape` that names an `.ssf` with an entry for its shape: 1,313 shapes in 724 NIFs ("outfits").

- Delta ID layout: `id = (segment_index << 16) | (sub_segment_index << 8)`, the sub-segment index as a plain number, not a bit. 3,984 IDs, low byte zero in all of them, sub-segment index 0 to 8, never a bit mask (indices 3, 5, 6, 7 occur: 0x300, 0x500, 0x600, 0x700). Nine-sub-segment tables exist (27 shapes, 21 NIFs) and use indices up to 8. The old generator encoded `1 << (8 + index)` and refused tables with more than 8 sub-segments, which refused all 21 of those NIFs.
- Which sub-segments are listed: a sub-segment with cut offsets and a bone hash is listed under its own bone (the name whose CRC32 of the lower-cased string equals the user-data hash) in 2,354 of 2,370 cases in shapes whose base bone is `DISABLED`; 7 list the previous sub-segment's bone instead and 9 list another bone. In shapes with another base bone 44 of 119 are listed, 75 are not (see question 2).
- Bone names: 2,725 cut sub-segments carry a hash, only 913 are named by the owning NIF's own strings. Every one of them is named by the strings of the vanilla `skeleton*.nif` files (2,196 distinct strings), so the hash is the CRC32 of a skeleton bone name. One non-cut hash (2616664052, `Meshes\Clothes\VaultTecSalesman\MCoat.nif`, shape `MCoat:0`, segment 5 sub 0) is named by no string in any archive.
- Effect: 24 of 703 outfits regenerate exactly before, 214 of 724 after (the 21 nine-sub-segment NIFs are now examined).

## What data cannot tell (still open)

Measured counts below are over the same 1,313 shapes.

1. Base bone choice. `BaseBoneName` is `DISABLED` in 876 shapes, `Head` 94, `RArm_UpperArm` 59, `LArm_UpperArm` 45, `LLeg_Thigh` 41, `RLeg_Calf` 39, `RArm_ForeArm1` 31, `LArm_ForeArm1` 29, `RLeg_Thigh` 22, `Generic` 21, `LLeg_Calf` 21, `*1` 6, absent 5, then rarer creature and `RCALF` names. It is not a function of the segment table: the same segment layout (five segments of which only the last holds one sub-segment) has base `DISABLED` in 46 shapes and `LArm_UpperArm`, `Head`, `LArm_ForeArm1`, `Generic` in the other 54; in 339 of the 429 shapes with a real base bone the base is not a string of the NIF. Of the 132 shapes with a cut first sub-segment, 53 use that sub-segment's bone as base bone and leave it unlisted (for example `Raider04M.nif` shape `UVRaiderPantsBackfacing:0`: base `RLeg_Calf`, delta `LLeg_Calf: 0x60000`), 64 use `DISABLED` and list it, 15 have another base. Question: what does the engine do with `BaseBoneName` (applied to which vertices, which segments), what does the bone name `DISABLED` mean, and which tool or rule chooses the value? Is it derived from something outside the NIF (the ARMA biped slots, the skeleton, the skin partition)?
2. Why cut sub-segments are sometimes not listed and no-cut sub-segments sometimes are. Segments with no cut offsets at all list the odd sub-segment indices in 5-sub-segment layouts (`AAABB`: `1:A 3:B` in 15 of 17 shapes) but the same layout lists nothing in `Meshes\Clothes\Residents\6SuitM.nif` (`body_Cloth:0`). Question: does the engine use the cut offsets, the user index or the listed IDs to decide what a dismember cut touches, and is a listed no-cut sub-segment different from a cut one?
3. Whole-segment IDs. 243 shapes carry a `DISABLED` delta, mostly `index 0` of single-sub-segment segments (`MaleHands.nif`: base `Head`, `DISABLED: 0x20000 0x40000` over hand segments 2 and 4 whose sub-segment carries `RArm_Hand`/`LArm_Hand`). Question: what does a delta with bone `DISABLED` do, and what does index 0 mean for a segment with one sub-segment?
4. Previous-bone listings. 7 cut sub-segments list the bone of the sub-segment before them, `Meshes\Actors\FeralGhoul\CharacterAssets\Pants.nif` `Pants:0`: segment 6 sub 1 has user index 2, cut offsets and `LCALF`, listed as `LTHIGH: 0x60100`. Question: is the listed bone the one the engine scales (the previous bone) with the cut sub-segment being a boundary?
5. User index semantics. Sub-segment user indices seen: 1 to 10 (2,228 / 1,977 / 1,677 / 1,291 / 1,240 for 1 to 5), 60 (274), 160 (191), 100, 101, 102 and a few others. Question: which of them does the engine read, and what are 60, 100, 101, 102 and 160 (extra sub-segments carrying no bone)?
6. Cut offsets. Floats, 1 to 8 per sub-segment (for example 7.6055, 9.50688, 11.4083, 13.3096). Question: what are they (distances along the bone, in which units, relative to what) and how many are the engine allowed to read.
7. Range limits. Is the sub-segment index byte capped (vanilla stops at 8), and may a segment index above 15 or 255 occur? Radicle now accepts 256 sub-segments per segment and 65,535 segments; both are guesses from the ID width. Six shipped IDs name a segment index beyond the table (`M_Arm_Heavy_R.nif` `M_Arm_heavy_R:0` lists segment 4 of a 3-segment table; `Mirelurk_King_Glowing.nif` lists segments 5 and 6 of a 5-segment table): is that ignored or a hazard?
8. The unnamed hash 2616664052 above: which name hashes to it, and is the CRC the same `bone_id_hash` (reflected CRC32, polynomial 0xEDB88320, input lower-cased, no final XOR) the engine uses?

## Answer format needed

For 1 to 6 and 8: short pseudo-code of what the engine does with `BaseBoneName`, `DeltaBones`, the IDs, the user index and the cut offsets, with the function addresses and the executable version it was read from (OG 1.10.163, NG 1.10.984, AE 1.11.240). For 7: the width and meaning of every field of the ID and the engine's behaviour for an out-of-range segment or sub-segment. Where a rule is a CK or tool habit (not engine behaviour), say so and name the tool.

## What the answer unblocks

Radicle's `ssf_shape_from_segments` can then write the base bone, the `DISABLED` deltas and the no-cut listings instead of always writing `DISABLED` and cut-only deltas, which is the remaining gap (510 of 724 outfits still differ: 54 only by the base bone, 173 only by deltas, 283 by both). It also settles whether a generated sidecar is safe to load, which `Radicle/docs/NIF.md` still lists as unproven.
