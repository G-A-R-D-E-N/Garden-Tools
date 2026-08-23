# CKRE Roadmap — Full Creation Kit Reverse Engineering

**Target:** CreationKit.exe 1.11.137.0 (64-bit), Fallout 4 Steam Edition
**Date:** August 22, 2026
**Status:** 25 documents, 2 validators, ~12,000 lines of decompile artifacts

---

## Phase 0 — Foundation (✅ COMPLETE)

Everything needed to understand *any* subsystem.

| doc | what | state |
|---|---|---|
| 00 | overview, tooling, methodology | ✅ |
| 01 | CreationKit.ini key inventory | ✅ |
| 02 | RTTI class map (4,438 classes) | ✅ |
| 03 | findings index (dated log) | ✅ |
| 04 | pipeline — how we extract/decompile | ✅ |
| 07 | source tree (1,116 `__FILE__` paths) | ✅ |
| 24 | tesforms source-file → class map | ✅ |

---

## Phase 1 — Settings & CLI (✅ COMPLETE)

How the CK is configured and how you drive it headless.

| doc | what | state |
|---|---|---|
| 05 | batch CLI surface (31 switches, arg grammars) | ✅ |
| 06 | 1,629 embedded settings key database | ✅ |
| 10 | settings reader (GetPrivateProfile → scatter table) | ✅ |
| 13 | CLI token→bitmask parser + dispatcher | ✅ |
| 15 | batch process internals (MasterfileOptimizer, MapMaker, FileSaver) | ✅ |
| 16 | settings write-back (scatter lookup → callback → ini write) | ✅ |
| 17 | INI file census (every path, mapped to reader function) | ✅ |
| 19 | batch exit-code contract (0x6b650000/0x6b650001 matrix) | ✅ |
| 22 | batch-pipeline setting dependencies | ✅ |

**Tools:** `validate_ckini.py` — lints CreationKit.ini against the 1,641-key descriptor table

---

## Phase 2 — Core Record Model (🟡 MOSTLY COMPLETE)

The four world-building classes — the skeleton of the CK.

| doc | what | state |
|---|---|---|
| 12 | record-definition dispatch table (all ~159 form types) | 🟡 factory map done; LoadRecord/SaveRecord for CELL/WRLD/LAND/REFR + QUST (doc 28) |
| 23 §1–7 | CELL vftable, ctor, member layout (0x170 bytes) | ✅ |
| 23 §8 | cell-data blocks (0x20 exterior, 0x88 interior) | ✅ |
| 23 §9 | exteriorcellloader streaming contract | ✅ |
| 23 §10a | CELL LoadRecord/SaveRecord subrecord set, verified against Fallout4.esm | ✅ |
| 23 §10b | WRLD LoadRecord/SaveRecord, DATA field map, verified against Fallout4.esm | ✅ |
| 23 §10c | full CELL/WRLD vftable override naming (all unnamed slots named) | ✅ |
| 23 §10d | LAND LoadRecord/SaveRecord vftable slots + subrecord set | ✅ |
| 23 §10e | REFR LoadRecord/SaveRecord vftable slots + ~160 subrecord dispatch | ✅ |
| 23 §10f | TESForm save header byte spec (0x18-byte block, vt+0x1b0/0x3b8/0x3c0) | ✅ |
| 23 §11 | file I/O primitives (TESFile read, subrecord writer, buffer/seek contract) | ✅ |
| 23 §12 | WRLD cell-map insert, NewCell helper chain, interior XCLC asymmetry | ✅ |

**Tools:** `validate_cell_save.py` + `plugin_parse.py` — strict GRUP-tree plugin walk; classifies every CELL subrecord in `.esm` against the SaveRecord contract

**Remaining for Phase 2:**
- LoadRecord/SaveRecord for the other ~155 form types (see Phase 5)
- REFR extra-data block layout (the 0xC6 tag internals)
- NAVM record model (separate from the four main classes)

---

## Phase 3 — Export Pipelines (🟡 MOSTLY COMPLETE)

Headless batch output formats — what the CK actually writes to disk.

| doc | what | state |
|---|---|---|
| 09 | PreCombined/PreVis dispatcher, grammar, process runner | ✅ |
| 14 | PreVis `.scene` format (writer decompile) | ✅ |
| 21 | `.uvd` runtime format (magic 0xD6000012), verified against real samples | ✅ |
| 25 | BGSCombinedCellGeometryDB — BTree index, ExtraCombinedRefs, CombinedObjects.esp | ✅ |
| SCOL writer | `decompile-SCOL-combined-mesh-writer.md` — `_OC.NIF`/`_Physics.NIF` naming | ✅ |
| — | CompileTextExport (FUN_140916980), TextExport.txt grammar | 🟡 decompiled, not fully doc'd |
| — | DialogueExporter (branch 0x2) — `-ExportDialogue` output paths | 🟡 decompiled, doc partial |
| — | `.STRINGS`/`.DLSTRINGS`/`.ILSTRINGS` format | 🟡 byte-parsed, doc needs cleanup |
| — | FaceGen exporter | 🔴 not started |
| — | Papyrus script compiler integration | 🔴 not started |

