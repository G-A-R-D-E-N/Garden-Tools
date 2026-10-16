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

## `BGS/app/MainWindow.Open.cs:MainWindow.AppendPhysicsNotes`

After an animation file loads its clip summary, append the sibling-sourced ragdoll note and the physics report note so the overlay origin and the load status stay on the playback summary line. Both notes are appended only when the current summary text (`_summaryBaseText`) does not already contain them, so the helper is idempotent and safe to call again after `LoadPose` overwrites the summary. A pose loaded from a file that is not the loaded document (an archive entry, or a different loose animation) clears the stored physics report first, so Copy diagnostics can never describe a different file.

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

## GT-097 BGS URL and HKX bounds

`BGS/app/AssistantUi.Sessions.cs:OpenUrl` passed whatever URL the Codex app server sent to `ProcessStartInfo` with `UseShellExecute = true`, so a `file:`, `ms-settings:` or `javascript:` target was handed to the shell. `OpenUrl` now launches `WebUrlToOpen(url)`, which accepts only absolute `http` and `https` URIs, and launches it as `uri.AbsoluteUri`, which percent-escapes quotes and spaces. It returns `true` only when it launched, and `false` when the URL is rejected or `Process.Start` fails. The callers `BGS/app/AssistantUi.Provider.cs:SignIn` and `BGS/app/AssistantUi.Provider.cs:DeviceCode` show the sign-in status error (`Ux.WarnBrush`) on `false` instead of the waiting text. `WebUrlToOpen` and `IsWebUrl` are `internal`, so `BGS/tools/tests/AssistantOpenUrlTests.cs` reaches `IsWebUrl` through the existing InternalsVisibleTo.

`BGS/src/Hkx/HkxBinaryReader.Animation.cs:ArrayAt` read the element count of an hkArray, and the lossless readers (`ReadFloats`, `ReadQuaternions`, `ReadWords64`, `ReadWords16`) passed it straight to `new List<T>(count)` and looped that many times. A count past the end of the file, or a negative count, therefore sized the allocation and the loop from attacker-chosen values, and element reads past the end silently returned zero through `ReadF32` and `ReadWords*`. `ArrayAt` now throws `InvalidDataException`, the reader's format exception, when the count is negative or when `count * elementSize` runs past the end of the buffer, before anything is allocated. Every element read in those loops is in range after that check, so the zero-on-miss primitives are unchanged: `ReadF32` and `ReadI32` are shared with the section, skeleton and spline readers, which rely on that fallback.

The other file-driven counts in `BGS/src/Hkx/HkxBinaryReader.Animation.cs` are bounded the same way, and negative counts throw:

- `ParseAnnotationTracks` checks the track count against the 0x18-byte entries that must follow in the file. Tracks may point at the same event run, so the total number of events across all tracks is capped by `MaxAnnotationEvents` (1 << 16) before each track's events are read; the declared count of a track that would exceed the budget throws. The cap is a judgement; a valid animation with more than 65,536 annotation events is rejected.
- `ParseInterleavedAnimation` rejects more tracks than transforms, and checks the transform count against the 48-byte transforms in the file. Each track therefore has real data behind it.
- `ParseSplineAnimation` rejects negative frame, track or frames-per-block counts. It caps `numFrames * numTracks` by `MaxFrameTracks` (1 << 22), the same cap as the lossless reader, because a static track stores one constant however many frames it spans. Before `DecompressSpline` adds the track objects, it checks that `numTracks * 4` mask bytes fit between the blob start and the end of the file. `DecompressSpline` also clamps each block to the frames that remain, so a non-final block with a huge frames-per-block value cannot produce more than `numFrames` frames.
- `ParseLosslessAnimation` cannot be bounded by file size, for the same static-track reason. Its `numFrames * numTracks` is capped by `MaxFrameTracks` (1 << 22) with long arithmetic instead. The cap is a judgement; a valid clip above 4,194,304 frame-tracks is rejected.
- `ParseAnimationBinding` checks the track-to-bone index count against the int16 entries in the file and throws on truncation instead of stopping.

The bounds errors surface as `InvalidDataException` through the callers' existing error paths (`BGS/src/Hkx/HkxBinaryReader.Animation.cs:ArrayAt`), so `BGS/app/MainWindow.Animation.cs`, `BGS/app/MainWindow.Playback.cs` and `BGS/src/Hkx/AssistantInspection.cs` keep their "Could not read" / `unreadable_file` messages.

Known remaining gap, not changed by GT-097: `BGS/src/Hkx/HkxBinaryReader.Helpers.cs:ReadF32` and `ReadI32` still return 0 past the end of the buffer. `BGS/src/Hkx/HkxBinaryReader.Skeleton.cs:ParseSkeleton` guards each pose read with `poseAbs + i * 48 + 48 <= data.Length`, so a truncated pose array stops early instead of reading past the end. The section header reads in `BGS/src/Hkx/HkxBinaryReader.Sections.cs:ParseHkx` have the same fallback.

Regressions: `BGS/tools/tests/HkxAnimationBoundsTests.cs` covers each bounded count (huge or negative array, annotation, annotation events shared across tracks, lossless frame cap, spline track count, spline per-block frame cap, interleaved tracks and transforms, binding indices), a truncated array one element past the end, and `TryReadAnimationReportsHugeCountAsInvalidData`. `ValidAnimationFixtureStillParses` guards the TurnLeft90 fixture against the new checks.

## `BGS/src/Nif/NifShape.cs:NifGeometry.Layout` — 16-byte full-precision position and exact skinned stride

