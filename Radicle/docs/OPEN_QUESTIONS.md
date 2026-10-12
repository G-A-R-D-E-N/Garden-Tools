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
4. **Answered, with an open caveat (see 6b).** `TESFile::AdjustFormIDFileIndex` (OG 1.10.163 address `0x131fa0`, `RE-Work/Runtime/03-FINDINGS.md`) masks a light plugin's local to 12 bits *destructively* before OR-ing in the slot, so anything above `0xFFF` silently aliases onto another record. From the encoding, usable range is `0x001`–`0xFFF`; the `0x800` floor belongs to the full-plugin encoding only. Whether light plugins pass through `AddCompileIndex`, which would apply that floor, is open (6b). Six of the nine shipped ESLs allocate below `0x800`, and `ccFSVFO4007-Halloween.esl` has 2078 own records, disproving the `0x800`–`0xFFF`/2048 folklore. See `ESL_FORMIDS.md`.
5. **Partial answer:** OG 1.10.163 and AE 1.11.240 statically establish separate full/light array positions and file-list construction, including the master-flag-only hoist and checked-state sources. `.esl` extension or header SmallFile flag selects the light array. NG 1.10.984 is decompile-only; external limits and live dependency behavior remain #389 work. Radicle retains caller-supplied order and safe caps; these runtime questions do not block proven stored-master edits. See `RUNTIME_PLUGIN_INDEX_EVIDENCE.md`. How NG assigns `smallFileCompileIndex` vs OG 1.10.163 is still open in [#28](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/28).
6. Can a plugin list more than 254 masters, and what happens?
6b. Do light plugins pass through `AddCompileIndex` (`0x1549f0`), which leaves IDs below `0x800` unremapped as engine-reserved? If so, own IDs `0x001`–`0x7FF` in a light plugin would not resolve as the plugin's own forms. **Unanswered.** RE request filed: `re-requests/2026-10-09-esl-light-floor-and-hedr-next.md`, question 4.
6c. Does the game or the Creation Kit read the HEDR next object ID for a light plugin? **Unanswered.** RE request filed: `re-requests/2026-10-09-esl-light-floor-and-hedr-next.md`, questions 2 and 3.

## HEDR

0. HEDR `formCount` appears to be (major records excluding TES4) + (all GRUPs, nested included). Heather and DLCworkshop01 match. Still want an ESL and a WRLD-heavy plugin before the writer emits this field.

## TES4 flags

7. **Answered for stored classification and OG/AE readers:** bit `0x80` set selects external IDs; clear selects inline text. CK uses "delocalized" for the same external state. See `TES4_LOCALIZATION_EVIDENCE.md`; naming does not change serialized meaning.
8. Are TES4 record flags and `TESFile::flags` the same bit layout? CommonLib treats them as the same enum.

## Compression / XXXX

9. **Answered for stored framing:** official compressed records and OG/AE init-wrapper evidence use zlib framing. This branch removes the raw-deflate retry for nonzero declared sizes. Radicle loads undecodable compressed content as an undecodable record with raw bytes kept (GT-205 PR1). NG window bits and live malformed-input behavior remain separate #391 questions; see `COMPRESSION_EVIDENCE.md`.
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
16. **Partial answer:** CK 1.11.137 delocalization sets bit `0x80` and its active string-writer mode stores four-byte IDs from tagged inline text. CK table emission/language ownership, NG string-reader semantics and live behavior remain #390 work. Proven stored editing does not require runtime reproduction. See `TES4_LOCALIZATION_EVIDENCE.md`.

## Complex chunks (deferred)

17. VMAD fragment layouts for PACK/INFO/SCEN phase extras are walked from xEdit counters. Type 6 and nonempty type 16 stop structural decoding; read-only collection clears partial VMAD discoveries and master remapping refuses. The verified empty type-16 stored case consumes only its zero count and permits complete structural scanning. Variable authoring, nonempty layouts, third-party fixtures and runtime binding behavior remain gaps in [#388](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/388). See `VMAD_VARIABLE_EVIDENCE.md`. Encoding of VMAD strings is UTF-8 per xEdit `wbEncodingVMAD`.
18. CTDA CIS1/CIS2 string params. Use Global comparison FormID remap is implemented (type bit 2). ESP flagged light can be stamped via `apply_master_kinds` / `remap_plugin_masters(MasterDescriptor)`; the library still cannot read SmallFile off a master that is only a MAST name.
19. PERK Add Activate Choice
20. NAVM / NAVI: stored modern version-15 grammar and native/C read surface are documented in `NAVM_NAVI.md` (`src/environment/navmesh.cpp:decode_navmesh` / `decode_navmesh_info`). Remaining traversal/flag, cover/waypoint and NG semantics stay open in #401; GardenEngine #146 consumer/probe acceptance remains separate.
20b. DIAL/INFO/SCEN stored fields and scoped condition lists have a read slice in `DIALOGUE_SCENES.md`. Action-specific scene payloads and VMAD fragments remain opaque; runtime response selection and scene execution require [RE #405](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/405). TES4 bit 7 remains question 7.
21. Persistent vs temporary REFR grouping rules on save

## BA2

22. Next-gen v7/v8 GNRL and DX10 differences. **Partial answer (#394):** every installed root-level v1, v7 and v8 GNRL and DX10 archive measured by `tools/corpus/ba2_measure.py` uses the v1 header, entry and chunk field widths, and Radicle reads and writes all three versions (`docs/BA2.md`). Still open: layouts and versions not present in the measured install, the meaning of the entry metadata byte, DX10 flag bits other than the cubemap bit and the tile mode, and how the engine itself treats v7/v8 archives at runtime.

## Writer policy

23. When CK omits default/zero subrecords, should an unmodified native rewrite match CK or match the original bytes? Default: keep the shared source range when the record was not edited.
