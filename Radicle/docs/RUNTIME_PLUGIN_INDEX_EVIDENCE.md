# Runtime plugin index evidence

Issue [#28](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/28), remaining research [#389](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/389).

Verified record: [versioned collection note](https://re.nomadicinteractive.dev/ReverseEng/Fallout4_Runtime_Research/src/commit/f18bee7912b793b3cd3ed8d47c6f9a45f4d01b3a/RE-Work/subsystems/plugin-io-garden-tools/small-file-compile-index-389/README.md), read 2026-10-04. Private local copy: `E:/GardenEngine/git/garden-tools/git/radicle-28-20261004/build/RE-Work/small-file-compile-index-389.md`, SHA256 `61c11eed0fadc6bdc69315241864d08aac3062d78b57c21710ccdc723208e1d2`. Ghidra 12.1.3 decompiles and the note's OG/AE disassembly checks are static evidence, not live load-order acceptance.

| Operation | OG 1.10.163 RVA | AE 1.11.240 RVA | Verified behavior |
|---|---|---|---|
| Add to collection | `0x1158c0` | `0x2e6dc0` | Append to separate full/light arrays, assign position starting at zero |
| Full rebuild | `0x116c20` | `0x2e8020` | Walk checked files in supplied list order, active file last, skip existing entries |
| Header classification | `0x12ed70` | `0x2f5eb0` | Header bit `0x200` or case-insensitive `.esl` extension makes the file light |
| Runtime ID conversion | `0x131fa0` | `0x2f91c0` | Light slot occupies bits 12 through 23 under the FE marker |

`build_runtime_form_id_context` already partitions its caller's ordered plugins into independent tables. It now recognizes the verified extension rule as well as the header flag. It leaves stored TES4 flags untouched and reuses `PluginKey::looks_light`. The input must be the caller's enabled, resolved load order, including any active-file positioning. Radicle does not parse Plugins.txt, invent checked/active state, reorder masters or deduplicate silently. Tests cover the extension-only file, flagged `.esm`, independent slot numbering and unchanged header, alongside existing duplicate and overflow refusal tests.

The note finds the same assignment structure in NG 1.10.984 decompiles, but its encrypted image was not byte-verified. No medium/update distinction, additional reserved slot or collection-site limit check was found in the inspected paths; this is not proof of absence throughout the engine. Radicle retains its explicit 254 full / 4096 light caps and duplicate refusal rather than reproducing truncation or wrap. Exact file-list construction, external limits and NG byte verification remain in #389; the context-driven API remains the boundary.

Address correction: OG 1.10.163 uses `0x131fa0`; `0x131f90` belongs to the older 1.10.155 image. The existing build-identification record in `OPEN_QUESTIONS.md` explains the two PE timestamps. Do not treat this as a universal symbol-table shift. CI provides validation for this change; no local build was run.