A full-precision (flag bit 10) position is three f32 plus one 4-byte value, 16 bytes; the vanilla corpus fixes this (descriptor 0x0041b00000650407 puts UV at byte 16, stride 28; `Radicle/docs/NIF.md` parity item 15). `Layout` used 12, so for full-precision skinned shapes (flags 0x45b stride 40 and 0x47b stride 44; six vanilla shapes under Meshes\SCOL per the corpus scan in GARDEN-TOOLS PR #498, commit d3901026) weights and indices were read 4 bytes early, and `BGS/src/Nif/SkinSave.cs:Apply` wrote the 12 skin bytes 4 bytes early, over the 4 bytes before the weights (the tangent for 0x45b, the color for 0x47b) and the true weight bytes, leaving the true indices stale; the reparse check used the same layout and passed. `Layout` now uses 16 and, for skinned shapes, throws `InvalidDataException` when the stride differs from the attribute total (position 16 or 8, 4 per bit 1-5, skin 12, bit 8 four), mirroring `Radicle/src/nif/geometry.cpp:vertex_bytes_for`. Unskinned shapes are not checked: only the position at byte 0 is read from them, and a throw would fail the whole file in `BGS/app/MainWindow.Pose.cs:LoadMesh`, `BGS/src/Hkx/MeshLookup.cs:IsMesh` and `BGS/src/Nif/SkinSave.cs:SkinDocument`, for mod meshes with descriptor bits this reader does not model.

Regressions: `BGS/tools/tests/NifVertexLayoutTests.cs` (decode at bytes 28/36, rejection of strides 36 and 44; both failed before the change). `UnskinnedVertexKeepsItsDeclaredStride` guards the skinned-only scope.

## BGS/src/Archive/Ba2.cs:Ba2

`Ba2.Open` wraps `Radicle.Interop.Ba2Archive`, which loads the native `radicle_c` library through `RadicleLibrary`. Radicle parses the header, entry table and chunk bounds. The wrapper maps each `RadicleException` to `InvalidDataException` with the same message and releases the native handle when opening fails. Only GNRL archives open. A DX10 archive is refused with "is a DX10 archive, not GNRL", and `BGS/src/Archive/GameData.cs:TryArchiveEntries` skips archives whose message contains "DX10", so that wording must stay. Non-empty archives without a name table are refused because their entries cannot be named; an empty GNRL archive opens.

Names are Radicle's Windows-1252 paths with `\` mapped to `/`. Rooted names and names containing `:` are refused when the archive opens, before any extraction. `Ba2.Read` extracts by `Entry.Index`; Radicle checks chunk bounds and decompressed sizes.

`Entry` carries `Unpacked` as `uint`; an entry above 4 GiB is refused before the cast, and GNRL stores a u32 size. `Offset` and `Packed` were removed because the Radicle C API does not expose them; `BGS/tools/symrm/Program.cs:Ba2Browse` was the only caller that read them.

Behaviour changes from the old reader: entry names are decoded as Windows-1252 (the old reader used UTF-8); only GNRL versions 1, 7 and 8 are accepted; Radicle caps a single entry at 1 GiB; a missing native library surfaces as `InvalidDataException` from `Ba2.Open` and `Ba2.Read`.

## BGS/Directory.Build.targets:WarnMissingRadicleNative

Every BGS project receives the native Radicle library as a `None` item from `RadicleNativeDir`, which defaults to `build/radicle-bgs/native` at the repository root, and copies it to output and publish. `ExcludeFromSingleFile` keeps the library beside the single-file executable, where the default NativeLibrary probing finds it. A missing file produces a warning that names `RadicleNativeDir`, not an error, so editor-only builds still succeed.

## BGS/tools/tests/TestArchives.cs:WriteGeneralBa2

Radicle requires each GNRL record to declare a 16-byte chunk header (the count and size word is `0x00100100`) and the `0xBAADF00D` chunk sentinel. Fixtures that wrote zeros there passed only because the previous reader ignored those fields. `BGS/tools/tests/AuditRegressionTests.cs:WriteBa2` uses the same layout.
Radicle requires each GNRL record to declare a 16-byte chunk header (the count and size word is `0x00100100`) and the `0xBAADF00D` chunk sentinel. Fixtures that wrote zeros there passed only because the previous reader ignored those fields. `TestArchives.WriteGeneralBa2` is now the single copy of that record layout, and it places the entry data before the length-prefixed name table, the order `Radicle/src/archive/ba2.cpp:write_ba2_general` writes: the earlier fixtures put the name table before the data, which only ever parsed for an archive with exactly one entry (a multi-entry archive written that way fails as "BA2 is truncated"). `BGS/tools/tests/Ba2RadicleTests.cs`, `BGS/tools/tests/ArchivePathRegressionTests.cs` and `BGS/tools/tests/AuditRegressionTests.cs` call the shared writer, and `TestArchives.TempDirectory` replaced the four identical per-class temp-directory helpers (the last one in `BGS/tools/tests/ModlistScanDataTests.cs:Mo2Fixture`).

## BGS/src/Archive/Ba2.cs:IsNativeLoadFailure

A stale radicle.dll or a RADICLE_NATIVE_PATH pointing at another build throws RadicleAbiMismatchException from the RadicleLibrary check. Ba2.Open and Ba2.Read only wrap native-load failures, so the mismatch escaped and crashed the modlist scan and GameData.Discover. It is now a native-load failure, both thrown directly and as the inner exception of a TypeInitializationException.

## BGS/src/Archive/GameData.cs:Names

A missing native library reaches Names as an InvalidDataException wrapping the native error. The catch used to treat that as a DX10 or unreadable archive and returned no names silently. Native-load failures now propagate with their "could not be loaded" message, so the caller sees the cause. `GameData.Entries` uses the same filter.

## BGS/tools/uismoke/RagdollLifecycleSmoke.cs:LifecycleSmoke.Main

Every uismoke mode now runs with TMPDIR, TMP and TEMP pointed at a fresh `bgs-uismoke-*` directory, which is deleted on exit. The smokes and the app code they drive write fixed-name paths under `Path.GetTempPath()`: `BGS/tools/uismoke/Smoke.cs` (`uismoke-templates`, `uismoke-archive.ba2`, `uismoke-savecurrent-*.hkx` and others), `BGS/tools/uismoke/AuditLifecycleSmoke.cs` (`bgs-audit-*.hkx`), and `BGS/app/MainWindow.Document.cs` (`bgs_edit`). Two BGS CI jobs on one runner host share the system temp folder as the same user, so these paths collided. A per-process root fixes all of them in one place. The alternative was editing each path inside `Smoke.cs`, which is over the 750-line limit. Path.GetTempPath reads TMPDIR on each call on Unix and TMP/TEMP on Windows, and managed child processes inherit the variables (native `getenv` on Unix does not see them).

## BGS/tools/uismoke/SettingsRetentionSmoke.cs:ConcurrentSettingsWritesPreserveAllValues

This check, moved here from `BGS/tools/uismoke/Smoke.cs` with `SettingsWriteFailureLeavesExistingFileUntouched`, used to reset `Settings.SettingsPathForTest` to null in its `finally`. As a result, every check `Smoke.Main` ran after it, including the Chain tab sweeps and `NativeAuthoringSmoke.Run`, read and wrote the real per-user settings file. Two BGS Linux CI jobs on the same runner host share that file, so one job's `gameDataFolder` replaced the other's and the Chain sweep checks failed intermittently. The check now restores the previous path. `BGS/tools/uismoke/RagdollLifecycleSmoke.cs:LifecycleSmoke.Main` also gives `RagdollLifecycle` its own temp settings file, because `Smoke.Main` and `WorkspaceSmoke.Run` each clear the path when they finish.

## BGS/app/ModlistScan.cs:ModlistScan.IsNativeLoadAbort — native-load aborts (GT-285, GT-286)

A native-load failure (missing or stale radicle.dll, surfaced by `BGS/src/Archive/Ba2.cs:Ba2.Open` as an InvalidDataException wrapping the native error) aborts `--scan-archives`, `--scan-modlist` and `--scan-clips` with exit 2 and kind "native", through `ModlistScan.Fail`. `ModlistScan.Run` used to skip every archive as unreadable and exit 0, and `ModlistScan.ScanModlist` / `ModlistScan.ScanClips` let it escape as a stack trace without a JSON report. Ordinary unreadable archives still skip through `BGS/src/Archive/GameData.cs:GameData.IsSkippableArchiveFailure`.

## BGS/src/Hkx/HkxDataStructures.cs:HkxAnimationData.UnsupportedReason - unsupported animation reasons (GT-663)

`BGS/src/Hkx/HkxDataStructures.cs:HkxAnimationData.UnsupportedReason` names the rejected compression for `hkaQuantizedAnimation` ("Unsupported compression (quantized)") and `hkaPredictiveCompressedAnimation` ("Unsupported compression (predictive)"). Any other class outside `DecodedAnimationClasses` keeps the lowercase fallback "unsupported animation class". Decoding either compression is tracked separately, so both stay rejected.

`BGS/src/Hkx/HkxBinaryReader.cs:HkxBinaryReader.ReadAnimation` throws NotSupportedException with that reason, then the class and the decoded-class list, for both the path and the byte[] overloads. Before, the text was the generic "unsupported animation class: <class>", so a user could not tell which compression was refused. The GUI paths never threw for these files: `BGS/app/MainWindow.Animation.cs:BuildAnimation` and `BGS/app/MainWindow.Playback.cs:LoadPose` take the `TryReadAnimation` false branch. BuildAnimation's summary line used to say "decode not implemented yet", and LoadPose's used to say "is not decoded, so it cannot be drawn." Both now show the reason. The raw NotSupportedException reaches callers of `ReadAnimation`, such as `BGS/tools/uismoke/Smoke.cs` and `BGS/tools/uismoke/AuditLifecycleSmoke.cs`. The `frames --digest` block in `BGS/tools/symrm/Program.cs` calls `ReadAnimation` without a catch, so that command fails with an unhandled exception on these files; this is a known gap.

`BGS/tools/tests/HkxAnimationBoundsTests.cs:HkxAnimationBoundsTests.UnsupportedAnimationRejectionNamesTheReason` detects a rejection that reverts to the generic "unsupported animation class" text for either compression class or for a non-decoded class with no specific reason (`hkaReferencePoseAnimation`), or a reader that stops rejecting them with NotSupportedException. It uses the same crafted one-object packfile as its neighbours, so the fallback class lookup in `ParseHkx` is the path under test.

`BGS/tools/symrm/Program.cs` tallies unsupported classes by splitting the NotSupportedException text on "." and stripping "unsupported animation class: ". Because the fallback text is lowercase, every non-decoded class that uses it keeps a bare class-name key. The two compression classes do not match the strip, so their tally key is the whole reason text, such as "Unsupported compression (quantized): hkaQuantizedAnimation", instead of the bare class name. The tally still counts them, but its labels differ from before.
## BGS/src/Archive/GameData.Entries.cs:GameData.EnumerateWinners

`BGS/src/Archive/GameData.Entries.cs:GameData.EnumerateWinners`: enumeration matches the precedence `ReadAnimation` and `FindEntry` apply, for order only: mod roots from the last `ModRoots` entry (highest priority) down, then the Data folder's loose files, then `ArchivePaths` in order, where the first archive holding a path wins. Mod archives come first in `ArchivePaths`, so a mod's top-level BA2 outranks base archives with the same path. The `Meshes` gate that `ModLooseIndex` applies to animation lookups is not applied here, so a mod root without a `Meshes` folder (a sound or script override, for example) still wins its prefix. Removing the gate means an empty prefix lists all files under each enabled mod root and Data, not only roots with a `Meshes` folder. An asset's identity is its lower-cased, '/'-separated path including the extension, so `.hkt` does not alias `.hkx` here, although `ReadAnimation` still does. `ModLooseIndex` keys strip the extension through `Normalize`, which is why this method does not reuse it: `shared.hkx` and `shared.txt` in one root would collapse, and an extension filter could never see the dropped file. Loose copies are located at a folder (the mod root, or `DataFolder`) and archive copies at a file (the full archive path). `ArchivePath` is non-null only on a winner that comes from an archive. A shadowed copy carries only `Source` and `Location`: it is a loose file when `Source` is `loose` or starts with `loose:` (then `Location` is the root folder), otherwise `Location` is the archive path. Callers must branch on these before treating `Location` as a file. A mod root's loose copy is sourced `loose:<mod folder>`; a Data copy is sourced `loose`; an archive copy is sourced by its file name. Texture (DX10) BA2s are not listed, because `Entries` yields nothing for them. Prefixes are normalized: `\` becomes `/`, a leading run of `/` and `./` segments is stripped, a bare `.` means the whole tree, and a prefix without a trailing `/` gets one. Only that leading run is normalized: interior `.` or empty segments (`meshes//n`, `meshes/./n`) are not. An extension has its leading dots trimmed and then one `.` added, so `hkx` and `.hkx` match alike, and an extension that is empty or only dots (`""`, `"."`) is ignored (no filter), so extensionless files cannot be requested. Each call walks the whole Data folder and every archive entry. Loose listing recurses into directory links without loop protection, matching `ModLooseIndex`. An unreadable (access-denied) subfolder is skipped, but any other IOException while listing a root yields an empty list for that root.

## BGS/src/Archive/GameData.Entries.cs:GameData.TryReadEntry

`BGS/src/Archive/GameData.Entries.cs:GameData.TryReadEntry`: successful opens are cached in the `Open` handle cache that `ReadAnimation` and `SkeletonBytes` use, so each good archive is opened once per GameData. A failed or DX10 archive is not cached: `Ba2.Open` is retried on every call and fails again. A missing entry and a skippable archive failure both return null; `ReadEntry` turns that null into FileNotFoundException. Each name call scans the archive's entry list. A caller that already holds a `Ba2.Entry` uses the entry overload, which skips the scan and reads through the same cached handle; `BGS/src/Archive/SubgraphIndex.cs:SubgraphIndex.ReadEntry` does this for every manifest, so one `SubgraphIndex.Discover` opens each archive once. Names are normalized as in `EnumerateWinners`: `\` becomes `/`, and only a leading run of `/` and `./` is stripped.

## BGS/src/Archive/GameData.cs:GameData.ModlistPath

The modlist.txt path rule that `DiscoverModded` and `BGS/src/Archive/Mo2ScanData.cs:Mo2ScanData.TryProfile` share; `TryProfile` uses it for the selected profile, the instance root and the Default profile. Selection order, error text and profile enumeration stay in `TryProfile`, because each failure is reported differently. This change refactored only `DiscoverModded`, `Mo2ScanData.TryProfile` and `GameData.SkeletonBytes`; `GameData.ModlistProfile`, `BGS/app/MainWindow.Chain.cs` and `Mo2ScanData.ModHkxPaths` were not. `SubgraphIndex` reads its manifests through `GameData.TryReadEntry` since #696, but its overlay was not moved.

## BGS/src/Archive/GameData.Entries.cs:GameData

The archive and loose entry access (`TryReadEntry`, `ReadEntry`, `EnumerateWinners`, `AssetWinner`, `AssetCopy`) lives in this partial file, while `BGS/src/Archive/GameData.cs` keeps discovery, animation resolution and the archive handle cache. Splitting by responsibility keeps the read and enumeration surface in one place. The split was also needed because adding it to `GameData.cs` breached the 750-line limit the pre-edit hook enforces.

## BGS/src/Archive/GameData.cs:GameData.IsSkeletonPath

Matches an .hkx under any CharacterAssets folder. `SkeletonBytes` adds the actor's own CharacterAssets prefix on top. `BGS/app/ModlistScan.cs:ModlistScan.IsBehaviourPath` also matches `/characterassets/`, but for behaviour files, so it stays separate.

## BGS/src/Archive/SubgraphIndex.cs:SubgraphIndex.ReadEntry

Reads one manifest or offset entry through the entry overload `GameData.TryReadEntry(archivePath, entry)` and maps a null (a skippable archive failure) to an empty array, as before. It used to open and dispose its own `Ba2` per entry, so one `Discover` opened a large archive 1+N times. It now reads through the GameData handle cache, which `EnumerateEntries` already fills for every archive, so each archive is opened once per GameData and stays open until `GameData.Dispose`. The lock lifetime during `Discover` is unchanged, because `EnumerateEntries` already held those handles; what went away is the extra handle per entry. `BGS/tools/tests/GameDataSharedPathTests.cs:GameDataSharedPathTests` pins the single open.

## BGS/src/Archive/SubgraphIndex.cs:SubgraphIndex.Discover - why the overlay is not on EnumerateWinners (#696)

The manifest overlay, the loose scan and `Mo2ScanData.ModHkxPaths` stay off `GameData.EnumerateWinners`, because its semantics differ:
- Manifests merge by content id (`byId[sub.Id] = sub`, last entry wins), while `EnumerateWinners` keeps one winner per path.
- The loose AnimTextData scan is top-level only (`LooseManifestFiles`, `TopDirectoryOnly`), while `EnumerateWinners` recurses into subfolders.
- `Mo2ScanData.ModHkxPaths` is per root (loose, then archive) and covers mod roots only, so it does not match the Data-plus-archives order.
- `Mo2ScanData.DescribeSource` would call the winner lookup per row, making the work O(rows x files).
- `ModlistScan --scan-archives` scans a user-picked tree independent of load order, so there are no winners to take.

## BGS/tools/tests/Ba2RadicleTests.cs:EnumerateWinnersShadowsArchiveCopiesUnderLooseOverrideAndSkipsSiblingsAndMissingEntryIsNull

`BGS/tools/tests/Ba2RadicleTests.cs:Ba2RadicleTests`: regressions, one Fact each in this file. `EnumerateWinnersShadowsArchiveCopiesUnderLooseOverrideAndSkipsSiblingsAndMissingEntryIsNull` covers a loose override that stops shadowing archive copies, a sibling extension or the `meshes/xy/` folder leaking into a `Meshes/X` prefix (no trailing slash), and a missing archive entry that reads bytes instead of null. `ModArchiveOutranksBaseArchivesWithTheSamePath` pins a mod's top-level BA2 ahead of base archives. `HigherModRootWinsAndLowerRootAndDataCopiesAreListedInPrecedenceOrder` pins mod-root precedence and the shadowed-copy order. `ModRootLooseOverrideBeatsDataLooseCopyAndIsLocatedByItsRoot` pins the mod root over the Data copy. `ModRootWithoutMeshesFolderStillWinsItsSoundPrefix` pins that the `Meshes` gate is not applied. `EnumerateWinnersNormalizesPrefixAndExtensionSpellings` pins backslash and `./` prefixes, a bare `.` prefix, a dotless `hkx` extension that must not match `notahkx`, and an empty extension. `IsSkeletonPathAcceptsOnlyHkxUnderCharacterAssets` is a theory over `GameData.IsSkeletonPath`; its `Meshes/X/CharacterAssets/a.txt` row fails if the `.hkx` check is dropped, so a non-hkx file under CharacterAssets is not taken as a skeleton. `TryReadEntryReusesTheOpenArchiveHandleInsteadOfReopening` reads `Ba2.OpenCountForTest`, which is `[ThreadStatic]`, so the count is per thread. The class sits in the `ScanCli` collection, which disables parallelization, to match `ScanCliTests`; the collection is not what keeps the count correct. `TryReadEntryReturnsNullForAMissingArchiveFile` pins the missing-archive null.

`BGS/src/Archive/GameData.Entries.cs:GameData.TryReadEntry` and `GameData.EnumerateWinners` read through archive handles that `GameData.Dispose` closes, so any caller that needs entry bytes after discovery must keep the `GameData` instance alive for as long as it reads.

## BGS/src/Archive/GameData.cs:GameData.ModLooseIndex - mod root index options (#752 Low)

`BGS/src/Archive/GameData.cs:GameData.ModLooseIndex` lists each mod root with `Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories)`, the legacy overload, which already includes hidden files. The shared `GameData.LooseEnumeration` options from `BGS/src/Archive/GameData.Entries.cs:GameData.LooseFiles` change one thing for it: `IgnoreInaccessible = true`. Before, the enumeration stopped at the first unreadable subfolder: files already yielded stayed in the index and the rest of that root was lost. Now only the unreadable subtree is skipped. This half has no regression test, because a test needs an ACL-denied folder.

The `Meshes` gate in `BGS/src/Archive/GameData.cs:GameData.ModLooseIndex` now calls `CaseInsensitivePath` instead of `Directory.Exists`. On a case-sensitive filesystem a mod that ships `meshes` in lower case is no longer skipped. On Windows the checks agree for a `Meshes` folder; a file named `Meshes` is the one difference. `BGS/src/Archive/SubgraphIndex.cs:SubgraphIndex.Discover` resolves its loose AnimTextData folders the same way (see its note below). In `GameData.ModLooseIndex`, enumeration still uses the mod root as given, so a root whose own casing differs from disk is still skipped on Linux.

`BGS/tools/tests/Ba2RadicleTests.cs:ModLooseIndexFindsFilesUnderLowercaseMeshesRoot` reads a `.hkx` from a lower-case `meshes` mod folder through `GameData.ReadAnimation`, which reaches `ModLooseIndex`, and expects its bytes. Before the fix it returns null on Linux, where the `Meshes` gate is case-sensitive. The test is not distinguishing on Windows, where `Directory.Exists` already matches `Meshes` case-insensitively, so it passes there with or without the fix.

## BGS/src/Archive/SubgraphIndex.cs:SubgraphIndex.Discover - loose AnimTextData lookup (#752 Low)

`BGS/src/Archive/SubgraphIndex.cs:SubgraphIndex.Discover` reads loose manifests from `Meshes/AnimTextData/AnimationFileData` and `Meshes/AnimTextData/AnimationOffsets` under the data folder and under each mod root. Those three folders used to be tested with a case-sensitive `Directory.Exists`, so on a case-sensitive filesystem a mod that ships `meshes/animtextdata/animationfiledata` in lower case was skipped. Each folder now resolves through `BGS/src/Archive/GameData.cs:GameData.CaseInsensitivePath`, the helper `GameData.ModLooseIndex` already uses, and a name that does not resolve is skipped, and each resolved folder is also checked with `Directory.Exists` after resolving, because the helper returns a path for a file too and `Directory.EnumerateFiles` on a file would throw out of `Discover`. The whole path goes through the helper, so the mod root's own casing is resolved here too.

`BGS/tools/tests/Ba2RadicleTests.cs:SubgraphIndexFindsLooseManifestUnderLowercaseAnimTextData` builds a mod root whose `meshes/animtextdata` subtree is lower-case (the root folder itself is mixed case) and asserts that the manifest's primary behavior and the offsets entry are found. On Windows the file system is case-insensitive, so the old `Directory.Exists` check also found those folders and the test passes with or without the fix. It fails without the fix only on a case-sensitive filesystem such as Linux.

On a case-sensitive filesystem the helper stops at the deepest folder that exists, so a root holding both `Meshes` and `meshes/animtextdata`, or both `AnimTextData` and `animtextdata`, resolves only the first match; that layout was already unsupported (#752 follow-up).

## BGS/src/Archive/SubgraphIndex.cs:SubgraphIndex.Discover - unreadable loose manifests (#752 Low)

`BGS/src/Archive/SubgraphIndex.cs:SubgraphIndex.LooseManifestFiles` lists the loose AnimationFileData and AnimationOffsets folders with the same top-directory `*.txt` call as before and returns no files when the listing is denied. Before, the `UnauthorizedAccessException` from `Directory.EnumerateFiles` in `BGS/src/Archive/SubgraphIndex.cs:SubgraphIndex.Discover` (its `ScanLoose`) escaped and aborted the whole index. The call is now inside an `IOException` or `UnauthorizedAccessException` catch.

`BGS/tools/tests/Ba2RadicleTests.cs:SubgraphIndexKeepsReadableLooseManifestsWhenAModRootListingIsDenied` denies the current user's list right on one mod root's AnimationFileData (`icacls /deny ...:(RD)` on Windows, mode 0 elsewhere; a failing `icacls` throws) and restores it in `finally` (Unix mode 0700). It returns without asserting if a probe shows the denial did not take effect, so on such a machine it proves nothing. Otherwise `Discover` must not throw (an exception fails the Fact), and it asserts that a manifest and an offsets entry in the readable data folder are found. It fails on the unfixed code with `UnauthorizedAccessException` from `ScanLoose`, and passes with the fix. A denied AnimationOffsets folder and the per-file `FileInfo.Length` guard in the offsets loop have no test.

## BGS/src/Archive/GameData.cs:GameData.Discover - unreadable mod root top level (#752 Low)

`BGS/src/Archive/GameData.cs:GameData.Discover` lists each mod root's top-level `.ba2` files with `Directory.EnumerateFiles(..., SearchOption.TopDirectoryOnly)`. An access-denied listing of one root used to throw `UnauthorizedAccessException` out of `Discover`, so the whole GameData failed to open. That listing is now inside a catch for `UnauthorizedAccessException` and for `IOException` other than `DirectoryNotFoundException` (the siblings' filter minus `DirectoryNotFoundException`, so a missing root still throws): the root stays in `ModRoots` and is still counted by the modded summary in `BGS/app/MainWindow.Chain.cs`, but contributes no archives, so a path only that root's archives override resolves to a lower-priority copy with no warning. Its loose files are still left to the loose paths (`BGS/src/Archive/GameData.cs:GameData.ModLooseIndex` and `BGS/src/Archive/GameData.Entries.cs:GameData.LooseFiles`), which skip what they cannot read. The base Data folder listing is not guarded, so an unreadable Data folder still throws, and a missing mod root still throws `DirectoryNotFoundException`. `BGS/src/Archive/GameData.cs:GameData.DiscoverModded` has no listing of its own and calls `Discover`, so it behaves the same way. `BGS/src/Archive/GameData.cs:GameData.ModlistProfile` guards its `<mods>/profiles` listing with a catch for `IOException` and `UnauthorizedAccessException` (the Mo2ScanData filter, so unlike Discover a vanished profiles folder also returns null; see its section below) (#752 Low).

`BGS/tools/tests/Ba2RadicleTests.cs:DiscoverSkipsAModRootWhoseTopLevelListingIsDeniedAndKeepsTheOtherArchives` denies the current user's list right on the first mod root (`icacls`, or mode 0 elsewhere, through the shared `DenyListing` / `RestoreListing` helpers) and runs `Discover` with that root and a readable root holding `Readable.ba2`. It asserts that `Discover` does not throw and that `EnumerateWinners` finds the readable root's archive entry. It returns without asserting if a probe shows the denial did not take effect, so on such a machine it proves nothing. It fails on the unfixed code with `UnauthorizedAccessException` at the top-level listing in `Discover`, and passes with the fix. It does not cover an unreadable Data folder, a missing mod root, or a denied archive inside a root that is listed.

## BGS/src/Archive/GameData.cs:GameData.ModlistProfile - unreadable profiles folder (#752 Low)

`BGS/src/Archive/GameData.cs:GameData.ModlistProfile` picks the first profile (alphabetically, case-insensitive) under `<mods>/profiles` that ships a `modlist.txt`. The `Directory.EnumerateDirectories` call and the LINQ chain are unchanged, but the whole chain now runs inside a catch for `IOException` or `UnauthorizedAccessException`, and a failure returns null (no profile). Before, a denied profiles listing threw `UnauthorizedAccessException` out of `ModlistProfile`, and `BGS/app/MainWindow.Chain.cs:MainWindow.BuildChainCore` (the modded-open try) reported "game data could not be read" and the whole modded open failed. The catch filter is the one `BGS/src/Archive/Mo2ScanData.cs:Mo2ScanData.EnumerateDirectoriesSafe` uses; that helper is private to `Mo2ScanData`, so the filter is repeated here rather than shared. A denied profiles folder now reaches the "no modlist.txt under that folder, or its profiles folder is unreadable" status and a plain `Discover`. A profile whose own `modlist.txt` is denied is not covered: `DiscoverModded` reads it unguarded, or the profile is skipped if `File.Exists` reports it absent.

A null profile with no root `modlist.txt` makes the caller skip the layering and run a plain `GameData.Discover(folder)` with the summary "no modlist.txt under that folder". So a denied profiles folder silently drops the mods rather than failing, and the user sees only that summary line.

`BGS/tools/tests/Ba2RadicleTests.cs:ModlistProfileNamesTheProfileUnlessTheProfilesListingIsDenied` creates `mods/profiles/Default/modlist.txt` and asserts `ModlistProfile` returns "Default" while the folder is readable. It then denies the current user's list right on `profiles` through the shared `DenyListing` / `RestoreListing` helpers (`icacls` on Windows, mode 0 elsewhere) and asserts null, restoring the folder in `finally` before the temp folder is deleted. It returns without asserting if a probe shows the denial did not take effect. It fails on the unfixed code with `UnauthorizedAccessException` from `ModlistProfile`'s profiles listing, and passes with the fix. It does not cover a denied profile folder's own `modlist.txt` or an `IOException` raised mid-enumeration.

### src/Archive/GameData.cs — OrderByPlugins (later plugin's archive wins)

`BGS/src/Archive/GameData.cs:GameData.OrderByPlugins` now reverses the plugin-matched base archives before the unplaced ones are appended. `ArchivePaths` is highest priority first, so the archive of the last-loaded plugin is now first and wins. Before, the first plugin listed in plugins.txt came first, and its archive won, which is the reverse of the game.

This follows the Radicle load-order rule in `docs/code-notes/GARDEN-TOOLS.md` under "**Load order.**" and "**Precedence.**": INI-listed archives load first, then each plugin's archives in load order, and the last-mounted archive wins.

Archives that match no plugin, such as `Fallout4 - Animations.ba2` when Fallout4.esm is not in plugins.txt, stay after every plugin-matched archive, as the lowest priority. They stand in for INI-listed archives, which load before any plugin archive. The early `return archives;` for an unreadable plugin list is unchanged, so that case keeps the alphabetical order. Mod-root archives are unchanged.

Tests: `BGS/tools/tests/Ba2RadicleTests.cs:LaterPluginsBaseArchiveWinsOverEarlierOne` covers A, B and Fallout4 archives with plugins.txt `*A.esm`, `*B.esm`. It expects B's archive to win and `ArchivePaths` to be B, A, Fallout4. `BGS/tools/tests/GameDataPluginOrderTests.cs:PluginListOrdersBaseArchivesWithRadicleRules` expects the new order. The symrm test `BGS/tools/symrm/Tests.cs:GameDataLoadOrderFollowsPlugins` was deleted because it duplicated `PluginListOrdersBaseArchivesWithRadicleRules`.
## BGS/tools/symrm/Program.cs:Frames - unsupported tally by animation class

The directory branch of `Frames` counts animations the reader does not decode by `HkxAnimationData.AnimationClass`, taken from `HkxBinaryReader.TryReadAnimation`, not by splitting the `NotSupportedException` message. The message now starts with `Unsupported compression (quantized)` or `Unsupported compression (predictive)` for the two compressed classes, so splitting on `.` gave mixed keys. Keying on the class also gives every unsupported class the same raw class name as its key.

## BGS/app/MainWindow.Pose.cs:ClearPose - pose skeleton cleared

`ClearPose` also sets `_poseSkeleton` to null. Its callers all want the pose gone: `BGS/app/MainWindow.Open.cs:Load` and the failure paths in `BGS/app/MainWindow.Playback.cs:LoadPose`. `LoadPose` assigns `_poseSkeleton` before it reads the animation, so without this a later unreadable or unsupported animation left the previous skeleton set with no animation to pose.
## BGS/src/Hkx/AnimationCompression.cs — unsupported animation compression (#663)

Before this change every surface wrote its own sentence for an animation this reader cannot decode, and `BGS/tools/symrm/Program.cs:Frames` recovered the class name by parsing the reader's exception text (`ex.Message.Split('.')[0].Replace("unsupported animation class: ", "")`). `AnimationCompression` is now the single owner of that wording and of the two Fallout 4 class names, so the window, the inspection assistant, the round-trip report and the command line cannot drift apart. `BGS/src/Hkx/AnimationCompression.cs:QuantizedClass` is `hkaQuantizedAnimation` (Havok quantized, hkaAnimation type 4) and `BGS/src/Hkx/AnimationCompression.cs:PredictiveClass` is `hkaPredictiveCompressedAnimation` (Havok predictive, type 5): the two compressed kinds Fallout 4 ships.

- `BGS/src/Hkx/AnimationCompression.cs:Describe` returns one sentence in three shapes: a compressed kind names its compression, any other non-empty class gets the same sentence with the class name in it, and a null or empty class gets "This file does not name an animation class BGS can decode". Every read-path sentence ends with "The file has not been changed." because a refusal happens while the file is being read and nothing was written. `BGS/src/Hkx/HkxBinaryReader.cs:Unsupported` is the only producer of the refusal exception and reuses `Describe`, so `BGS/app/MainWindow.Animation.cs:BuildAnimation`, `BGS/app/MainWindow.Playback.cs:LoadPose`, `BGS/src/Hkx/AssistantInspection.cs:InspectAnimation`, `BGS/tools/symrm/Program.cs:Pose` and `BGS/src/Hkx/RoundTripReport.cs:ForBytes` all show the one text.
- `BGS/src/Hkx/AssistantInspection.cs:InspectAnimation` and `BGS/src/Hkx/AssistantInspection.cs:TryReadModel` keep the `unsupported_hkx` code but pass the reader's message through `Display` instead of replacing it with "animation format is not supported"; the generic string survives only for an empty message. Consumers group by code, not by text.
- `BGS/src/Hkx/AnimationCompression.cs:DescribeSplineEncodingForWrite` is deliberately not `DescribeSplineEncoding`. The read wording claims "The file has not been changed"; the write wording says only "BGS cannot encode the spline sub-format (rotation quantization format N)." An encode refusal is raised before the encoder opens or writes any file, so the read sentence's claim about the file on disk would be unverifiable there. `BGS/tools/tests/AnimationSaveTransactionTests.cs:SplineEncoderRefusesRotationFormatsItCannotWrite` (formats 0, 3 and 4) asserts the write wording and that it omits "has not been changed"; `SplineDecodersRejectRotationFormatsWithoutAKnownLayout` (3 and 4) asserts the read wording through `ParseHkx` and `SplineEncoder.Decode`.
- `BGS/src/Hkx/HkxDataStructures.cs:SupportedAnimationClasses` was removed with its callers: the joined class list is no longer part of any user-visible sentence, because `BGS/src/Hkx/AnimationCompression.cs:Describe` names the refused class instead.

### `BGS/src/Hkx/SplineQuat.cs:Unsupported` — which rotation formats each direction accepts

- Decode reaches the read wording through `BGS/src/Hkx/SplineFormat.cs:RotWidth`, which returns 5, 6 and 16 bytes for formats 1, 2 and 5 and throws otherwise, and through `BGS/src/Hkx/HkxBinaryReader.Spline.cs:ReadQuat`, which accepts 0, 1, 2 and 5 and throws for the rest. Format 0 is the reader's own POLAR32 path in `BGS/src/Hkx/HkxBinaryReader.Spline.cs:Read32BitQuat`; the shared codec in `BGS/src/Hkx/SplineQuat.cs:Read` handles only 1, 2 and 5. That is the fail-closed behaviour recorded above for THREECOMP24 and STRAIGHT16.
- Encode always reports the write wording, because `BGS/src/Hkx/SplineEncoder.cs:AddQuat` calls `BGS/src/Hkx/SplineQuat.cs:Write` before it asks `RotWidth` for a width, and `Write` throws the write-wording exception for anything but 1, 2 and 5. `BGS/src/Hkx/SplineEncoder.cs:Options.RotationFormat` defaults to 1, and `BGS/src/Hkx/SplineFit.cs:FitRotation` rounds control points through `BGS/src/Hkx/SplineFit.cs:Round`, which calls the same `Write`, so a caller asking for any other format is refused before output exists.
- `BGS/src/Hkx/SplineQuat.cs:Unsupported` stays the decode-side factory used by `SplineQuat.Read`, `SplineFormat.RotWidth` and the spline block readers; both factories return `NotSupportedException`, so no existing catch changes.

### `BGS/src/Hkx/UnsupportedAnimationException.cs:UnsupportedAnimationException` — grouping refusals without parsing text

- The exception carries the class name in `AnimationClass`, which is what let `BGS/tools/symrm/Program.cs:Frames` and `BGS/tools/symrm/Program.cs:Scale` drop their string surgery and count and group refusals by class. It derives from `NotSupportedException` on purpose: every boundary that already catches that type keeps working, including `BGS/src/Hkx/RoundTripReport.cs:IsFormatFailure`, which matches `NotSupportedException` among `InvalidDataException`, `InvalidOperationException` and `ArgumentException`. A refused spline sub-format keeps a plain `NotSupportedException`, because there is no class name to attach to it.

### `BGS/src/Hkx/RoundTripReport.cs:ForBytes` — catch order matters

- The `catch (NotSupportedException error)` clause must precede `catch (Exception error) when (IsFormatFailure(error))`. Reversing the two would swallow a refusal into the empty `IsFormatFailure` branch and drop the loss, because `IsFormatFailure` matches `NotSupportedException` too. With the order as written, a refused class or spline sub-format becomes one `RoundTripLossKind.UnsupportedAnimation` loss carrying the reader's sentence, while a malformed file still falls into the unreadable path reported by the normal reader and project/scan diagnostics.
- `BGS/tools/tests/HkxAnimationCompressionTests.cs:TheRoundTripReportNamesTheClassAsOneLoss` pins that a quantized file yields one such loss whose message names the class and says the file was not changed. The rest of `BGS/tools/tests/HkxAnimationCompressionTests.cs` covers the four `Describe` shapes, `TryReadAnimation` refusing these classes while still filling `AnimationClass`, `ReadAnimation` throwing `UnsupportedAnimationException` with the class name, and a supported class still reading from the same crafted file.
- `BGS/tools/tests/CraftedPackfile.cs:CraftedPackfile` moved out of `BGS/tools/tests/HkxAnimationBoundsTests.cs`, where it was a private nested class, so both test files can build a packfile holding one object of a chosen Havok class; `BGS/tools/tests/CraftedPackfile.cs:Animation` builds that one-object file.

### `BGS/tools/symrm/Program.cs:Frames` — exit codes and `--strict`

- `Frames` and `Scale` separate four outcomes: a class this reader does not decode, a refused spline sub-format, a real read failure, and a file that decoded to nothing. The first two are expected answers: they are counted, listed and named, but they exit 0 unless `--strict` is given. A real read failure, the "decoded to nothing" files counted by `Frames`, and a run where nothing decoded and nothing was explained always exit 1. `BGS/tools/symrm/Program.cs:Pose` still exits 1 for an unsupported class; only its message changed.
- `Frames` classifies without exceptions: `TryReadAnimation` fills `AnimationClass` on its refusal return, so the class is counted from that property, and only a `NotSupportedException` from the read path means a refused spline sub-format, whose sentence is grouped by message because there is no class name. `Scale` uses the same split and reports unsupported classes, refusals and read failures separately.
- `--digest` changed shape: `Frames` catches the refusal itself, prints the reader's sentence and returns `strict ? 1 : 0`, so one unsupported file no longer decides the run's status. `BGS/tools/symrm/SymrmHelp.cs` lists `--strict` for `frames` and `scale`. `BGS/tools/tests/SymrmUnsupportedAnimationTests.cs:SymrmUnsupportedAnimationTests` drives the real `Program.Main` in every mode; it redirects `Console.Out`, so it lives in the `symrm-console` collection with `DisableParallelization` to keep that redirection off the parallel path.

## BGS/app/MainWindow.Animation.cs — trim, retime and loop in the GUI (#680)

### Where the controls are

The Animation tab ribbon ends with three groups after the existing Frame navigation, Find a frame and Edit selected frame groups: **Trim frames** (`first` and `last` frame boxes, `Preview trim`, `Trim`), **Retime** (a `x` speed factor box and a `seconds` new-length box, `Preview retime`, `Retime`) and **Loop** (a `blend frames` box, `Preview loop`, `Loop`). Enter in any of the boxes runs that group's apply handler, matching the frame-edit boxes. The panel keeps two answer lines under the ribbon: the existing `_frameEditAnswer` for a single frame, and a new `_geometryAnswer` for trim, retime and loop, so the two kinds of edit never overwrite each other's report.

All of the new fields and handlers live in `BGS/app/MainWindow.Animation.Geometry.cs`, a partial of the same window, so `BGS/app/MainWindow.Animation.cs` only gained the three group controls (`BuildAnimationTab` takes `GeometryToolbarGroups()`), the extra `AddTop(panel, _geometryAnswer)` line and two `ResetGeometry()` calls (one when `BuildAnimation` starts, one after a clip decodes). `ResetGeometry` clears the pending timeline and the boxes, and seeds the trim range with `0` to `NumFrames - 1`. The same reset runs wherever `_animationData` is set to null in `BGS/app/MainWindow.Open.cs`.

### What each operation does

Preview and apply go through the same method per operation, so a preview can never describe something the apply would not do, and a preview never touches `_animationData`:

- **Trim** calls `AnimationEdit.Trim(animation, motion, firstFrame, lastFrame)`. Blank boxes mean the whole clip; a non-integer is refused with its own message; everything else (a range outside the clip, `first > last`, fewer than two frames kept) is refused with `AnimationEdit.Trim`'s own wording. The answer is the `AnimationEdit.Trimmed` record's text, which names the frames kept, the new length and the annotation count dropped.
- **Retime** takes either a speed factor, passed straight to `AnimationEdit.Retime(animation, motion, scale)`, or a new length in seconds, converted to `wanted / ((animation.NumFrames - 1) * AnimationEdit.FrameDuration(animation))`, the same length `AnimationEdit.Retime` measures the clip by. Filling both, or neither, is refused. `Retime` keeps the clip's frame rate and rounds the frame count, so a new length lands on the nearest whole frame; the answer says so when the length box was used, and the record's text reports the scale actually achieved, the frame count, the length and the resample error.
- **Loop** calls the new `AnimationEdit.Loop(animation, motion, blendFrames)`, which sits beside `Trim` and `Retime`. A blank box means one frame. With one frame the last frame becomes frame 0 exactly: translations and scales are lerped and rotations are slerped toward frame 0. With N frames the last N frames ease toward frame 0 with weight `(k + 1) / N` for the k-th frame of the window, so the last frame lands exactly on the first and the correction is spread over the window instead of one jump. Frame count, duration, frame rate and annotations are unchanged, and the record reports how far the moved frames travelled. A loop keeps a cycle's travel, so the root motion is handed back exactly as it came in and is never blended toward frame 0: matching the last frame to the first would otherwise erase a clip's turn or its forward travel. The answer says so, and it also says that the last frame landing on the first means the seam holds that pose for one frame. `Loop` builds a new animation instead of mutating its input, which is what lets a preview call it. It refuses fewer than two frames, a blend of zero, a blend that would reach frame 0 (which is the frame being matched to), and a track with fewer frames than the clip declares.

### Annotations

In memory, `AnimationEdit.Trim` drops every annotation outside the kept span (a note within half a frame of either end still counts as inside) and shifts the survivors to `time - from`, while `AnimationEdit.Retime` scales the times by the ratio actually achieved and clamps them into the new length. The dropped count is not a hidden side effect: it is `Trimmed.AnnotationsDropped`, it is printed in both the preview and the apply answer as `N annotation(s) dropped`, and the frame grid re-renders with only the surviving notes.

On disk the same rule runs over the file's own annotation array in `NativeAnimation.RebaseAnnotations`, which drops notes outside the timeline's `[FromTime, ToTime]` and rewrites the survivors as `(when - FromTime) * Scale`, clamped to the new duration. The slack it allows is the timeline's `Slack`, held in file seconds: half the current clip's frame duration divided by the timeline's scale, which is the same half frame the in-memory filter allows once the file-to-clip mapping is undone. It is deliberately not the fixed `1/60` it used to be: that only matched a 30 fps clip and drifted from the memory filter as soon as a retime changed the file-time length of a frame, so a note the editor had dropped could stay in the file, the counts disagreed and the save was refused. `NativeAnimation.Timeline.Of` sets the slack from the clip it is handed, and every `Then` composition recomputes it from the clip that operation produced and the scale in force after it, so a trim, a retime and a loop each carry the slack for the frame duration the editor is now looking at. `NativeAnimation.Recompress` and `NativeAnimation.Interleave` both take the same optional timeline and both rebase with it, so an uncompressed rewrite moves the notes exactly as a spline rewrite does. `AnimationSaveTransaction.Verify` refuses the write when the rebuilt file's annotation count differs from the count the editor is showing, and it also compares the surviving notes' times within that slack and their texts, so a note that landed somewhere else is refused rather than written.

### The pending timeline

`Recompress` needs a `NativeAnimation.Timeline` to write a new duration, rebase the file's annotations and replace the extracted-motion reference frame; without one it keeps the source duration, which is what a plain frame edit wants. `Interleave` gained the same optional timeline for the same three jobs, so a lossless clip can be trimmed, retimed and looped as well, and `AnimationSaveTransaction.Commit` now passes its `timeline` argument to whichever writer the clip's class selects.

The window keeps one pending `_animationTimeline` that maps the original file's time to the current clip's time as `currentTime = (fileTime - FromTime) * Scale`, and each operation composes into it instead of replacing it. `NativeAnimation.Timeline.Identity(clip)` is that mapping for the file as opened, and `Then(Trimmed)`, `Then(Retimed)` and `Then(Looped)` compose one operation onto it: a trim keeps `Scale` and moves `FromTime` to `FromTime + from / Scale` (and `ToTime` the same way), a retime multiplies `Scale` by the ratio achieved, and a loop keeps the duration, the span and the scale because only the clip's own frames moved. The slack is recomputed from the clip each operation produced, so it always describes the frame duration the editor is looking at rather than the file's. The file on disk is always the original, so a second operation's frame numbers are in the already-edited clip's time base. The motion handed to `AnimationEdit` is the pending timeline's motion when there is one and `RootMotion.Read` of the file when there is not, so a second operation rebases the travel it is actually looking at; a read that fails hands over nothing rather than an empty motion, which is what stops the writers from replacing a real reference frame with zero samples.

### The save, and why it is still the verified one

Saving is the existing frame-edit path: `BGS/app/MainWindow.Document.cs:WriteAnimation` calls `AnimationSaveTransaction.Commit(_hkxPath, anim, _sourceStamp, _editTrack, _editFrame, timeline: _animationTimeline, verificationFault: VerifyFaultForTest)`, which re-reads the source, checks the source stamp, encodes, verifies the reparse, re-checks the stamp and only then replaces the file, keeping the original as `<file>.bak`. When a timeline is pending, `Verify` additionally walks every frame of every track of the rebuilt clip against the animation that was asked for, with the same tolerance a frame edit uses (0.05 units or radians for a spline clip, 0.001 for an uncompressed rewrite), and measures the worst position, the worst rotation and the track and frame they happened on. That measurement is part of the success message: a save that carried a timeline ends with `The worst rebuilt frame is ... unit(s) and ... radian(s) from the edit, on track N frame M.`, so the drift is visible in the answer instead of only in a refusal. `Verify` also compares the annotation counts and, within the timeline's slack, the times and texts of the surviving notes. A trim, retime or loop therefore cannot land on disk as something different from what the tab showed; a plain frame edit carries no timeline and takes the unchanged path. After a successful save `ReloadAfterWrite` reloads the file, which is what clears the pending timeline and `_animationEdited`; a refused save leaves both in place with the file untouched.

### The extracted-motion reference frame

A timeline carries the motion the editor is looking at, and `NativeAnimation.Interleave` and `NativeAnimation.Recompress` replace the file's `hkaDefaultAnimatedReferenceFrame` only when that motion actually holds samples: a null motion (a read that failed) or an empty one means "keep what is there", and the new animation's `extractedMotion` pointer is carried over to the original object. `RootMotion.Read` failing is therefore not turned into an empty motion that would overwrite a real reference frame with nothing. `Interleave` used to append a second reference-frame object and leave the first in the file; it now rewrites the existing object where it is (duration and a repointed sample array), so an uncompressed save holds exactly one reference-frame object, while `Recompress` keeps its existing append-and-drop. Because `Loop` hands the motion back untouched, a looped clip is written with the travel it came in with.

### Undo

The application's Undo and Redo stack holds XML document text only (`BGS/app/MainWindow.History.cs:Commit`), and editing a frame has never pushed an entry, so trim, retime and loop do not either. They set the same `_animationEdited` flag a frame edit sets, which is what drives the existing discard/save prompts on open, close and a rejected open, and the original bytes are recoverable from the `.bak` the transaction writes. No animation history was invented for this change. Because there is no undo, every applied trim, retime or loop appends the short hint `No undo: reopen without saving to go back; a save keeps the original as .bak until the next save replaces it.` to its answer in the geometry answer line, and `Adopt` clears the trim, retime and loop boxes after an edit, so a second Enter starts from a blank form and cannot repeat or compound the edit that was just applied.

### Files that are not animations

With no decoded animation (`_animationData` is null) every preview and apply prints the Animation tab's existing summary text verbatim, so the unsupported-class message and "This is a behaviour file. It holds no animation." are what the user keeps seeing, and nothing becomes dirty. A clip whose class the writers do not re-encode (an already-interleaved file) is still refused by `NativeAnimation.Interleave` at save time with its existing message; the in-memory edit is not blocked earlier, exactly as for a frame edit.

### Coverage

`BGS/tools/tests/AnimationGeometryTests.cs` (20 tests) covers the loop's end-matching, its easing window, its annotation handling, its refusals and the travel it must not touch, the trim and retime annotation arithmetic, and real-file and synthetic round trips: `fixtures/vanilla/Meshes/Actors/RadRoach/Animations/TurnLeft90.hkx` (spline, 19 frames, 38 tracks, 0.600s, 4 annotations, 19 travel samples) trimmed to frames 4 to 14 and saved gives 11 frames of 0.333s with 3 annotations and 1 dropped, the same clip looped and saved comes back with 19 frames, 0.600s, each track's last frame on its first and its 90 degree turn and travel intact, the lossless fixture `fixtures/vanilla/Meshes/Actors/Character/Animations/Paired/PairedKill2HMBashKneeAndHead_AttackerLead.hkx` (79 frames, 94 tracks, 7 annotations) trims through the interleaved writer with its annotation count intact, and a timeline that does not match the animation it is written with is refused with the source bytes untouched.

The slack is covered by two tests on a synthetic 60 fps lossless clip built by `BGS/tools/tests/SyntheticAnimation.cs` (on the shared `BGS/tools/tests/CraftedPackfile.cs`, which `BGS/tools/tests/HkxAnimationBoundsTests.cs` also uses): a trim of a clip whose notes sit just inside and just outside half a frame of the kept range saves with exactly the annotations the editor kept, and the same clip retimed to twice its length and then trimmed saves with exactly the one note the editor kept even though the disk filter's file-time window is half what it was. Both would keep extra notes and be refused by `Verify` if the writers went back to a fixed `1/60`. A third pair covers the reference frame: a timeline whose motion is null or empty leaves the file's reference-frame samples, duration and turn untouched, and a timeline that does substitute motion leaves exactly one `hkaDefaultAnimatedReferenceFrame` object holding the substituted samples.

`BGS/tools/uismoke/AnimationGeometrySmoke.cs` drives the real window on a copy of the same spline clip, linked into the smoke output as `fixtures/RadRoachTurnLeft90.hkx`: a trim preview reports the annotation it would drop and changes nothing, trim/retime/loop each apply, save and reopen, and the reopened file is checked for frame count, duration, annotation count and placement, for the loop landing back on the first frame without losing its travel, and for the applied answers carrying the no-undo and `.bak` hint. The retime check also confirms the speed and duration boxes and the loop box are cleared by the apply. A file with no animation keeps its existing message. It runs from `BGS/tools/uismoke/RagdollLifecycleSmoke.cs:LifecycleSmoke.Run` in the default uismoke mode, alongside the other smokes, and gives itself a temporary settings file while restoring the previous one.

`symrm trim` and `symrm retime` themselves were left alone: they are corpus sweeps over spline clips, and they call the same `AnimationEdit.Trim` and `AnimationEdit.Retime` the tab calls, so the same frame range or scale produces the same clip either way. The GUI reaches the extra lossless case only because it writes through `Interleave` with a timeline; the CLI sweeps do not read lossless clips at all, which is unchanged.

## BGS/src/Hkx/HkxBinaryReader.Skeleton.cs — full hkaSkeleton read (#661)

`BGS/src/Hkx/HkxBinaryReader.Skeleton.cs:ParseSkeleton` reads all of the 136-byte `hkaSkeleton` object
instead of its first five members, so the model carries `lockTranslation`, `referenceFloats`,
`floatSlots`, `localFrames` and `partitions` rather than the reader skipping them. The member offsets
are the ones the class initializers in `BGS/src/Hkx/HavokClassLayouts.json` declare: `name` 16,
`parentIndices` 24, `bones` 40, `referencePose` 56, `referenceFloats` 72, `floatSlots` 88,
`localFrames` 104, `partitions` 120. Every array member is an `hkArray<T>`: an eight-byte data pointer
at the member offset, an int32 count at member + 8 and an int32 capacity at member + 12. Element sizes
and inner offsets come from the same file through `BGS/src/Hkx/HkxBinaryReader.Skeleton.cs:StructSize`
and `MemberOffset`, whose fallbacks are the shared ones: `hkaBone` 0x10 with `lockTranslation` at 8,
`hkaSkeletonLocalFrameOnBone` 0x10 with `boneIndex` at 8, `hkaSkeletonPartition` 0x10 with
`startBoneIndex` at 8 and `numBones` at 10, and a 48-byte `qstransform` per pose. Fallout 4 writes
64-bit packfiles and `ParseHkx` refuses every packfile version but 11, so a pointer field is eight
bytes wide — and is still never read as a pointer.
`BGS/src/Hkx/HkxBinaryReader.Skeleton.cs:Require16BitMember` refuses a layout whose element is too
small to hold the 16-bit member it must contain, because reading it there would take the neighbouring
element's bytes instead.

Pointer fields are resolved through the local fixup table with one lazy fallback to the global fixup
table, now read by `BGS/src/Hkx/HkxBinaryReader.Sections.cs:ParseGlobalFixupsWithSections`, which
keeps each entry's destination section index rather than dropping it. Local fixups only cover pointers
inside `__data__`; a global fixup is not a `__data__` pointer unless it names `__data__`, so the
fallback is restricted to that section, and a global fixup into any other section throws
`InvalidDataException` instead of being read as a `__data__` offset.

A resolved pointer is validated against the end of the `__data__` payload —
`BGS/src/Hkx/HkxBinaryReader.Sections.cs:SectionInfo.LocalFixupAbs`, where that section's local fixup
table begins — and not against `HkxBinaryReader.cs:SectionInfo.End`. The section's own end also spans
the local, global, virtual, export and import tables, and those bytes are neither array data nor
strings. The checks run in long arithmetic so a hostile offset cannot wrap into an address that looks
plausible.

Counts are strict and never trimmed. A negative count, an unusable element size, a pointer outside
`__data__`, or `count * stride` larger than the bytes left in `__data__` throws `InvalidDataException`
naming the member. A count of zero returns before the pointer check, because a stale pointer beside a
zero count is harmless. One refusal keeps its old shape deliberately: the bones array still returns a
null skeleton for a count that cannot fit, because the existing count regressions depend on that,
though its pointer is checked in long arithmetic like the rest.

A floatSlot or partition whose entry has no fixup at all stores `null`, which is not the same as a
fixup resolving to the empty string; `BGS/src/Hkx/HkxDataStructures.cs:HkxSkeleton.FloatSlots` and
`HkxSkeletonPartition.Name` are nullable for that reason.
`BGS/src/Hkx/HkxDataStructures.cs:HkxSkeletonLocalFrame` keeps the frame object opaque: it records
only `BoneIndex`, `HasFrame` and the section-relative `FrameOffset` (`-1` when unresolved, and not a
byte offset into the file). No layout is invented for the frame object's contents.

`BGS/src/Hkx/SkeletonJson.cs:SkeletonJson.Write` refuses a non-finite float rather than writing it:
the JSON number grammar cannot carry NaN or infinity, and substituting 0 would hand back a different
skeleton. Reading is typed and strict — a wrong JSON kind, a missing required member, a wrong-length
vector or quaternion, or an integer that does not fit throws `InvalidDataException` naming the member
path (`bones[3].translation[1]`). The arrays this change added are optional: a missing member and an
explicit JSON `null` both mean "not carried", which is what the writer does with an empty array, so
documents written before the change still read. `fo4-skeleton` stays at version 1.

`BGS/src/Hkx/SkeletonDocument.cs:SkeletonDocument` patches the original XML in place, so the members
the document does not own — `partitions`, `localFrames` and the frame reference inside a localFrame —
survive an edit and a save untouched, and `SkeletonDocument.Read` now exposes them read-only. A
localFrame's XML reference is an object reference rather than a byte offset, so `FrameOffset` stays `-1`
there while `HasFrame` keeps the fact that the reference exists.
`BGS/src/Hkx/SkeletonDocument.cs:Standalone` refuses a bone-count or bone-order change when either
array is non-empty: neither is one of the arrays the standalone editor owns, and `partitions` is
indexed by bone, so a remap would be required.

The shipped fixture
`BGS/tools/tests/fixtures/vanilla/Meshes/Actors/Character/CharacterAssets/skeleton.hkx` holds two
`hkaSkeleton` objects. `ReadSkeleton` reports the one with the most bones: `Root`, 95 bones, 93 of them
with `lockTranslation` set. The other is `Ragdoll_NPC COM`, 18 bones, which is the skeleton
`HavokPhysicsExtractor` reports. All four of `referenceFloats`, `floatSlots`, `localFrames` and
`partitions` are empty in that fixture, which is also why a standalone skeleton with no partitions is
still standalone.

Regressions, all in `BGS/tools/tests/HkxSkeletonReadTests.cs`:
`TheVanillaFixtureReadsEverySkeletonMember`, `FloatSlotsReferenceFloatsPartitionsAndLocalFramesAllReadBack`,
`TheSkeletonJsonRoundTripKeepsTheNewMembersAndStillReadsOlderDocuments`,
`AnUneditedVanillaSkeletonLoadsAndSavesByteIdentical`,
`EditingAPoseKeepsPartitionsLocalFramesFloatSlotsAndReferenceFloats`,
`ANonEmptyPartitionArrayStopsAStandaloneSkeletonFromChangingBoneCount`,
`ACountThatCannotFitInTheSectionIsRefused`, `ANegativeArrayCountIsRefused`,
`AnArrayPointerPastTheDataSectionIsRefused`, `AnArrayPointerBeforeTheDataSectionIsRefused`,
`AnArrayPointerPastTheEndOfTheFileIsRefused`, `ATruncatedSkeletonFileIsRefused`,
`AGlobalFixupIntoTheDataSectionResolvesASkeletonMember`, `AGlobalFixupIntoAnotherSectionIsRefused`,
`AMissingSlotOrPartitionNameIsKeptApartFromAnEmptyOne`, `ANonFiniteReferenceFloatIsRefusedOnWrite`,
`ANonFinitePoseValueIsRefusedOnWrite` and `WronglyTypedJsonIsRefused`.

## BGS/src/Hkx/PhysicsLoadClassifier.cs — physics load report (#660)

`PhysicsLoadClassifier.Classify(PhysicsLoadFacts)` is a pure function of measured facts, so a status is testable without a fixture that produces it. It chooses one dominant status in a fixed order and puts that finding first:

- **WrongContainer** — `ContainerProblem` is set when the extraction produced no usable ragdoll *and* `PhysicsLoadClassifier.ContainerProblem` found the container unusual: empty, shorter than a header, XML (an unpacked tagfile), a binary tagfile (the ASCII marker `TAG0` at byte offset 4), a wrong magic, a version other than `Fallout4FileVersion` 11, a pointer width other than `Fallout4PointerSize` 8, big-endian, or layout rules other than `8 1 0 1`. The message names what was detected.
- **ParseError** — reading failed after the header; `FailingStage` names the stage (`container`, `byte-backed XML`, `model` or `ragdoll`).
- **LegacyPhysics** — legacy hkp rigid bodies, shapes or a physics system, and no `hknpRagdollData`. `IsLegacyPhysicsClass` excludes constraint classes and constraint atoms on purpose (`hkpRagdollConstraintData`, `hkpLimitedHingeConstraintData`, `hkp*ConstraintAtom`): a Fallout 4 ragdoll carries those legitimately, so hkp constraint data alone never selects this status.
- **SkeletonOnly** — no `hknpRagdollData` at all; Warning when an `hkaSkeleton` is present, Info otherwise, with a hint naming the file's other physics classes or the sibling skeleton.
- **NoBodies** — a ragdoll is present but `UsableBodies == 0`: it declares no bodies, or every `hknpBodyCinfo` frame failed to read.
- **MapperMissing** — readable bodies but `Mappings == 0`. With no `hkaSkeletonMapper` class the message says so; with the class present but no usable mapping it says "An hkaSkeletonMapper is present but none of its mappings link this ragdoll to the animation skeleton". Either way the ragdoll cannot follow the animation.
- **UnsupportedShapes** — a readable ragdoll that is incomplete: bodies using a shape other than `hknpCapsuleShape`, or body/constraint entries that could not be read. The remaining bodies are still listed, with no collision hull.
- **Ok** — everything above held.

The container is never refused before it is read. `HavokPhysicsExtractor.TryExtract` always attempts `PackfileImage.Read` and the extraction, and the strict container facts are only used to classify: a failure (an exception, or no usable ragdoll at all) chooses WrongContainer when the container is unusual and ParseError otherwise, while a usable ragdoll with an unusual container keeps the status its physics deserves and gets `PhysicsLoadFacts.ContainerNotice`, which `Classify` appends as an extra Info finding. The same principle keeps the measurement visible whatever the primary status is: `AddExtraFindings` adds Info findings for unsupported-shape bodies, skipped body entries and skipped constraint entries unless the primary finding already states them, and a readable ragdoll that also carries hkp objects gets the legacy Info finding on `Ok` and on `NoBodies` — Fallout 4 loads the hknp ragdoll, so those objects are ignored rather than a downgrade.

`HavokPhysicsExtractor.TryExtract(byte[]? hkx, out PhysicsLoadReport report)` is the reporting overload; the one-argument overload and its null contract are unchanged, and `PhysicsLoadClassifier.Inspect` is the model-free entry point the census uses. It always attempts `PackfileImage.Read` and the extraction: the strict container facts are only used to classify, so a container that reads anyway is measured instead of refused, and an extraction that produced no usable ragdoll chooses WrongContainer when the container is unusual and ParseError otherwise. The shape/body/constraint/mapper work is wrapped in the `ragdoll` stage so nothing escapes as an unhandled exception, and `PhysicsLoadReport.StatusLabel`/`StatusSlug`/`Summary`/`Diagnostics` render the outcome for logs, machine output and the clipboard.

UI: `BGS/app/MainWindow.Playback.cs:ShowPhysicsReport` stores a non-Ok sentence in `_physicsReportNote` and colours it by severity. `BGS/app/MainWindow.Playback.cs:ChooseRagdoll` is the sibling-skeleton seam `TryLoadRagdoll` uses: it prefers the sibling's report for every status except `SkeletonOnly` (so the sibling's `ParseError`, `WrongContainer`, `NoBodies` and `MapperMissing` are shown, not only `LegacyPhysics`/`UnsupportedShapes`), adopts the sibling model and names it as the source when it has bodies, and turns a sibling read `IOException`/`UnauthorizedAccessException`/`InvalidDataException` into a `ParseError` whose failing stage is exactly `sibling read` instead of swallowing it. `BGS/app/MainWindow.Open.cs:AppendPhysicsNotes` (the renamed `AppendRagdollSourceNote`) appends each note at most once — both the source note and the physics note are checked against `_summaryBaseText`, so it is idempotent — and it is called again at the end of `LoadPose` so the notes come back after `LoadPoseFromSelection` overwrites the summary. `ClearPhysicsReport` resets the report, its path and the note, disables Copy diagnostics, and is called from `ClearPose` and from a `LoadPose` whose key is not the loaded document, so Copy diagnostics can never describe a different file. "Copy diagnostics" is enabled only once a report exists and disabled when a load measures nothing; with no report it says so, and with no clipboard it says the environment has none instead of throwing. `BGS/app/RigAuthoringWindow.Physics.cs:OpenPhysics(string path, out PhysicsLoadReport report)` returns whether a usable ragdoll was loaded, always stores the report for the path it was asked to open (a `ReadHkx` failure becomes a `ParseError` whose stage is `read`), and keeps the previously loaded ragdoll on a failed open with a status line that says the previous ragdoll kept in the preview is unchanged. `BGS/app/MainWindow.AssistantFiles.cs` refuses the assistant's `ragdoll` file operation with `report.Summary` when no usable ragdoll was loaded.

