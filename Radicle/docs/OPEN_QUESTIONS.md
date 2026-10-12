# Open questions

Speculative answers must not be hardcoded as facts.

## FormID / ESL

1. What FormID appears in an ESL's **own** major-record headers on disk: `00xxxxxx`, `FExxxxxx`, or `(master_count << 24) | local`? **Partial answer:** `ccBGSFO4096-AS_Enclave.esl` uses `(master_count << 24) | local` (`0x01xxxxxx` with one master). Repeat on more ESLs before treating as universal.
2. Exact predicate of `TESFile::CheckValidIDForSmallFile`. **Answered.** The body is
   `f7 c1 00 f0 ff 00 0f 94 c0 c3` = `test ecx, 0x00FFF000; sete al; ret`, i.e.
   `(id & 0x00FFF000) == 0` — bits 12–23 must be clear, so a small-file local ID is
   valid iff it is `<= 0xFFF`. This independently confirms the range derived from
   `AdjustFormIDFileIndex` in `ESL_FORMIDS.md`.
   **Address depends on the build, and the workspace holds two unpacked images that
   look alike, which is an easy trap.** Build table: `RE-Work/docs/26-RE-TARGETS.md` (section 4).
   `Symbols/Fallout4.exe` is timestamp `0x5dac778e` (Tool-Build, the RE doc labels it so): predicate at RVA
   `0x130550`, `AdjustFormIDFileIndex` at `0x131f90`.
   `Symbols/1.10.163/Fallout4.exe.unpacked.exe` is `0x5ddc40fa` (1.10.163):
   predicate at `0x130560`, `AdjustFormIDFileIndex` at `0x131fa0` — the listed RVAs.
   Each symbol table matches its own image byte-for-byte. An earlier note here
   claimed a `0x10` symbol-table shift; that was this cross-build mix-up, not a
   shift. Check the PE timestamp before trusting an address.
   Verified source: private RE repository `ReverseEng/commonlib_NonVR`
   (re.nomadicinteractive.dev) at commit
   `dbaeca7486300f4f1cc15e4607f8208f0df93013`, `RE-Work/docs/26-RE-TARGETS.md`,
   section 4, the section-aware predicate bytes and build-stamp table; inspected read-only.
3. Exact bytes of `TESFile::AdjustFormIDFileIndex`. **Partly answered for OG 1.10.163**
   (PE timestamp `0x5ddc40fa`, RVA `0x131fa0`); `RE-Work/Runtime/03-FINDINGS.md` "FormID compile-index remap decoded" has pseudocode only, not bytes. See `ESL_FORMIDS.md`
   and the same repository's `RE-Work/Runtime/03-FINDINGS.md` "Exact runtime FormID
   stamping" (`0x131fa0`). The timestamp `0x5dac778e` build uses `0x131f90`.
