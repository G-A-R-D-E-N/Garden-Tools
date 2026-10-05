# VMAD variable property evidence

Issue [#15](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/15), research [#388](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/388).

Verified record: [runtime note](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/f18bee7912b793b3cd3ed8d47c6f9a45f4d01b3a/RE-Work/subsystems/plugin-io-garden-tools/vmad-variable-types-388/README.md), read on 2026-10-04. Private local copy: `E:/GardenEngine/git/garden-tools/git/radicle-15-20261004/build/RE-Work/vmad-variable-types-388.md`, SHA256 `685d21864fd63c9ffef3740148f1f7dd804ecfe4140f474c3c23328cf41641c2`. This is a Ghidra 12.1.3 decompile record for OG 1.10.163, rather than a live game acceptance test.

The original record describes the OG property reader and the failure of type 6 and nonempty type 16 without a nested discriminator. The subsequent verified record below resolves the zero-count case.

The accompanying survey reports 20,374 VMAD script lists across Fallout4.esm, six DLC masters and nine installed Creation Club plugins, zero parse/decompression failures, and zero type-6/type-16 properties at the top level or inside structs. This does not cover third-party plugins, record-specific fragment tails, other installed game versions or every possible Creation Club file. No absence claim extends beyond that corpus. The survey is existing measured evidence, not a new Radicle corpus test result.

## Empty type-16 stored layout

Verified follow-up: `E:/GardenEngine/git/garden-tools/git/radicle-341-re-followup-20261005/build/RE-Work/6916afb/vmad-388-15/README.md`, pinned research commit `6916afbcbec8ab09ecc0dfd30661502d0064f4f7`. The record has OG byte-level and decompile verification at RVA `0x26e59a0`: an empty type-16 property consumes its name, type, status and a four-byte zero count; its element reader never runs. Nested struct members use the same property reader. AE evidence is decompile-only and partial; NG and CK remain unexamined. This is static evidence, not live game acceptance or proof of runtime binding behavior.

`src/schema/vmad.cpp:walk_property` and `read_property` accept only the zero-count type-16 case. The read model retains raw type 16 and `element_count=0`; it does not turn it into another property type or emulate the runtime's value normalization. Type 6 and nonempty type 16 remain opaque: tolerant FormLink discovery discards all offsets from the affected VMAD and master remapping refuses it. Truncated counts and unknown tails still prevent remapping. No property writer or discriminator is added.

`tests/test_vmad_unsupported.cpp:EmptyVariableArrayPreservesValuesAndRemapsFollowingReferences` covers top-level and nested empty arrays, preceding and following object references, unchanged serialization and edit/write/reparse after master remapping. The payload comparison permits only the two proven reference index bytes to change. `EmptyVariableArrayDoesNotPermitUnknownTailOrTruncatedCountRemap` checks the unsafe boundaries. Existing nonempty/type-6 refusal tests remain.

The measured official corpus contains no type-6/type-16 occurrences, so this edge case has synthetic proof backed by the verified record, not a real-plugin occurrence claim. A named third-party fixture and CK authoring evidence remain separate gaps in #388/#15. Safe creation, addition, removal and nonempty variable mutation are not claimed.