Packed sibling fallback: `BGS/app/MainWindow.Playback.cs:TryLoadRagdoll` still prefers the loose sibling `SiblingSkeletonPath` returns, but when that returns null and the loaded file sits under `_gameData.DataFolder`, `DataRelativePath` turns the file into its data-relative virtual path and `PackedSkeletonBytes` asks the existing `GameData.SkeletonBytes` for the packed sibling, so a fully packed actor gets the same "measured from the sibling skeleton" ragdoll it would get from a loose one. No new discovery was added: with no `GameData` in that context, or a file outside the data folder, the behaviour is exactly as before, and the extract callback reuses the bytes already read instead of reading the sibling path again.

Coverage: `BGS/tools/tests/PhysicsLoadReportTests.cs` (every status, the unusual-container notice, the binary tagfile, the `container` ParseError, the diagnostics dump, the extractor paths), `BGS/tools/tests/RagdollSiblingTests.cs` (the `ChooseRagdoll` seam: every sibling status except `SkeletonOnly`, the `sibling read` ParseError, adoption and own-ragdoll priority), and `BGS/tools/uismoke/RigAuthoringSmoke.cs` (the rig status line and the assistant refusal) with `BGS/tools/uismoke/RagdollLifecycleSmoke.cs` (Playback note and Copy diagnostics button).

