# BGS — code notes

`BGS/app/MainWindow.Bridge.cs:StartTour` appends AI chat guidance targeting the existing settings and chat buttons, without opening settings or sending requests. Its completion callback clears the active-tour guard so Home can replay the guide after Skip or Done. `BGS/app/AssistantPane.cs:QuickStartLines` points chat users to the tour. `BGS/tools/uismoke/TourGuideSmoke.cs:Run` walks the ordered guide and replays it after Skip before finishing with Done.

`BGS/app/AssistantEditor.cs:AssistantEditor` discovers visible controls in the main window, owned authoring windows and open menus. Control IDs and a state fingerprint bind proposals to observed state; handlers run on the UI thread after approval through the existing chat ownership gate. Dispatch is separate from verified mutation/save. Password controls and descendants, assistant panes, account settings and approval windows are excluded; clipboard shortcuts are refused. Graph positions and visible-world bounds support gestures. File proposals use the existing loaders/exporters in `BGS/app/MainWindow.AssistantFiles.cs:AssistantFile`, `BGS/app/MainWindow.Pose.cs:OpenArchive` and `BGS/app/MainWindow.Search.cs:WriteDiff` with path-specific approval.

`BGS/app/AssistantTools.cs:ApprovePendingClipAnimation` retains the existing conversation result shape while dispatching editor actions. `BGS/app/AssistantUi.cs:OnSelectionChanged` supplies an excluded approval window when a modal disables the main pane. `BGS/app/AssistantApprovalWindow.cs:AssistantApprovalWindow` keeps its buttons outside tool discovery. `BGS/tools/uismoke/AssistantEditorSmoke.cs:Run` checks chat ownership, stale state, rejection, rig loading, workspace/rig tab discovery, viewport input and password exclusion without contacting a provider.

`BGS/app/GraphView.Assistant.cs:AssistantItems` and `BGS/app/SkeletonView.Assistant.cs:AssistantItems` expose the same projected coordinates used by drawing and picking. Camera/geometry stamps bind gestures to observed positions; moving scenes must be paused before coordinate-based selection. `BGS/app/AssistantEditor.cs:Label` uses only window captions, preventing window content from exposing excluded chat/password descendants. Availability includes `Window.IsDialog`, since platform modal blocking does not change logical `IsEffectivelyEnabled`.

`BGS/app/AssistantTools.cs:ApprovePendingAction` binds approval to a proposal identity. `BGS/app/AssistantUi.Composer.cs:SendNextQueued` waits at pending approval; `BGS/app/AssistantUi.cs:Approve` records an editor event and resumes result inspection after dispatch. `BGS/app/AssistantConversationController.cs:SendAsync` persists that continuation as a tool event instead of a user message. `BGS/app/MainWindow.AssistantFiles.cs:AssistantOpen` reuses the asynchronous discard dialog before opening dirty documents; the existing reload guard only skips the duplicate synchronous prompt after that decision. Assistant file-picker buttons use the path tool, while normal UI buttons are unchanged. Diff export stays within the active project directory.

## `src/Archive/ScanReport.cs` — headless-scan JSON contract (issue #192)

`ScanReport` / `ScanFinding` / `ScanContext` are the machine-readable result of the headless scan
modes, emitted as one JSON document under `--json`.

- **Why it lives in `BehaviourGraph.Core`, not the app:** the model and its JSON envelope are
  unit-testable directly from `tools/tests` (`tools/tests/ScanReportTests.cs`) without the GUI;
  `app/ModlistScan.cs` only populates and serializes it. The end-to-end CLI contract (stdout/stderr
  routing, exit codes, real finding accumulation) is covered by `tools/tests/ScanCliTests.cs`, which
  references the app project (`ValidateExecutableReferencesMatchSelfContained=false`) to drive the
  real `ModlistScan` commands.
- **`ScanReport.ExitCode` mirrors the process exit code** (0 clean, 1 defects, 2 usage/config). It is
  set by the scan method — and by `ModlistScan.Fail` for the exit-2 paths — so the JSON's `exitCode`
  and the process status can never diverge; CI can gate on either.
