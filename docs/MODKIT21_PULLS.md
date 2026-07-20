# modkit21 feature-pull tracker

Bryant-21/modkit21 (GPL-3.0) is a much larger FO4 modding tool built by a friend of this project's
maintainer, who gave explicit permission to review its source and pull ideas into FO4RecordEditor.
This doc tracks that ongoing comparison: what's been reviewed, what got pulled (as MCP tools), what
was deliberately left out and why, and what's still open.

FO4RecordEditor's own scope is plugin (ESP/ESM/ESL) record editing via Mutagen, exposed to an AI
through MCP tool calls. That scope is the filter for every decision below — modkit21 covers meshes,
textures, audio, animation, and physics far beyond that, and most of it doesn't translate to a
headless, chat-driven tool even where it's a genuinely good feature for a GUI artist workflow.

## Shipped

| # | Feature | MCP tools | Source verified from (modkit21 / py-creation-lib) | Notes |
|---|---|---|---|---|
| 1 | Master table inspect + reorder | `list_masters`, `reorder_masters` | `cli/esp_commands.py` (masters list/add/remove/reorder) | Building `reorder_masters` required reading Mutagen's own `ModHeaderWriteLogic.cs`: `SortMasters` runs unconditionally even under `NoCheck` content, so the tool needs `MastersListOrdering=NoCheck` explicitly or a plain reorder silently gets discarded by `save_plugin`'s load-order derivation. Documented in `docs/Code Notes/FO4RecordEditor.md` (outer workspace). |
| 2 | ESL/Small header flag | `set_light_flag` | `cli/esp_commands.py` (`esp header --set-light`) | `compact_to_esl` never set this bit; closes a known gap from earlier work. |
| 3 | BA2/BSA archive read | `archive_list`, `archive_extract`, `archive_extract_all` | `cli/archive_commands.py` → native Rust `bsarchive_native` | Zero new dependencies — Mutagen already vendors a full BA2/BSA reader (`Archives/Ba2`, `Archives/Bsa`), already used internally by `TextureService.cs`. |
| 4 | Papyrus function/script lookup | `papyrus_function_lookup`, `papyrus_script_info` | `cli/data_commands.py` (function/functions/api/hierarchy) → `creation_lib.creation_data` | Built by parsing our own offline Creation Kit Wiki HTML mirror directly instead of porting modkit's Python index (we already had the source data). Needs `--ck-wiki <folder>` at launch or `CkWikiPath` in Settings. First regex draft was wrong (assumed section `id` was on `<h2>`; it's on the inner `<span>`) — caught by testing against real markup, then verified against the real mirror. |
| 5 | BGSM material field editor | `bgsm_inspect`, `bgsm_set_field` | `native/materials/src/{base.rs,bgsm.rs}` (their own material_tools library, not even wired into their CLI) | Byte-for-byte C# port of the Rust parser/writer — Mutagen has no material-file support, so this needed real, from-source binary-format work, not just wiring up an existing capability. Verified byte-identical round-trip against all 400 real `.bgsm` files found anywhere in the workspace (versions 1-2 only; higher-version branches verified by hand-built + round-trip tests instead, no real v3+ sample found). **BGEM (effect materials) is NOT implemented yet** — same architecture, just needs its own field list ported from `native/materials/src/bgem.rs`. |
| 6 | Mod folder catalog | `catalog_mod_folder` | `creation_lib/mod/inspector.py::catalog_assets()` | Buckets a mod folder's files by category (meshes/textures/materials/sounds/scripts/plugins/archives/voice) with per-category counts, for "what's in this unfamiliar mod" triage. |

Tool surface: 57 → 68 across these 6 pulls (see `docs/MCP_SETUP.md` for the full current list).

## Open — real candidates, not yet built

| # | Feature | Where it lives in modkit21 | Why it's not done | Rough size |
|---|---|---|---|---|
| 7 | Auto-skinning / weight transfer / dismemberment partitioning | `cli/nif_commands.py` (`auto-skin`, `transfer`, `partitions`, `validate`, `normalize`) → `creation_lib/skinning/` | The single biggest capability gap found across all research passes — turns a static clothing/armor mesh into a game-ready skinned outfit, currently Blender/Outfit-Studio-only for us. Fully mechanical (nearest-surface/barycentric geometry math, no artistic judgment) but needs our own clean-room weight-transfer implementation plus a reference body asset. | Large — comparable to or bigger than #8. |
| 8 | Cloth physics parameter tweak + validate | `cli/cloth_commands.py` (`cloth tweak`, `cloth validate`) → native Havok bindings | Real, scoped, headless-appropriate (structured field edits on the HCL cloth graph, no simulation/preview involved) — but needs reverse-engineering Bethesda's Havok cloth binary format from scratch; we have nothing like Mutagen's ready-made archive reader to lean on here. | Large — open-ended binary-format RE project. |
| 9 | SWF icon batch injection | `cli/swf_commands.py` (`symbols inject`, `markers build`) | Real and mechanical (byte-exact SymbolClass splice from a donor SWF, driven by a static lookup table) but narrow use case — specifically FO76→FO4 icon porting. | Small-medium, low priority. |
| 10 | Texture recolor (hue-shift/tint/colorize/gradient) | `cli/texture_commands.py` (`recolor` group) → `creation_lib/textures/recolor.py` | Plain PIL color math, no ML/perceptual judgment. Needs our own DDS↔PNG round-trip (may partially exist from earlier texture-preview work in `TextureService.cs`). | Moderate. |
| 11 | HKX ↔ XML pack/unpack | `cli/build_commands.py` (`pack-hkx`/`unpack-hkx`) → native `havok_native` | Mechanical binary/XML round-trip for Havok behavior files. Only useful if we ever touch behavior graphs directly. | Small, low priority. |
| 12 | NIF validate/mesh-fix backport, generic NIF block copy | `nif/validation.py`, `nif/operations/mesh.py`, `nif copy` command | Their `validate_nif` catches more (orphaned blocks, duplicate names, absolute texture paths) than our `nif_verify`; `update_bounds`/`prune_degenerate_tris`/`flip_faces`/`flip_uvs_v` are trivial fixes. Not new tools — fold the extra checks into `nif_verify`/`nif_fix`. | Small. |