## BGS/app/ModlistScan.SkeletonPhysics.cs — --scan-skeleton-physics (#659)

`ModlistScan.ScanSkeletonPhysics` (a partial `ModlistScan`, dispatched from `BGS/app/Program.cs`) censuses the physics of a folder. Discovery and every read go through `GameData` (#696): `SkeletonPhysicsData` builds one `GameData` for the folder — the `GameData` `Mo2ScanData.TryOpen` already discovered with the instance's mod roots when the folder is an MO2 instance, otherwise `GameData.Discover(folder)` — and `EnumerateWinners("", GameData.IsSkeletonName, warn)` returns the winners in the game's own precedence order. The old code walked `*.ba2` recursively and called `Ba2.Open` itself; its two name rules (`IsSkeletonEntry` for entries, `IsLooseSkeleton` for files: a `.hkx` whose last path component starts with `skeleton`) are exactly `GameData.IsSkeletonName`, so the censused set is unchanged. Winners are read with `GameData.Read(GameAsset)`, which routes a packed winner through the held-open archive cache and a loose winner through its file: a loose winner is now read by `File.ReadAllBytes` instead of `InputFilePolicy.ReadHkx`, so the census has one read path and the 512 MB HKX input cap no longer applies to a loose skeleton row (a file that cannot be read at all is still `unreadable`, and a loose file is written into `Read`'s null-for-IO contract). A loose skeleton that overrides a packed copy is therefore one row — the winner — and the copies it hides are the winner's `Shadowed` list, rendered by `ShadowDetails` as one `shadowed: <virtual path> [<source>]` line in the finding's `details` (JSON only) and counted by the new `shadowed` summary key. `RecordSkeletonPhysics` turns each winner into a `PhysicsLoadReport` and one row with its status, bodies, shapes, constraints, physics/shape/constraint classes, mapper and material. A skeleton with no ragdoll is a row, not a failure — this is a census, not a pass/fail list.

Under `--json` the output is a `ScanReport` named `scan-skeleton-physics` with summary keys `archives`, `scanned`, `ok`, `skeletonOnly`, `legacyPhysics`, `unsupportedShapes`, `mapperMissing`, `noBodies`, `wrongContainer`, `parseError`, `unreadable`, `skippedArchives` and `shadowed`; each file's finding kind is the report's status slug. Human mode prints one `STATUS LABEL path [source]` line per file plus its detail lines, and its `done` line reports `OK`, preview gaps, skeleton-only files, files with no usable ragdoll and unreadable entries separately.

Exit code 0 when every scanned file is `Ok`, `UnsupportedShapes` or `SkeletonOnly` and nothing was unreadable; 1 only when a file is `LegacyPhysics`, `MapperMissing`, `NoBodies`, `WrongContainer` or `ParseError`, or something could not be read; 2 for a missing or nonexistent directory, through the shared usage path. `SkeletonOnly` is 154 of the 195 vanilla skeletons, so it is reported separately and is never part of the "no usable ragdoll" count. A `.ba2` that cannot be opened contributes the `warn` callback's message: `skippedArchives` counts it, the archive's relative path is recovered by matching the warning's file name against `GameData.ArchivePaths`, and the `skip` finding does not change the exit code. A winner that `GameData.Read` cannot read is an `unreadable` finding and does force exit 1.

Re-run on the vanilla install (`D:\SteamLibrary\steamapps\common\Fallout 4\Data`, read-only) after the refactor: 195 scanned, 21 `Ok`, 20 `UnsupportedShapes`, 154 `SkeletonOnly`, 7 archives, 0 shadowed, 0 unreadable, exit 0 — identical to the pre-refactor census, and 195 findings.

Measured census and per-fixture evidence: `BGS/docs/research/fo4-skeleton-physics-census.md`. CLI coverage: `BGS/tools/tests/ScanCliTests.cs` on `BGS/tools/tests/TestArchives.cs` (the three GameData staging cases: a packed winner, a loose winner shadowing a packed copy, and an MO2 instance).

## BGS/src/Hkx/SkeletonMapperDocument.cs — skeleton mapper write-back (#672)

`SkeletonMapperDocument` edits `hkaSkeletonMapper` data and writes it back into the packfile.
`symrm mapper <file> [--recompute] [--out <file>] [--force] [--data <Data folder>]` is its user-facing
surface (`BGS/tools/symrm/MapperCli.cs:Mapper`); `BGS/tools/tests/SkeletonMapperWritebackTests.cs` (18
tests), `BGS/tools/tests/SkeletonMapperWriterTests.cs` (11) and
`BGS/tools/tests/SkeletonMapperReaderLeniencyTests.cs` (2) cover it, and
`BGS/tools/tests/MapperCliTests.cs` (6) covers the command line. `BGS/tools/tests/SkeletonMapperAuthorTests.cs`
(18) covers the generator's own arithmetic.

**Layout provenance.** The field table below is RE doc 68 §2 (`hkaSkeletonMapperData`, 176 B;
`hkaSkeletonMapper`, 192 B with `mapping` @ `0x10`; simple row 64 B; chain row 112 B), which doc 63
§1.2/§1.3 and doc 67 §1 cross-check against what `mapPose` reads. It was compared field by field with
`BGS/src/Hkx/HavokClassLayouts.json` and `BGS/src/Hkx/HavokClassTypes.json` before any code was
written: every offset, size and type agrees, with **no disagreements** (`hkaSkeletonMapperData`:
`skeletonA` @0, `skeletonB` @8, `partitionMap` @`0x10`, `simpleMappingPartitionRanges` @`0x20`,
`chainMappingPartitionRanges` @`0x30`, `simpleMappings` @`0x40`, `chainMappings` @`0x50`,
`unmappedBones` @`0x60`, `extractedMotionMapping` @`0x70`, `keepUnmappedLocal` @`0xA0`,
`mappingType` @`0xA4`; simple row `boneA` int16 @0, `boneB` int16 @2, `aFromBTransform` @`0x10`;
chain row `startBoneA`/`endBoneA`/`startBoneB`/`endBoneB` int16 @0/2/4/6,
`startAFromBTransform` @`0x10`, `endAFromBTransform` @`0x40`). A `hkQsTransform` is 12 lanes with
translation at lane 0, rotation at lane 4 and scale at lane 8 (doc 63 §1.3).

**Reading, and the two layers of strictness.** `SkeletonMapperReader` used to drop everything but the
simple rows. It now also reads `chainMappings`, `unmappedBones`, `keepUnmappedLocal` and
`extractedMotionMapping`, and keeps each row's 12 raw lanes in `HavokSkeletonMappingTransform`. That
matches the shipped corpus: doc 66 §2.1 measures chain rows in 25 of 43 forward mappers and doc 66 §2.2
non-empty `unmappedBones` on every reverse mapper, and the committed human fixture reads back as 4 chain
rows per mapper and 71 unmapped bones on the reverse mapper, the figures doc 63 §3 and doc 66 §2.2
report for it. `SimpleMappings` (the consumer view the physics extractor uses) is derived from the raw
rows, so no existing caller changed.

The four added members are **optional for the reader and required for the writer**. A mapper whose chain
array or unmapped-bone array cannot be located, or whose `keepUnmappedLocal` or
`extractedMotionMapping` cannot be read, still comes back with its `SimpleMappings` — the unreadable
member is replaced by its empty/default value and `HavokSkeletonMapper.OptionalMembersRead` is false
(`SkeletonMapperReader.ReadChains`, `ReadUnmappedBones`, `ReadOptionalMembers`). `HavokPhysicsExtractor`
therefore keeps the mappings it used to get, which is the whole point: a mapper the extractor can still
use must not disappear because a member it never reads is unreadable. Everything that *writes* is
stricter. `SkeletonMapperDocument.Reload` refuses to load such a file at all, and
`SkeletonMapperWriteback.RequireComplete` refuses the write, because a write-back would rewrite the file
from a model that has silently lost those rows. Both say which mapper and why.

**Editing, and what is refused.** Simple rows can have their bones or their transform set, be added,
inserted or removed; chain rows have their four indices and two transforms replaced; `mappingType`
and `keepUnmappedLocal` can be set. `SkeletonMapperAuthor.Recompute` adds the single-row form of the
doc 63 §2 value `inverse(modelSpace_A[boneA]) * modelSpace_B[boneB]`, and `RecomputeSimpleRow(s)`
writes only its translation and rotation onto the stored row. Refusals are explicit
`InvalidOperationException`s: bone indices outside the referenced skeleton, non-finite lanes, a
non-unit rotation (tolerance `SkeletonMapperAuthor.UnitTolerance`), a `mappingType` other than the 0
(ragdoll) and 1 (retargeting) the generator itself defines (doc 63 §6.4), and a simple-row edit that
would map a bone twice. The duplicate rule is a policy, not a format limit: shipped files contain
exact duplicate rows (doc 66 §2.3) and doc 68 §3.5 records that nothing rejects them, so an explicit
`allowDuplicatePair: true` writes one while the default refuses it. The **scale and pad lanes are
never invented**: doc 63 §3 and doc 65 §4.1 leave row scale semantics unresolved, so a recompute
preserves the stored lanes, the `(translation, rotation)` overload preserves them too, and only a
caller who passes a whole transform replaces them.

**Chain rows are never recomputed at all — endpoints included.** There is no chain counterpart to
`RecomputeSimpleRow`: the only way a chain transform changes is `SetChainRowTransforms`, which stores
the caller's transform verbatim. The reason is narrower than "the interior is unresolved". The
doc 63 §2 formula is the *single-bone-pair* value `inverse(modelSpace_A[boneA]) * modelSpace_B[boneB]`,
and the endpoint of a chain run is not that value for its own bone pair — it is whatever the run's
distribution puts there (doc 67 §2/§4, doc 65 §4.3). Nothing measures a chain endpoint against
`Recompute`, so deriving one and writing it would be an invented value with no check behind it; the
command says so out loud ("chain rows were left alone, interior and endpoints alike") and the
`Recompute*` entry points never touch `ChainMappings`. Chain rows also cannot be added or removed:
`WriteChains` calls `RequireResized`, so a chain count that differs from the file's is refused with
"only simple rows are resized". The `symrm` add-then-remove sweep and the tests only ever resize the
simple rows.

**Write-back, two paths.** A file with no edits returns its own bytes, so an unedited load and save is
byte-identical by construction — that shortcut is exactly why an unedited round trip proves nothing
about the writer. `SkeletonMapperDocument.ForceRebuild` is the honest test: it hands
`SkeletonMapperWriteback.Write` a no-op change for every mapper (`force: true`), so the whole write path
runs — storage location, the value pass over every row and chain row, `RequireConfined`,
`PackfileImage.Rebuild` and the read-back `Verify` — while nothing is meant to change, and the result
must equal the input byte for byte. `Write` refuses to combine `force` with real edits.

Edits that change no row count are patched straight into the `__data__` section at the row's offset and
the file is rebuilt from its sections; each written field records the byte range it owns, and a diff
byte outside every recorded range is refused rather than written. Chain row ranges are recorded per
field (four 2-byte index ranges plus two 12-lane transform ranges) rather than as one span, so the
padding between the indices and the transforms is not silently claimed.

Row add/remove goes through the native relocate path, mirroring what `NativeSave.Regrow` does for an
instance's array: append a 16-byte-aligned run to `__data__`, copy the rows that survive, repoint the
array's fixup (its local one, or its global one; a header that has both, or neither, is refused), move
any fixup that pointed inside the old run, write the count at `+8` and `flags|count` at `+12`, reorder
fixups with `FixupOrder.Reorder` and rebuild. That machinery is reimplemented here rather than called
through `NativeSave.Apply` because `hkaSkeletonMapperData` is an embedded struct inside
`hkaSkeletonMapper`: `NativeSave.Resolve` looks an edit's class up among the packfile's instances, so a
change naming `hkaSkeletonMapperData` has no instance to land on, and `NativeSave.Compare` sees only one
flattened `mapping.simpleMappings` text field with no byte layout. The primitives are the same ones
`Apply` uses (`PackfileSection` append/align/fixup setters, `FixupOrder`, `PackfileImage.Rebuild`).

**What the structural path is allowed to touch, and the rules that keep it there.**

- *Shared row storage is checked against the whole old run.* `RequireStorage` is called with the
  **original** row count before the relocate, so the inspected window is the run as the file holds it —
  including the rows a shrink is about to drop. Using the smaller (edited) count would have missed a
  pointer into a dropped row and `Move` would have discarded it silently. After the relocate the same
  check runs again with the **edited** count against the freshly appended run, so a larger array checks
  its new storage too. A pointer into either is refused with "shares its row storage with another
  object". The window is not widened past the run: extending it to the new, larger count walks into the
  array that legitimately follows the old run and refuses a valid edit, which is how this check first
  failed on the human fixture.
- *The global-fixup half of the header check reads only this section.* `Relocate` looks for the header
  in `data.Globals()`, not in every section's global table. Source offsets are per-section byte spaces,
  so a global entry in `__classnames__` (or anywhere else) whose source happens to equal this header's
  offset is a different pointer, and treating it as a second fixup on this header refused a file that
  only has a local one. `BGS/tools/tests/SkeletonMapperWriterTests.cs:AGlobalFixupInAnotherSectionOnTheSameOffsetIsIgnored`
  pins that, and `AGlobalFixupHeaderIsRelocated` is the opposite control: a header whose only fixup is a
  global one in its own section still relocates.
- *After the rebuild the change is confined.* `RequireStructuralConfined` diffs the new `__data__`
  against the old one and allows only the array header's pointer slot and its `+8`/`+12` pair, the
  fields the value pass recorded as its own, the zeroed old run, and the appended region (the alignment
  padding must be zero there and the rest must be inside the appended run). `RequireOtherInstancesIntact`
  additionally walks every **other** object instance in the file and refuses if a single byte of it
  changed, so an unrelated mapper, skeleton or `hknpRagdollData` in the same file cannot be reached.
  `RequireFixupsConfined` compares the local and global tables as multisets outside the repointed
  headers: a structural edit may repoint its own header, and nothing else, in either table. Any of the
  three refusals returns nothing at all.
- *The orphaned run is zeroed, not reused.* `Relocate` clears the old run's bytes after copying the
  survivors out of it, so a stale pointer left behind reads as zeros rather than as the dropped rows.
  The old run stays where it is: each structural save appends a fresh `count * stride` run and never
  reuses the previous one, so **saving the same structural edit again grows the file by another row
  run** (plus its alignment padding) every time. That is the price of never moving the objects that
  other fixups point at, and it is why `symrm mapper --data` reports the byte delta a single added row
  costs on each rig.

After either path the produced bytes are re-read through `SkeletonMapperReader` and every mapper —
edited and untouched — must come back equal to the model that was written, or nothing is returned at
all. `Save` writes through `FileSafety.ReplaceChecked` with the source stamp, like the other documents.

**The command line.** `MapperCli.Mapper` parses `--recompute`, `--out`, `--force` and `--data` and
refuses anything else, or a missing value, with exit 1 and the `mapper` usage text. `--out` is refused
when it names the file being read or an existing file, unless `--force` is passed; a write always goes
to a temporary file beside the target and is moved into place (`FileSafety.Replace`), so a failure
leaves the existing file untouched. `--recompute` on its own prints the error it measured and says that
nothing was written. `--data` switches to the corpus sweep, which writes nothing anywhere, so `--out`
is refused outright in that mode rather than ignored. The sweep's unedited check now calls
`ForceRebuild` and counts a file as byte-identical only when the comparison succeeded, instead of
counting first and reporting the failure afterwards.

**Measured.** The Fallout 4 install (7 archives, the 43 rigs and 86 mappers of doc 66 §1) was re-run
through the forced rebuild. **43 of 43 mapper-bearing files come back byte-identical when an unedited
file is pushed through the writer's full path** — `RequireStorage`, the value pass, `RequireConfined`,
`PackfileImage.Rebuild` and `Verify` — and the committed human fixture does the same (1 of the 11
committed fixtures holds a mapper). Before this was measured, the "byte-identical" figure came from
`Serialize` returning the source clone for an unedited document and said nothing about the writer; the
number above is the writer's, and `SkeletonMapperWritebackTests.EveryMapperFixtureForcedRebuildIsByteIdenticallyRebuilt`
is the standing check. 43 of 43 also survive an add-then-remove of a simple row read back through the
reader. What a **structural** save does *not* reproduce is the fixup-table layout: `FixupOrder.Reorder`
rewrites the local and global tables in walk order, which on the human fixture moves 104 bytes of the
global table (the first difference is at `0xa350`, the source field of global entry 100) while leaving
the XML rendering identical. That is expected, it is inside the "fixup tables" allowance above, and it
is the reason a structural save is compared by its invariants rather than byte for byte.

Recomputing the shipped simple rows reproduces them to `3.31e-4` translation and `6.91e-4` rad rotation
worst case over the corpus, and to `4.72e-5` translation and `0` rotation over the human fixture's 36
rows. The corpus number is larger because the larger rigs are measured in larger units: the human
fixture's larger skeleton spans 120.5 units and the error there is `3.9e-7` of that span, so the standing
assertion is **relative to the extent** (`SkeletonMapperWritebackTests.RecomputedRowsMatchTheShippedValues`)
with rotation bounded separately in radians. A second scale-aware figure lives in the `--recompute`
output, which prints both the absolute error and its fraction of the extent.


## BGS/src/Archive/GameData.cs — shared read-by-entry and precedence API (#696)

The audit behind #696 found BGS features opening `.ba2` files and deciding loose-versus-archive precedence themselves, because `GameData` had no public API for it. The rule now lives in one place: `BGS/src/Archive/GameData.Assets.cs` (a partial `GameData`) exposes `ReadEntry`, `TryReadEntry`, `Read(GameAsset)`, `EnumerateWinners`, `ModlistPath`, `IsSkeletonName`, `IsCharacterAssetPath` and `IsSkeletonPath`, plus the `GameAsset` record and the `GameAssetOrigin` enum. Everything there goes through `Ba2` (Radicle.Interop) and `GameData`'s held-open archive cache; no BA2 byte parsing was added. Follow-up work moves the features onto this surface. `GameData` is single-threaded: it carries no synchronisation, and the held-open archives, the mod-loose index and the per-archive lookup are only safe from one thread at a time.

- **Read-by-entry.** `ReadEntry(archivePath, entryName)` returns `byte[]?`; `TryReadEntry(...)` is the same read with `out byte[] bytes, out string? warning`. Both use `GameData.Open`, so an archive's index is parsed once per run and reused (the test counts `Ba2.OpenCountForTest`: two reads, one open). Matching prefers an EXACT name first — the request flattened with `'\\'` → `'/'` and compared to the archive's own `Ba2.Entry.Name` with `OrdinalIgnoreCase` — and only falls back to the `Normalize` key (extension dropped, `'.'`/`'..'` collapsed) when no entry has that exact name. That is what lets `skeleton.hkx` and `skeleton.hkt` in one archive each read their own bytes: the old normalize-only match collapsed both onto one key and always returned whichever came first in index order. `Read(GameAsset)` passes the asset's exact `EntryName` (the name `EnumerateWinners` took from the archive), so a winner reads its own entry. A missing entry is `false` with a null warning. A DX10 archive is `false` with a null warning, preserving the wording contract `BGS/src/Archive/Ba2.cs:Ba2.Open` documents. A skippable failure (missing, unreadable, truncated) is `false` with `"<file name>: <first line>"`, the shape `TryArchiveEntries` already produced for the scan. Native-load failures still propagate (`IsSkippableArchiveFailure`).
- **Per-archive lookup.** Each archive gets one `ArchiveIndex` built on first use: the exact `Ba2.Entry.Name` → entry index, and the `Normalize` key → the index of the FIRST entry with that key. An archive that cannot be indexed is remembered together with its warning (null for DX10), so a texture archive is opened once per run instead of on every lookup. It is built once with the held-open archive and cleared in `Dispose`, so `ReadEntry`/`Read` are O(1) rather than a scan of every entry, and `FindEntry` (and therefore `ReadAnimation`) resolves a declared path by one dictionary hit instead of walking the entry list. `FindEntry`'s suffix fallback picks the SMALLEST matching index, which is the entry the old first-match-in-archive-order scan returned. The index replaces the old `GameData.Names` normalized-name set, which `Subfolders` still reads through.
- **Precedence.** `EnumerateWinners(virtualPrefix, nameFilter, warn)` walks, in order: mod roots highest to lowest (`ModRoots` reversed), the Data folder's loose files, then `ArchivePaths` in its current order. That archive order is unchanged and stays `OrderByPlugins`' business (#695). Candidates are filtered first: `virtualPrefix` matches the `'/'`-normalized *full* path with its extension, so the directory `Meshes/Actors/Turret` never claims `Meshes/Actors/Turret.hkx`, and `nameFilter` receives the `VirtualPath` as spelled by the source. Precedence is then keyed by `GameData.Normalize`, which is the cross-source collapse: the first candidate for a key wins, and every later candidate for that key in a lower-precedence source becomes one entry of that winner's `Shadowed` list, in the same source order. Distinct files *within one source* are never dropped — a second distinct file with the same key inside the winning source is returned as its own winner (the `skeleton.hkx`/`skeleton.hkt` case on a case-sensitive filesystem), and every winner in that same-source group receives the same lower-priority shadows, so no caller loses the loser list by picking the other twin. A caller counting winners for one key can therefore see more than one top-level asset.
- **Archive warnings.** `warn` (an `Action<string>?`, optional) is fed from the same `TryArchiveEntries` handling the read API uses: any archive that raises a skippable failure contributes its `"<file name>: <first line>"` warning and is skipped, instead of disappearing from the result silently. A DX10 texture archive returns `false` with a null warning, so it stays silent; a native-load failure still propagates rather than being downgraded to a warning.
- **Loose walking.** Each root is walked by hand rather than with `SearchOption.AllDirectories`, which follows links. Reparse points (symlinks, junctions) are skipped for files and directories below the root, and every candidate must satisfy `IsInsideRoot(root, full)`. A root that is ITSELF a junction is deliberately followed: only links below it are refused, so a Data folder reached through a junction still enumerates. `IsInsideRoot` is the one containment rule (it replaced the old private `IsUnder`), so a path that escapes through `..` or through a link target is refused rather than indexed. The walk resolves the root once and checks containment once per file, yielding already-validated full paths, so `OfferLoose` does not repeat `Path.GetFullPath`/`IsInsideRoot`. The walk covers each root's whole subtree — the prefix prunes candidates, not directories — so callers in a hot loop should cache the result. A loose file whose size cannot be read gets `Size == -1`; an archive asset's `Size` is `Ba2.Entry.Unpacked`, the decompressed size.
- **Lifetime.** `GameData` owns the held-open archives, so a caller that keeps entries for a later read must keep the `GameData` instance alive. `Dispose` closes every opened archive and clears the archive index caches (successes and failures) and the mod-loose cache; it is idempotent. Any archive call after `Dispose` throws `ObjectDisposedException` — `ReadEntry`, `TryReadEntry`, `Read` of an archive asset, `EnumerateWinners`, and through them `FindEntry`/`ReadAnimation` once an archive has to be opened or indexed — instead of silently reopening a fresh native handle.
- **`Read(GameAsset)`** dispatches to the loose file or to `ReadEntry`, and returns null when a loose read fails (IO or permission), mirroring `ReadEntry` and `ReadAnimation`.
- **`ModlistPath(modsFolder, profile)`** is the single MO2 modlist rule extracted from `DiscoverModded`, which now calls it: `<modsFolder>/profiles/<profile>/modlist.txt`, or `<modsFolder>/modlist.txt` when no profile is named, and null when that file does not exist.
- **The skeleton predicates are split.** `IsSkeletonName(fileName)` is the name-only rule: `.hkx` (OrdinalIgnoreCase) whose last path component starts with `skeleton` (`skeleton_physics.hkx` yes, `skeletons/rifle.hkx` no). `IsCharacterAssetPath(virtualPath)` is the folder rule alone: any `CharacterAssets` component except the last. `IsSkeletonPath(virtualPath)` is the documented combination — the `.hkx` gate, then `IsSkeletonName` or `IsCharacterAssetPath` — and keeps its previous behaviour exactly. A caller that only needs "is this file a skeleton by name" should use `IsSkeletonName`: that is the R0 archive scan and the mapper sweep, which sweep entries one file name at a time and must not treat every `.hkx` under a `CharacterAssets` folder as a rig. A caller that wants the folder convention keeps `IsCharacterAssetPath`/`IsSkeletonPath`; `GameData.SkeletonBytes` still asks `IsSkeletonPath` for the name test after it has already required the `<prefix>/CharacterAssets/` entry prefix, and `BGS/app/MainWindow.Animation.cs:SiblingSkeletonPath` still scans a `CharacterAssets` folder for the first `.hkx` that parses. Accepting any `.hkx` under `CharacterAssets` is intentional, because that is what `SkeletonBytes` already did.

`SkeletonBytes` and the archive branch of `ReadAnimation` were refactored onto these members without a behaviour change. Regression coverage is `BGS/tools/tests/GameDataAssetsTests.cs`: read hits/misses, exact-name reads when two extensions share a normalize key, DX10 and skippable archives, the truncated-archive warning, precedence across mod/Data/archive sources, archive order, shadow lists, within-source pairs and their shared shadows, reparse-point skip, outside-root refusal, `ModlistPath` parity with `DiscoverModded`, and the `IsSkeletonName`/`IsCharacterAssetPath`/`IsSkeletonPath` cases.

`BGS/tools/tests/TestArchives.cs:WriteGeneralBa2` is a hand-rolled copy of the GNRL record layout — the 16-byte chunk header with the `0x00100100` size word and the `0xBAADF00D` sentinel, entry data before the length-prefixed name table — kept in step with `Radicle/src/archive/ba2.cpp:write_ba2_general` by hand; Radicle #694 exports the real writer, and this fixture should be pointed at it instead of carrying its own copy.

Radicle #692/#693 should replace this loose walk and precedence rule with the native resolver's own enumeration; until then `EnumerateWinners` is the only implementation of the rule in BGS.
