# VMAD variable property evidence

Issue [#15](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/15), research RE-005 on #520.

Verified record: [runtime note](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/f18bee7912b793b3cd3ed8d47c6f9a45f4d01b3a/RE-Work/subsystems/plugin-io-garden-tools/vmad-variable-types-388/README.md), read on 2026-10-04. This is a Ghidra 12.1.3 decompile record for OG 1.10.163, rather than a live game acceptance test.

The original record describes the OG property reader and the failure of type 6 and nonempty type 16 without a nested discriminator. The subsequent verified record below resolves the zero-count case.

The accompanying survey reports 20,374 VMAD script lists across Fallout4.esm, six DLC masters and nine installed Creation Club plugins, zero parse/decompression failures, and zero type-6/type-16 properties at the top level or inside structs. This does not cover third-party plugins, record-specific fragment tails, other installed game versions or every possible Creation Club file. No absence claim extends beyond that corpus. The survey is existing measured evidence, not a new Radicle corpus test result.

## Empty type-16 stored layout

Verified follow-up: pinned research commit `6916afbcbec8ab09ecc0dfd30661502d0064f4f7`. The record has OG byte-level and decompile verification at relative virtual address `0x26e59a0`: an empty type-16 property consumes its name, type, status and a four-byte zero count; its element reader never runs. Nested struct members use the same property reader. This is static evidence, not live game acceptance or proof of runtime binding behavior.

The stored value of a zero-count type 16 is the None variant (type word 0), not a typed empty array. This corrects the earlier survey wording (OG bytes, RE-005 on #520 comments 7897 and 9942). The same holds for a zero-count type-16 struct member.

Cross-build status of the empty type-16 path:
- OG 1.10.163: verified in bytes and decompile.
- AE 1.11.240: verified in bytes (research comment 9706). The scalar switch jump-table entry for type 6 falls to the default failure path, and AE has no case-6 reader.
- NG 1.10.984: the VMAD chain is verified on the clean unpacked build (research comment 9942): header, script and property readers, the type gate, the scalar reader with no case 6, and the tail dispatcher. The bind step, the tail-reader bodies and the struct reader remain open.
- Creation Kit: not examined for this path.

Refusal covers the whole VMAD. A type 6 or nonempty type 16 failure ends the property loop of its script. The reader does not resynchronize, so later scripts and the fragment tail are read from misaligned bytes. `read_vmad_scripts` stops at the first unsupported property, and `vmad_coverage` and `vmad_formid_offsets_for_remap` refuse the whole VMAD (the same rule applies to struct members).

The 8-plugin third-party sample (research comment 9942) contains no type-6 or type-16 properties. This is a small sample, not a survey of third-party plugins.

`src/schema/vmad.cpp:walk_property` and `read_property` accept only the zero-count type-16 case. The read model retains raw type 16 and `element_count=0`; it does not turn it into another property type or emulate the runtime's value normalization. Type 6 and nonempty type 16 remain opaque: tolerant FormLink discovery discards all offsets from the affected VMAD and master remapping refuses it. Truncated counts and unknown tails still prevent remapping. These empty-array changes add no variable-property writer or nested discriminator.

`tests/test_vmad_unsupported.cpp:EmptyVariableArrayPreservesValuesAndRemapsFollowingReferences` covers top-level and nested empty arrays, preceding and following object references, unchanged serialization and edit/write/reparse after master remapping. The payload comparison permits only the two proven reference index bytes to change. `EmptyVariableArrayDoesNotPermitUnknownTailOrTruncatedCountRemap` checks the unsafe boundaries. Existing nonempty/type-6 refusal tests remain. The fixtures for the refusal and tail rules are `UnsupportedPropertyTypesRefuseMasterRemap` (type 16 with count 1), `VariablePropertyStopsTheScriptAndLeavesFollowingBytesUninterpreted` (type 6 followed by an int), `VariablePropertyRefusesTheWholeVmadIncludingLaterScriptsAndTail` (type 6 in the first of two scripts), and `EmptyVariableArrayThenTailParsesTheTailAsTrailingBytes`. The nested empty-array case is `EmptyVariableArrayPreservesValuesAndRemapsFollowingReferences` with `nested` set.

The measured official corpus contains no type-6/type-16 occurrences, so this edge case has synthetic proof backed by the verified record, not a real-plugin occurrence claim. A named third-party fixture and CK authoring evidence remain separate gaps tracked as RE-005 on #520 and in #15. Safe creation, addition, removal and nonempty variable mutation are not claimed.
