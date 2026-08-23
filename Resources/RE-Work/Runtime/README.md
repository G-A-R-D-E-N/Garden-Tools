# Fallout 4 Reverse Engineering — Ghidra Pipeline

This folder is the home for reverse-engineering the **Fallout 4 1.10.163 (OG)** binary to support
F4SE/CommonLibF4 plugin work (PrismaHUD, FalloutChat, Rad Mod, the C# bridge, etc.).

It exists because the workspace *claimed* to have a "ready Ghidra-OG that names ~99% of the binary,"
but on 2026-06-30 that turned out to be broken in two ways (jar-stripped Ghidra + an un-named project).
We rebuilt a real, working pipeline and wrote it down here so it never has to be re-discovered.

## Contents

| File | What it covers |
|------|----------------|
| [01-SETUP.md](./01-SETUP.md) | The environment: where Ghidra/the project/scripts live, every gotcha we hit, how to reproduce from scratch |
| [02-WORKFLOW.md](./02-WORKFLOW.md) | Day-to-day: how to search for a function, decompile it, and re-name a fresh DB |
| [03-FINDINGS.md](./03-FINDINGS.md) | Verified facts log — struct offsets, function addresses, and the bugs RE has already caught |
| [04-ROADMAP.md](./04-ROADMAP.md) | What we plan to RE next, and the full menu of what this pipeline *can* answer |
| [08-PAPYRUS-VM-QUESTS-ROADMAP.md](./08-PAPYRUS-VM-QUESTS-ROADMAP.md) | Dedicated Papyrus VM, script-linking, native-registration, quest, alias, and persistence workstream |
| [Organized/1.10.163/README-Papyrus-VM.md](./Organized/1.10.163/README-Papyrus-VM.md) | First decompile-verified OG VM scheduler, tasklet opcode dispatch, compiled-script loader, and linker findings |
| [05-LABEL-RECOVERY.md](./05-LABEL-RECOVERY.md) | The 2026-07-07 packed-exe fix + the community IDC label decode/apply technique (durable, reusable for future version gaps) |
| [06-FUNCTION-INDEX.md](./06-FUNCTION-INDEX.md) | The 2026-07-10 subsystem function index + a 1.10.155-vs-1.10.163 cross-reference that surfaced 313 candidate CommonLib RTTI-matching errors and 2,803 safe naming-gap fills |
| [07-DECOMPILE-CANDIDATES.md](./07-DECOMPILE-CANDIDATES.md) | The 2026-07-10 decompile-verification pass over all 313 candidates: 150 confirmed real naming bugs, 68 likely linker code-folding artifacts (a newly-documented false-positive pattern), 63 cases where the original name was actually right, 32 inconclusive |
| [scripts/](./scripts/) | The actual working scripts (FW_Query.java, FW_Stats.java, FW_ExportAll.java, name_db.py, run_query.ps1, ApplyDecodedLabels.java, ...) |

## The 30-second version

- **Real Ghidra:** `E:\F4SE OG\ReverseEngineering\Ghidra\ghidra_12.1_PUBLIC` (the repo's bundled copy is jar-stripped — do not use it).
- **⚠️ Use `fwgh_proj_1.10.163_unpacked`, not `fwgh_proj`:** the original `fwgh_proj\BethesdaGhidraScripts`
  was discovered 2026-07-07 to have been built from a **packed/DRM-encrypted exe** — its `.text` decompiles
  to garbage (`in()`/`out()`/`halt_baddata()`), even for "named" functions. A Steamless-unpacked exe fixed
  it: **`E:\F4SE OG\ReverseEngineering\Projects\fwgh_proj_1.10.163_unpacked\`** — program `/Fallout4.exe.unpacked.exe`,
  image base `0x140000000`, **209,817 functions**, **14,824** named `Class::Method` from CommonLibF4 RTTI
  matching, plus **657,980** additional names applied from a decoded community IDC label file (40,073
  logged disagreements, mostly cosmetic — see `03-FINDINGS.md`). See `01-SETUP.md` for the full story and
  `05-LABEL-RECOVERY.md` for the reusable label-decode-and-apply technique. The old `fwgh_proj` is kept for
  reference only.
- **NG/AE work: use `fwgh_proj_1.11.221_unpacked` for the current AE project; the roadmap target is AE 1.11.240 where available** (added 2026-07-17, previously undocumented here —
  filled in 2026-07-18). CommonLibF4's own runtime classifier
  (`CommonLibF4/lib/commonlib-shared/src/REX/FModule.cpp`) draws the line at exactly `1.10.163` = `kOG`,
  `> 1.10.163` and `<= 1.10.984` = `kNG`, `> 1.10.984` = `kAE` — and `F4SE/Version.h` defines
  `RUNTIME_LATEST = RUNTIME_1_11_221`. That means **both** `fwgh_proj_1.11.191_unpacked` and
  `fwgh_proj_1.11.221_unpacked` fall in the `kAE` bucket, not `kNG` — there is no separate "NG-only"
  Ghidra project in this folder. `1.11.221` is the current/latest `kAE` target (use this one for real
  AE-runtime hook addresses); `1.11.191` was RE'd first and `1.11.221`'s vtables were auto-propagated
  from it (`Projects/fwgh_proj_1.11.221_unpacked/autovt_191_to_221*.log`) — keep `1.11.191` only as an
  intermediate/secondary cross-check, the same role `1.10.155` plays for OG.
- **Run a query:** edit/point at a `.java` GhidraScript and run `scripts\run_query.ps1` (wraps `analyzeHeadless`) —
  update its `-projLoc`/project-name defaults to the corrected project above. ~1–2 min per lookup. Output =
  function name + entry address + RVA + signature, and decompiled C on demand.
- **Python scripts** (e.g. the CommonLib naming pass) need pyghidra, not `analyzeHeadless` — see 02-WORKFLOW.

## What reverse engineering buys us

Plugins that hook the engine constantly hit two walls; Ghidra knocks both down:

1. **"What is the exact memory layout of this struct?"** — CommonLibF4 forward-declares many engine types
   (no field offsets). The binary is ground truth. *This already caught a real bug:* PrismaHUD's compass
   read `TESQuestTarget::alias` at offset `0x00` when it's actually at `0x10` (see 03-FINDINGS).
2. **"Where is the function I want to hook / call?"** — REL::IDs, vtable slots, and the actual vanilla
   algorithm (so we can replicate or intercept it) are all readable from the named DB.

See [04-ROADMAP.md](./04-ROADMAP.md) for the full capability menu.

> Cross-refs: the session memory notes `reference_ghidra_og_setup`, `project_workspace_reorg_2026_06_30`,
> and the Prisma findings at `E:\F4SE OG\Projects\Prisma\docs\COMMONLIB_FINDINGS.md`.

---

## Where this folder fits the rework archive

This directory is the heart of the `rework` archive. The root guides are the curated views over it:

- [WORK-LOG.md](../WORK-LOG.md) — the month-by-month narrative distilled from the numbered docs
- [FINDINGS-INDEX.md](../FINDINGS-INDEX.md) — every dated entry of `03-FINDINGS.md`, one line each, linked
- [ISSUES-MAPPING.md](../ISSUES-MAPPING.md) — BGS issues/PRs ↔ findings and releases
- [MANIFEST.md](../MANIFEST.md) — exactly what is tracked here vs kept local (the 42 GB of working
  data — Ghidra, projects, symbols, Havok SDK — stays local)
- [CONTRIBUTING.md](../CONTRIBUTING.md) — how to add a finding and keep the root files in sync

`CLAUDE.md` in this folder is the folder's own master reference — read it first for the layout and
for which Ghidra project to use. The root `README.md` is the archive's hub.
