# TES4 localization flag evidence

Partial work for [#29](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/29); remaining research [#390](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/390).

Verified source: [OG loader and CK record](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/f18bee7912b793b3cd3ed8d47c6f9a45f4d01b3a/RE-Work/subsystems/plugin-io-garden-tools/tes4-localized-flag-390/README.md), read 2026-10-04. Private copy: `E:/GardenEngine/git/garden-tools/git/radicle-29-20261004/build/RE-Work/tes4-localized-flag-390.md`, SHA256 `7405934a8f6eb0b89542d41282fc4459ee2f8533caed998321a8d7d91ccf8329`. The record carries Ghidra 12.1.3 OG 1.10.163 decompile/disassembly addresses. This is static evidence, not live game acceptance.

| OG RVA | Observed operation |
|---|---|
| `0x132100` | Read runtime flags bit 7 |
| `0x118750`, `0x1b3db0` | Open string-table readers when that bit is set |
| `0x1b30b0` | Clear bit: copy inline chunk text. Set bit: read a four-byte ID; zero selects empty text, otherwise look up the STRINGS table |
| `0x1b37a0`, `0x1b3520` | ILSTRINGS and DLSTRINGS readers use the same branch direction |

The named installed survey covers Fallout4.esm and all six DLC masters with flags `0x81`, plus nine CC ESL files with flags `0x281`. Every sampled FULL is a four-byte ID; no official bit-clear plugin was available. The survey samples FULL fields, rather than proving every text field in every record. The clear-bit direction comes from the executable reader.

The record's headline calls CommonLib's `kDelocalized` a misnomer, but its own CK assertion uses "Delocalized files" for the same external-table state. That does not prove either naming convention wrong. Radicle retains its existing `is_localized` API and serialized bit without renaming CommonLib or changing upstream code.

`schema::read_localized_string` already combines the flag with the localized-field schema. The added tests classify the same four-byte payload in both states and preserve a zero ID. Payload size alone cannot establish membership. Resolving an ID to language-specific text still requires a caller-supplied table; no filesystem or language inference is added. Existing EYES editing remains unchanged.

The 6916afb follow-up is available locally at `E:/GardenEngine/git/garden-tools/git/radicle-341-re-followup-20261005/build/RE-Work/6916afb/localized-strings-390-29/README.md`. CK 1.11.137 decompiles at `0x922bf0` / `0x922560` and their setter calls establish that delocalization sets bit `0x80`. The `0x9235e0` writer stores four-byte IDs from tagged inline text while that mode is active. This is a static direction proof; table emission, language ownership and every writer suppression condition were not established. CK 1.11.240 has matching setter/call-site bytes but its delocalizer bodies were not decompiled.

AE 1.11.240 predicate `0x2f9380` and reader `0x3517c0` establish the same set-ID / clear-inline direction as OG. The record identifies a locally authored bit-clear `CommonwealthOnline.esp` with inline FULL fields; it is not an official or delocalizer-output sample and is not part of this branch's test corpus. NG has predicate/header-import byte evidence from an unpacked image, but its string reader remains unresolved. No live runtime or CK transformation was executed.

`schema::read_localized_string` needs no runtime emulation change for these answers. Field-aware string edits and explicit sidecar ownership remain stored-format acceptance work under #287/#32; unresolved runtime/CK details stay in #390 without blocking an otherwise proven stored edit. Regression-test execution is recorded per PR head; these private surveys are supporting evidence, not execution of Radicle tests.
