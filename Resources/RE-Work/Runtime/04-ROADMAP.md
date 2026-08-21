# 04 — Roadmap & Capability Menu

## What this pipeline can answer

Whenever plugin work hits one of these questions, the named DB is the fastest authoritative source:

1. **Exact struct field offsets** for types CommonLibF4 only forward-declares.
   *Method:* decompile a function that touches the type, read the displacement; or cross-check CommonLibSSE +
   a `sizeof` static_assert. *Proven:* the `TESQuestTarget::alias @ 0x10` fix (03-FINDINGS).

2. **Function addresses / REL::IDs for hooking or calling.**
   *Method:* search the name → take the RVA → wrap with `REL::Relocation`/a vtable patch. Useful for any
   "I need to call/intercept engine function X" task.

3. **Vtable layouts** (which slot is which method) for subclassing or detouring.
   *Method:* the DB labels `VTABLE_<Class>`; walk it in the GUI or script the slots.

4. **The actual vanilla algorithm** so we can replicate or intercept it (compass math, activation flow,
   ammo/HUD update, save-load behavior, AI ticks).
   *Method:* decompile the owning function and read the C.

5. **Verifying a CommonLib assumption before trusting it.** Many headers carry "// probably" offsets; the
   binary settles it cheaply.

6. **Diffing across game versions** (future): when the modlist updates past 1.10.163, the
   `IDADebugger` structural-hash scripts (in `…\Prisma\Reference_Tools\IDADebugger`) re-locate a *known* hook
   in the new build. Complementary to this naming pipeline, not a replacement.
   - **Update 2026-07-10:** a lighter version of this was done for a different purpose — not
     locating new hooks, but cross-checking the 1.10.163 project's own naming *disagreements*
     against the independent 1.10.155 named project to catch more CommonLib RTTI-matching errors. See
     [06-FUNCTION-INDEX.md](./06-FUNCTION-INDEX.md). Full structural/byte-level VT across the
     version gap is still not done.

## Concrete next targets (in priority order)

### 1. Quick-loot container suppression — the activate hook  *(open task)*
Current PrismaHUD approach hijacks Activate + hides `ContainerMenu` via `MenuOpenCloseEvent`+`kHide`, which is
a bit racy. RE goal: find the clean interception point.
- Search starts: `PlayerActivatePickRefEvent`, `BGSProcedureActivate`, `ActivateChoiceListener`,
  `TESObjectREFR::ActivateRef`, the `PlayerControls`/`ActivateHandler` input path.
- Deliverable: the address + signature of the function that opens `ContainerMenu` on activate, so we can
  short-circuit it deterministically instead of racing the menu open.

### 2. Vanilla ammo readout via GFx  *(open task)*
With FallUI disabled, the vanilla `HUDMenu.swf` ammo path is predictable again. RE goal: confirm how the engine
pushes ammo to the HUD so PrismaHUD can either hide the vanilla ammo widget cleanly or mirror it.
- Search starts: `HUDMenu`, `HUDAmmoCounter` / ammo-related `BSTValueEventSink`s, `EquippedWeaponData`.

### 3. Audit remaining "// guessed" offsets in PrismaHUD / other plugins
Sweep the plugins for `reinterpret_cast`/raw-offset reads against forward-declared types (the same class of bug
as the compass alias) and binary-verify each. Likely candidates wherever a comment says "well-known layout".

### 4. Save-load freeze (diagnostic only)
The recurring freeze is an engine `BSStringPool`/Havok-job race during load (see `project_fw_load_freeze_stringpool`
memory) — **not** ours to fix in code, but the named DB can confirm any future thread dump's main-thread frames
quickly (e.g. `LoadGameJob::Run`, `BGSSaveLoadManager::UpdateQueuedTasks`).

### 5. Decompile-verify the 313 candidate CommonLib misname bugs  *(DONE 2026-07-10)*
[06-FUNCTION-INDEX.md](./06-FUNCTION-INDEX.md)'s cross-reference pass flagged 313 addresses where
CommonLib's existing name is specific/confident but contradicted by an independent 1.10.155 named project
match on the community-decoded name — the same signature as the two already-confirmed real bugs.
All 313 were decompile-verified (batch Ghidra decompile + parallel-agent review against the actual
C body): **150 confirmed real bugs**, 68 likely linker code-folding artifacts (not bugs — a newly
documented false-positive pattern), 63 cases where the original CommonLib name was actually right,
32 inconclusive. **Applied to the Ghidra project the same day**: 137 renamed, 13 reverted after a
CommonLibF4-header cross-check found the old name was actually already correct in those cases (see
07-DECOMPILE-CANDIDATES.md's methodology note — check CommonLibF4 headers before applying next
time). Zero live plugin code depended on any of the wrong names, so no CommonLibF4 header/RVA-table
changes were needed. Full breakdown, the actionable list, and every methodology lesson in
[07-DECOMPILE-CANDIDATES.md](./07-DECOMPILE-CANDIDATES.md).

### 6. Apply the 2,803 safe naming-gap fills  *(DONE 2026-07-10)*
The low-risk tier from the same cross-reference: `Class::FuncN`/`_NNN`/`thunk_FUN_` placeholders → real
names. **1,959 same-class fills applied** (`Scripts\ApplySafeGaps.java`), 219 un-nameable STL template
internals skipped. The 202 engine-class *reassignments* that were held out (base-vs-derived ambiguity)
were then **decompile-reviewed and mostly applied too**: **78 renamed** (real inherited base-class
methods — `MovementControllerAI`, `BSNavmeshSearch`, `BSPathingRequest`, `Scaleform::GFx::ImageCreator`,
etc.), 121 skipped as un-nameable giant templates, 3 held (1 derived-specific + 2 inconclusive). The
base-vs-derived caution turned out unfounded for all 202 — none were sibling-class misattributions.
See 03-FINDINGS.md (2026-07-10 reassignment entry) + `Organized\safe_gaps_deferred_reassignments.md`.
(The other 423 originally-deferred rows are `std::_Func_*` template-formatting diffs, ignorable.)

### 7. Resolve the ~20 Havok hknpShape INCONCLUSIVE vtable slots  *(DONE 2026-07-11)*
Resolved via the 1.10.155 named project vtable slot order (a better source than the public Havok SDK header — the
symbols has the exact per-build slot→method mapping). CommonLib's names were shifted by 4 slots; applied the
authoritative names to all 26 hknpShape slots. Same technique then closed **every** remaining
INCONCLUSIVE / KEEP / deferred naming item from the whole cross-reference effort — see 03-FINDINGS.md
(2026-07-11 entry). **No open RE naming follow-ups remain.** New reusable tooling: `FindDumpVtable.java`
(locate+dump any class's vtable in either project), `CleanRename.java` (force-rename clearing colliding
labels). The symbol-vtable-order technique is now the go-to resolver for any "which method is this vtable
slot" question.

## How to add a target
1. Decide the question (offset? address? algorithm?).
2. Run a name search (02-WORKFLOW A) to locate the class/function.
3. Decompile (02-WORKFLOW B) or read the struct layout.
4. Record the result in [03-FINDINGS.md](./03-FINDINGS.md) **and** `Projects\Prisma\docs\COMMONLIB_FINDINGS.md`.
5. Apply to the plugin, build, deploy, and note the fix in the changelog.

## Non-goals / cautions
- Addresses here are **1.10.163 only**. NG (1.11.x) is a different binary — don't reuse RVAs.
- Don't over-RE: if CommonLibF4 already exposes a typed accessor, use it. RE is for the gaps.
- Keep the named copy disposable; never point the pipeline at the user's E: original for writes.