- **camelCase + null-omitting** (`ScanReport.Options`): `--scan-archives` (no profile/dataFolder) and
  the modlist modes share one schema; `ScanContext.Profile`/`DataFolder` and `ScanFinding.Source`
  drop out of the JSON when null.
- **`schemaVersion`** (`ScanReport.CurrentSchemaVersion`, `[JsonPropertyOrder(-1)]` so it leads the
  document) lets CI consumers detect envelope changes. Bump it on any breaking schema change.
- Finding `kind` values: `missing-animations`, `missing-clip-animations`, `unreadable-character`,
  `unreadable-behaviour`, `corrupt`, `unreadable`, `skip`, `archive-warning`, plus `usage`/`config`
  for the terminal exit-2 outcomes below.
- **`summary` field set per mode** (the stable CI contract, asserted in `tools/tests/ScanCliTests.cs`):
  - `scan-archives`: `archives`, `scanned`, `corrupt`
  - `scan-modlist`: `checked`, `broken`, `missing`, `unreadable`
  - `scan-clips`: `checked`, `broken`, `missingClips`, `unreadable`
  - a terminal usage/config failure emits an **empty** `summary` with `exitCode: 2`.
- **`--json` always yields a report** (`ModlistScan.Fail`): every terminal outcome — a missing CLI
  argument, a bad `--scan-archives` directory, or an `Mo2ScanData.TryOpen` failure — emits a valid
  `ScanReport` with `exitCode: 2` (finding kind `usage` or `config`) to stdout, never a bare stderr
  line, so redirected stdout is always parseable JSON.

## `app/ModlistScan.cs` — `--json` wiring

- Under `--json`, progress/layout lines go to **stderr** (`ModlistScan.Info`) and per-defect human
  lines are suppressed, so **stdout carries only the JSON document** — safe to redirect in CI
  (`bgs --scan-clips <mo2> --json > report.json`).
- Findings accumulate during the existing, unchanged scan loops (`ScanModlist`, `ScanClips`, `Run`);
  the JSON `details` array carries the *full* defect list while human output still truncates
  (40 animations / 20 clips) for readability.
- **`skip` is warning-level and does NOT affect the exit code** (decision for `Run`/`scan-archives`).
  A `.ba2` that cannot be opened is surfaced as a `skip` finding but not counted toward `corrupt`,
  because it is most often a benign non-behaviour archive (e.g. a DX10 texture archive); the finding's
  `details` carry the reason. `exitCode` reflects only defects found in *inspected* content
  (`corrupt`). A consumer that must treat "could not inspect an archive" as a failure asserts the JSON
  has no `skip` finding rather than relying on the process status — see the exit-0 assertion in
  `tools/tests/ScanCliTests.cs`.

### `BGS/app/ModlistScan.cs:ModlistScan.ScanClips`

- `missing-clip-animations` from `ModlistScan.ScanClips` is a static unresolved-reference finding:
  a clip whose animation resolves nowhere across the merged load order. It is not a reproduction of
  the Fallout 4 animation-DB or binding failure path. Runtime evidence recorded in issue #207 shows
  guarded lookup, update gating, default-animation fallback, and validation diagnostics for missing
  or unready bindings in the examined path. The scan's path rule remains the authoring-time `.hkt`
  tail mapped to the corresponding `.hkx` in the merged view; `GameData.Normalize` and
  `ProjectChain.ResolvePath` implement that existence check.

## Headless scan progress + performance (issue #193)

### `src/Archive/GameData.cs` — `ResolveSource` (existence without decompression)

