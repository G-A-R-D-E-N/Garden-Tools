# 06 — Subsystem Function Index & 155-vs-163 Cross-Reference (2026-07-10)

A browsable organization pass over the workspace's raw RE symbol dumps, plus a cross-reference
that uses the independent 1.10.155 named project to catch more CommonLib RTTI-matching errors in the
1.10.163 target project — the same bug class that already caught two real ones
(`PlayerCharacter::ServePrisonTime`/`GoToPrison`, see `05-LABEL-RECOVERY.md` §3).

## What exists now

| Artifact | What | Regenerate |
|---|---|---|
| `ReverseEngineering\Symbols\1.10.163\Fallout4_163_functions.txt` | Full function export of the **1.10.163 project** (365,059 functions, real addresses — the build we actually hook) in the same browsable format as the existing 1.10.155 named project dump. Didn't exist before this pass — the 1.10.163 project only had query/decompile access, no flat dump. | `ReverseEngineering\Scripts\FW_ExportAll.java` via `run_query.ps1`-style `analyzeHeadless` invocation (pure listing walk, no re-analysis, ~1 min at this scale) |
| `ReverseEngineering\Organized\INDEX.md` | Subsystem breakdown (19 categories: Havok, Scaleform, Papyrus, TES/BGS forms, Actor, NetImmerse graphics, AI/Combat, Pathing, SaveLoad, Input, STL, BS templates, ...) with function counts for **both** 1.10.155 and 1.10.163 side by side | `python ReverseEngineering\Scripts\organize_functions.py` |
| `ReverseEngineering\Organized\1.10.155\<Category>.txt` / `1.10.163\<Category>.txt` | The actual bucketed function lists — same category set in both versions, so "what does subsystem X look like" is directly comparable across the version gap | same script |
| `ReverseEngineering\Organized\crossref_163_disagreements_vs_155.csv` | Every one of the 40,073 `label_apply_disagreements.csv` rows (05-LABEL-RECOVERY.md §3) plus a `verdict_vs_1.10.155` column | same script |
| `ReverseEngineering\Organized\crossref_review_candidates.md` | **313 high-priority candidates** — see below | same script |
| `ReverseEngineering\Organized\crossref_safe_naming_gaps.md` | **2,803 low-risk candidates** — see below | same script |

