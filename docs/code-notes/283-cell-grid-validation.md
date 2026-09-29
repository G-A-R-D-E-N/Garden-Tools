# Cell Viewer exterior grid integer validation

Issue: #283

## Trace

`PluginEditTool/web/src/CellPanel.tsx` used `Number.parseInt(gridX, 10)` and `Number.parseInt(gridY, 10)` in both exterior-grid loading and exterior CELL creation. JavaScript accepts a numeric prefix, so `12abc` became `12` and `1.5` became `1` before the native backend saw the request.

The same permissive check also controlled both grid action buttons.

## Fix

`PluginEditTool/web/src/util/cellGrid.ts:parseCellGridCoordinate` accepts only a complete optional-sign plus decimal-digit token within the signed 32-bit CELL grid range. Both load/create paths and both button guards in `PluginEditTool/web/src/CellPanel.tsx` use the same parser.

The int32 bound matters for loading: `PluginEditTool/native/src/plugin/RadiclePluginCell.cpp:intArgument` converts the grid arguments of `GetPlacedReferencesAtGrid` with an unchecked `get<int>()`, so an out-of-range safe integer such as `4294967297` would wrap to a different cell. Create already range-checks natively through `RadiclePluginLifecycle.cpp:lifecycleSignedField`.

The native backend remains authoritative for Fallout 4 coordinate range and CELL semantics. This change only prevents the frontend from silently changing the user's token before dispatch.

## Proof

The current branch carries the regression in `f8dd7b3d` before the implementation in `f29d22d6`. Before the implementation, the requested helper did not exist and the `Number.parseInt` behavior reproduced the defect:

- `12abc` -> `12`
- `1.5` -> `1`

Post-fix parser checks in `PluginEditTool/web/src/CellPanel.test.ts` cover signed integer acceptance at both int32 bounds plus suffix, decimal, exponent, whitespace, empty, and out-of-range refusal.

`PluginEditTool/web/src/CellPanelGrid.test.tsx:Cell Viewer exterior grid dispatch` renders the real panel with a simulated native bridge and plugin picker. Invalid persisted X or Y disables both actions and Enter sends no load request. Correcting an invalid value restores loading. Signed coordinates reach CreateRecord, SavePlugin and the subsequent grid reload unchanged. This verifies React dispatch; it does not verify live WebView2 or in-game behavior.