**Tools:** `validate_ckini.py --compile-text` mode, `--strings` mode

---

## Phase 4 — LOD & Terrain (🟡 PARTIAL)

Cross-referenced from the xLODGen reverse-engineering in `lodgen/reverse-engineering/`.

| area | what | state |
|---|---|---|
| XLODGEN_TERRAIN_INTERNALS.md | WaterDelta, hide quads, skirts/ZSHIFT, BTR chunking, water nodes, reducers | ✅ (xLODGen side) |
| XWEM → water mesh builder | WRLD water height feeds CK's bgsterrainwatermeshbuilder.cpp | ✅ decompiled 2026-08-22 — see dumps/decompile-bgsterrainwatermeshbuilder.md (roadmap range was stale; real functions 0xf8e1b0..0xf8f0e0) |
| CK LOD subsystem (doc 08) | overview of built-in LOD generation | 🟡 summary only |
| Distant terrain pipeline | bgsterrainmanager, chunk/node tree, texture/object builders | 🟡 all source files mapped to functions via __FILE__ strings (see watermeshbuilder dump); bodies still to document |
| `_WaterHeights.dds` | runtime water height texture | 🔴 not investigated |
| BTR2RAW `.raw` format | int32 heightmap, lengthX/lengthY derivation | ✅ documented |

**Remaining for Phase 4:**
- Decompile `bgsterrainwatermeshbuilder.cpp` body (0xf76000–0xf83fff)
- Decompile `bgsterrainchunk.cpp` / `bgsterrainnode.*` — the chunk tree
- Decompile `bgsterraintexturebuilder.cpp` — texture atlas baking
- Decompile `bgsterrainobjectblockbuilder.cpp` — BTO output
- Cross-check CK built-in LOD against xLODGen output for parity

---

## Phase 5 — The Other ~155 Form Types (🔴 NOT STARTED)

Only CELL/WRLD/LAND/REFR have named virtuals and subrecord sets. Every other
form type needs:

1. **RTTI → vftable** — already mapped (doc 02), just needs slot diff against TESForm base
2. **LoadRecord decompile** — the subrecord 4CC switch tree per class
3. **SaveRecord decompile** — which subrecords it writes
4. **Member layout** — from the constructor + factory Create
5. **Cross-check against Fallout4.esm** — byte-parse real records

Priority form types by game impact:

| tier | types | why |
|---|---|---|
| **S-tier** | ✅ COMPLETE — QUST (28), DIAL/INFO (29), PACK (30), SCEN (31) | quest/dialogue system — most of the game's content |
| **A-tier** | NPC_, RACE, CSTY, FACT, CLAS | actor/NPC system |
| **B-tier** | SPEL, ENCH, ALCH, MGEF, PERK, LVLI, LVLN, LVSP | magic/perk/leveled-list system |
| **C-tier** | WEAP, ARMO, AMMO, MISC, BOOK, NOTE, KEYM, ALCH, INGR, FLOR, TREE | inventory/object system |
| **D-tier** | everything else (~100 types) | one-off, simple, or rarely used |

**Estimated effort:** ~2-4 hours per form type (vftable diff + LoadRecord decompile + cross-check),
or ~300-600 hours for all 155. The S/A-tier types are the highest-value next targets.

---

## Phase 6 — Runtime Systems (🔴 NOT STARTED)

The CK's editor-side counterparts of the game's runtime systems.

| subsystem | source files | what to document |
|---|---|---|
| **Render window** | `bsrenderwindow*.cpp`, `bsscene*`, `bscameracontrol*` | viewport pipeline, selection, gizmos |
| **Navmesh editor** | `navmesheditor*.cpp`, `navmeshtool*` | NAVM generation, pathfinding debug |
| **Havok physics** | `bhk*`, `hknp*`, `hcl*` | physics world, ragdoll, collision export |
| **Papyrus compiler** | `papyrus*`, F4PCompiler.dll | script compilation pipeline |
| **FaceGen** | `bgssceneaabbfinder*`, `bsfacegen*` | head mesh generation |
| **Landscape editor** | `teslandscapeedit*`, `*landscape*` | terrain painting, vertex editing |
| **Audio** | `audiomanager*`, `bswwiser*` | WWise integration, audio playback |

---

