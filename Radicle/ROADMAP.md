# Radicle roadmap

Radicle is the native xEdit-style Fallout 4 plugin backend for PluginTool / PluginEditTool and GardenEngine. Canonical identity is `FormKey`, never a raw load-order form ID. Unknown bytes are preserved.

Use [#86](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/86) as the product roadmap, [#287](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/287) and [FO4_PARITY.md](docs/FO4_PARITY.md) as the operation coverage matrix, and [#43](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/43) for reusable infrastructure justified by proven gaps. The phases below are a historical foundation outline, not the current completion ledger.

Evidence must establish the stored layout and safe operation. xEdit definitions, official-plugin measurements, CK/CKRE, existing typed paths and synthetic tests can suffice. Runtime evidence takes precedence when relevant, but missing executable RE does not automatically block stored-format work. Require RE for unresolved layouts, unsafe mutation or actual runtime reproduction.

The editing contract comes from the documentation in [#287](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/287): parse -> expose -> edit -> serialize -> reparse, including valid add/remove structure, form ID/master remap, rejected unsafe writes and preservation of untouched fields. Read surfaces may land separately; they do not complete authoring parity.

GitHub milestones, labels, and issues live on [GARDEN-TOOLS](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/milestones).

## Phases

| Phase | Status | Issues |
|------:|--------|--------|
| 1 Foundation | shipped | [#2](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/2) reader, [#3](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/3) FormKey |
| 2 Preservation writer | shipped | [#4](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/4) writer, [#5](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/5) header remap |
| 3 FormLinks and light plugins | shipped | [#6](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/6) CTDA, [#7](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/7) VMAD, [#8](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/8) ESL/structured |
| 4 FO4IDE surface | **now** | [#9](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/9) C ABI, [#10](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/10) stamp from paths, [#11](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/11) docs, [#12](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/12) CI, [#13](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/13) FO4IDE smoke |
| 5 Schema completeness | next | [#14](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/14)–[#19](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/19) PTDT, VMAD vars, CIS1/2, PERK, NAVM (stored read and indexed edits shipped; cover, waypoint and NG layout open, `docs/OPEN_QUESTIONS.md` item 20b), HEDR |
| 6 BA2 archives | shipped | [#20](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/20) GNRL, [#21](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/21) DX10/v7/v8: GNRL and DX10 at v1/v7/v8, measured on installed archives (`docs/BA2.md`); unmeasured layouts and engine behaviour stay in `docs/OPEN_QUESTIONS.md` item 22 |
| 7 Load order and conflicts | later | [#22](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/22) TESFileCollection, [#23](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/23) graph, [#24](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/24) strings |
| Research backlog | ongoing | [#25](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/25)–[#30](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/issues/30). Do not hardcode guesses in the writer |

## Labels

| Prefix | Meaning |
|--------|---------|
| `type:` | feature, bug, research, docs, infra |
| `area:` | formid, plugin-io, formlinks, vmad, ba2, fo4ide, load-order |
| `p0`–`p3` | blocker through later |
| `tool:radicle` | this SDK |
| `needs-evidence` | obtain sufficient evidence for the specific stored-format or runtime operation |
| `status:shipped` | already in tree |

## Rules

- Do not treat `0xFExxxxxx` as on-disk ESL encoding.
- Do not sweep every `uint32` as a FormID.
- Light kind is TES4 SmallFile when known. `.esl` is filename fallback.
- Speculative answers stay in `docs/OPEN_QUESTIONS.md` until proven.
