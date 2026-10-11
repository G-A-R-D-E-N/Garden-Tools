# Radicle copy of nested CELL children (GT-200)

`copy_record_as_override` used to refuse every record that is not directly under a top-level GRUP, so a placed
record in a CELL child GRUP (type 8, 9 or 10) failed with `source_containment_unsupported`. It now copies those
records and recreates their parent chain in the destination. Anything the plan does not fully cover still
returns `source_containment_unsupported` and leaves the destination unchanged.

## Radicle/src/plugin/copy_hierarchy.cpp:plan_nested_copy

Finds the source record's GRUP path by pointer and accepts only these shapes, outermost first:

- Interior: `CELL` top, 2, 3, 6, then 8, 9 or 10.
- Exterior: `WRLD` top, 1, 4, 5, 6, then 8, 9 or 10.
- Worldspace persistent: `WRLD` top, 1, 6, then 8, 9 or 10.

The GRUP 6 label must resolve to the parent CELL, which must be the record directly before that GRUP. The leaf
GRUP label must equal the GRUP 6 label. For exterior and persistent shapes the GRUP 1 label must resolve to the
parent WRLD, which must be the record directly before GRUP 1 in the top group.

Accepted kinds are the reference list (REFR ACHR ACRE PGRE PMIS PARW PBEA PFLA PCON PBAR PHZD) in types 8, 9 and
10, plus PGRD, LAND and NAVM only in type 9. Source citations:
`source:xEdit Core/wbImplementation.pas:17492-17510 @93cc0bc5` (kind lists) and
`source:xEdit Core/wbImplementation.pas:17125-17447 @93cc0bc5` (GRUP nesting, group 6 label and CELL/WRLD
parents). Evidence status: concept-level from xEdit source, not checked against a Fallout4.esm sample. TODO-verify
with a corpus test when one exists.

Known difference: the parent CELL and WRLD come from the `source` argument only. xEdit resolves them through
`HighestOverrideVisibleForFile` across the load order (`source:xEdit Core/wbImplementation.pas:17199 @93cc0bc5`,
`source:xEdit Core/wbImplementation.pas:17377 @93cc0bc5`), and Radicle has no load-order view here. A parent that
is only defined by an earlier master is therefore refused, not copied from that master.

Lookup note: the brief said placement resolves a child GRUP to the record before it. `Radicle/src/plugin/placement.cpp:place_group`
resolves by the group label instead. The plan requires both the adjacency and the label, so either source layout
the reader can see is handled and anything else fails closed.

## Radicle/src/plugin/copy_hierarchy.cpp:insert_nested_copy

Edits the caller's copy of the destination only. `Radicle/src/plugin/mutation.cpp:copy_record_as_override` assigns that
copy back only on success, so a false return discards every partial change made here.

- Top group: found by signature, or created with the source header.
- World: the WRLD record is found or appended in the top group, then GRUP 1 must sit directly after it. A WRLD or
  GRUP 1 that exists elsewhere is refused.
- Blocks: GRUP 2, 3, 4 and 5 are found by type and label, or created. Labels of types 2, 3, 4 and 5 are block and
  grid values and are copied byte for byte.
- Cell: the CELL record is found directly in the innermost container, or inserted as a clone. A CELL that exists
  elsewhere is refused. The GRUP 6 must sit directly after the CELL record, and a GRUP 6 with the same label
  elsewhere in the container is refused.
- Leaf: the GRUP 8, 9 or 10 is found or created inside GRUP 6 by `find_or_add_leaf_group`, and the copied record is
  appended to it.

## Radicle/src/plugin/copy_hierarchy.cpp:insert_nested_copy — persistent CELL position

A new persistent CELL (path size 4, WRLD top, GRUP 1, GRUP 6, GRUP 8) is inserted at the start of the GRUP 1 children,
with its GRUP 6 directly after it, and not appended. The reviewer's reading of vanilla layout is that the persistent
CELL and its GRUP 6 come first in a WRLD's GRUP 1, before any type 4 block group. That layout was not checked against
a Fallout4.esm sample here. The test `PersistentCellPrecedesBlockGroupsInWorldChildren` pins the order.

## Radicle/src/plugin/copy_hierarchy.cpp:find_or_add_leaf_group — GRUP 6 order

A new leaf group inside GRUP 6 is inserted before the first sibling with a higher group type, so the children stay in
ascending type order (8, 9, 10). xEdit sorts a group's children by a sort key that starts with the group type
(`source:xEdit Core/wbImplementation.pas:17999-18014 @93cc0bc5`). Only leaf groups are sorted this way, because
`find_or_add_child_group` still appends the GRUP 1, 2, 3, 4 and 5 path groups. The test
`CellChildGroupsStayInAscendingTypeOrder` copies a type 9 record and then a type 8 record and asserts 8 then 9.

## Radicle/src/plugin/copy_hierarchy.cpp:insert_nested_copy — refusals stricter than xEdit

The CELL-elsewhere and WRLD-elsewhere refusals are stricter than xEdit. xEdit's record add looks the source
form ID up in the destination file and reuses the record it finds (`source:xEdit Core/wbImplementation.pas:17030-17034 @93cc0bc5`,
reuse branch at `source:xEdit Core/wbImplementation.pas:17044 @93cc0bc5`), so an existing CELL or WRLD in another
place is reused rather than refused. Radicle refuses in `insert_nested_copy` (the `record_index` and `find_record`
checks for the WRLD and the CELL) because a copy into the expected group would duplicate a FormKey. They stay
fail-closed as a guard for an anomalous layout where the parent record is outside the group the chain expects.

Labels that carry a form ID (types 1, 6, 8, 9 and 10) are re-encoded for the destination by
`Radicle/src/plugin/copy_hierarchy.cpp:destination_header`. Parent clones use `Radicle/src/plugin/copy_hierarchy.cpp:clone_record`,
which applies the same link remap and clears cached bytes as the copied record in `copy_record_as_override`. Header
flags, including Partial Form, are copied verbatim from the source, and no new partial form is made. Whether xEdit's
`wbAllowMakePartial` default matches that is not verified here.

In xEdit the GRUP 3 and 5 path reaches the parent CELL through its own child group (`source:xEdit Core/wbImplementation.pas:17378-17382 @93cc0bc5`)
rather than a label search. For a valid plugin the label and adjacency rules here give the same parent, and any other
shape fails closed.

## Radicle/src/plugin/mutation.cpp:copy_record_as_override

Parent masters are required like the copied record. Each key in the record and parent CELL and WRLD keys goes through
the same link-owner loop, so masters exist before form ID labels are encoded. Writer behaviour (group sizes, HEDR
count, ONAM) is unchanged: `Radicle/src/plugin/writer.cpp:write_group` recomputes sizes, and ONAM is rebuilt at
write time.

## Not covered

DIAL, INFO and quest children (`source:xEdit Core/wbImplementation.pas:17505-17507 @93cc0bc5` covers DIAL, DLBR and SCEN
only under quest children), and any shape outside the three above. PET keeps its message for those cases.

## Related code, not changed

`Radicle/src/forms/placed_reference_copy.cpp:ensure_group_path` and `Radicle/src/forms/placed_reference_copy.cpp:ensure_cell_children_group`
already build the same group chain for placed references, in an anonymous namespace inside a file already over the
750-line limit. A later change can hoist these into a shared header and reuse them here.
