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

xEdit deletes existing ONAM before rebuilding. It walks masters in master-table order and records in fixed-FormID order. Radicle therefore emits one deterministic ONAM array sorted by the current plugin-relative raw FormID and removes stale ONAM when no eligible overrides remain.

Master reorder must preserve the logical FormKeys represented by ONAM even though the serialized high-byte master ordinals change.

## Scope boundary

ONAM maintenance alone does **not** make arbitrary `CopyAsOverride` into an ESM target safe. Placed references, LAND/NAVM, INFO, and similar records have structural containment requirements. Radicle must preserve or explicitly construct the correct parent GRUP hierarchy before PluginEditTool removes its current master-target override refusal.

That containment work is separate from ONAM metadata maintenance and remains fail-closed until proven.
