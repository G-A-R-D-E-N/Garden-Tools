# RE Tracking

Last updated 2026-10-19.

Check here before asking the RE machine anything: it tracks every reverse-engineering question GARDEN-TOOLS depends on (RE-001 to RE-026 plus research-repo issue #19). Open questions are posted on the umbrella issue #520.

- **Full register** (long form, per-item prose, every answer bullet): [RE-Work/RE-TRACKING.md](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/branch/docs/re-tracking-2026-10-19/RE-Work/RE-TRACKING.md) on branch `docs/re-tracking-2026-10-19` (PR #28). This page is the short form.
- **Coverage map** (every finding, every build, including findings outside the build maps): https://re2.nomadicinteractive.dev
- **Which repo holds what**: [REPOS.md](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/branch/main/REPOS.md)
- Evidence is on `main`: `ReverseEng/Fallout4_Runtime_Research` (PR #11, commit `644a4a33`) and `ReverseEng/Havok_Research` (PR #4, commit `481a14c6`). The branch commits quoted in the full register are the original work commits; the packages themselves are merged.
- Every count below was recomputed from the package `findings.jsonl` on `main` on 2026-10-19, not copied forward from earlier prose.

No row is Open as of 2026-10-19: every ID has a reviewed static answer on `main`. What remains is per ID in the last column.

## How to read the status columns

- **Status values.** Open: no RE answer yet. Partial: questions answered, residual questions remain (static, data or runtime). Runtime-only: every question has a reviewed static answer and the only thing left needs a game or Creation Kit run. Answered: all questions answered, awaiting implementation or verification by the consumer issue. Verified: answer re-checked in a later comment and nothing remains. Superseded: replaced or made obsolete, the row names the replacement.
- **Review is per row, not per package.** Each `findings.jsonl` row carries `verified_by`. `review-YYYY-MM-DD` means a second agent re-derived the deciding bytes; `pending-review` means nobody has yet. The two columns **Reviewed** and **Pending review** in the counts table come from that field, so a package can be mostly reviewed and still carry pending rows.
- **A `PARTIAL`, `UNRESOLVED` or `NOT_FOUND` verdict is about one claim**, not about the topic. A topic is Partial when a specific question still has no answer.
- **`REFUTED` is a correction, not a failure.** Those rows are claims made earlier that the review disproved; they are kept so a wrong statement is not re-derived. A high REFUTED count means the package was audited hard, not that it is wrong now.
- **`INFERRED`** marks a claim argued from the surrounding code rather than read out of a deciding byte; treat it as a lead, not as evidence.
- **`RUNTIME_ONLY` / `DATA_MISSING`** mark rows that need a game or CK run, or files that were not on the RE machine.
- PDB, Oracle and CommonLib names are naming evidence only, never hook RVAs.

## Register

One row per RE item. "Evidence" is the comment that carries the reviewed answer; the package it lives in is named in the counts table below.

| ID | Topic | Status | Date | Evidence | Still missing |
|---|---|---|---|---|---|
| RE-001 | Runtime behavior of injected records (parent of RE-002) | Runtime-only | 2026-10-18 | [c17037](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-17037) | Q1 save/load cases a-f and Q2 CK load/save of an injected record (both Runtime); evidence is shared with RE-002. |
| RE-002 | Fallout4.exe: how the runtime loads and owns injected forms | Runtime-only | 2026-10-18 | [c17037](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-17037) | Q1 in-game save test and Q2 the two CK gate values (both Runtime); 27 of the 68 rows are still `pending-review`. |
| RE-003 | Creation Kit precombine and previs generation rules | Partial | 2026-10-18 | [c17037](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-17037) | Q4 CK output against the recovered rules (Runtime); ~22% of precombine ids need the real per-object tile grid - the OC-id generator is found (Static). |
| RE-004 | PapyrusAssembler string-table insertion order and spelling winner | Partial | 2026-10-18 | [c17037](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-17037) | Q6 which assembler and compiler builds made the shipped `.pex` (Data); Q7 the no-.NET-2.0 hang (Runtime); why only `s`->`S` and `i`->`I` permute (Static). |
| RE-005 | VMAD property types 6 and 16: nested type discriminator and value layout | Partial | 2026-10-18 | [c17037](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-17037) | A third-party plugin with type 6 or a non-empty type 16 (Data); what a dropped status-2 property means to the script (Runtime). |
| RE-006 | How the game assigns smallFileCompileIndex: OG 1.10.163 vs next-gen 1.11.x | Partial | 2026-10-18 | [c17037](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-17037) | `Plugins.txt` order as the engine applies it (Runtime); launcher and mod-manager limits (Data); what `0x1073050` does with its parser argument (Static). |
| RE-007 | TES4 flag bit 7 (0x80): localized or delocalized, and when it is set | Partial | 2026-10-18 | [c17037](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-17037) | 43 CK stores that could touch the localized flag are untyped; no clear path found (Static, bounded). |
| RE-008 | DecompressCurrentForm: zlib framing vs raw deflate, and XXXX handling | Partial | 2026-10-18 | [c17037](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-17037) | Malformed-record behaviour in a live game (Runtime); the game's XXXX writer has no static caller (Static). |
| RE-009 | Cell/worldspace water-type precedence and finite default-height sentinel | Runtime-only | 2026-10-18 | [c17037](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-17037) | The deleted water form and the live resolver order (Runtime). |
| RE-010 | Climate timing units, moon packing and weather transition duration bounds | Runtime-only | 2026-10-18 | [c17037](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-17037) | Q5 live observation of a weather transition (Runtime). |
| RE-011 | Sound descriptor selection and playback control semantics | Runtime-only | 2026-10-18 | [c17037](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-17037) | The runtime source of non-zero category ducking values, and the streamer cache contents (Runtime); 114 of 229 rows are `pending-review`. |
| RE-012 | NAVM disk geometry versus CK reader grammar and runtime flag semantics | Partial | 2026-10-18 | [c17037](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-17037) | The cover field 5/6 setter exists but nothing calls it (why?); masks are passed as parameters (Static). |
| RE-013 | Grass placement density, slope and water constraints in the runtime | Partial | 2026-10-18 | [c17037](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-17037) | Trunk and branch flexibility (Runtime); the PFPC reader is not found (Static); 93 of 235 rows are `pending-review`. |
| RE-014 | Dialogue condition selection and scene phase/action execution semantics | Partial | 2026-10-18 | [c17037](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-17037) | The 0x68 context size is an alignment inference, and the NG/AE readiness predicate is skeleton-level (Static). |
| RE-015 | Package procedure/input dispatch and NPC combat/idle selection semantics | Partial | 2026-10-18 | [c17037](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-17037) | The PathingRequest bit 0x08 reader is not found after two bounded scans (Static, bounded); 231 of 754 rows are `pending-review`. |
| RE-016 | Light-plugin own-ID floor (0x001 vs 0x800) and HEDR next object ID | Runtime-only | 2026-10-18 | [c17037](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-17037) | An end-to-end CK-assigned id (Runtime). No evidence package on `main`: the record is the answer comment c13469. |
| RE-017 | AVIF and PERK stored loader contracts on OG and AE | Partial | 2026-10-17 | [c16788](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-16788) | The object compared in the unsigned rank-vs-numRanks test at OG 0x57179C is inferred to be the perk (Static). |
| RE-018 | QUST stored loader contracts and quest execution semantics on OG and AE | Partial | 2026-10-18 | [c17037](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-17037) | Whether OnQuestInit and OnInit also run in queue order (Runtime); FillAliases internals are only PARTIAL (Static); 97 of 205 rows are `pending-review`. |
| RE-019 | CLMT WLST weather selection by chance | Superseded | 2026-10-12 | [c13387](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-13387) | Nothing: GardenEngine builds its own weather and sky system and the old code is removed by GardenEngine #391. |
| RE-020 | plugins.txt / loadorder.txt line parsing: encoding, trimming, comments, duplicates | Answered | 2026-10-17 | [c16788](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-16788) | Nothing: implemented by #592 (`nomad/dev` 73f23c22d) and written up in `Radicle/docs/LOAD_ORDER_FILES.md`. |
| RE-021 | Havok packfile pointer and fixup emission order for physics objects | Partial | 2026-10-18 | [c17037](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-17037) | The predicate semantics of the gate at OG 0x17C6C21 (its fingerprint polynomial); site and bytes are verified (Static). |
| RE-022 | FO4 creature skeleton shapes (sphere, convex polytope, compound, hknpBSMaterial) | Partial | 2026-10-18 | [c17037](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-17037) | The roles of the `+0x3A0` COLL mapping and of the `+0xA0` table are inferred, not read (Static). |
| RE-023 | Body-part / VATS chain (BSSubIndexTriShape segments, BSDismemberSkinInstance, BPTD) | Partial | 2026-10-18 | [c17037](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-17037) | The nine UI readers of the selected limb were read only around each access, not swept as a set (Static). |
| RE-024 | What the FO4 Havok export filter set writes for physics | Partial | 2026-10-17 | [c16787](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-16787) | Real 2014.1.x `hkClass` definitions for the classes HCT lacks. Neither the game files nor the exe provide them, so this is Data, not static work. |
| RE-025 | Root motion reference-frame semantics (frameType, referenceFrameSamples) | Partial | 2026-10-18 | [c17037](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-17037) | The slot 5 argument names and the blend formula (Static); the in-game turn direction (Runtime). |
| RE-026 | Quantized and predictive animation decode | Partial | 2026-10-18 | [c17037](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-17037) | A hand-built quantized file and a 31-frame predictive `.hkx` sampled in the game (Runtime); the renormalise consumer (Static). |
| RE#19 | PACK procedure-tree data inputs and leaf payloads (research-repo issue #19) | Answered | 2026-10-19 | [c17306](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/520#issuecomment-17306) | The NG/AE counterparts of the `InitItem` remap and the leaf Loads (Static); `UseWeapon`/`Acquire` Int compare signedness (Static); 16 of 39 rows are `pending-review`. |

## Per-package verdict counts

Rows, not claims: a package can answer more than one RE item, and RE-001/RE-002 and RE-010/RE-019 are split by the `issue` tag inside a shared package. RE-016 has no package on `main`.

| ID | Evidence package | Rows | VERIFIED | PARTIAL | UNRESOLVED | NOT FOUND | REFUTED | INFERRED | Other | Reviewed | Pending review |
|---|---|---|---|---|---|---|---|---|---|---|---|
| RE-001 | plugin-io-garden-tools/injected-forms-385 (RE-001 rows) | 76 | 53 | 5 | 9 | 0 | 9 | 0 | - | 76 | 0 |
| RE-002 | plugin-io-garden-tools/injected-forms-385 (RE-002 rows) | 68 | 55 | 2 | 3 | 0 | 5 | 3 | - | 41 | 27 |
| RE-003 | plugin-io-garden-tools/precombine-386 | 85 | 61 | 8 | 4 | 3 | 9 | 0 | - | 66 | 19 |
| RE-004 | plugin-io-garden-tools/papyrus-string-order-387 | 149 | 117 | 9 | 5 | 0 | 18 | 0 | - | 149 | 0 |
| RE-005 | vmad-variable-types-388 + vmad-388-15 | 267 | 211 | 16 | 22 | 1 | 13 | 4 | - | 233 | 34 |
| RE-006 | small-file-compile-index-389 + light-plugin-index-389-28 | 333 | 240 | 41 | 27 | 1 | 24 | 0 | - | 333 | 0 |
| RE-007 | tes4-localized-flag-390 + localized-strings-390-29 | 198 | 139 | 20 | 12 | 2 | 25 | 0 | - | 198 | 0 |
| RE-008 | decompress-current-form-391 + compression-391-30 | 153 | 113 | 16 | 5 | 7 | 12 | 0 | - | 134 | 19 |
| RE-009 | radicle-re-garden-tools/water-395-341 | 85 | 69 | 6 | 2 | 0 | 6 | 0 | RUNTIME-ONLY 2 | 50 | 35 |
| RE-010 | radicle-re-garden-tools/weather-climate-397-343 (RE-010 rows) | 176 | 143 | 12 | 7 | 3 | 11 | 0 | - | 176 | 0 |
| RE-011 | radicle-re-garden-tools/sound-399-340 | 229 | 184 | 36 | 1 | 1 | 7 | 0 | - | 115 | 114 |
| RE-012 | radicle-re-garden-tools/navigation-401-339 | 222 | 146 | 33 | 15 | 12 | 16 | 0 | - | 222 | 0 |
| RE-013 | radicle-re-garden-tools/vegetation-403-342 | 235 | 189 | 5 | 14 | 16 | 6 | 5 | - | 142 | 93 |
| RE-014 | radicle-re-garden-tools/dialogue-scenes-405-344 | 303 | 228 | 44 | 8 | 0 | 17 | 6 | - | 248 | 55 |
| RE-015 | radicle-re-garden-tools/packages-combat-idle-407-345 | 754 | 653 | 30 | 18 | 26 | 23 | 3 | CONTRADICTED 1 | 523 | 231 |
| RE-016 | none (record in #520 c13469; no package on `main`) | - | - | - | - | - | - | - | - | - | - |
| RE-017 | plugin-io-garden-tools/avif-perk-loaders-443-re017 | 140 | 115 | 6 | 3 | 0 | 3 | 13 | - | 137 | 3 |
| RE-018 | plugin-io-garden-tools/quest-loaders-445-re018 | 205 | 182 | 11 | 3 | 0 | 7 | 1 | RUNTIME-ONLY 1 | 108 | 97 |
| RE-019 | radicle-re-garden-tools/weather-climate-397-343 (RE-019 rows) | 42 | 31 | 10 | 0 | 0 | 0 | 1 | - | 42 | 0 |
| RE-020 | plugin-io-garden-tools/plugins-txt-parser-re020 | 63 | 46 | 9 | 0 | 3 | 2 | 3 | - | 56 | 7 |
| RE-021 | Havok_Research animation/garden-tools-520/RE-021 | 23 | 19 | 4 | 0 | 0 | 0 | 0 | - | 23 | 0 |
| RE-022 | Havok_Research animation/garden-tools-520/RE-022 | 51 | 39 | 4 | 2 | 0 | 6 | 0 | - | 51 | 0 |
| RE-023 | Havok_Research animation/garden-tools-520/RE-023 | 32 | 29 | 2 | 0 | 0 | 1 | 0 | - | 32 | 0 |
| RE-024 | Havok_Research animation/garden-tools-520/RE-024 | 15 | 10 | 2 | 0 | 0 | 3 | 0 | - | 15 | 0 |
| RE-025 | Havok_Research animation/garden-tools-520/RE-025 | 23 | 21 | 2 | 0 | 0 | 0 | 0 | - | 23 | 0 |
| RE-026 | Havok_Research animation/garden-tools-520/RE-026 | 36 | 28 | 8 | 0 | 0 | 0 | 0 | - | 36 | 0 |
| RE#19 | radicle-re-garden-tools/pack-procedure-inputs-602-c8 | 39 | 30 | 4 | 5 | 0 | 0 | 0 | - | 23 | 16 |

4,002 rows across the 27 entries above.

## Flagged conflicts

Points where the register, the packages and earlier prose disagree. The `findings.jsonl` rows are the authority; the note on each item says which side wins.

1. **RE-018: the umbrella answer and the package disagree.** c17037 lists only the runtime item for RE-018, but the package status table marks the `FillAliases` internals PARTIAL. The package rows are the primary record, so the row here stays Partial. Do not close RE-018 on the umbrella comment alone.
2. **RE-023 to RE-026: README headers are stale.** Those four package READMEs still open with "all rows remain `pending-review`", but all 46 rows of RE-021 to RE-026 carry `review-2026-10-16` or `review-2026-10-17`. `findings.jsonl` is the authority (c17517); the README headers are stale prose, not an open review.
3. **RE-004: does function order inside a state permute?** c6960 said functions permute too; the 40-run SOTest3 check found function-name order stable within a state, and c10049 records the correction. Variables, properties and user-flag names do permute on every run.
4. **RE-009: Creation Kit notes 56, 54, 57 and 23 contradict the engine and Fallout4.esm** and must not be used. XCLW is a float (FLT_MAX in 39,265 of 40,165 cells), 518 CELL XCWT records do exist, NAM2 is a WATR form ID, and child PNAM is 0x16 or 0x1F.
5. **RE-012: retracted claims.** Bit-packed counts, 24/32 bytes per triangle, a second 21-byte table, a header "checksum" (the constant is the factory key 0xA5E9A03C) and edge-info flags in bits 8-10 (they are bits 0-2) were all retracted. CK doc 51 size claims are contradicted by 12,558 exact parses.
6. **RE-015: PDTO byte 0 was corrected.** c13144 was superseded by c13392: byte 0 is a kind field, and kind 1 holds a FourCC dialogue-topic subtype (324 of 15,004 shipped PDTO records). Cite c13392, not c13144.
7. **RE-016: no package, and its prose is stale.** The evidence is the answer comment c13469, so the ID shows no row counts in the counts table. The section prose still opens with "No RE answer yet"; the c13469 answers are the record.
8. **RE-024: the original question is answered DATA-MISSING.** No Havok Content Tools installation exists on the machine, so the export-filter question cannot be answered statically at all. The follow-up work reads the installer archives and the Anarchy SDK source instead. Do not re-ask it as an unresolved static question.
9. **RE-001 and RE-002 share one package, and so do RE-010 and RE-019.** Rows are split by the `issue` tag inside `injected-forms-385` and `weather-climate-397-343`, so the review state differs inside a single package: RE-001 is 76 of 76 reviewed while RE-002 is 41 of 68.
10. **RE-019 is superseded, not answered for us.** It was answered anyway on 2026-10-12 before the withdrawal reached the RE machine (chooser OG 0x48F660). It is kept for reference only; nothing consumes it.
