# FO4RecordEditor — VSCode-style UI Remake

Date: 2026-06-11
Status: Draft for review

## Problem

The current WPF UI is functional but dated and hard to read:

- The record/conflict view dumps raw reflection field paths — `Model.Data[0]`…`Model.Data[31]`,
  `MarkerColor.R/G/B/A`, `Conditions[0].Data.ParameterOneRecord`, `Components[0].Component` — one
  row per CLR property. It is noisy and not human-readable.
- Conflicts can only be *viewed* in the Conflicts tab; editing happens in a separate Record tab.
  The user wants to edit a conflicted record **inline, in the conflict view**.
- The chrome (flat tab strip, plain toolbar) does not look modern.

The view must stay **generic over every record type** (xEdit reads them all via its element-def
system; we read them all via Mutagen's typed records + a reflection walker). Readability fixes must
therefore be generic, with per-type enhancements — not hardcoded to a few record types.

## Goals

1. A modern **VSCode-style shell**: custom title bar, activity bar, collapsible sidebar, tabbed
   editor host, status bar, command palette, one cohesive dark theme.
2. A single **editable, generic record/conflict editor**: field rows × one column per plugin, with
   the winning column editable inline; edits are written to a patch plugin.
3. **Human-readable rendering for all field types** via an extensible renderer registry, with a
   safe fallback to the current generic tree for anything unknown.

## Non-goals (YAGNI for this effort)

- No new file formats, no networked features, no plugin-format changes.
- No change to environment/MO2 loading — "Open ESP / Load Env / Open MO2" behave exactly as today.
- Worldspace/cell *navigation* redesign is out of scope; cells/refs still render via the generic tree.

## Approach

Incremental **shell-first**: build the new chrome as a shell that hosts the existing panels, then
replace the editor surface, then layer in readable renderers, then palette/status polish. The app
stays runnable and visually testable after every phase. (Rejected: a big-bang rewrite — unverifiable
in slices; a theme-only veneer — does not deliver inline editing or the chrome.)

## Architecture

### 1. Shell & chrome
- `ShellWindow` (replaces `MainWindow`) using `WindowChrome` for a custom title bar with the
  app menu and window buttons.
- **Activity bar** (far left, icon rail): Explorer, Conflicts, Search, AI, Settings. Selecting an
  icon swaps the sidebar content.
- **Sidebar** (collapsible): hosts the existing Explorer tree / AI chat / settings panels.
- **Editor host** (center): records open as closable **tabs** (VSCode editor tabs).
- **Status bar** (bottom): environment/MO2 profile, plugin count, conflict count, current AI model,
  unsaved-changes indicator.
- **Command palette**: `Ctrl+Shift+P` runs registered commands (open env, scan conflicts, revert
  overrides, save patch, …); `Ctrl+P` jumps to a record by EditorID/FormKey.

### 2. Theme system
- One `Theme.xaml` ResourceDictionary of design tokens using the VSCode Dark+ palette
  (`#1E1E1E` editor, `#252526` panels, `#2D2D30` chrome, `#3C3C3C` borders, `#007ACC` accent,
  `#CCCCCC` text). Conflict colors: winner `#FFB86B`, differs `#3A2424`/`#F0B0B0`, benign `#2D2D2D`.
- Styles for every control we use (Window, DataGrid, TreeView, TabItem, Button, TextBox,
  ComboBox, CheckBox, ContextMenu, ScrollBar, ToolTip). All reference tokens so re-theming is one file.

### 3. Editor surface — generic editable matrix
- One control, `RecordMatrixView`, replaces today's split Record/Conflicts tabs (the Conflicts tab
  becomes editable, removing the need for a separate Record tab).
- Rows = the record's field tree (collapsible groups); columns = one per plugin that carries the
  record, in load order, winner last and visually marked.
- The **winner column is editable inline**. A commit routes through the existing
  `WriteService.SaveRecordEditsToPatch` / `SetFieldByPath` to write the edit into the patch plugin
  (patch name from a field in the view, default `FallenWorld_CraftingFixes.esp`). Non-winner columns
  stay read-only reference views.
- Built on the existing `MutagenLoader.BuildConflictMatrix` (already generic over record types) plus
  the field tree from `WalkObject`.

### 4. Readable rendering — renderer registry
- A registry mapping a CLR/Mutagen element type to a function that produces ONE readable line and
  decides whether to recurse:
  - `IConditionGetter` → `GetGlobalValue(MAIMLiteInstalled) == 0 [on Reference PlayerRef]` (done).
  - `IConstructibleObjectComponentGetter` → `Adhesive x3` (done).
  - `IEffectGetter` / magic effects → `EffectName (mag/area/dur)`.
  - Script properties → `name = value (type)`.
  - `IFormLinkGetter` → `EditorID "Name" [FormID:File]` (done, via FormatFormLink).
  - Color (`MarkerColor` R/G/B/A) → one row with a **swatch** + hex.
  - `Model` → show `Model.File` as "Model", collapse the `Model.Data[]` bounds array to a single
    "Bounds" summary row (hidden by default, expandable).
- A **friendly field-name map** renames raw property names to labels (e.g. `EditorID` → "Editor ID",
  `CreatedObject` → "Created Object"). Unknown names pass through unchanged.
- **Fallback:** any element with no registered renderer uses today's generic tree walk, so no record
  type is ever unviewable.

### 5. Conflict status (beyond xEdit's binary look)
- Per-row status upgraded from binary `Differs` to: *no-conflict*, *benign override* (differs from a
  master but harmless / identical winner), *critical conflict*, *identical-to-master*. Mirrors xEdit's
  `caOverride`/`caConflict*` model, with matching cell colors, plus xEdit's "deleted record overridden
  by later plugin" warning.

## Data flow

Record opened → `BuildConflictMatrix` builds versions + field tree (via `WalkObject`, now routed
through the renderer registry) → `RecordMatrixView` renders rows×plugin-columns → inline edit on the
winner column → `SaveRecordEditsToPatch` writes to the patch → status bar reflects unsaved/saved.

## Error handling

- Renderer registry calls are wrapped; a renderer that throws falls back to the generic tree for that
  element (never blanks the row).
- Inline edits that fail validation (bad FormKey, wrong type) surface an inline message and leave the
  patch unchanged, exactly as `SetFieldByPath` already reports.
- Loading/scan failures continue to log to the Log panel; no crash dialogs.

## Testing

- Unit-testable (no UI): renderer registry output per element type (extend `WriteToolsTests`); the
  friendly-name map; conflict-status classification.
- Manual/run verification for the shell, theme, tabs, palette, and inline-edit-to-patch flow (WPF UI
  cannot be unit-tested here, only run).

## Rollout (phases → each becomes part of the implementation plan)

1. **Theme + shell chrome** hosting the existing panels (activity bar, sidebar, tab host, status bar).
2. **Renderer registry + friendly names** wired into `WalkObject` (improves the existing views first).
3. **Editable matrix** (`RecordMatrixView`) replacing the Record/Conflicts split with inline edit→patch.
4. **Command palette + richer conflict-status colors** and polish.

Each phase keeps the app runnable; standalone + "Open MO2" loading is untouched throughout.
