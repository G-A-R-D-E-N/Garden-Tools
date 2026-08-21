# Radicle roadmap

Replace Mutagen inside FO4IDE. Canonical identity is `FormKey`, never a raw load-order FormID. Unknown bytes are preserved. Evidence order: FO4 runtime, then CK, then CKRE, then xEdit, then Mutagen as oracle.

GitHub milestones, labels, and issues live on [GARDEN-TOOLS](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/milestones).

## Phases

| Phase | Status | Issues |
|------:|--------|--------|
| 1 Foundation | shipped | [#2](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/2) reader, [#3](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/3) FormKey |
| 2 Preservation writer | shipped | [#4](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/4) writer, [#5](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/5) header remap |
| 3 FormLinks and light plugins | shipped | [#6](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/6) CTDA, [#7](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/7) VMAD, [#8](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/8) ESL/structured |
| 4 FO4IDE surface | **now** | [#9](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/9) C ABI, [#10](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/10) stamp from paths, [#11](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/11) docs, [#12](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/12) CI, [#13](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/13) FO4IDE smoke |
| 5 Schema completeness | next | [#14](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/14)–[#19](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/19) PTDT, VMAD vars, CIS1/2, PERK, NAVM, HEDR |
| 6 BA2 archives | later | [#20](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/20) GNRL, [#21](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/21) DX10/v7/v8 |
| 7 Load order and conflicts | later | [#22](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/22) TESFileCollection, [#23](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/23) graph, [#24](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/24) strings |
| Research backlog | ongoing | [#25](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/25)–[#30](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/30). Do not hardcode guesses in the writer |

## Labels

| Prefix | Meaning |
|--------|---------|
| `type:` | feature, bug, research, docs, infra |
| `area:` | formid, plugin-io, formlinks, vmad, ba2, fo4ide, load-order |
| `p0`–`p3` | blocker through later |
| `tool:radicle` | this SDK |
| `needs-evidence` | wait for runtime/CK/CKRE |
| `status:shipped` | already in tree |

## Rules

- Do not treat `0xFExxxxxx` as on-disk ESL encoding.
- Do not sweep every `uint32` as a FormID.
- Light kind is TES4 SmallFile when known. `.esl` is filename fallback.
- Speculative answers stay in `docs/OPEN_QUESTIONS.md` until proven.
