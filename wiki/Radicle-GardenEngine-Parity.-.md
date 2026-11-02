# Radicle for GardenEngine (Mutagen replacement)

Driver: [GardenEngine #114](https://git.nomadicinteractive.dev/garden-engine/GardenEngine/issues/114). GardenEngine reads plugins through a vendored Mutagen (`GameLoader`, `Mo2ProfileLoader`, `CellService`, `WorldspaceService`, `TerrainService`, `ReferenceWriteback`, `ModelPathResolver`, `TextureService`, `Ba2ExtractorService`, `CellCompiler`, `AssetConverterMain`). **Done in GardenEngine as of 2026-10-03:** the game project now reads plugins through Radicle behind the stable C ABI plus a narrow C# interop layer, and no longer references Mutagen (a verbatim Mutagen copy stays in GardenEngine's parity tests only). It was done one capability at a time, failing closed on anything unproven. See the GardenEngine page [Radicle-Integration](https://git.nomadicinteractive.dev/garden-engine/GardenEngine/wiki/Radicle-Integration.-). Tracking milestone: "Radicle - GardenEngine parity (retire Mutagen)" (id 34).

```mermaid
flowchart LR
  classDef old fill:#f8d7da,stroke:#b02a37,color:#000
  classDef new fill:#d1e7dd,stroke:#0f5132,color:#000
  subgraph Today["Before GardenEngine PR #130"]
    C1["GardenEngine consumers: GameLoader, Mo2ProfileLoader, CellService, WorldspaceService, TerrainService, ReferenceWriteback, ModelPathResolver, TextureService, Ba2ExtractorService, CellCompiler, AssetConverterMain"] --> Mut["Vendored Mutagen"]:::old
  end
  subgraph Target["After PR #130"]
    C2["The same consumers"] --> Interop["Narrow C# interop layer with a layout probe: #331"]:::new
    Interop --> Abi["Stable C ABI: radicle_env_*, #322"]:::new
    Abi --> Core["Radicle core"]:::new
  end
```

Both paths existed side by side during the move. After the migration GardenEngine's parity tests compare Radicle against Mutagen on real data: 13 of 13 passed on 2026-10-03 (98 minutes, profile Fallen World, local run, not CI).

## The stack (all merged)

Updated **2026-10-03**. The twelve slices were stacked pull requests, each based on the previous one. They merged into `main` in order between **2026-09-29 and 2026-10-02**, and GardenEngine pinned the result: [GardenEngine PR #130](https://git.nomadicinteractive.dev/garden-engine/GardenEngine/pulls/130) (merged 2026-10-03) builds Radicle at GARDEN-TOOLS `main` commit `2f9d4f51`, Radicle C ABI 3.

```mermaid
flowchart TD
  classDef done fill:#d1e7dd,stroke:#0f5132,color:#000
  classDef side fill:#e9ecef,stroke:#6c757d,color:#000
  M["main"]
  P322["#322 environment + C ABI, 09-29"]:::done
  P323["#323 WRLD / CELL, 09-29"]:::done
  P324["#324 placed refs + model paths, 09-29"]:::done
  P325["#325 LAND / LTEX / TXST, 09-29"]:::done
  P326["#326 string tables, 09-30"]:::done
  P327["#327 index-only BA2, 09-30"]:::done
  P328["#328 writeback, FormID remap, 10-01"]:::done
  P329["#329 writeback field edits, 10-01"]:::done
  P330["#330 LIGH BNDS LAYR LGTM CLMT WTHR SCOL, 10-01"]:::done
  P331["#331 C# bindings + layout probe, 10-01"]:::done
  P333["#333 load metrics, 10-02"]:::done
  P336["#336 parity fixes and tools, 10-02"]:::done
  M --> P322 --> P323 --> P324 --> P325 --> P326 --> P327 --> P328 --> P329 --> P330 --> P331 --> P333 --> P336
  P337["#337 QUST partial-form VMAD fix, 09-30"]:::done
  P314["#314 HEDR record-count acceptance, 09-30"]:::done
  P347["#347 environment and placed-reference repair, 09-29"]:::done
  P349["#349, 10-01"]:::done
  P277["#277 C ABI version probe: closed unmerged"]:::side
  M --> P337
  M --> P314
  M --> P347
  M --> P349
```

Merge dates are from each pull request page. #348 merged into a work branch rather than `main`. #335 (Radicle MSVC Debug CI) and #332 (`<algorithm>` include fix) were still open when this was checked and are not part of the stack.

## What each slice replaces

| Slice | Radicle surface | GardenEngine code it stands in for (from #114) |
| --- | --- | --- |
| #322 | `Environment`: ordered plugin loading, winner and all versions by FormKey, EditorID lookup, per-signature enumeration, plugin info, generic record metadata; `radicle_env_*` C ABI | `GameLoader`, `Mo2ProfileLoader`, the `ILinkCache` role |
| #323 | WRLD, CELL hierarchy, top cell, exterior/interior cells, merged cell children, winning landscape | `WorldspaceService`, `CellService`, `CellCompiler` |
| #324 | REFR/ACHR and traps, all placed-ref fields, model-path resolution | `CellService`, `ModelPathResolver`, `AssetConverterMain` |
| #325 | `decode_landscape` (heights, normals, colours, layers, VTXT), LTEX/TXST, diffuse walk | `TerrainService`, terrain texture baking |
| #326-#327 | string tables and localized names; BA2 index/extraction with asset precedence | `TextureService`, `Ba2ExtractorService`, localized name lookups |
| #328-#329 | override creation, transactional field edits, FormID remap | `ReferenceWriteback` |
| #330 | LIGH, BNDS, LAYR, LGTM, CLMT `WLST`, WTHR `NAM0` colours and `FNAM` fog, SCOL parts | light, climate and static-collection reads |
| #331, #333 | C# bindings with a layout probe; load metrics | the interop boundary; performance evidence |
| #336 | fixes found by the 644-plugin parity runs, plus `radicle_parity_dump` / `radicle_parity_writeback` | acceptance tooling |

The same table as a graph, from the GardenEngine consumer back to the slice that replaces it:

```mermaid
flowchart LR
  classDef wip fill:#d1e7dd,stroke:#0f5132,color:#000
  S322["#322 environment and C ABI"]:::wip --> G1["GameLoader, Mo2ProfileLoader, ILinkCache role"]
  S323["#323 WRLD and CELL"]:::wip --> G2["WorldspaceService, CellService, CellCompiler"]
  S324["#324 placed refs"]:::wip --> G3["CellService, ModelPathResolver, AssetConverterMain"]
  S325["#325 terrain"]:::wip --> G4["TerrainService, terrain texture baking"]
  S326["#326 strings and #327 BA2"]:::wip --> G5["TextureService, Ba2ExtractorService, localized names"]
  S328["#328 and #329 writeback"]:::wip --> G6["ReferenceWriteback"]
  S330["#330 light, climate, SCOL getters"]:::wip --> G7["Light, climate and static-collection reads"]
  S331["#331 bindings and #333 metrics"]:::wip --> G8["The interop boundary and performance evidence"]
  S336["#336 parity fixes and tools"]:::wip --> G9["Acceptance tooling"]
```

## Evidence recorded in the PRs (as of 2026-10-02)

- Cumulative #322-#333 stack, Windows clang-cl 19.1.5, Debug, `/W4 /WX`: **587 passed, 25 skipped, 0 failed** (612 CTest entries) (#330, 2026-09-26). Not a build of each PR head on its own.
- Installed game (Fallout 4 1.11.240): loaded 7 masters, 481,949,992 bytes, 2,192,230 records and 21 archives in about 61.7 s, peak RSS about 6.15 GB (#330).
- Parity runs (#336): versions, cells, placed refs, worlds, LAND and names match Mutagen row for row except two chains where Mutagen invents records; Radicle opens all 414 archives where Mutagen fails on 109; all 10 written references decode identically (only difference: a `-0.0` teleport rotation Radicle preserves). Local MSVC build ctest 615/615 (real-data tests skipped).
- CI: Forgejo runs on stack heads were "waiting" when the PR descriptions were last updated; no exact-head CI pass is claimed in those descriptions.

## Not covered yet (filed as issues)

| Gap | Feeds GardenEngine | Issue |
| --- | --- | --- |
| NAVM / NAVI geometry | #146 navmesh | [#339](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/339) |
| SNDR / SOUN / MUSC / FSTP / FSTS | #144 audio | [#340](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/340) |
| WATR | #147 water | [#341](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/341) |
| GRAS / TREE / FLOR, LTEX grass link | #148 vegetation | [#342](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/342) |
| CLMT sun timing, WTHR `DATA`, stable weather API | #145 weather | [#343](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/343) |
| DIAL / INFO / SCEN | #150 quest and dialogue | [#344](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/344) |
| PACK / CSTY / IDLE | #151 NPC behaviour | [#345](https://git.nomadicinteractive.dev/GARDEN/GARDEN-TOOLS/issues/345) |

```mermaid
flowchart LR
  classDef gap fill:#e9ecef,stroke:#6c757d,color:#000
  classDef ge fill:#dbeafe,stroke:#1d4ed8,color:#000
  I339["#339 NAVM and NAVI"]:::gap --> E146["GardenEngine #146 navmesh"]:::ge
  I340["#340 SNDR, SOUN, MUSC, FSTP, FSTS"]:::gap --> E144["GardenEngine #144 audio"]:::ge
  I341["#341 WATR"]:::gap --> E147["GardenEngine #147 water"]:::ge
  I342["#342 GRAS, TREE, FLOR, LTEX grass link"]:::gap --> E148["GardenEngine #148 vegetation"]:::ge
  I343["#343 CLMT sun timing, WTHR DATA, weather API"]:::gap --> E145["GardenEngine #145 weather"]:::ge
  I344["#344 DIAL, INFO, SCEN"]:::gap --> E150["GardenEngine #150 quest and dialogue"]:::ge
  I345["#345 PACK, CSTY, IDLE"]:::gap --> E151["GardenEngine #151 NPC behaviour"]:::ge
```

Already on `main` and not part of the stack: QUST stages, objectives and aliases with real-plugin tests, and a read-only WTHR decoder marked `CONCEPT-LEVEL / RE-VERIFIED`.

## Risks

- **Stack depth (resolved).** The twelve linked branches all merged between 2026-09-29 and 2026-10-02.
- **Mutagen removal (done for the game project).** Remaining items are the proof points GardenEngine #114 still tracks, such as next-generation BA2 archives, which the BA2 parity test may not have covered separately.
- **New readers.** NAVM, sound, WATR, grass, weather timing, dialogue and package semantics are still open (#339 to #345) and block the GardenEngine systems listed above.
- **Licence.** GardenEngine's GPL-3.0 rationale is the vendored Mutagen; see GardenEngine audit #143.
