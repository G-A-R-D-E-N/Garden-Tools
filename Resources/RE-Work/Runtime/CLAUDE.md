# ReverseEngineering — Master Reference

This folder is the **single consolidated home** for all Fallout 4 reverse-engineering assets:
the Ghidra install, all 3 RE projects, the raw PDB/symbol dumps, the organized subsystem index,
and every doc/script that operates on them. Consolidated 2026-07-10 from three previously
separate locations (`Tools\PDB\`, `Tools\Ghidra\`, `docs\ReverseEngineering\`) — if you find an
older doc/memory/comment referencing those paths, they now live here; see "Path history" below.

**Read this file first** when doing any RE work in this workspace. It exists so a fresh session
(or a fresh Claude) can orient in one read instead of re-discovering the layout, which
project to use, and which findings already exist.

## Folder map

```
ReverseEngineering\
├── CLAUDE.md                  ← this file
├── README.md                  ← original pipeline overview (start here for narrative context)
├── 01-SETUP.md                ← environment, every setup gotcha, how to reproduce from scratch
├── 02-WORKFLOW.md             ← day-to-day: search/decompile/rename workflow
├── 03-FINDINGS.md             ← verified facts log (struct offsets, addresses, bugs caught)
├── 04-ROADMAP.md              ← what to RE next, full capability menu, open tasks
├── 05-LABEL-RECOVERY.md       ← the packed-exe fix + community IDC label decode/apply technique
├── 06-FUNCTION-INDEX.md       ← subsystem index + 1.10.155-vs-1.10.163 cross-reference (2026-07-10)
├── 07-DECOMPILE-CANDIDATES.md ← decompile verification of all 313 candidates: 150 confirmed
│                                 real bugs, 68 linker-folding artifacts, 63 false positives, 32 inconclusive
├── Ghidra\
│   └── ghidra_12.1_PUBLIC\    ← the actual Ghidra 12.1 install. USE THIS ONE — the repo's own
│                                 bundled Ghidra copy (if referenced anywhere else in the
│                                 workspace) is jar-stripped and won't launch.
├── Projects\                  ← 5 Ghidra RE projects (.gpr/.rep pairs) as of 2026-07-18 (was 3 —
│   │                             two AE-runtime projects were added 2026-07-17 and are
│   │                             documented here for the first time). NEVER delete/rename the
│   │                             .gpr file inside — the project's *identity* is that filename,
│   │                             independent of the containing folder name.
│   ├── fwgh_proj_1.10.163_unpacked\   ← *** THE ONE YOU WANT FOR OG ***. Project name inside:
│   │                                     "Fallout4_163_unpacked". Real, Steamless-unpacked
│   │                                     1.10.163 OG binary — this is the build the workspace
│   │                                     actually targets and hooks on the OG runtime. 365,059
│   │                                     functions, the overwhelming majority named (CommonLib
│   │                                     RTTI matching + 657,980 community-label names applied).
│   ├── fwgh_proj_1.11.221_unpacked\   ← *** THE ONE YOU WANT FOR AE ***. Project name
│   │                                     "Fallout4_1_11_221_unpacked". CommonLibF4's own runtime
│   │                                     classifier (`lib/commonlib-shared/src/REX/FModule.cpp`)
│   │                                     puts everything above 1.10.984 in `kAE` — and
│   │                                     `F4SE/Version.h` defines `RUNTIME_LATEST` as this exact
│   │                                     version (1.11.221). Use this for AE-runtime hook
│   │                                     addresses/offsets. Its vtables were auto-propagated from
│   │                                     the 1.11.191 project below (`autovt_191_to_221*.log`).
│   ├── fwgh_proj_1.11.191_unpacked\   ← Project "Fallout4_1_11_191_unpacked". ALSO `kAE` per the
│   │                                     same classifier (>1.10.984), NOT a separate "NG" project
│   │                                     — RE'd first, superseded by 1.11.221 above as the current
│   │                                     AE target. Keep only as a secondary/intermediate
│   │                                     cross-check, the same role 1.10.155 plays for OG.
│   ├── fwgh_proj_1.10.155\            ← Project name "Fallout4_155". A DIFFERENT game version
│   │                                     (1.10.155, not 1.10.163) with a full private Microsoft
│   │                                     PDB applied — real signatures, but WRONG ADDRESSES for
│   │                                     our OG target. Useful only as an independent cross-check
│   │                                     source (see 06-FUNCTION-INDEX.md), never for hook RVAs.
│   └── fwgh_proj_OLD_packed_reference_only\   ← Project name "BethesdaGhidraScripts".
│                                                 DEPRECATED — built from a packed/DRM-encrypted
│                                                 exe; `.text` decompiles to garbage
│                                                 (`in()`/`out()`/`halt_baddata()`) even for
│                                                 "named" functions. Kept only so old
│                                                 addresses/notes referencing it are traceable.
│                                                 DO NOT decompile or trust anything from this
│                                                 project — see 05-LABEL-RECOVERY.md §1.
│
│                             **There is currently no dedicated `kNG`-only project** (CommonLibF4's
│                             `kNG` bucket is `>1.10.163` and `<=1.10.984`, narrower than either AE
│                             project above) — if NG-specific (not AE) addresses are ever needed,
│                             that's a gap, not something either 1.11.x project already covers.
├── Symbols\                   ← raw exe/PDB files + flat symbol dumps (not a Ghidra project —
│   │                             these are the inputs a Ghidra project was built FROM, or a
│   │                             parallel non-Ghidra extraction)
│   ├── Fallout4.exe / Fallout4.pdb            ← 1.10.155 retail, full private PDB
│   ├── Fallout4Beta.exe / Fallout4Beta.pdb    ← 1.10.155 beta variant
│   ├── Fallout4_functions.txt / _data.txt     ← 1.10.155 PDB dump via llvm-pdbutil (fast,
│   │                                             no-Ghidra-needed name+signature lookup)
│   ├── F4_1_10_163_IDCLabels.idc              ← raw community label source (obfuscated names)
│   ├── F4_1_10_163_labels_decoded.csv         ← same, decoded (698,074 rows, 0xADDR,name)
│   ├── SYMBOLS.md / GHIDRA_PROJECT.md         ← extraction method + stats for the 1.10.155 dump
│   └── 1.10.163\
│       ├── Fallout4.exe.unpacked.exe          ← the Steamless-unpacked exe fwgh_proj_1.10.163_unpacked was imported from
│       ├── Fallout4_163_functions.txt         ← full function export of that project (365,059 funcs) — see Scripts\FW_ExportAll.java
│       ├── Steamless.exe / Steamless.CLI.exe  ← the unpacker tool itself, kept for future version-gap unpacks
│       └── export_run*.log                    ← logs from the FW_ExportAll.java run
├── Organized\                  ← generated by Scripts\organize_functions.py — DO NOT hand-edit,
│   │                              regenerate instead
│   ├── INDEX.md                              ← 19-category subsystem breakdown, both versions
│   ├── 1.10.155\<Category>.txt / 1.10.163\<Category>.txt   ← the bucketed function lists
│   ├── crossref_163_disagreements_vs_155.csv  ← every disagreement row + verdict
│   ├── crossref_review_candidates.md          ← 313 genuine-misname candidates (see below)
│   └── crossref_safe_naming_gaps.md           ← 2,803 safe placeholder→real-name fills
├── Scripts\                    ← every script that operates on the above, merged from the two
│   │                              old script folders (no filename collisions)
│   ├── run_query.ps1            ← query/decompile fwgh_proj_1.10.163_unpacked (THE target)
│   ├── run_pdb_query.ps1        ← query/decompile fwgh_proj_1.10.155 (cross-check source)
│   ├── FW_Query.java / FW_Stats.java / FW_ExportAll.java / DecompileAt.java / VTableDump.java /
│   │   VT_ByteDump.java / XrefDump.java / ListFuncsInRange.java / FindCallers.java /
│   │   FW_Imports.java / FW_SymSearch.java     ← 1.10.163-project GhidraScripts (run via run_query.ps1)
│   ├── FindDumpVtable.java       ← locate+dump ANY class's vtable (slot→method) in either project;
│   │                               the 155-project dump is the authoritative slot-order resolver
│   │                               for "which method is this vtable slot" ambiguities
│   ├── DecompileCandidates.java  ← batch-decompile a CSV of addresses to Decompiles\<addr>.txt
│   ├── ApplyConfirmedMisnames.java / ApplySafeGaps.java   ← batch force-rename from a CSV (fn+data)
│   ├── RevertMisapplied.java / CleanRename.java   ← force-rename (CleanRename clears colliding labels first)
│   ├── PDB_Query.java / PDB_Stats.java / SetPdbFile.java   ← 1.10.155-project GhidraScripts (run via run_pdb_query.ps1)
│   ├── ApplyDecodedLabels.java  ← applies F4_1_10_163_labels_decoded.csv to a Ghidra project
│   ├── name_db.py / name_db_1.10.163_unpacked.py   ← CommonLib RTTI-vtable naming pass (pyghidra, not analyzeHeadless)
│   └── organize_functions.py    ← regenerates everything in Organized\ (Python, ~15s runtime)
├── Decompiles\                 ← per-candidate decompile output from the 313-candidate pass
│   └── <address>.txt per candidate (claimed names + Ghidra's own current name + full decompiled
│                                     C body) — see 07-DECOMPILE-CANDIDATES.md for the verdicts
├── Havok\                      ← Havok Content Tools Compendium, ~29 GB, added 2026-08-09. Installers
│   │                             for HCT 5 through 2014, the Anarchy 2014.1.1 SDK/samples/PDBs, and
│   │                             the Havok Animation Tool. See "Havok SDK source" below before use.
│   └── SDK-2014.1.1\           ← extracted from Havok_Anarchy_SDK_2014_1_1_Windows.exe: 1,598 files.
│                                 Source\Behavior (306 headers), Source\Animation, Source\Common
│                                 (incl. the packfile reader/writer), Tools\FBXImporter (real .cpp),
│                                 ChangeLogs. Reached through the f4-re-mcp tools, not by hand.
└── Logs\                       ← stray historical query logs (out.log..out7.log) — transient, safe to delete
```

**Gitignored** (see workspace root `.gitignore`): `Symbols\`, `Ghidra\`, `Projects\`, `Organized\`,
`Decompiles\`, `Logs\`, `Havok\`. Everything in those is either huge (100s of MB–GB), regeneratable from
the game files + scripts, or licensing-murky to redistribute (the game exe/PDB). Only the numbered
markdown docs + this `CLAUDE.md` + `README.md` are tracked — they're small and are the actual
distilled knowledge; the folders are working data.

## Havok SDK source (added 2026-08-09)

`Havok\SDK-2014.1.1\` holds Havok's own class declarations for the version the game is built on.
Fallout 4 ships `hk_2014.1.0-r1`; this is 2014.1.1, one point release later. No class layout patch
set exists for 2014 (the newest is `2013_3`), which is evidence the layouts did not move, so field
*meanings* carry over. Field *offsets* still come from `HavokClassTypes.json` or from the game
binary, never from here.

Reach it through the MCP rather than by hand:

- `havok_class` — read a class declaration with its members and their notes. Exact name.
- `search_havok_source` — keyword search, for what a flag bit means or which classes mention a field.

Bethesda's own `BS*` classes are not in it. Those stay reverse engineering work.

**The constraint that matters.** This material is Havok's, is marked confidential, and was never
published. Nothing from it goes into any repository, and its wording is never copied or closely
paraphrased. It is a place to check an understanding. What gets written down is the conclusion, in
your own words, backed by something a real file can be shown to do. `BehaviourGraphStudio`'s
`docs/findings/` is the worked example: every entry there is a measurement over the sample corpus,
with the method recorded, and no Havok prose in it.

Note also that this is a second, independent authority. Where it disagrees with the game binary, the
binary wins, because that is what actually runs.

## The one thing to never get wrong

**Use `Projects\fwgh_proj_1.10.163_unpacked` (project name `Fallout4_163_unpacked`) for OG hook
addresses/offsets, and `Projects\fwgh_proj_1.11.221_unpacked` for AE ones** — those are the two
that need to be true of the game builds actually run. The other three projects exist for specific,
narrow reasons (1.10.155 = independent OG cross-check source with wrong addresses; 1.11.191 =
intermediate AE cross-check, superseded by 1.11.221; OLD_packed = historical reference for old
notes, actively wrong if decompiled) — reaching for any of them for a real hook address or struct
offset is the single most likely mistake to make in this folder.

## Quick start

```powershell
# Search + decompile against the REAL target (1.10.163)
& "E:\F4SE OG\ReverseEngineering\Scripts\run_query.ps1" "PlayerCharacter::Update"
& "E:\F4SE OG\ReverseEngineering\Scripts\run_query.ps1" "decompile:PlayerCharacter::Update"
& "E:\F4SE OG\ReverseEngineering\Scripts\run_query.ps1" "addr:0x749ea0,decompile"

# Cross-check against the independent 1.10.155 PDB (different addresses — names/signatures only)
& "E:\F4SE OG\ReverseEngineering\Scripts\run_pdb_query.ps1" "decompile:PlayerCharacter::Update"

# Regenerate the subsystem index + crossref after either symbol source changes
python "E:\F4SE OG\ReverseEngineering\Scripts\organize_functions.py"
```

Both `run_*.ps1` wrappers already handle the two recurring headless-Ghidra gotchas: piping blank
stdin (dodges an indefinite `pause` hang on non-zero exit) and deleting a stale `.lock` file before
running. Full gotcha list in `01-SETUP.md`.

## What's already been found (read before re-deriving)

- **`03-FINDINGS.md`** — the verified-facts log. Check here before assuming a struct offset or
  function address needs re-deriving; it might already be nailed down.
- **`06-FUNCTION-INDEX.md`** — 2026-07-10 pass: full subsystem breakdown of both versions, plus a
  cross-reference that found 313 candidate CommonLib RTTI-matching errors and 2,803 safe
  naming-gap fills (`Organized\crossref_safe_naming_gaps.md`).
- **`07-DECOMPILE-CANDIDATES.md`** — same day, all 313 candidates decompile-verified: **150
  confirmed real naming bugs**, 68 likely MSVC identical-COMDAT-folding artifacts (NOT bugs —
  read this section before "fixing" a trivial thunk that legitimately has two valid names), 63
  cases where CommonLib's original name was actually right, 32 inconclusive. **137 of the 150
  are now live in `Projects\fwgh_proj_1.10.163_unpacked`** — 13 were reverted after a
  CommonLibF4-header cross-check found the old name was already correct in those cases (full
  list + the methodology lesson in that doc's "Applied to the Ghidra project" section). Zero
  live plugin code depended on any of the wrong names.
- **Safe naming-gap fills (2026-07-10)** — the low-risk tier (`Class::FuncN` placeholders → real
  names): **1,959 applied** (`Scripts\ApplySafeGaps.java`, report `Organized\applied_safe_gaps.csv`),
  219 un-nameable STL template internals skipped. The 202 engine-class *reassignments* originally
  deferred were then decompile-reviewed and **78 more applied** (real inherited base-class methods —
  `MovementControllerAI`, `BSNavmeshSearch`, `BSPathingRequest`, …; 121 un-nameable templates skipped,
  3 held). See `03-FINDINGS.md` + `04-ROADMAP.md` item 6, held-list in
  `Organized\safe_gaps_deferred_reassignments.md`. The Ghidra project now has ~2,200 more
  correctly-named functions than the raw import.
- **`05-LABEL-RECOVERY.md`** — why there are 3 projects and not 1, and the reusable
  decode-community-labels technique for the *next* version gap.
- Before trusting **any** existing name/hook that traces back to CommonLibF4's original RTTI
  matching pass, grep `Organized\crossref_163_disagreements_vs_155.csv` and
  `Projects\fwgh_proj_1.10.163_unpacked\label_apply_disagreements.csv` for it — a real minority of
  those names are confirmed wrong (see the two originally-caught bugs in `03-FINDINGS.md`,
  `PlayerCharacter::ServePrisonTime`/`GoToPrison`).

## Path history (for old references you might still find elsewhere)

Before 2026-07-10 this was three separate locations. If a memory file, an old session doc, or a
source-code comment elsewhere in the workspace mentions one of these, the content moved here:

| Old path | New path |
|---|---|
| `Tools\Ghidra\ghidra_12.1_PUBLIC\` | `ReverseEngineering\Ghidra\ghidra_12.1_PUBLIC\` |
| `Tools\Ghidra\fwgh_proj_1.10.163_unpacked\` | `ReverseEngineering\Projects\fwgh_proj_1.10.163_unpacked\` |
| `Tools\Ghidra\fwgh_proj_1.10.155\` | `ReverseEngineering\Projects\fwgh_proj_1.10.155\` |
| `Tools\Ghidra\fwgh_proj\` | `ReverseEngineering\Projects\fwgh_proj_OLD_packed_reference_only\` (renamed for clarity) |
| `Tools\PDB\` (files directly in it) | `ReverseEngineering\Symbols\` |
| `Tools\PDB\1.10.163\` | `ReverseEngineering\Symbols\1.10.163\` |
| `Tools\PDB\organized\` | `ReverseEngineering\Organized\` |
| `Tools\PDB\scripts\` + `docs\ReverseEngineering\scripts\` | `ReverseEngineering\Scripts\` (merged, no filename collisions) |
| `docs\ReverseEngineering\*.md` | `ReverseEngineering\*.md` (same filenames, moved to root) |

Historical narrative in changelogs/session-handoff docs describing *past* work (e.g. "queried
Tools/PDB's Fallout4.pdb") was deliberately left unrewritten — those are accurate descriptions of
what was true at the time, not live references that need to resolve today.
