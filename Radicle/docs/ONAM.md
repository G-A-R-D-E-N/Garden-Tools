# Fallout 4 TES4 ONAM writer rule

Status: **source-proven against xEdit `TwbFile.PrepareSave` on the `dev-4.1.6` branch**.

Primary source:

- `TES5Edit/TES5Edit/Core/wbImplementation.pas`, `TwbFile.PrepareSave`
- ONAM rebuild block around lines 4986-5088 on the referenced branch
- `TwbMainRecord.GetIsPersistent`, which returns the record-header `Persistent` flag
- xEdit `whatsnew.md`, `ONAMUpdate Mode`

## What ONAM is for

xEdit documents ONAM as the file-header list used for overridden temporary records. ESM-flagged files require this metadata so the game can load those temporary records on demand. xEdit removes existing ONAM during save and rebuilds it.

## Fallout 4 inclusion rule

For Fallout 4, xEdit considers these signatures while rebuilding ONAM:

- `NAVM`
- `LAND`
- `REFR`
- `PGRE`
- `PMIS`
- `ACHR`
- `PARW`
- `PBEA`
- `PFLA`
- `PCON`
- `PBAR`
- `PHZD`
- `SCEN`
- `DLBR`
- `DIAL`
- `INFO`

The source also enters the branch for `CELL`, but explicitly does not emit the CELL FormID itself.

This is the Fallout 4 `TES4` `ONAM` definition, `wbArray(ONAM, 'Overridden Forms', wbFormIDCk('Form', [ACHR,DIAL,DLBR,INFO,LAND,NAVM,PARW,PBAR,PBEA,PCON,PFLA,PGRE,PHZD,PMIS,REFR,SCEN]))` (source:TES5Edit/TES5Edit@dev-4.1.6 `Core/wbDefinitionsFO4.pas:12109`). The `PrepareSave` branch is shared across games: it also names `ACRE`, which is not a Fallout 4 record type, and `PARW`. `Radicle/src/plugin/onam.cpp:participates_in_fo4_onam` first listed `ACRE` and left out `PARW`, so a temporary `PARW` override was missing from ONAM. Test: `Radicle/tests/test_onam.cpp:Fo4ParticipantsIncludeParwAndNotAcre`.

A record is emitted only when:

1. it is an override owned by one of the file's masters, not a new local record;
2. it is in the supported signature set above; and
3. its record-header `Persistent` flag (`0x00000400`) is clear.

`TwbMainRecord.GetIsPersistent` reads that header flag directly. xEdit normally keeps CELL child-group placement consistent with it, moving persistent placed records to the persistent-children group and temporary records to the temporary-children group, but ONAM rebuild itself tests the flag. Radicle therefore also uses the flag directly rather than inferring persistence from GRUP type.

## Ordering and replacement

xEdit deletes existing ONAM before rebuilding. It walks masters in master-table order and records in fixed-FormID order. Radicle emits one deterministic ONAM array sorted by the current plugin-relative raw FormID, which has the same master-ordinal-then-local-ID ordering for on-disk FormIDs. Stale ONAM is removed when no eligible overrides remain.

Master reorder must preserve the logical FormKeys represented by ONAM even though the serialized high-byte master ordinals change.

Dirty master plugins (master flag set, or an `.esl` name per `Radicle/src/formid.cpp:is_master_plugin`) rebuild ONAM automatically on write. xEdit also treats an `.esm` name as master (source:xEdit Core/wbLoadOrder.pas:332-334 @93cc0bc5); Radicle does not yet, so an `.esm` without the master flag is not rebuilt. Clean plugins still return their preserved original bytes without being rebuilt.

## CopyAsOverride containment boundary

ONAM maintenance alone does not make every missing-record override insertion safe. Placed references, LAND/NAVM, INFO, and similar records may require nested parent GRUP hierarchy.

Radicle therefore distinguishes two cases:

- replacing an override already present in the destination is allowed because its existing containment is preserved;
- inserting a missing record is allowed only when the source record is a direct child of a top-level GRUP whose header can be reproduced safely.

A missing nested source record fails with `source_containment_unsupported` in Radicle and `RecordContainmentUnsupported` through PluginEditTool. This prevents REFR/INFO/world records from being silently moved into invalid top-level groups.

With that boundary enforced, PluginEditTool may write containment-safe top-level overrides into editable master-flagged targets. The writer rebuilds ONAM automatically on the resulting dirty master file.
