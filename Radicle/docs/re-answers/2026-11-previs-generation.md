# Previs / precombine generation (#80, #1093 Part 1)

Answers synthesized from shipped vanilla 1.11.240 data (PR #1066 real-data gates), `wiki/RE-Tracking.md` RE-003, and the in-repo `precombine_plan` tool. CK byte-oracle fixtures belong under `Radicle/testdata/previs/oracles/`; regenerate with `tools/previs/oracle_capture.ps1` when Creation Kit is available.

## Q12 (oracles)

| Artifact | Rule (measured / inferred) |
|----------|----------------------------|
| `_OC.NIF` | Shared `BSPackedCombinedSharedGeomDataExtra` under plain `NiNode` roots; name `Meshes\PreCombined\[plugin\]<cell8>_<id8>_OC.NIF` with cell low-24 and sequential OC id from plugin (RE-003: Static tile grid for ~22% of ids — pending full CK run). |
| XCRI | Word-pair layout `8 + 4*M + 4*W`; pairs `(REFR local, OC mesh id)`; see `parse_xcri_bytes`. |
| PCMB / VISI | 2-byte packed dates in `PCMB` / `VISI`; stale when child asset date exceeds cell stamp (`is_precalc_stale`). |
| CDX row | 16 bytes; exterior `word0` packs signed 12-bit grid + kind; `word1` is cell/world form id. |
| CSG | `bcsg` + zlib chunks to 64 KiB; 20-byte row table meaning unmeasured (per-cell verdict deferred). |
| UVD | Magic `0xD6000012`, CRC-32C over `[8,end)`, banner at `0xBC`. Native write covers header + CRC; Umbra payload is accepted, not necessarily byte-identical to CK (#1093 Q11). |

## Native pipeline (this PR)

1. `precombine_plan` (C#) — eligibility and model groups.
2. `bake_oc_single_source*` (C++) — pass-through bake for sources that already contain `BSPackedCombinedSharedGeomDataExtra` (until Shared geom encoder lands).
3. `stamp_cell_previs` (C++) — writes PCMB, VISI, XCRI on a CELL override.
4. `write_cdx` / `write_csg` / `write_uvd` — container encoders round-trip with readers; FO4IDE exposes `WritePrevisContainer` / MCP `write_previs_container`.

## Target author workflow (CK-free, better than PJM)

| PJM pain | Garden approach |
|----------|------------------|
| Separate check script + seed ESP | **Previs scan** in FO4IDE; broken/outdated rows are the worklist |
| CK generate + merge esp | **Single patch plugin**; overrides and assets stay in the editor journal until `save_plugin` |
| Missing `_OC` found at merge time | **Plan → bake → verify SharedGeom** before `stamp_cell_previs`; scan re-runs show zero broken |
| Manual PCMB/VISI from CK dates | Stamps from **output file mtimes** (same rule as `is_precalc_stale`) |
| CK CDX/CSG/UVD | **`write_cdx` / `write_csg` / `write_uvd`**; `.psg` never used (vanilla ships `.csg` only) |
| PleaseRemove REFRs | **No scaffolding** — plan operates on real STAT placements only |
| 8-step bat | **Wizard actions per cell or batch**: disable stale → plan → bake → stamp → update containers → pack BA2 |

Open engineering for “complete” patching: **#34** encode `BSPackedCombinedSharedGeomDataExtra` from placements (not pass-through copy); **#1093** Umbra tome payload generation beyond header/CRC validation. Oracles under `testdata/previs/oracles/` pin format, not CK as a runtime dependency.
