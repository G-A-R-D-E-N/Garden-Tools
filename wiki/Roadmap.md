# Roadmap

Board: [GARDEN-TOOLS Roadmap](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/projects/2). Columns are tools and areas; status is on each card (`status:*`, `p0`-`p3`, open/closed). Findings and cleanup checklist: [#338](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/338).

## Milestones

| Tool | Milestone | Scope |
| --- | --- | --- |
| Radicle | Phase 1-3 (closed) | Foundation, preservation writer, FormLinks and light plugins: shipped |
| Radicle | Phase 4 FO4IDE surface | 0 open, 5 closed, still open; `Radicle/ROADMAP.md` still calls it "now" |
| Radicle | Phase 5 Schema completeness | PTDT/PDTO, VMAD variables, CIS1/2, NAVM, PERK extras, HEDR (#15, #19, #314) |
| Radicle | Phase 6 BA2 archives | DX10, next-gen v7/v8 (#21) |
| Radicle | Phase 7 Load order and conflicts | 0 open, 3 closed, still open |
| Radicle | Research backlog | #26-#30, must be answered from runtime/CK evidence |
| Radicle | GardenEngine parity (retire Mutagen) | delivered: the 12-PR stack merged 2026-09-29 to 2026-10-02 and GardenEngine #130 merged 2026-10-03; open: reader gaps #339-#345; see [Radicle-GardenEngine-Parity](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/wiki/Radicle-GardenEngine-Parity.-) |
| PluginEditTool | Native rewrite and hardening | #86, #182 (retire C#), #82, #43, #37, #41, #77, #81; PRs #316, #318-#321 |
| PluginEditTool | xEdit parity | #275, #287 umbrellas, #283, #285, #83, #84, #40, #39, #33, #32, #56; PRs #312, #315, #317 |
| PluginEditTool | CK-free precombine and previs | #80, #34, #35 |
| PluginEditTool | Expansion tools | #47 Nexus lookup, #49 F4SE DLL audit, #51 dialogue validator, #78 Papyrus fidelity |
| BGS | v1.1.x, v1.2, v1.3, Editor surface, Havok interop | see below |
| F4LODGEN | v0.1, v0.2, v0.3, v0.4-v1.0 | see below |

## Radicle phases

```mermaid
flowchart TB
  classDef done fill:#d1e7dd,stroke:#0f5132,color:#000
  classDef part fill:#fff3cd,stroke:#b58900,color:#000
  classDef todo fill:#e9ecef,stroke:#6c757d,color:#000
  P1["1 Foundation: shipped"]:::done --> P2["2 Preservation writer: shipped"]:::done
  P2 --> P3["3 FormLinks and light plugins: shipped"]:::done
  P3 --> P4["4 FO4IDE surface: 5 closed, 0 open, milestone still open"]:::done
  P4 --> P5["5 Schema completeness: PTDT, VMAD variables, CIS1 and CIS2, NAVM, PERK extras, HEDR"]:::todo
  P5 --> P6["6 BA2 archives: GNRL done, DX10 and v7 and v8 open"]:::part
  P6 --> P7["7 Load order and conflicts: 3 closed, milestone still open"]:::done
  P7 -.-> Par["GardenEngine parity stack merged, reader gaps #339 to #345 open"]:::part
  Res["Research backlog #26 to #30: answered from runtime and CK evidence, never guessed"]:::todo
```

Phase order is the roadmap's numbering, not a strict dependency chain. `Radicle/ROADMAP.md` still marks phase 4 as "now" and phases 6 and 7 as "later", while the Radicle README lists the BA2 GNRL reader and the load-order and conflict graph as implemented. The roadmap file is behind the README, and the milestone counts above are from the board.

## F4LODGEN chain

`F4LODGEN/docs/ROADMAP.md` (last updated 2026-08-18) numbers its issues #1-#19 from before the move. Current numbers in this repository:

| Roadmap # | Current # | Phase |
| --- | --- | --- |
| 1 | [#36](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/36) | v0.1 release tracker |
| 2 | #38 | 0: Clang CI gate |
| 3 | #42 | 0: archive precedence |
| 19 | #69 | 1: terrain source semantics parity gate |
| 4, 5 | #44, #45 | 2: BTR calibration and parity |
| 6, 7 | #46, #48 | 3: terrain textures, atomic output |
| 8, 9 | #50, #52 | 4: object asset foundation, candidate graph |
| 10, 11 | #53, #55 | 5: BTO writer, culling and materials |
| 12 | #57 | 6: object texture compiler |
| 13 | #59 | 7: generate, cache, manifest |
| 14 | #61 | 8: desktop UI |
| 15 | #63 | 9: v0.1 acceptance and release |
| 16, 17, 18 | #64, #66, #67 | v0.2, v0.3, v0.4-v1.0 |

```mermaid
flowchart TD
  I38["#38 Clang CI"] --> I42["#42 archive precedence"]
  I38 --> I69["#69 terrain semantics gate"]
  I69 --> I44["#44 BTR calibration"]
  I44 --> I45["#45 terrain BTR parity"]
  I45 --> I46["#46 terrain textures"]
  I46 --> I48["#48 atomic terrain output"]
  I42 --> I50["#50 object asset reader"]
  I50 --> I52["#52 candidate graph"]
  I52 --> I53["#53 BTO writer"]
  I53 --> I55["#55 culling + materials"]
  I55 --> I57["#57 object textures"]
  I48 --> I59["#59 generate / cache / manifest"]
  I57 --> I59
  I59 --> I61["#61 desktop UI"]
  I59 --> I63["#63 v0.1 release"]
  I63 --> I64["#64 v0.2 missing LOD models"]
  I64 --> I66["#66 v0.3 patch writer"]
  I66 --> I67["#67 v0.4 to v1.0"]
```

Edges are the roadmap's own "Dependency chain" section with numbers translated. The roadmap says phases 0-2 are in progress, but no open pull request mentions F4LODGEN, so the true state needs confirming.

## BGS

Milestones (existing): v1.1.x stability and polish (1.1.0 is shipped; #309, #310, #311, #313 are open), v1.2 character and skinning authoring (#126 body-part/VATS regions, #127 character/race workspace), v1.3 skeleton, ragdoll and physics (#117 Ragdoll Studio, #118 ragdoll preview, #265), Havok interop (#116 semantic version conversion, #119 scene container, #334 spline reader bug), Editor authoring surface. PR [#161](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/pulls/161) bundles skeletons, skin weights, retargeting, ragdolls, a NIF writer and Havok tagfiles across v1.2 and v1.3 and has no milestone.

```mermaid
flowchart LR
  classDef done fill:#d1e7dd,stroke:#0f5132,color:#000
  classDef todo fill:#e9ecef,stroke:#6c757d,color:#000
  classDef pr fill:#dbeafe,stroke:#1d4ed8,color:#000
  V110["1.1.0: released"]:::done --> V11x["v1.1.x stability and polish: #309, #310, #311, #313 open"]:::todo
  V11x --> V12["v1.2 character and skinning authoring: #126 body-part and VATS regions, #127 character and race workspace"]:::todo
  V12 --> V13["v1.3 skeleton, ragdoll and physics: #117 Ragdoll Studio, #118 ragdoll preview, #265"]:::todo
  Hav["Havok interop: #116 version conversion, #119 scene container, #334 spline reader bug"]:::todo
  Ed["Editor authoring surface"]:::todo
  P161["PR #161: skeletons, skin weights, retargeting, ragdolls, NIF writer, Havok tagfiles. No milestone"]:::pr
  P161 -.-> V12
  P161 -.-> V13
```

Version order follows the milestone names; the board states no dependencies between the BGS milestones, so Havok interop and the editor surface are drawn unconnected.

## Open decisions

| Decision | Issue |
| --- | --- |
| Adopt `Mutagen.Bethesda.Analyzers` for the Problems drawer, or not, given #182 retires Mutagen | #31 |
| Split or place PR #161 | #338 |
| Merge order for the Radicle stack | #338 (resolved: merged in order 2026-09-29 to 2026-10-02) |
| Shared plugin and BA2 layer across tools | [#346](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/346) |

```mermaid
flowchart LR
  D346["#346 shared plugin and BA2 layer"] --> RAD["Radicle"]
  D346 --> F4["F4LODGEN"]
  D346 --> BGS["BGS"]
  D31["#31 Mutagen analyzers for the Problems drawer"] --> PET["PluginEditTool"]
  D182["#182 retire the C# editor and Mutagen"] --> PET
  D31 -.->|"conflicts with"| D182
  D338a["#338 merge order for the Radicle stack: done"] --> STK["Radicle parity stack #322 to #336, merged"]
  D338b["#338 split or place PR #161"] --> P161b["BGS PR #161"]
```

The dotted edge is the tension noted in the table: adopting Mutagen analyzers is questionable while #182 retires Mutagen.
