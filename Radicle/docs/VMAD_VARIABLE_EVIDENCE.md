# VMAD variable property evidence

Issue [#15](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/15), research [#388](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/388).

Verified record: [runtime note](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/f18bee7912b793b3cd3ed8d47c6f9a45f4d01b3a/RE-Work/subsystems/plugin-io-garden-tools/vmad-variable-types-388/README.md), read on 2026-10-04. Private local copy: `E:/GardenEngine/git/garden-tools/git/radicle-15-20261004/build/RE-Work/vmad-variable-types-388.md`, SHA256 `685d21864fd63c9ffef3740148f1f7dd804ecfe4140f474c3c23328cf41641c2`. This is a Ghidra 12.1.3 decompile record for OG 1.10.163, rather than a live game acceptance test.

The OG property reader at RVA `0x26e59a0` reads name, type and status. Array types 11 through 17 read a u32 count and call the scalar value reader with type minus ten. The scalar reader at RVA `0x26e5db0` supports 0 through 5 and 7; type 6 takes its failure path without reading a nested discriminator. A nonempty type-16 array therefore fails at its first element. The record describes possible success for count zero as a static inference; it remains untested and is not enabled in Radicle.

The accompanying survey reports 20,374 VMAD script lists across Fallout4.esm, six DLC masters and nine installed Creation Club plugins, zero parse/decompression failures, and zero type-6/type-16 properties at the top level or inside structs. This does not cover third-party plugins, record-specific fragment tails, other installed game versions or every possible Creation Club file. No absence claim extends beyond that corpus. The survey is existing measured evidence, not a new Radicle corpus test result.

`src/schema/vmad.cpp:skip_value` retains its refusal for both types. Read-only FormLink discovery discards all offsets from the affected VMAD; master remapping fails closed; unchanged plugin storage preserves the original bytes. No payload bytes or nested discriminator are guessed. `tests/test_vmad_unsupported.cpp` covers top-level and nested properties, preceding known references, unchanged bytes, remap refusal, and the deliberately unsupported zero-count array. CI supplies the test result; no local build was run.

Remaining work in #388: obtain a named third-party fixture if one exists, verify the zero-count path and CK writer behavior, and establish version-specific behavior beyond OG before changing this policy. #15 remains open.
