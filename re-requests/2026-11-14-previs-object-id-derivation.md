# RE request: what object id must a generated previs tome carry for each reference?

Date: 2026-11-14. Follows `2026-11-13-previs-tome-acceptance.md`.

The `[[...]]` answer notes named below are Obsidian links into the RE Docs Vault (`01 - Previs and Visibility/`); they are not in this repo and GitHub renders them as plain text. Consult the vault to verify them.

> **Answered in the vault 2026-11-14 (not re-verified from this checkout):** see [[2026-11-14 - previs-object-id-derivation-answer]]. Q1 is verified with a
> correction (`entry+4` is a distance bucket, not the key), Q2 is verified for the id grammar --
> `0xFD000000 + block*0x4000 + slot`, checked as `Q16`/`Q17` in `decode_uvd.py` -- and partial for the
> allocator's input order, and Q3-Q5 are verified. The `0xFD000000 | physicsBodyIndex` guess is refuted.
> **Follow-up answered 2026-11-14:** the allocator's input order is closed for its deterministic part in
> [[2026-11-14 - previs-block-allocation-order-answer]] (`Q18`/`Q18b`): blocks are strips laid along `+y`
> inside runs of consecutive numbers, 44/44 pooled; the starting block and the run order stay producer-side.
>
> **Follow-up status:** see the *Resolution status* table below. The verified/partial/blocked labels are the vault's;
> `decode_uvd.py` and the retail tomes are not in this repo, so every label is UNVERIFIED here until rechecked.

Source of every address, struct field and count below: the RE Docs Vault notes under
`01 - Previs and Visibility/` dated 2026-11-14 (`previs-tome-acceptance-answers`,
`previs-tome-bytes-q3-expand-and-ids-answer`, `previs-tome-query-path-q1-q4-q5-answer`).
The facts quoted under *Known* below come from those notes and were not independently re-derived here.

## Why this matters

The vault says the engine matches each id in the tome's id array against the cell's live references through the vis
registry, and skips an id with no live reference with no error and no log.

UNVERIFIED: our generator still writes `0xFD000000 | body index` for each object (`PluginEditTool/native/src/plugin/RadiclePluginPrevisPrep.cpp:write_tome`, `k_combined_id_prefix | index`). The vault answers below refute that guess; the vault's rule is Q2 in the resolution table, and the generator has not been changed to follow it.
The vault says retail ids are not sequential, for example `0xFDF1803B` and `0xFDF1C002`. If ours do not match what the
engine gives the cell's references, the tome would load and do nothing. We cannot tell that apart from success without an
in-game run, so we need the rule from the code.

## Known (quoted from the vault)

- Build: `Fallout4.exe` 1.11.240.0, MD5 `c5791a0ce539465701c6e38fa465960c`, SHA-256
  `fdcef37ac1230af6d0b0050eb2142b139ef3a867b37b9211fb6edfcc646072f8`. NG 1.10.984 has twins. No OG 1.10.163 twin was found.
- Vis registry record: 0x18 bytes, `{ref, key u32 @+8, id u32 @+0xc, state @+0x10, flag @+0x14}`.
- Per-id apply handler AE `0x4df1f0`, NG `0x48b050`: the whole body is inside a null check, so a missing record is a no-op.
- Cell-side producer AE `0x4c4990` walks the cell's ref array and inserts each surviving ref under the key
  `*(u32*)(entry+4)`. `entry+4` is a runtime field; no file bytes carry it.
- The tome id array is `+0x40` raw 4-byte values at `+0x50`. In sample A, 1,136 of 1,146 are `0xFD......` and 10 are
  ordinary ids such as `0x0000CD78`.
- Retail samples: vault `11 - Evidence/01 - Previs and Visibility/2026-11-14-uvd-retail/` (`0000C2E9`, `0000C346`,
  `0000E7FC`; `id_array_dump.txt` lists the ids).

## Questions

1. Where is `entry+4` set, and what is `entry`? Name the function that writes it and the value it stores for a
   reference in a cell with precombined data. Is it the reference's own FormID, a combined-object id, or a hash?
2. How is a `0xFDxxxxxx` id formed? Give the formula from its inputs (cell, combined mesh, reference, position,
   index). Say whether it can be computed from the cell's plugin data without running the CK. Test any candidate
   against the three retail tomes' ids and quote the match count.
