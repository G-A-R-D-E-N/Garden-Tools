# xEdit copy workflows (#362)

Evidence source:xEdit/xeMainForm.pas in TES5Edit/TES5Edit `dev-4.1.6`, read on 2026-10-01. Line numbers below refer to that file. These notes record what xEdit does, not what Fallout 4 requires.

## Copy as disabled override

- Menu: Referenced By → `mniRefByCopyDisabledOverrideInto`. It is only visible where "Copy as override into" is visible (line 14789).
- Handler `mniRefByCopyDisabledOverrideIntoClick` (line 8528) calls `CopyInto(False, False, False, False, False, Elements, AfterCopyDisable)`. That is a plain copy as override: not as new, not as a wrapper, not a deep copy, and no overwrite.
- `AfterCopyDisable` (line 8505) only sets `MainRecord.IsInitiallyDisabled := True` on each copied main record. It doesn't change position, the enable parent, XESP or the scale. Those belong to the separate undelete-and-disable (UDR) path (line 11120).

PluginEditTool: `CopyAsDisabledOverride` is `CopyAsOverride` followed by setting the record header's Initially Disabled flag on the copy.

## Copy as wrapper

- Only offered for leveled-list signatures. In the navigation tree these are `LVLB, LVLC, LVLI, LVLN, LVLP, LVSC, LVSP` (line 14660). In the view header they are `LVLC, LVLI, LVSP, LVLN` (line 14898). Fallout 4 has `LVLI`, `LVLN` and `LVSP`.
- `CopyInto` with `AsWrapper` (line 2738), for each record:
  1. Copy the source as a new record into the target, with an EditorID that has a user prefix or suffix applied. A different EditorID is required (line 2599).
  2. Copy the source as an override into the target, reset it to defaults (`MainRecord.Assign(Low(Integer), nil, False)`), and make sure "Leveled List Entries" holds exactly one entry.
  3. Point that entry's reference at the new record from step 1, with Count 1 and Level 1, and restore the original EditorID on the override.
- Result: the override becomes a one-entry list that defers to a renamed copy of the original list.

Not implemented yet. Step 2's "reset to defaults" has no Radicle equivalent for LVLI/LVLN/LVSP. It needs the authoring schema for those records (#287) so that the defaults are written from evidence rather than guessed.

## Inject Forms into master

- Menu `mniNavRenumberFormIDsInject`. It is visible only for a file that has masters (line 14552).
- It shares its handler with "Renumber FormIDs from…" (line 12298). The user picks one master as `TargetFile` and, optionally, whether to preserve object IDs. Each of the source file's own records is then renumbered so its file index is the target master's. For a light target the object IDs go up to `0xFFF`, otherwise up to `0xFFFFFF` (line 12385). An update module can't be a target (line 12327).
- The records stay in the source plugin, but their FormKeys now belong to the master. The game resolves them as records the master never defined.

Not implemented. Whether Fallout 4 accepts such injected records, and how the CK treats them, has not been confirmed from runtime or CK evidence. Per the Radicle evidence order (`Radicle/ROADMAP.md`), xEdit's ability to produce them is not enough.