4. **Answered (see 6b and 6c).** `TESFile::AdjustFormIDFileIndex` (OG 1.10.163 address `0x131fa0`, `RE-Work/Runtime/03-FINDINGS.md`) masks a light plugin's local to 12 bits *destructively* before OR-ing in the slot, so anything above `0xFFF` silently aliases onto another record. From the encoding, usable range is `0x001`–`0xFFF`; the `0x800` floor belongs to the full-plugin encoding only. A light plugin reaches the remap straight from the header read, so `AddCompileIndex`'s floor does not decide its own IDs; an own ID `0x001`–`0x7FF` is allowed exactly when the plugin has a master (see 6b). Six of the nine shipped ESLs allocate below `0x800`, and `ccFSVFO4007-Halloween.esl` has 2078 own records, disproving the `0x800`–`0xFFF`/2048 folklore. See `ESL_FORMIDS.md`.
5. **Partial answer:** OG 1.10.163 and AE 1.11.240 statically establish separate full/light array positions and file-list construction, including the master-flag-only hoist and checked-state sources. `.esl` extension or header SmallFile flag selects the light array. NG 1.10.984 is verified by instruction comparison with its unpacked image, the same as AE apart from the 7-name official table and AE's second Voices predicate (missing-archive handling is PARTIAL) (#520 comment 13470, RE-006 Q2); external limits and live dependency behavior remain #389 work. Radicle retains caller-supplied order and safe caps; these runtime questions do not block proven stored-master edits. See `RUNTIME_PLUGIN_INDEX_EVIDENCE.md`. NG's append routine `0x2A3000`, master-flag predicate, kChecked setter and hoist were verified byte-identical to AE in #389 comment 9707, and the rebuild `0x294110` equals AE `0x2E8020` (220/220 instructions, #520 comment 13470), consistent with NG assigning indexes as AE does; only runtime behavior (not tested in game) remains.
6. Can a plugin list more than 254 masters, and what happens?
6b. Do light plugins pass through `AddCompileIndex` (`0x1549f0`), which leaves IDs below `0x800` unremapped as engine-reserved? If so, own IDs `0x001`–`0x7FF` in a light plugin would not resolve as the plugin's own forms. **Answered (RE-016, #520 comment 13469, evidence commit `78e013b2`, `RE-Work/subsystems/plugin-io-garden-tools/small-file-compile-index-389/`).** A light plugin reaches the remap straight from the header read (OG `0x132740`, NG `0x2A5420`, AE `0x2F9A30`), which applies `(id-1) <= 0x7FE` to the raw 32-bit ID. Masterless: an own ID such as `0x005` stays in the hardcoded range and never resolves to the plugin's own record. With masters the stored ID carries a nonzero master index and is remapped to the light runtime ID (verified by model from the instruction bytes). Never rejected. On OG 1.10.163 the reserved test at `0x1541F0` is `(id-1) <= 0x7FE`, so the reserved range is `1`..`0x7FF`. Id `0` is not reserved and is remapped normally (RE-002 on #520, comment 9747). The test is applied to the full 32-bit id, as read in RE-002 comment 6947; comment 9747 gives the formula only. For a light owner the remap at `0x131FA0` gives `0xFE000000 | slot<<12`.
6c. Does the game or the Creation Kit read the HEDR next object ID for a light plugin? **Answered (CK caller edge cases PARTIAL; RE-016, #520 comment 13469, evidence commit `78e013b2`, `RE-Work/subsystems/plugin-io-garden-tools/small-file-compile-index-389/`).** The runtime reads the HEDR next object ID once at file open (OG `0x12ED70`, NG `0x2A1950`, AE `0x2F5EB0`): if the raw value is `<= 0x7FF` it becomes `0x800` (unsigned compare before masking), then it is stored as `(v & 0xFFFFFF) | compileIndex << 24`, so the on-disk `0xFF` prefix is discarded; nothing else reads the field. The engine and CK compare the raw value before masking, so a stored `0xFF000001` is not clamped and becomes local 1; Radicle masks first and then clamps to the plugin's own floor, the safer choice. The Creation Kit (1.11.137 and 1.11.240) applies the same clamp, never allocates the prefix, shows an ESL counter of `0xFFF` minus forms owned, and refuses convert-to-light when a live form ID is above `0xFFF`; its allocator has no bound. Radicle masks the HEDR next to 24 bits (`Radicle/src/plugin/mutation.cpp:reserve_local_form_key`). Residual: one CK setter at vtable slot `0x398` is not traced.

## HEDR

0. HEDR `formCount` appears to be (major records excluding TES4) + (all GRUPs, nested included). Heather and DLCworkshop01 match. Still want an ESL and a WRLD-heavy plugin before the writer emits this field.

## TES4 flags

7. **Answered for stored classification and OG/AE readers:** bit `0x80` set selects external IDs; clear selects inline text. CK uses "delocalized" for the same external state. See `TES4_LOCALIZATION_EVIDENCE.md`; naming does not change serialized meaning.
8. Are TES4 record flags and `TESFile::flags` the same bit layout? CommonLib treats them as the same enum.

## Compression / XXXX

9. **Answered for stored framing:** official compressed records and OG/AE init-wrapper evidence use zlib framing. This branch removes the raw-deflate retry for nonzero declared sizes. Radicle loads undecodable compressed content as an undecodable record with raw bytes kept (GT-205 PR1). NG window bits are verified (#520 comment 13471, RE-008), so zlib framing is verified on OG, NG and AE; live malformed-input behavior remains a separate #391 question; see `COMPRESSION_EVIDENCE.md`.
10. **Answered for the inspected OG path:** the chunk-header reader consumes `XXXX`, replaces the type with the next header, and applies its extended size before higher-level reads. Radicle retains the physical marker to preserve file bytes. See `COMPRESSION_EVIDENCE.md` for per-build scope.
10b. Engine `GetChunkData` truncates oversize chunks (`data truncated`). Should the native reader match that or fail hard? Currently fail hard.

## Groups

11. Confirm group type integers 0-10 against CK/runtime, not only xEdit.
12. Interior cell block grouping math (which cells share a block).

## CK vs file-load table

13. Why CKRE's file-load table appears to omit `RACE` while RACE records exist in `Fallout4.esm`. Special-case loader vs dump error.
14. CKRE factory join was wrong for indexes 1–10. **Answered:** MSVC `$0N` for 1–10 is `(N-1)`, so `$03` is KYWD=4 not GMST=3.

## Localization

15. `.STRINGS` / `.DLSTRINGS` / `.ILSTRINGS` layout for FO4, including next-gen changes.
16. **Partial answer:** CK 1.11.137 delocalization sets bit `0x80` and its active string-writer mode stores four-byte IDs from tagged inline text. CK table emission, language ownership and live behavior remain RE-007 on #520; NG string-reader semantics are verified as AE's (#520 comment 13471, evidence commit 78e013b2). Proven stored editing does not require runtime reproduction. See `TES4_LOCALIZATION_EVIDENCE.md`.

## Complex chunks (deferred)

17. VMAD fragment layouts for PACK/INFO/SCEN phase extras are walked from xEdit counters. Type 6 and nonempty type 16 stop structural decoding; read-only collection clears partial VMAD discoveries and master remapping refuses. The verified empty type-16 stored case consumes only its zero count and permits complete structural scanning. Variable authoring, nonempty layouts, third-party fixtures and runtime binding behavior remain gaps tracked as RE-005 on #520. See `VMAD_VARIABLE_EVIDENCE.md`. Encoding of VMAD strings is UTF-8 per xEdit `wbEncodingVMAD`.
18. CTDA CIS1/CIS2 string params. Use Global comparison FormID remap is implemented (type bit 2). ESP flagged light can be stamped via `apply_master_kinds` / `remap_plugin_masters(MasterDescriptor)`; the library still cannot read SmallFile off a master that is only a MAST name.
19. PERK Add Activate Choice
20a. **Answered:** the stored modern version-15 NAVM/NAVI grammar and its native/C read surface are documented in `NAVM_NAVI.md` and decoded by `src/environment/navmesh.cpp:decode_navmesh` / `decode_navmesh_info`. Indexed geometry and fixed info-field edits are shipped; NAVM/NAVI creation and counted-structure edits stay refused.
20b. **Open (#401):** the readings listed in `NAVM_NAVI.md` ("Flags, links and NVPP") are verified as reads, with inferred meanings; sources are #401 comments 9765, 9785, 10005 and 12948, and #520 comment 13148 (RE-012). Partly answered by RE-012: the NVPP index formula and the CK 1.11.137 gate are verified; bit 11 is the island flood-fill visited mark (#520 comment 13394); the door portal, waypoint and cover layouts are partly answered. Still open: 21,632 unexplained cover vertex entries; no OG writer for cover fields 5 and 6; cover labels inferred; waypoint flag consumer not found; why 27,759 shipped triangles have bit 11 clear; bits 4 and 9 not found (bit 4 on 15 triangles, bit 9 on 50); traversal bit names other than 0x08; NG: 25 OG/NG routine pairs matched, OG 0x24FA400 and 0x769E40 unmatched, NG bit-10 sites matched by count only. GardenEngine #146 consumer and probe acceptance remains separate.
20c. DIAL/INFO/SCEN stored fields and scoped condition lists have a read slice in `DIALOGUE_SCENES.md`. Action-specific scene payloads and VMAD fragments remain opaque; runtime response selection and scene execution require [RE #405](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/405). TES4 bit 7 remains question 7.
21. Persistent vs temporary REFR grouping rules on save

## BA2

22. Next-gen v7/v8 GNRL and DX10 differences. **Partial answer (#394):** every installed root-level v1, v7 and v8 GNRL and DX10 archive measured by `tools/corpus/ba2_measure.py` uses the v1 header, entry and chunk field widths, and Radicle reads and writes all three versions (`docs/BA2.md`). Still open: layouts and versions not present in the measured install, the meaning of the entry metadata byte, DX10 flag bits other than the cubemap bit and the tile mode, and how the engine itself treats v7/v8 archives at runtime.

## Writer policy

23. When CK omits default/zero subrecords, should an unmodified native rewrite match CK or match the original bytes? Default: keep the shared source range when the record was not edited.

24. **OG 1.10.163.0 and NG 1.10.984.0 are measured, but not build-verified.** On 2026-10-12 the BA2 versions, TES4 headers and form-version census were read from both installs and agree with AE on the points in `docs/FORMAT_NOTES.md` (Releases) and `docs/BA2.md` (Per release). NG's TES4 header reader is verified by instruction comparison with AE (#520 comment 13470, RE-006 Q2, evidence commit 78e013b2); that static result does not replace the test runs below. Still open: the `HeaderVersionsReal` byte-identical rewrite in `tests/test_header_versions_real.cpp` and the `tests/test_ba2_versions_real.cpp` checks have not run on OG or NG (they need `FO4_DATA_DIR` and a build). The NG `DLCworkshop03 - Voices_en.ba2` copy is incomplete and unreadable, so NG counts exclude it until a complete copy is measured.