## Confirmed out of scope (investigated, not dismissed by category alone)

The user explicitly pushed back once on an earlier "GUI-only" dismissal, so these were re-examined by
reading actual source rather than judged by feature name:

- **3D world viewer** (`cli/world_commands.py`) — confirmed render/stats-only (`WorldReport` is just
  counts/warnings/timings). No navmesh data, no per-object FormID/position listing exposed via CLI.
  `WorldScene.query_visible()`/`inspect_instance()` exist in their library but aren't wired to any CLI
  command either — unexposed library surface, not a hiding feature.
- **Texture tools beyond recolor** — checked `creation_lib/textures/` directly: only `recolor.py`,
  `naming.py`, `texture_dirs.py` exist. No resize/mipmap/format-convert/DDS-validate anywhere, CLI or
  library. Nothing was missed.
- **LOD generation** (`lod/billboards.py` + native `generate_lod`) — a full xLODGen-equivalent
  (terrain+object+tree, needs a worldspace/plugin/GPU headless context). No simple per-mesh decimation
  pass exists anywhere in their code. Whole-map authoring, not a scoped operation.
- **BODY/ARMA/ARMO slot management** — no dedicated tooling found anywhere; just generic ESP record
  fields, already covered by our own record editor.
- **bone_edit/** (pose/IK/skeleton editing) — genuine artistic/visual judgment, GUI-only.
- **Cloth simulation itself** (as distinct from #8's tweak/validate) — needs a physics
  viewport/preview, not headless-appropriate.
- **CK shell-out group** (`ck` CLI group) — literally launches the real Creation Kit GUI; contrary to
  this toolchain's CK-free approach.
- **`git`/`index`/`data audit-yaml`/build-pack groups** — tightly coupled to modkit's own
  project/YAML/Gitea authoring conventions (or, for `audit-yaml`, to the sibling `bacup`
  FO76→FO4-conversion repo's field whitelists), not portable capabilities. `index` duplicates this
  workspace's own `brain/search.py` + CK wiki mirror infrastructure.
- **40 of modkit21's 42 desktop "workspaces"** — visual drag-and-drop GUI paradigms (Weights, Cloth
  preview, Bone Editor, Behavior Graph, World Viewer, BSA Viewer's GUI shell, ...) with no headless
  equivalent. The 2 that DID have a real underlying scriptable capability (mesh/NIF tooling, materials)
  are exactly items #5-7 and #12 above — found by reading the CLI command source underneath the
  workspace, not the workspace description itself.

## Research provenance

Three background research passes, each scoped narrower than the last as earlier ground got covered:

1. **Pass 1** — `esp` command group only → items #1-2.
2. **Pass 2** — broad survey of every other CLI group (archive, texture, audio, mesh, animation,
   havok, papyrus-data, ck, git, index, swf, world, build, cloth) → items #3-4, plus first sighting of
   #8/#9/#10/#11 at survey depth.
3. **Pass 3a** (NIF/materials deep dive) → items #5-7, #12, confirmed #10-11's scope.
3. **Pass 3b** (re-examination of "GUI-only" areas: cloth, world, swf, texture — triggered by explicit
   user pushback on the earlier dismissal) → items #8-9 promoted from "dismissed" to "real candidate,
   large/narrow", confirmed world/texture/LOD/bone_edit dismissals with source citations.

## How to update this doc

When a row in "Open" gets built: move it to "Shipped" with its tool names, update the tool-surface
count, and note what got verified before shipping (real-file corpus, hand-built ground truth, etc. —
match the rigor of rows #5/#1 above, not just "added it"). When a new modkit21 area gets investigated,
add a row to whichever table it lands in with a source citation, even if the verdict is "confirmed out
of scope" — the point of this doc is that every verdict here traces back to source someone actually
read, not a guess from a feature name.