## Phase 7 — Missing Gaps in Completed Areas

Things we know exist but haven't fully documented.

| gap | where | priority |
|---|---|---|
| `ExtraCombinedRefs` XCRF subrecord 4CC | doc 25 §6 | medium — verify against real CombinedObjects.esp |
| BTree page layout (BSTBTreeFile) | doc 25 §2 | low — standard Bethesda code, not CK-specific |
| Interior XCLL 5-float field names | doc 23 §8.5c | low — directional/ambient/fog likely standard |
| `bgsterrainwatermeshbuilder.cpp` body | doc 23 §10b.2 | medium — feeds WaterDelta understanding |
| Distant terrain object block builder | Phase 4 | medium — BTO output format |
| CompileTextExport full doc | Phase 3 | low — already validated, just needs doc cleanup |
| DialogueExporter full doc | Phase 3 | medium — DIAL/INFO record path |
| STRINGS/DLSTRINGS/ILSTRINGS format doc | Phase 3 | low — byte-parsed, needs write-up |
| FaceGenExporter doc | Phase 3 | medium |
| 9 remaining JSONSerializer classes | doc 23 §7d | low — only REFR/WEAP/AIMS schema builders decompiled |
| Missing exterior cells in parser | validate_cell_save.py | ✅ resolved 2026-08-22 — walker rebuilt on strict GRUP walk (`plugin_parse.py`); full population is 40,165 CELL, see doc 23 §10a.4 |
| WRLD SaveRecord validator | validate_wrld_save.py | ✅ done 2026-08-22 — see doc 23 §10b.2 |

---

## Phase 8 — Tools & Automation

Scripts that turn the docs into usable utilities.

| tool | status | purpose |
|---|---|---|
| `validate_ckini.py` | ✅ | lint CreationKit.ini against 1,641 key descriptor table |
| `validate_cell_save.py` | ✅ | cross-check CELL subrecords in `.esm` against SaveRecord |
| `parse_wrld_esm.py` | ✅ | extract WRLD subrecord sets from `.esm` |
| `decompile-some-function.java` | ✅ | Ghidra Query.java script for batch decompilation |
| `validate_wrld_save.py` | ✅ | WRLD subrecord census + contract check across all four masters — 17 records, 0 issues; CLSZ/OFST/RNAM shapes corrected (doc 23 §10b.1/§10b.2) |
| `validate_esp.py` | ✅ | general .esm/.esp/.esl validator — structural + formID-range + localization-shape checks plus proven contracts (CELL/WRLD/PROJ/EXPL/HAZD/template grammar/QUST); 2.2M records across four masters in ~3 s, negative-test corpus rejected correctly |
| `batch_cli_runner.py` | 🔴 | wrap CK headless switches with arg validation |
| `precombined_inspector.py` | 🔴 | parse _OC.NIF and verify block types |
| `uvd_compiler.py` | 🔴 | standalone .uvd compiler from doc 21 spec |

---

## Phase 9 — CK Replacement Milestones

The long-term goal. Each milestone produces something usable without the CK binary.

| milestone | depends on | what it does |
|---|---|---|
| **M1: INI-free batch runner** | Phase 1 | run any CK batch switch without touching CreationKit.ini |
| **M2: PreCombined generator** | Phase 3 | produce `_OC.NIF` + `_Physics.NIF` + `CombinedObjects.esp` |
| **M3: PreVis generator** | Phase 3 + doc 21 | produce `.scene` + `.uvd` files |
| **M4: Terrain LOD generator** | Phase 4 | produce `.btr` files at xLODGen parity |
| **M5: Object LOD generator** | Phase 4 | produce `.bto` files |
| **M6: FaceGen generator** | Phase 6 | produce FaceGen mesh/texture data |
| **M7: Full plugin validator** | Phase 2 + Phase 5 | validate any `.esm`/`.esp` against all ~159 form type contracts |
| **M8: Headless plugin save** | Phase 2 + Phase 5 | load, modify, and save plugins without the CK |
| **M9: CI pipeline** | M1–M8 combined | Docker/Wine-based headless CK replacement for automated builds |

---

## Dependency Graph

```
Phase 0 (foundation)
  ├─► Phase 1 (settings/CLI) ──► M1 (batch runner)
  ├─► Phase 2 (record model) ──► Phase 5 (all form types) ──► M7 (validator) ──► M8 (plugin save)
  │                                                              │
  ├─► Phase 3 (export pipelines) ──► M2 (PreCombined) ──► M3 (PreVis) ──► M9 (CI)
  │                                                                           │
  └─► Phase 4 (LOD/terrain) ──► M4 (terrain LOD) ──► M5 (object LOD) ────────┘
```

---

## Next Immediate Steps (Recommended Order)