`ContainsAnimation` / `ResolveAnimation` used to route through `ReadAnimation`, which reads and
**decompresses** the target file's bytes just to report where it resolves. In the scan hot loop that
meant every per-clip / per-animation existence check (`ModlistScan.ScanClips.Resolves` →
`Mo2ScanData.ExistsDataRelative` → `GameData.ContainsAnimation`) decompressed the matched animation —
the dominant cost on large load orders. `GameData.ResolveSource` now determines the source
(`"loose"` / archive file name / `null`) using only the already-cached indices (`ModLooseIndex`,
`CaseInsensitivePath`, and `FindEntry` over the cached `Names`/`Entries`) with **no byte read**.
`ReadAnimation` still reads bytes and is used where the caller needs the file contents (reading a
character/behaviour file to parse it).

Policy: `ResolveSource` / `ContainsAnimation` validate **virtual-path resolution only** — whether the
declared animation resolves to a loose file or an archive entry. Whether that entry's payload can
actually be read/decompressed (corruption, unsupported compression) is a **separate validation
concern**, deliberately out of scope here — do not "fix" this back into decompression. The runtime
RE pass did not establish a corresponding engine fault branch for an animation class unsupported by
BGS's decoder; see issue #207 and the rejected hypotheses in #208/#209.

### `src/Archive/GameData.cs` — `TryArchiveEntries` (archive index parsed once per run)

`Mo2ScanData.ModHkxPaths` used to `Ba2.Open()` each mod archive itself (parsing its index/name table),
enumerate, and dispose it — then `GameData` parsed the *same* archive a second time for existence/read
resolution. `GameData.TryArchiveEntries` exposes the shared, cached `Open` (`GameData._opened`) with the
same DX10-texture / unreadable-GNRL handling, and `ModHkxPaths` now consumes entries through it. So each
archive's index is parsed **once per run** and reused by both discovery and resolution — the actual
#193 "parsed once" goal, not just relying on a cache that discovery bypassed. Regression:
`tools/tests/ScanCliTests.cs` (`ArchiveIsParsedOnceAcrossDiscoveryAndResolution`) counts
`Ba2.OpenCountForTest` and asserts a single parse across `ModHkxPaths` + `ExistsDataRelative`.

### `app/ModlistScan.cs` — progress to stderr

- `ModlistScan.Progress` writes to **stderr in every mode** (progress is transient diagnostic; stdout
  stays a single clean JSON document under `--json`, and the report under human mode).
