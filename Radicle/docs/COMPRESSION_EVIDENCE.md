# Compressed records and extended subrecord evidence

Partial work for [#30](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/30); remaining research [#391](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/391).

Verified source: [versioned inflater and chunk record](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/f18bee7912b793b3cd3ed8d47c6f9a45f4d01b3a/RE-Work/subsystems/plugin-io-garden-tools/decompress-current-form-391/README.md), read 2026-10-04. Private copy: `E:/GardenEngine/git/garden-tools/git/radicle-30-20261004/build/RE-Work/decompress-current-form-391.md`, SHA256 `b5a3f11f90134f49699b8cc1f056d13e562eb2b06ba01cbc55901c2463c6a8c5`. It carries real Ghidra 12.1.3 OG 1.10.163 decompiles; older PDB names are naming evidence only.

| OG RVA | Verified operation |
|---|---|
| `0x132500` | Read a declared output size followed by compressed bytes; initialize the inflater and require stream end |
| `0x1b58820`, `0x1b58730` | Initialize zlib with window bits 15, enforcing its wrapper and Adler32 checksum |
| `0x132830`, `0x131730` | Consume `XXXX`, advance to the actual header and use the extended size |
| `0x131d70` | Emit `XXXX` and a zero size16 actual header for payloads at least `0x10000` bytes |

Raw deflate and gzip are not selected by the inspected OG initialization. Radicle removes its raw-deflate retry for nonzero declared-size records, throws a parse error on rejected framing/checksum, and continues to emit zlib when rebuilding. It does not reproduce the runtime's unsafe failure path. The existing zero-size early return is unchanged and not claimed as verified framing enforcement.

The executable does not compare total output with the declared size after success in this function. A smaller buffer fails, while a larger declaration can succeed. This change does not invent exact-length equality or change that existing policy. Other callers and the additional load gate remain unexamined.

The source survey reports 76,895 compressed records across 16 named installed plugins: 56,399 Fallout4, 463 Robot, 50 workshop01, 11,474 Coast, 5 workshop02, 131 workshop03, 8,219 NukaWorld, and 154 across nine CC files. All were zlib framed, inflated to the declared size, and had zero raw streams or failures. The installed data version was not independently pinned to the OG executable. These measurements are source evidence, not execution of the new Radicle tests.

Tests reject a raw stream stripped from a valid zlib frame, a gzip-wrapped copy of that stream, and a corrupted Adler32. A compressed extended-payload test verifies both the retained physical marker and byte-identical writing. The corpus test reads each of the seven named official masters, checks every compressed record's framing, and verifies unmodified plugin bytes with the plugin-level shortcut cleared. Bethesda bytes are not committed; `FO4_DATA_DIR` unset skips the test.

The OG chunk-header path hides `XXXX` from logical runtime readers. Radicle's low-level physical subrecord list intentionally retains it for byte preservation and marks the following subrecord as extended. No API change or guessed runtime emulation is needed.

AE 1.11.240 is only a structural cross-check: its inflater wrapper's window bits were not opened. NG behavior, the additional load gate, zero-size handling, external output-length checks and a live malformed-record test remain unresolved in #391. CI supplies validation for this change; no local build or test run was performed.