1. ~~Decompile `bgsterrainwatermeshbuilder.cpp` body~~ ✅ done 2026-08-22 — dumps/decompile-bgsterrainwatermeshbuilder.md
2. ~~Add `validate_wrld_save.py`~~ ✅ done 2026-08-22 — doc 23 §10b.2
3. **Start S-tier form types** — ✅ QUST started 2026-08-22: LoadRecord/SaveRecord located + 1,862-record census (doc 28)
4. ~~Write `validate_esp.py`~~ ✅ done 2026-08-22 — Phase 8/M7 groundwork
5. ~~Decompile exteriorcellloader grid-walk~~ ✅ done 2026-08-22 — doc 23 §9.1 (CellLoaderTask enqueue + scatter-table pump identified)

---

## Phase R — Runtime Reverse Engineering (Fallout4.exe)

**Targets:** Fallout4.exe 1.10.163 (OG) and 1.11.240 (AE)  
**Output repo:** https://github.com/PRISMA-USER-INTERFACE-FRAMEWORK/commonlib_NonVR  
**Local docs:** `Resources/RE-Work/Runtime/`

Separate from the CK work — this reverse-engineers the **game binary itself** to support
F4SE/CommonLibF4 plugin development (PrismaHUD, FalloutChat, Rad Mod, C# bridge).

### Runtime assets

| doc | what | state |
|---|---|---|
| Runtime/01-SETUP.md | environment: Ghidra, projects, gotchas | ✅ |
| Runtime/02-WORKFLOW.md | day-to-day: search, decompile, re-name | ✅ |
| Runtime/03-FINDINGS.md | verified facts log — offsets, addresses, bugs caught | ✅ |
| Runtime/04-ROADMAP.md | runtime RE capability menu + next targets | ✅ |
| Runtime/08-PAPYRUS-VM-QUESTS-ROADMAP.md | Papyrus VM, compiled-script linking, native registration, quests/aliases, persistence | 🟡 P1/P2 started |
| Runtime/Organized/1.10.163/README-Papyrus-VM.md | first OG decompile pass: scheduler, tasklet opcode dispatch, loader, linker anchor | 🟡 |
| Runtime/05-LABEL-RECOVERY.md | community IDC label decode/apply technique | ✅ |
| Runtime/06-FUNCTION-INDEX.md | subsystem function index + 1.10.155-vs-1.10.163 cross-ref (313 candidate misnames) | ✅ |
| Runtime/07-DECOMPILE-CANDIDATES.md | decompile verification: 150 confirmed bugs, 68 false-positives, 63 correct | ✅ |
| Runtime/Organized/ | subsystem READMEs: Landscape, Quests, Workshop, Pipboy, Controllers, Weather | ✅ |
| Runtime/Scripts/ | Ghidra Java scripts (FW_Query, ApplyDecodedLabels, ApplySafeGaps, etc.) | ✅ |

### Key runtime metrics

- **209,817 functions** in the OG unpacked project
- **14,824 named** `Class::Method` from CommonLibF4 RTTI matching
- **657,980 additional names** applied from community IDC label decode
- **150 confirmed CommonLib naming bugs** found and fixed
- **1,959 safe naming-gap fills** applied (Class::FuncN → real names)
- **AE 1.11.240 project** auto-propagated from 1.11.191 with vtable cross-check

### Runtime roadmap (from Runtime/04-ROADMAP.md)

The dedicated Papyrus/quest/scripting workstream is tracked in [Runtime/08-PAPYRUS-VM-QUESTS-ROADMAP.md](../Runtime/08-PAPYRUS-VM-QUESTS-ROADMAP.md), with P1 VM execution and P2 script loading/linking as the first decompile queue.

1. **Quick-loot container suppression** — find clean activation interception point (PrismaHUD)
2. **Vanilla ammo readout via GFx** — confirm HUD ammo push path (PrismaHUD)
3. **Audit "// guessed" offsets** — sweep plugins for unverified struct accesses
4. **Save-load freeze diagnostic** — engine `BSStringPool`/Havok-job race during load
5. **Diff across game versions** — structural-hash scripts for relocating hooks in new builds

### Runtime ↔ CK cross-pollination

| CK finding | runtime value |
|---|---|
| TESObjectCELL member layout (doc 23 §8) | cell flag reads in runtime plugins |
| TESObjectREFR LoadRecord subrecord set (doc 23 §7e) | reference traversal in save/load hooks |
| TESWorldSpace cell-map insert (doc 23 §8.3) | worldspace bounds in map mods |
| bgsterrainwatermeshbuilder.cpp (doc 23 §10b.2) | LOD water behavior understanding |
| Elric PDB symbols (doc 18) | confirmed class names for runtime vtables |