- `ModlistScan.StepDue` throttles per-file progress to ~20 updates regardless of load-order size.
- `Mo2ScanData.ModHkxPaths` takes an optional `onArchive` callback so the archive-indexing phase
  (which opens every enabled mod's `.ba2`) reports progress too.
- Progress strings use ASCII `...`, not `…` (U+2026), to avoid mojibake under the Windows console
  codepage.

## Packfile pointer-layout conversion (issue #188)

### `BGS/src/Hkx/PackfileConverter.cs:PackfileConverter.ConvertTo`

The conversion matrix is deliberately limited to the canonical little-endian layouts reproduced
by `LayoutWalker`:

| Source pointer/layout | Target pointer/layout | Status | Basis or refusal |
| --- | --- | --- | --- |
| 8-byte / `[8,1,0,1]` | 4-byte / `[4,1,0,1]` | Supported | Existing FO4 packfiles and the shipped class table; tested against the checked-in vanilla fixtures. |
| 4-byte / `[4,1,0,1]` | 8-byte / `[8,1,0,1]` | Supported | A 4-byte result produced from the FO4 fixtures is read back and widened successfully. |
| 8-byte / `[8,1,0,1]` | 8-byte / `[8,1,0,1]` | Supported | Header restamp/no-op after signature validation. |
| 4-byte / `[4,1,0,1]` | 4-byte / `[4,1,0,1]` | Supported | Header restamp/no-op for a canonical 4-byte image. |
| Any other pointer width | Any target | Unsupported | `TryConvertTo` reports that only 4-byte and 8-byte pointers are representable. |
| Canonical source | Canonical target with an unsafe section, fixup, signature, value, or placement | Unsupported | The conversion remains fail-closed; the refusal names the first exact condition. |
| Any newer or non-canonical Havok layout | Any target | Unsupported | No real fixture or verified layout rule justifies inferring the conversion. `#115` and `#116` remain out of scope. |

`PackfileImage.Read` rejects non-canonical source rules before conversion. `ConvertTo` is the
tool-facing throwing boundary: it returns `true` for a proven conversion and otherwise throws
`InvalidDataException` naming the source layout, target layout, and refusal. `TryConvertTo` keeps
the boolean fail-closed API and exposes the precise refusal reason for callers that need to handle
it without throwing. Neither API commits a partially converted image; bytes and fixups are
committed only after every safety and placement check succeeds.

### `BGS/tools/tests/PackfileConverterTests.cs:PackfileConverterTests`

Focused tests cover both canonical directions, the source/target wording for an unsupported width,
and the exact section-level refusal. The broader FO4 graph, array, variant, relative-array, fixup,
overflow, and real-file coverage remains in `BGS/tools/symrm/Tests.cs`.

## Behaviour compare export and filtering (issue #191)

### `BGS/src/Hkx/BehaviourCompareSession.cs:BehaviourCompareSession.ApplyFilter`

Compare filters are applied to the session result, not in the Avalonia view. `BehaviourDiffFilter`
composes an optional change kind with an exact object-class match. The result is explicitly ordered by
change kind, class, field/name, and before/after values so repeated comparisons produce the same rows.

### `BGS/src/Hkx/BehaviourCompareSession.cs:BehaviourCompareSession.CreateExport`

`BGS/src/Hkx/BehaviourCompareSession.cs:BehaviourCompareSession.CreateExport` produces the single
structured `BehaviourDiffExport` model. JSON is serialized from that model with
camelCase properties and string change kinds; text is rendered from the same ordered entries. The app
view uses the same filtered model for its grid and offers file exports for `.txt` and `.json`. This is
read-only comparison output; it does not add patch, apply, or merge behavior. The model retains the
original difference count and whether a filter is active, so an empty filtered result is reported as
zero matches rather than as identical files. The UI likewise leaves the grid empty for zero matches
and reserves the `no difference` row for an actually identical unfiltered comparison.

### `BGS/tools/tests/BehaviourCompareSessionTests.cs:BehaviourCompareSessionTests`

Focused coverage checks no-filter, kind, class, combined, and empty filters plus deterministic JSON
and text output ordering.

## `BGS/app/EditorShell.cs:EditorShell` — workspace chrome

The nine existing TabControl pages stay as the real views. The tab strip is hidden (`EditorShell.HideStrip` only hides the TabControl's own TabStrip, not nested drawer strips). A left activity rail maps:

- Home → Bridge
- Graph → Graph
- Inspect → Tree, Symbols
- Animation → Animation, Playback
- Project → Project search, Chain, Compare

`last_tab` still stores the original tab header so existing persistence and `GoToTab("Tree")` keep working. Command chrome (Open/Save/Undo/Redo/Check) lives in the shell command bar; Expand all / Collapse all / the object filter stay on Inspect → Tree. Graph search is a separate field on the Graph toolbar (`_graphFilter`). `MainWindow.Filter(string)` still sets both, which is the API the existing uismoke battery uses.

`NativeAuthoringUi.Attach` / `BuildInfoUi.Attach` add Batch authoring and About into `EditorShell.Tools` when an `EditorShell` is present. They no longer wrap the window in a second top `DockPanel` strip on the production `App.CreateMainWindow()` path. The wrap remains only as a fallback for a window whose content is not an `EditorShell`.

## `BGS/app/AdvancedAuthoringUi.cs:AdvancedAuthoringUi.Attach`

Structure authoring is inserted into the same native strip. Lookup is `EditorShell.Tools` (or a fallback `DockPanel` child), not `window.Content as DockPanel`. That DockPanel check is what left the button missing after the shell stopped wrapping the window.

Tree-only typing must not call `GraphView.Filter`. Otherwise Inspect → Tree search leaves Graph dimmed with no visible find field.

## `BGS/app/MainWindow.Document.cs:MainWindow.OnClosed`

Dispose `_gameData` here. `BuildChain` replaces the previous `GameData` while the window lives, but closing used to leak archive file handles for the process lifetime. `OnClosing` can still be cancelled, so dispose belongs on `OnClosed`.

## `BGS/app/MainWindow.Open.cs:MainWindow.Load`

BGS edits the 8-byte FO4 packfile layout. A readable 4-byte file is refused before a graph is shown, and the previous document model is fully replaced so the path cannot name a file that was never loaded. `NonEightByteLayout` returns the pointer width for a readable non-8-byte packfile, otherwise null.

## `BGS/app/MainWindow.Open.cs:MainWindow.AppendRagdollSourceNote`

After an animation file loads its clip summary, append the sibling-sourced ragdoll note so the overlay origin stays on the playback summary line.

## `BGS/app/MainWindow.Bridge.cs:MainWindow.HasDroppedFiles`

Avalonia 12 has a single file format, `DataFormat.File`. `DroppedPaths` keeps only items with a local path so a virtual/remote drop reports zero files instead of a bogus path.

## `BGS/app/MainWindow.Templates.cs:MainWindow.RefreshPasteSlots`

Paste stays disabled unless `_bytes` is loaded. A rejected (for example 4-byte) open nulls `_bytes` even when the path is remembered.

## `BGS/app/MainWindow.GraphApi.cs:MainWindow.UpdateScaleStatus`

The ragdoll scale readout is the fitted body half-extent and frame-axis stub length in the file's units. Refreshing on every load swaps the numbers or hides the pill.

## `BGS/app/NativeAuthoringUi.cs:BatchAuthoringWindow.Invalidate`

Any change to either authoring queue drops a pending preview so Apply cannot run against a batch it no longer describes.

## `BGS/app/NativeAuthoringUi.cs:BatchAuthoringWindow`

Variables are written first on their own verified pass so the clip/state pass sees a graph that already declares them. Each pass byte-verifies and graph-validates its own output.

## `BGS/app/HkGrid.cs:HkGrid` — header/body horizontal scroll

Before: the header Grid was `Dock.Top` on the outer DockPanel, and only the TreeView sat in a ScrollViewer with `HorizontalScrollBarVisibility.Auto`. Wide rows could scroll horizontally while column titles stayed put.

After: one horizontal ScrollViewer owns a sheet that contains the header and a vertical-only body scroller. Pixel columns keep a `MinWidth` so the sheet can actually overflow. Proof: `BGS/tools/uismoke/WorkspaceSmoke.cs:HkGridHeaderScrollsWithRows`.

## `docs/audits/bgs/2026-09-12/repro/Program.cs:Audit` — historical audit reproduction

The version-bound harness records seven failures at `d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958`; its assertions expect the historical failing behavior. `Audit:Main` uses an isolated temporary directory and preferences, `Audit:PendingClose` focuses the real headless property control, and `Audit:ModlistScans` creates synthetic mod-manager inputs. Fixture object IDs come from the existing BGS test builders; this evidence makes no additional closed-engine layout or runtime claims. The companion `Audit.csproj` references the audited checkout through `AuditSourceRoot`, independently of the documentation branch's production code. See `docs/audits/bgs/2026-09-12/README.md` for reproduction and provenance limits.

## `BGS/app/MainWindow.Document.cs` — PR #300 findings 1–3 on current main

Current main still routed toolbar `Save` through `GraphValidator.SaveRefusal` while close/open “Save and continue” called `SavePendingChanges` → `SaveCurrent` without that check. Combined document+animation dirty saves called only `SaveCurrent`, whose `Load()` reset in-memory animation edits. `OnClosing` inspected dirty flags without `CommitPendingFields`.

`MainWindow.Document.cs:WriteDocument` is the shared document write: it refuses with `GraphValidator.SaveRefusal` before `DocumentSaveTransaction.Commit`. `SavePendingChanges` writes the document then the animation against the refreshed source stamp, then reloads once. `OnClosing` and `CloseAfterDecision` flush `_fieldCommits` before discard handling.

`AuditLifecycleSmoke.cs:ClosingRunsPendingFieldCommitsBeforeDiscard` proves that contract through `MainWindow.Document.cs:QueuePendingFieldCommitForTest`, without selecting a property textbox. Setting `TextBox.Text` and then `UpdateTarget` with `UpdateSourceTrigger=Explicit` can restore the old source value and is not a valid pending-edit. `AuditLifecycleSmoke.cs:CombinedSaveKeepsAnimationEdits` materializes the editable fixture with `AnimationSaveTransaction.Commit` over a spline stub (`NativeAnimation.SplineClass`). A lossless stub becomes interleaved on that first commit; `NativeAnimation.Interleave` then cannot save the edited frames because nothing still points at the leftover compressed object. Spline stays recompressible, which is the path `AnimationSaveTransactionTests.cs:AnimationScope` uses for `spline: true`.

## `BGS/src/Hkx/HkxBinaryReader.Skeleton.cs:ParseSkeleton` — PR #300 finding 4

`HkxBinaryReader.Skeleton.cs:ParseSkeleton` trusted `hkaSkeleton.bones` count and synthesized `Bone_i` names when a per-bone name fixup was missing, so a tiny malformed file could request 100,000 names. Bounding against `data.Length` still accepted a count whose `count * 0x10` payload crossed `__data__` `HkxBinaryReader.cs:SectionInfo.End` into later file bytes. That `End` is already `absoluteDataStart + end` from the packfile section header, the exclusive bound of the owning section. `ParseSkeleton` now takes that end from `HkxBinaryReader.cs:ParseHkx`. Annotation and lossless `ReadFloats` count paths stay unchanged without a current proven failing case. `HkxSkeletonCountTests.cs:DeclaredBoneCountThatExceedsTheSectionIsRejected` and `DeclaredBoneCountThatCrossesTheOwningSectionIsRejected` are the regressions.

## `BGS/app/MainWindow.Animation.cs:SiblingSkeletonPath` — sibling rig resolution

The sibling skeleton file is the first `*.hkx` under the sibling `CharacterAssets` folder that reads as a skeleton, or null. The ragdoll fallback uses the exact same file the pose pipeline poses with, so the mapper's `skeletonA` always matches the rig being played.

## `BGS/app/MainWindow.Playback.cs` — ragdoll scale readout and animation overlay

The ragdoll scale readout lives on the viewport's status row and is only shown once a file with rigid bodies is open (`TryLoadRagdoll` -> `UpdateScaleStatus`).

An animation file carries no ragdoll of its own; the character's sibling skeleton (the same rig the clip poses) does, including the mapper that lets the overlay follow the clip. For an animation file, the ragdoll summary is replaced by the clip summary a moment later, so the sibling-sourced ragdoll gets its note appended there instead.

## `BGS/app/MainWindow.Pose.cs:ShowConstraintInspection` — constraint limit readout

The Playback summary line carries per-constraint limit inspection while a constraint is hovered or pinned (amber, like the constraint overlay), and restores the previous summary text and colour when the selection clears.

## Skeleton reference-pose persistence

`BGS/src/Hkx/NativeSave.cs:Compare` and `BGS/src/Hkx/NativeSave.Apply.cs:Apply` separate edit planning from binary application without changing the existing mutation gate. `BGS/tools/tests/SkeletonPoseSaveTests.cs:ReferencePoseEditCanUseTheVerifiedNativeSavePipeline` reproduces the missing qstransform-array save path using the shipped skeleton fixture. `BGS/src/Hkx/HavokClassLayouts.json` and `BGS/src/Hkx/HavokClassTypes.json` remain the authoritative schemas. `BGS/src/Hkx/SaveVerifier.cs:Verify` checks object counts and intended edits; shared storage requires the separate ownership guard below.

## Rig editing and persistence

`BGS/app/RigAuthoringUi.cs:RigAuthoringUi.Attach` mounts skeleton and skin controls through the production window factory. `BGS/src/Hkx/SkeletonDocument.cs:Edit` validates and rebuilds edits in memory before adding history; dependent bone indices remain stable, while pose padding and other skeleton fields are retained. `BGS/src/Hkx/PackfileObjects.cs:WriteStringAt` extends the existing append-and-repoint string writer to owned struct members such as bone names.

`BGS/src/Nif/NifShape.cs:NifGeometry.ReadShape` records the existing reader's vertex skin spans. `BGS/src/Nif/SkinSave.cs:Apply` writes only those spans in a cloned BSVersion 130 source and verifies a reparse with half-precision quantization. Geometry, binding transforms and all other bytes remain unchanged. `BGS/src/Nif/SkinSave.cs:SkinDocument.Save` uses the existing source-stamp and backup transaction. `BGS/tools/uismoke/RigAuthoringSmoke.cs:Run` exercises the mounted controls, undo/redo, backups and saved-file reopening.

## Interactive physics preview

`BGS/src/Hkx/RagdollSimulation.cs:RagdollSimulation` uses BEPU 2.4.0 with explicit user-authored masses and uniform-density hull inertia. The engine receives the measured convex vertices and complete joint frames, refuses incomplete/unsupported geometry, and preserves BEPU's hull-centroid offset when converting between solver poses and displayed body origins. BEPU's version-tagged `ConvexHull`, `TwistLimit`, `SwingLimit`, `Hinge`, `Weld`, and self-contained demo source define this adapter's engine API.

`BGS/app/RigAuthoringWindow.Physics.cs:SetJoints` explicitly authors preview limits rather than importing native angular atoms, motors, material settings or inertia. `BGS/src/Hkx/RagdollSimulation.Joints.cs:AddJoint` supports ball/socket, fixed, hinge, limited hinge, and cone/twist preview joints. This is a collision-based editor simulation, not a claim of equivalent Havok or game behavior. Adjacent bodies are filtered; nonadjacent bodies and the ground collide. Settings stay in the window and do not modify HKX bytes.

`BGS/app/SkeletonView.Drop.cs` owns the existing constrained-drop approximation after a mechanical split. `BGS/app/SkeletonView.Simulation.cs:ShowSimulationFrames` renders the independent disposable simulation owner's frames. `BGS/app/RigAuthoringWindow.Physics.cs:StopPhysics` stops its UI timer and disposes native buffers on reset, replacement, failure or window close. `BGS/tools/tests/RagdollSimulationTests.cs` exercises off-origin rotated hulls, collisions, asymmetric hinge limits, input refusal, source preservation and repeated disposal.

`BGS/src/Hkx/NativeSave.Ownership.cs:RequireUnaliasedArrays` rejects edits that would mutate or move fixups in array storage referenced by another owner. `BGS/tools/tests/SkeletonDocumentTests.cs:SharedBoneArrayIsRefusedWithoutChangingEitherSkeletonOrTheFile` first reproduced an unchecked shared bone array mutation using two skeletons in the measured fixture; rejection occurs before editing bytes or adding history. Numeric array edits append new storage and retain the old storage/fixups.

`BGS/app/RigAuthoringWindow.Physics.cs:EnsureSimulation` freezes gravity/ground inputs while their simulation exists; Reset releases that owner before allowing changes. `BGS/tools/uismoke/RigAuthoringSmoke.cs:Run` checks this state and the Save/Discard/Cancel dirty-close paths.

`BGS/src/Hkx/RagdollSimulation.cs:HullEdges` copies the solver hull's face edges and transforms them by the same rigid poses used for collision solving. `BGS/app/RigAuthoringWindow.Physics.cs:AdvancePhysics` feeds these into the existing mesh renderer. `BGS/app/SkeletonView.Rendering.cs:Render` also accepts mesh-only views without an animation pose. The physics proof includes rotated fixed frames and cone/twist limits; the shared-storage proof includes global references sourced in another section.

## Top workspace toolbars

`BGS/app/EditorShell.cs:EditorShell` disables auto-hiding overlays on the command and workspace scroll viewers so Fluent reserves a separate row for horizontal scrollbars. `BGS/tools/uismoke/WorkspaceSmoke.cs:TopToolbarsLeaveTheWorkspaceClear` compares the content presenter bottom with the scrollbar top on all eight editing views.

`BGS/app/SkeletonView.cs:SkeletonView` clips all preview drawing to its control bounds. `BGS/app/RigAuthoringUi.cs:AuthoringPanel` separates scrollable settings from the expanding preview and connects Fit preview to the existing camera reset. `BGS/tools/uismoke/RigAuthoringSmoke.cs:ViewportFits` checks clipping, resize growth, scroll ownership and Fit zoom/pan reset on all three authoring tabs.

`BGS/app/EditorShell.cs:EditorShell` hosts the five workspace selectors and a contextual command ribbon above the editor. `BGS/app/MainWindow.Layout.cs:RegisterToolbar` keeps the original control instances and handlers when moving commands out of workspace content. Inspectors and the playback timeline stay with the data they affect. Wide ribbons scroll horizontally instead of stacking over the viewport.

`BGS/app/MainWindow.BridgeDeck.cs:BuildBridgeTab` exposes the existing authoring windows in Home's searchable tool cards. `BGS/app/RigAuthoringUi.cs:RigAuthoringWindow` accepts the initial tab so skin and physics shortcuts open their respective tools. `BGS/tools/uismoke/WorkspaceSmoke.cs:HomeOpensAuthoringTools` exercises all five launch buttons and verifies the selected rig tab while Home stays selected.

## `BGS/src/Hkx/HkxBinaryReader.Spline.cs:ReadQuat` — spline rotation formats without a known layout fail closed

The rotation quantization nibble of an `hkaSplineCompressedAnimation` track mask selects the quaternion encoding. The Creation Kit lists the enum as `POLAR32, THREECOMP40, THREECOMP48, THREECOMP24, STRAIGHT16, UNCOMPRESSED` (source:ReverseEng/Fallout4_Runtime_Research @ `1cc0179`, `Fallout4/CreationKit/ghidra-evidence/strings-asc.part-003.txt:17617-17622`), so format 3 is THREECOMP24 and format 4 is STRAIGHT16. Neither that repo nor `ReverseEng/commonlib_NonVR` (@ `dbaeca7`, which only carries `hkaSplineCompressedAnimation` RTTI/VTABLE IDs) gives their bit layout or byte width.

`ReadQuat`, `BGS/src/Hkx/SplineQuat.cs:Read`/`Write` and `BGS/src/Hkx/SplineFormat.cs:RotWidth` used to send every unlisted format to the 40-bit codec through a `_ =>` arm. A THREECOMP24 or STRAIGHT16 track was then decoded as 5-byte THREECOMP40 values, which reads the wrong bytes, misaligns every later channel in the block and produces wrong rotations without an error; the encoder labelled 40-bit data as format 0, 3 or 4 (format 0 also truncated each 5-byte value to `RotWidth(0)` = 4 bytes). They now throw `NotSupportedException` for those formats, as `ParseHkx` already does for an unsupported packfile version. `HkxBinaryReader` keeps its POLAR32 reader (`Read32BitQuat`); the shared codec supports 1, 2 and 5 only. `SplineEncoder.cs:ReadRotation` looks the width up only after the identity early return, so an identity channel with any format nibble still decodes.

Regressions: `BGS/tools/tests/AnimationSaveTransactionTests.cs:SplineEncoderRefusesRotationFormatsItCannotWrite` (formats 0, 3, 4) and `SplineDecodersRejectRotationFormatsWithoutAKnownLayout` (formats 3, 4, through `ParseHkx` and `SplineEncoder.Decode`). All five cases failed before the change.
