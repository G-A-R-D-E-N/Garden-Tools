# xEdit copy workflows (#362, #364)

Evidence source: `xEdit/xeMainForm.pas` and `xEdit/xeMainForm.dfm` in TES5Edit/TES5Edit at commit 93cc0bc5a1251936c3c7859eee3150eda12a62d7, the xEdit pin. Line numbers below refer to that commit and were checked against it on 2026-10-13. These notes record what xEdit does, not what Fallout 4 requires.

| Workflow | Status |
|---|---|
| Copy as disabled override | Implemented: `PluginEditTool/native/src/plugin/RadiclePluginDisabledOverride.cpp` |
| Copy as wrapper | Implemented: `PluginEditTool/native/src/plugin/RadiclePluginCopyAsWrapper.cpp`. Shape differs from xEdit, see below. |
| Inject Forms into master | Implemented as `InjectFormsIntoMaster` (#362). The runtime behavior is recorded below. |
| Fixup race-specific leveled items | Declined (#364) |
| Copy idle animations into... | Declined (#364) |

## Copy as disabled override

- Menu: Referenced By → `mniRefByCopyDisabledOverrideInto`. It is only visible where "Copy as override into" is visible (line 15758).
- Handler `mniRefByCopyDisabledOverrideIntoClick` (line 9370) calls `CopyInto(False, False, False, False, False, Elements, AfterCopyDisable)`. That is a plain copy as override: not as new, not as a wrapper, not a deep copy, and no overwrite.
- `AfterCopyDisable` (line 9348) only sets `MainRecord.IsInitiallyDisabled := True` on each copied main record. It doesn't change position, the enable parent, XESP or the scale. Those belong to the separate undelete-and-disable (UDR) path (`mniNavUndeleteAndDisableReferencesClick`, line 11803).

PluginEditTool: `CopyAsDisabledOverride` is `CopyAsOverride` followed by setting the record header's Initially Disabled flag on the copy.

## Copy as wrapper

- Only offered for leveled-list signatures. In the navigation tree these are `LVLB, LVLC, LVLI, LVLN, LVLP, LVSC, LVSP` (lines 14660-14669). In the view header they are `LVLC, LVLI, LVSP, LVLN` (line 14898). Fallout 4 has `LVLI`, `LVLN` and `LVSP`.
- `TfrmMain.CopyInto` with `AsWrapper` (function at line 2360, wrapper branch at lines 2738-2770), for each record:
  1. Copy the source as a new record into the target, with an EditorID that has a user prefix or suffix applied (line 2745). A different EditorID is required for one record (lines 2612-2616), a prefix or suffix for several (lines 2633-2639).
  2. Copy the source as an override into the target, reset it to defaults (`MainRecord.Assign(Low(Integer), nil, False)`, line 2756), and make sure "Leveled List Entries" holds exactly one entry (lines 2757-2768).
  3. Point that entry's reference at the new record from step 1, with Count 1 and Level 1, and restore the original EditorID on the override (lines 2767-2769).
- Result: the override becomes a one-entry list that defers to a renamed copy of the original list.

PluginEditTool (#362): `CopyAsWrapper` follows the flow above for LVLI, LVLN and LVSP only; other types are refused with `WrapperUnsupportedType`. xEdit's menu test (`xeMainForm.pas:14660-14669`) also shows the wrapper for LVLB, LVLC, LVLP and LVSC, which this port does not accept. The copy's EditorID is the caller's value, required to differ from the source EditorID case-insensitively (`WrapperEditorIdSameAsSource`); empty is allowed. The source owner is added as a master of the target, as copy-as-override does. The selected FormKey becomes the one-entry wrapper, and the copy gets a new FormKey. An existing override of the selected FormKey in the target is reused only when its content equals the source's, EDID aside, compared in the source's master context (the target record is re-encoded into the source's form ID context first). Otherwise Copy as wrapper refuses with `WrapperTargetOverrideDiffers`, so the target's entries are kept.

The wrapper override takes the picked source record's EditorID, or none if the source has none. A `LOSES:` result is returned without writing when a plugin that loads after the target also overrides the record, as `CopyAsOverride` does.

## Inject Forms into master

- Menu `mniNavRenumberFormIDsInject`. It is visible only for a file that has masters (lines 15521-15524).
- It shares its handler with "Renumber FormIDs from…" (`mniNavRenumberFormIDsFromClick`, line 13054; the inject branch is lines 13126-13156). The user picks one master as `TargetFile` and, optionally, whether to preserve object IDs and whether to abort if not all of them can be preserved. Each of the source file's own records is then renumbered so its file index is the target master's. For a light target the object IDs go up to `0xFFF`, otherwise up to `0xFFFFFF` (lines 13218-13223). An update module can't be a target (lines 13158-13161).
- The records stay in the source plugin, but their FormKeys now belong to the master.

Runtime behavior (source: the #362 comment of 2026-10-04, which cites the RE evidence tracked in #374 and #385; RE note `injected-forms-385/README.md` in the Fallout4_Runtime_Research repository, branch `re/garden-tools-issues-2026-10-04`):

- The injecting plugin becomes `sourceFiles[0]`, while the form ID carries the master's index.
- A later same-ID master record silently overwrites (and one of a different type is skipped).
- Light-master ids are masked to `0xFFF` with no range check.

So the engine neither reports an ID collision nor enforces the light range; a writer would have to do both itself. #362 records no CK-side note for injected forms.

PluginEditTool (#362): `InjectFormsIntoMaster(plugin, master, start, apply)` renumbers the plugin's own records to consecutive ids in the chosen master, starting at `start`. It is a dry run unless `apply` is true, and it stages changes without writing; `SavePlugin` persists them. Radicle's `fo4::inject_local_record` does the rekey, and it repoints in-plugin FormLinks to the new keys. The op refuses:

- `InjectMasterNotListed`: the master is not in the plugin's master list.
- `InjectMasterNotLoaded`: the master is not loaded, so id collisions cannot be checked.
- `InjectRangeInvalid`: `start` or `start + count - 1` is outside `fo4::local_form_id_range(master)`. A light master's bound is taken from that function, not from a constant.
- `InjectTargetOccupied`: a target FormKey is already used in the plugin or in the master. The message names the key.
- The nested-record refusal and `parentGroupRelabelRefusal`, as renumber has them.

Other plugins' references to the old ids are not repointed; the apply message says they dangle until patched.

The runtime evidence above is why the op checks both collisions and the range itself. Per the Radicle evidence order (`Radicle/ROADMAP.md`), runtime evidence takes precedence, and xEdit's ability to produce injected records is not enough on its own.

## Declined (#364)

Both are declined for Fallout 4 because xEdit itself never offers them for it, so there is no Fallout 4 behavior to reproduce. The decision was recorded on #364 on 2026-10-02 from xEdit `dev-4.1.6` and re-checked at the pin on 2026-10-13.

### Fixup race-specific leveled items

- `xeMainForm.dfm:2672-2677` declares `mniNavRaceLVLIs` ("Fixup Race-specific LVLIs") with `Visible = False`. `xeMainForm.pas` mentions the item only in its field declaration (line 321), its handler declaration (line 654) and the handler (lines 11005-11069), so no code makes it visible. It is hidden for every game.
- The handler is an EditorID naming convention, not record data: for each NPC_ with a race, an Items entry that points at an LVLI is repointed to the LVLI whose EditorID is that list's EditorID followed by the race's EditorID, when one exists.

### Copy idle animations into...

- `xeMainForm.pas:15642` sets `mniNavCopyIdle.Visible := (wbGameMode <= gmFNV) and ...`. `TwbGameMode` (`Core/wbInterface.pas:4519`) declares `gmFO4` after `gmFNV`, so the condition is false for Fallout 4 and xEdit never shows the item.
- The handler (lines 3695-3808) works on IDLE records grouped by the folder of their `MODL\MODL` path.
