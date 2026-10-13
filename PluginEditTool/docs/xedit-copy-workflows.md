# xEdit copy workflows (#362, #364)

Evidence source: `xEdit/xeMainForm.pas` and `xEdit/xeMainForm.dfm` in TES5Edit/TES5Edit at commit 93cc0bc5a1251936c3c7859eee3150eda12a62d7, the xEdit pin. Line numbers below refer to that commit and were checked against it on 2026-10-13. These notes record what xEdit does, not what Fallout 4 requires.

| Workflow | Status |
|---|---|
| Copy as disabled override | Implemented: `PluginEditTool/native/src/plugin/RadiclePluginDisabledOverride.cpp` |
| Copy as wrapper | Not implemented yet. Not declined. |
| Inject Forms into master | Not implemented yet. Not declined. The runtime behavior is recorded below. |
| Fixup race-specific leveled items | Declined (#364) |
| Copy idle animations into... | Declined (#364) |

## Copy as disabled override

- Menu: Referenced By → `mniRefByCopyDisabledOverrideInto`. It is only visible where "Copy as override into" is visible (line 15758).
- Handler `mniRefByCopyDisabledOverrideIntoClick` (line 9370) calls `CopyInto(False, False, False, False, False, Elements, AfterCopyDisable)`. That is a plain copy as override: not as new, not as a wrapper, not a deep copy, and no overwrite.
- `AfterCopyDisable` (line 9348) only sets `MainRecord.IsInitiallyDisabled := True` on each copied main record. It doesn't change position, the enable parent, XESP or the scale. Those belong to the separate undelete-and-disable (UDR) path (`mniNavUndeleteAndDisableReferencesClick`, line 11803).

PluginEditTool: `CopyAsDisabledOverride` is `CopyAsOverride` followed by setting the record header's Initially Disabled flag on the copy.

## Copy as wrapper

- Only offered for leveled-list signatures. In the navigation tree these are `LVLB, LVLC, LVLI, LVLN, LVLP, LVSC, LVSP` (lines 15629-15638). In the view header they are `LVLC, LVLI, LVSP, LVLN` (line 15867). Fallout 4 has `LVLI`, `LVLN` and `LVSP`.
- `CopyInto` with `AsWrapper` (function at line 2817, wrapper branch at lines 3195-3231), for each record:
  1. Copy the source as a new record into the target, with an EditorID that has a user prefix or suffix applied (line 3202). A different EditorID is required for one record (line 3072), a prefix or suffix for several (line 3101).
  2. Copy the source as an override into the target, reset it to defaults (`MainRecord.Assign(Low(Integer), nil, False)`, line 3213), and make sure "Leveled List Entries" holds exactly one entry (lines 3214-3218).
  3. Point that entry's reference at the new record from step 1, with Count 1 and Level 1, and restore the original EditorID on the override (lines 3223-3226).
- Result: the override becomes a one-entry list that defers to a renamed copy of the original list.

Not implemented yet, and not declined. Step 2's "reset to defaults" has no Radicle equivalent for LVLI/LVLN/LVSP: `Radicle/docs/LEVELED_LISTS.md` records no LVLI or LVLN creator. It needs the authoring schema for those records (#287) so that the defaults are written from evidence rather than guessed.

## Inject Forms into master

- Menu `mniNavRenumberFormIDsInject`. It is visible only for a file that has masters (lines 15521-15524).
- It shares its handler with "Renumber FormIDs from…" (`mniNavRenumberFormIDsFromClick`, line 13054; the inject branch is lines 13126-13156). The user picks one master as `TargetFile` and, optionally, whether to preserve object IDs and whether to abort if not all of them can be preserved. Each of the source file's own records is then renumbered so its file index is the target master's. For a light target the object IDs go up to `0xFFF`, otherwise up to `0xFFFFFF` (lines 13218-13223). An update module can't be a target (lines 13158-13161).
- The records stay in the source plugin, but their FormKeys now belong to the master.

Runtime behavior (source: the #362 comment of 2026-10-04, which cites the RE evidence tracked in #374 and #385; RE note `injected-forms-385/README.md` in the Fallout4_Runtime_Research repository, branch `re/garden-tools-issues-2026-10-04`):

- The injecting plugin becomes `sourceFiles[0]`, while the form ID carries the master's index.
- A later same-ID master record silently overwrites (and one of a different type is skipped).
- Light-master ids are masked to `0xFFF` with no range check.

So the engine neither reports an ID collision nor enforces the light range; a writer would have to do both itself. #362 records no CK-side note for injected forms.

Not implemented yet, and not declined. The runtime evidence exists; what is missing is the decision to implement and the implementation. Per the Radicle evidence order (`Radicle/ROADMAP.md`), runtime evidence takes precedence, and xEdit's ability to produce injected records is not enough on its own.

## Declined (#364)

Both are declined for Fallout 4 because xEdit itself never offers them for it, so there is no Fallout 4 behavior to reproduce. The decision was recorded on #364 on 2026-10-02 from xEdit `dev-4.1.6` and re-checked at the pin on 2026-10-13.

### Fixup race-specific leveled items

- `xeMainForm.dfm:2672-2677` declares `mniNavRaceLVLIs` ("Fixup Race-specific LVLIs") with `Visible = False`. `xeMainForm.pas` mentions the item only in its field declaration (line 321), its handler declaration (line 654) and the handler (lines 11005-11069), so no code makes it visible. It is hidden for every game.
- The handler is an EditorID naming convention, not record data: for each NPC_ with a race, an Items entry that points at an LVLI is repointed to the LVLI whose EditorID is that list's EditorID followed by the race's EditorID, when one exists.

### Copy idle animations into...

- `xeMainForm.pas:15642` sets `mniNavCopyIdle.Visible := (wbGameMode <= gmFNV) and ...`. `TwbGameMode` (`Core/wbInterface.pas:4519`) declares `gmFO4` after `gmFNV`, so the condition is false for Fallout 4 and xEdit never shows the item.
- The handler (lines 3695-3808) works on IDLE records grouped by the folder of their `MODL\MODL` path.