`ReverseEngineering\Symbols\` (all of it — exe/symbols/csv/idc + this new `organized\` output, ~1GB+ combined) is now
gitignored; it's fully regeneratable from the game files + these scripts, and redistributing the
exe/symbols is licensing-murky. Never commit it.

## Why cross-reference against 1.10.155 at all

`05-LABEL-RECOVERY.md` §4 explicitly deprioritized Version Tracking (cross-version correlation)
because the community IDC label file already gives **657,980** direct, version-exact names for
1.10.163 — better than VT would produce. That's still true for *filling gaps*. But it doesn't
help with the 40,073 rows where the community label **disagreed** with CommonLib's existing name
and so was conservatively left unapplied (05-LABEL-RECOVERY.md's "don't silently overwrite a
real name" rule) — those rows are exactly where a genuine CommonLib RTTI-matching error would
hide, and the original two bugs were found by hand-spot-checking a tiny sample of them.

The 1.10.155 named project is a **third, independent, Microsoft-verified** source (not derived from either
CommonLib's RTTI matcher or the community label file). If a disagreement row's community-decoded
name has an exact match in 1.10.155 but CommonLib's existing name does **not**, that's
meaningfully stronger evidence than the disagreement alone — the same signature as the two
already-confirmed bugs.

## Method

1. Exported the 1.10.163 project's full function list (previously only queryable, not dumped) —
   `FW_ExportAll.java`, a plain `FunctionManager.getFunctions()` walk writing
   `Class::Method :: <prototype>` per line, no decompile/re-analysis.
2. Extracted a fully-qualified name from every 1.10.155 named project signature line (tracking `<...>`
   template-bracket depth so the name/args boundary is found correctly even through generics).
3. Bucketed both function lists into 19 subsystem categories by **top-level class prefix**
   (`hk*`/`bhk*` → Havok, `TES*` → Elder Scrolls forms, `BGS*` → Bethesda Game Studio records,
   `Ni*` → NetImmerse graphics, `BST*` → Bethesda template containers, keyword rules for
   AI/Combat/Pathing/SaveLoad/Input/Audio/Animation, ...). This is a naming-convention heuristic,
   not a real namespace model — FO4's engine doesn't have subsystem namespaces — but it turns two
   400K-line flat files into something actually browsable by topic. `Misc_Uncategorized` is ~24-27%
   of each version; a real gap, not swept under the rug (see `INDEX.md`'s own counts).
4. For each `label_apply_disagreements.csv` row, extracted `Class::Method`-shaped tokens from both
   the existing name and the decoded name and checked each against the full 1.10.155 name set
   (functions + data symbols).
5. **False-positive fix, important:** many CommonLib "existing" names are legitimate but
   *unqualified* (e.g. `Call`, `PostLinkObject`, `LinkObject` — real vtable-slot method names
   missing their class prefix). A raw qualified-token check can't verify those against 155 at all
   (no `::` to extract), so it initially misclassified 253 of them as "wrong." Fixed with a bare
   method-name fallback: if the existing name has no `::` but its bare identifier matches the tail
   of a 155-confirmed decoded token, treat it as confirmed (just missing qualification), not wrong.
6. Split the resulting candidates into two tiers by whether the current CommonLib name is a
   generic placeholder (`Class::FuncN`, bare `_NNN`, `thunk_FUN_...`, empty `Class::` — CommonLib's
   honest "don't know" marker per 05-LABEL-RECOVERY.md §3) or a specific, confident-looking name:
   - **`crossref_safe_naming_gaps.md` (2,803 rows):** placeholder → real name. Low risk — no
     existing confident claim is being overturned, just filled in. Good candidates for a batch
     CommonLib header update; don't need individual decompile verification.
   - **`crossref_review_candidates.md` (313 rows):** specific existing name, still contradicted by
     an independent 155 match. This is the dangerous class — CommonLib was *confident and wrong*.
     **Not yet individually decompile-verified** — this is a prioritized candidate list, not a
     confirmed-bug list.

## Notable candidates spotted on inspection (not yet decompile-verified)

A few from `crossref_review_candidates.md` stood out on a manual skim as unusually strong —
worth decompiling first if picking this list up:

- `0x142122990` current name `KERNEL32.DLL::GetTickCount64`, 155-confirmed as
  `Scaleform::Timer::GetTicksMs`. `KERNEL32.DLL::*`-style names are IAT/thunk labels, not real
  engine methods — this one landed on a real Scaleform class method, suggesting a stale/generic
  label got attached during an earlier naming pass.
- `0x1421246D0` current name `MSVCR110.DLL::_stricmp`, 155-confirmed as
  `Scaleform::String::CompareNoCase`. Same pattern as above.
- `0x1422F3770` / `0x142216DC0` current name `CRectTracker::CRectTracker` /
  `CControlRectTracker::CControlRectTracker` (155-confirmed as
  `Scaleform::HeapPT::SysAllocGranulator::SysAllocGranulator` and
  `Scaleform::GFx::AS3::Classes::fl_events::EventDispatcher::EventDispatcher` respectively).
  `CRectTracker`/`CControlRectTracker` are **MFC class names**, not Fallout 4 engine classes at
  all — strong evidence of leftover generic/template naming pollution rather than a genuine
  RTTI-matcher mistake on a real FO4 type. Worth checking whether other addresses share this same
  wrong label (grep `organized\1.10.163\` for `CRectTracker`).
- `0x141D819B0` / `0x141D81170`: `bhkCharacterController::SetSceneGraphObject`/
  `UpdateWorldData` vs 155-confirmed `bhkNPCollisionObjectUnlinked`/`bhkNPCollisionObject`
  equivalents — plausible sibling-class vtable-slot confusion (adjacent classes in the same Havok
  collision-object hierarchy), the same failure mode as the original two confirmed bugs.

## Next steps (not done in this pass)

1. ~~Decompile-verify the 313 `crossref_review_candidates.md` rows~~ **DONE 2026-07-10** — all 313
   verified, 137 real bugs applied to the Ghidra project. See `07-DECOMPILE-CANDIDATES.md`.
2. ~~Batch-apply the 2,803 `crossref_safe_naming_gaps.md` rows~~ **MOSTLY DONE 2026-07-10** — 1,959
   same-class fills applied (`Scripts\ApplySafeGaps.java`), 219 un-nameable STL internals skipped,
   202 engine-class reassignments deferred for decompile review (`safe_gaps_deferred_reassignments.md`).
   See `03-FINDINGS.md` (2026-07-10 safe-gap entry) and `04-ROADMAP.md` item 6.
3. `Misc_Uncategorized` (113K / 87K functions) could be further subdivided if a specific subsystem
   search keeps landing there — the category rule list in `organize_functions.py` is meant to be
   extended, not treated as final.
