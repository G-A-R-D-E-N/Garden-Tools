# Fallout 4 TES4 ONAM writer rule

Status: **source-proven against xEdit `TwbFile.PrepareSave` on the `dev-4.1.6` branch**.

Primary source:

- `TES5Edit/TES5Edit/Core/wbImplementation.pas`, `TwbFile.PrepareSave`
- ONAM rebuild block around lines 4986-5088 on the referenced branch
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
- `ACRE`
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

A record is emitted only when:

1. it is an override owned by one of the file's masters, not a new local record;
2. it is in the supported signature set above; and
3. it is not persistent.

For Radicle's structural tree, records below a `cell_persistent_children` GRUP are therefore excluded. Temporary-cell descendants are eligible. Record flags are not treated as a universal persistence bit because the same flag positions have record-type-specific meanings elsewhere in the format.

## Ordering and replacement

xEdit deletes existing ONAM before rebuilding. It walks masters in master-table order and records in fixed-FormID order. Radicle emits one deterministic ONAM array sorted by the current plugin-relative raw FormID, which has the same master-ordinal-then-local-ID ordering for on-disk FormIDs. Stale ONAM is removed when no eligible overrides remain.

Master reorder must preserve the logical FormKeys represented by ONAM even though the serialized high-byte master ordinals change.

Dirty master-flagged Radicle writes rebuild ONAM automatically. Clean plugins still return their preserved original bytes without being rebuilt.

## CopyAsOverride containment boundary

ONAM maintenance alone does not make every missing-record override insertion safe. Placed references, LAND/NAVM, INFO, and similar records may require nested parent GRUP hierarchy.

Radicle therefore distinguishes two cases:

- replacing an override already present in the destination is allowed because its existing containment is preserved;
- inserting a missing record is allowed only when the source record is a direct child of a top-level GRUP whose header can be reproduced safely.

A missing nested source record fails with `source_containment_unsupported` in Radicle and `RecordContainmentUnsupported` through PluginEditTool. This prevents REFR/INFO/world records from being silently moved into invalid top-level groups.

With that boundary enforced, PluginEditTool may write containment-safe top-level overrides into editable master-flagged targets. The writer rebuilds ONAM automatically on the resulting dirty master file.
