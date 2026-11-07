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
2. `bake_oc_single_source*` (C++) — copies a measured source NIF to the OC path (placeholder until Shared geom encoder lands).
3. `stamp_cell_previs` (C++) — writes PCMB, VISI, XCRI on a CELL override.
4. `write_cdx` / `write_csg` / `write_uvd` — container encoders round-trip with readers.