3. Why are 10 of 1,146 ids ordinary refs such as `0x0000CD78`? Are they load-order relative (master index in the top
   byte, remapped at load) or raw, and which refs get one instead of a combined id?
4. What happens to a live reference that is in the cell but not in the tome's id array: drawn, culled, or left as the
   precombined data made it? This decides whether an incomplete id list is only wasteful or hides geometry.
5. The `+0x48` records are parallel to the ids. Does id order matter beyond keeping each id with its own box? Does the
   registry or the query need sorted ids?

## Answer format

A short markdown note answering each question, tagged verified, partial or open as the vault notes do. Give function
addresses by RVA for 1.11.240, say which retail tome bytes you checked, and give the Q2 formula as pseudocode with the
match count.

## What it unblocks

- The id rule in the previs generator (`RadiclePluginPrevisPrep.cpp` `write_tome`, in the previs-switch work). Until it
  is answered we should not make the tile-model tome the default, because a wrong id rule gives a tome that loads and
  silently does nothing.
- A worthwhile in-game test of a generated tome.

---

# Resolution status (updated 2026-11-15)

Questions 1-5 in the section above are closed in the vault (verified or corrected there). Of the nine follow-up rows 6-14 below: rows 7, 8, 10 and 11 are **verified**; row 6 is **verified / partial**; row 9 is **verified form, blocked predicate**; row 12 is **partial**; rows 13 and 14 are **blocked**. Only rows 7, 8, 10, 11 are fully closed; 6, 9, 12, 13 and 14 have an open part (blocked: 9's predicate, 13, 14). All labels are the vault's and UNVERIFIED from this checkout. Check ids refer to `decode_uvd.py` in
`11 - Evidence/01 - Previs and Visibility/2026-11-14-uvd-retail/`; the suite is at **175 PASS, 0 FAIL** over
the three retail tomes (byte coverage 2676816 / 2013136 / 2638496 exactly).

Answer notes behind the table (vault notes, absent from this repo; the links do not resolve on GitHub): [[2026-11-14 - previs-object-id-derivation-answer]] (Q1-Q5),
[[2026-11-14 - previs-block-allocation-order-answer]] (Q18), [[2026-11-14 - previs-payload-block-directory-answer]]
(Q19), [[2026-11-14 - previs-strip-run-order-answer]] (Q20), [[2026-11-14 - previs-col7-flag-answer]] (Q21),
[[2026-11-14 - previs-portal-ordinal-record-decoded]] (Q22), [[2026-11-14 - previs-ordinal-payload-sections]]
(Q25), [[2026-11-14 - previs-blocker-inventory]] (Q23/Q24), [[2026-11-14 - previs-col7-predicate-blocked]]
(Q26/Q27).

| # | question | status | answer | check |
|---|---|---|---|---|
| 1 | where `entry+4` is set, what it is | **verified, with a correction** | not the key: `entry+4` is a **distance bucket** clamped into `[DAT_142ee4328, DAT_142ee4340]` with `DAT_142ee4358` forced (AE `0x4df3e0`); the registry key is the id | — |
| 2 | how a `0xFDxxxxxx` id is formed | **verified** | `id = 0xFD000000 + block*0x4000 + slot`, `block = (id-0xFD000000)>>14`, `slot = id & 0x3FFF`; matches **1136/1136, 671/671, 802/802**; `0xFD000000 == 0x3F400 << 14`, so the tag is block 0 of a bump allocator whose granularity is one 0x4000 window per combined object | `Q16`, `Q17` |
| 3 | why some ids are ordinary refs | **verified** | they are master-index-0 FormIDs (`0x0000CD78` style), 10 / 17 / 30 of them, never combined ids; the combined set is dense per block (10 of 20/18/21 blocks hold slots `0..n-1` exactly) | `Q16` |
| 4 | a live ref missing from the id array | **verified** | silent no-op: the apply handler AE `0x4df1f0` is entirely inside a null check, so an unmatched id is skipped with no error and no log | AE `0x4df1f0` |
| 5 | does id order matter | **verified** | only to pair an id with its own box; the id array is **unsorted** with sparse slots, and nothing sorts it | `Q16`, `Q22` |
| 6 | how blocks are allocated and ordered | **verified / partial** | blocks form runs of consecutive numbers; inside a run the mean `y` ascends **44/44 pooled** and each block is a `y` strip (19/19, 17/17, 19/19 defined); the lowest block differs per tome (**0 / 3 / 6**) and no file key orders the runs (mean `x` 10/12 at best, min/max `x` 7/12, mean `y` and lowest object index 5/12), so **block origin and run order are producer state** | `Q18`, `Q18b`, `Q20`, `Q20b` |
| 7 | how the payload block directory is laid out | **verified** | slot count = `ceil(tiles/2)`, directory entries 16 B, blocks contiguous and 16-aligned with the last ending at EOF; the row count is **not** recoverable (3..128 candidates per block, 256/256 ambiguous) | `Q19` |
| 8 | what a run boundary contains | **verified** | a hole of **27..895 slot numbers** at every one of the 12 run boundaries, 0 holes inside a run; 84-98 % of the spanned block numbers belong to objects the tome never lists, so the block space is shared across cells | `Q20c`, `Q20c-b` |
| 9 | what col-7 (`+0x1c`) of an object record marks | **verified form, blocked predicate** | two-state `FLT_MAX` vs `2^24` = `4096²` (61/48/36 marked of 1146/688/832), a 7.1-9.1x median-size signature but **not a size rule** (best of eight box functionals still misclassifies 7/7/10) and not the LOD level-1 set; the predicate needs the plugin's per-object flags | `Q21`, `Q21-b`, `Q26`, `Q26-b` |
| 10 | what the 20-byte portal ordinal record holds | **verified** | five `u32`: packed `(x<<3)|y` with `y` never 0, start offset, end offset (strictly ascending, 16-aligned, disjoint), an item count and an interior split, with `count == 0 ⇔ split == 0` on **389/389** records | `Q22`, `Q22-b` |
| 11 | are the two payload sections the same kind of data | **verified negative** | the split is a **type boundary**: after it, a **256-quantised coordinate array** (96.4 % of 2380 words on the lattice, bounded at 139,136, 0 plane rows); before it, a large-integer array (12.4 % on the lattice, 84-87 % of entries led by values >= 2^20) | `Q25`, `Q25-b` |
| 12 | what the first payload section's integers are | **partial** | a mask-bearing integer domain with **no in-file referent**: 0 of 8152 words occur in the id array, 15.5 % are below K, 17.5 % of entries are led by a `2^k-1` mask; which mask space and what the other 82.5 % encode stay open | `Q27`, `Q27-b` |
| 13 | what `f0`'s high 29 bits index | **blocked** | reaches 7892/5500/10436 against 959/665/1009 payload entries and 1146/688/832 objects, so it numbers a table outside the file; no in-file reference matches above 2 % | `Q23`, `Q23-b` |
| 14 | what the payload arrays are called | **blocked** | no name strings (0 of 8 candidates over 6.7 MB) and no enum tag (first word takes 614/385/660 distinct values) - a naming question has no decider in the bytes | `Q24` |

## Consequence for the generator

`write_tome` could compute ids as below (the vault's rule; not implemented, and UNVERIFIED in-engine):

```
block = <combined object index in the cell's combined-mesh list>   # allocated by the producer, not derived
slot  = <index of the reference inside that combined object>       # 0..n-1, dense
id    = 0xFD000000 + block * 0x4000 + slot
```

Two caveats the vault answers make explicit:

1. **The block number is ours to assign.** The tomes show the allocator's *shape* (runs of consecutive numbers,
   `y`-ordered strips, a shared space with 27-895-slot holes) but its **origin differs per cell (0 / 3 / 6)** and
   nothing in the file orders the runs. Whether the engine accepts self-numbered blocks, for example from 0
   per cell, is **UNVERIFIED**: nothing in this repo or the vault answers shows the engine matching ids it did not
   produce, and the producer-side state (block origin, run order) is unknown. Exact Bethesda numbers cannot be reproduced.
2. **Slot density is what the engine walks.** Retail blocks are dense from 0 in 10 of 20/18/21 cases and no retail id repeats (`Q16`: 0 duplicates); density is not shown to be required. The vault reports that a tome with sparse or colliding slots still
   loads (not re-checked here), but the combined-object association is what the ordinals index.

An in-game test remains the only way to confirm the engine accepts a generated tome; the vault answers name the id
rule, and the byte-level payload (blocks, rows, portal ordinals) is still the part a generator must
match for the tome to do anything visible.
