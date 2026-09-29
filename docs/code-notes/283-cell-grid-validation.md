# Cell Viewer exterior grid integer validation

Issue: #283

## Trace

`PluginEditTool/web/src/CellPanel.tsx` used `Number.parseInt(gridX, 10)` and `Number.parseInt(gridY, 10)` in both exterior-grid loading and exterior CELL creation. JavaScript accepts a numeric prefix, so `12abc` became `12` and `1.5` became `1` before the native backend saw the request.

The same permissive check also controlled both grid action buttons.

## Fix

`PluginEditTool/web/src/CellPanel.tsx:parseCellGridCoordinate` accepts only a complete optional-sign plus decimal-digit token and requires the converted number to be a safe integer. Both load/create paths and both button guards use the same parser.

The native backend remains authoritative for Fallout 4 coordinate range and CELL semantics. This change only prevents the frontend from silently changing the user's token before dispatch.

## Proof

The current branch carries the regression in `f8dd7b3d` before the implementation in `f29d22d6`. Before the implementation, the requested helper did not exist and the `Number.parseInt` behavior reproduced the defect:

- `12abc` -> `12`
- `1.5` -> `1`

Post-fix parser checks cover signed integer acceptance plus suffix, decimal, exponent, whitespace, empty, and unsafe-integer refusal.

`PluginEditTool/web/src/CellPanelGrid.test.tsx:Cell Viewer exterior grid dispatch` renders the real panel with a simulated native bridge and plugin picker. Invalid persisted X or Y disables both actions and Enter sends no load request. Correcting an invalid value restores loading. Signed coordinates reach CreateRecord, SavePlugin and the subsequent grid reload unchanged. This verifies React dispatch; it does not verify live WebView2 or in-game behavior.
