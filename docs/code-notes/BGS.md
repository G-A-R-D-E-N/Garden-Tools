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

Radicle requires each GNRL record to declare a 16-byte chunk header (the count and size word is `0x00100100`) and the `0xBAADF00D` chunk sentinel. Fixtures that wrote zeros there passed only because the previous reader ignored those fields. `TestArchives.WriteGeneralBa2` is now the single copy of that record layout, and it places the entry data before the length-prefixed name table, the order `Radicle/src/archive/ba2.cpp:write_ba2_general` writes: the earlier fixtures put the name table before the data, which only ever parsed for an archive with exactly one entry (a multi-entry archive written that way fails as "BA2 is truncated"). `BGS/tools/tests/Ba2RadicleTests.cs` and `BGS/tools/tests/AuditRegressionTests.cs` call the shared writer, and `TestArchives.TempDirectory` replaced the four identical per-class temp-directory helpers (the last one in `BGS/tools/tests/ModlistScanDataTests.cs:Mo2Fixture`).

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

The Animation tab shows these three groups as the **Trim**, **Retime** and **Loop** tools of its tool selector (`BGS/app/MainWindow.Animation.Tools.cs`), one panel at a time: **Trim** (`first` and `last` frame boxes, `Preview trim`, `Trim`), **Retime** (a `x` speed factor box and a `seconds` new-length box, `Preview retime`, `Retime`) and **Loop** (a `blend frames` box, `Preview loop`, `Loop`). Enter in any of the boxes runs that tool's apply handler, matching the frame-edit boxes. The tab keeps two answer lines: the existing `_frameEditAnswer` for a single frame, and a new `_geometryAnswer` for trim, retime and loop, so the two kinds of edit never overwrite each other's report; `ShowAnimationTool` moves `_geometryAnswer` into the selected tool's answer slot, and the Find frame panel keeps `_fractionAnswer`.

All of the new fields and handlers live in `BGS/app/MainWindow.Animation.Geometry.cs`, a partial of the same window, so `BGS/app/MainWindow.Animation.cs` only gained the three tool panels (`AnimationToolRibbon` takes `GeometryButtons()`, which builds the six Preview/Apply buttons and keeps their Enter wiring) and two `ResetGeometry()` calls (one when `BuildAnimation` starts, one after a clip decodes). `ResetGeometry` clears the pending timeline and the boxes, and seeds the trim range with `0` to `NumFrames - 1`. The same reset runs wherever `_animationData` is set to null in `BGS/app/MainWindow.Open.cs`.

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
though its pointer is checked in long arithmetic like the rest. `BGS/src/Hkx/HkxBinaryReader.Sections.cs:ParseHkx` catches that refusal per skeleton candidate and skips it, so a malformed embedded skeleton cannot make a whole animation unreadable; `ParseHkx(data, true)`, which `ReadSkeleton` alone uses, rethrows it, so a standalone skeleton read, the skeleton document path and the `symrm` skeleton commands still fail closed on the same bytes.

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
`ANonFinitePoseValueIsRefusedOnWrite`, `WronglyTypedJsonIsRefused` and `AClipWithAMalformedEmbeddedSkeletonStillDecodesAndSaves`.

## BGS/src/Hkx/PhysicsLoadClassifier.cs — physics load report (#660)

`PhysicsLoadClassifier.Classify(PhysicsLoadFacts)` is a pure function of measured facts, so a status is testable without a fixture that produces it. It chooses one dominant status in a fixed order and puts that finding first:

- **WrongContainer** — `ContainerProblem` is set when the extraction produced no usable ragdoll or threw, *and* `PhysicsLoadClassifier.ContainerProblem` found the container unusual: empty, shorter than a header, XML (an unpacked tagfile), a binary tagfile (the ASCII marker `TAG0` at byte offset 4), a wrong magic, a version other than `Fallout4FileVersion` 11, a pointer width other than `Fallout4PointerSize` 8, big-endian, or layout rules other than `8 1 0 1`. The message names what was detected.
- **ParseError** — reading failed after the header; `FailingStage` names the stage (`container`, `byte-backed XML`, `model` or `ragdoll`).
- **LegacyPhysics** — legacy hkp rigid bodies, shapes or a physics system, and no `hknpRagdollData`. `IsLegacyPhysicsClass` excludes constraint classes and constraint atoms on purpose (`hkpRagdollConstraintData`, `hkpLimitedHingeConstraintData`, `hkp*ConstraintAtom`): a Fallout 4 ragdoll carries those legitimately, so hkp constraint data alone never selects this status.
- **SkeletonOnly** — no `hknpRagdollData` at all; Warning when an `hkaSkeleton` is present, Info otherwise, with a hint naming the file's other physics classes or the sibling skeleton.
- **NoBodies** — a ragdoll is present but `UsableBodies == 0`: it declares no bodies, or every `hknpBodyCinfo` frame failed to read.
- **MapperMissing** — readable bodies but `Mappings == 0`. With no `hkaSkeletonMapper` class the message says so; with the class present but no usable mapping it says "An hkaSkeletonMapper is present but none of its mappings link this ragdoll to the animation skeleton". Either way the ragdoll cannot follow the animation.
- **UnsupportedShapes** — a readable ragdoll that is incomplete: bodies using a shape other than `hknpCapsuleShape`, or body/constraint entries that could not be read. The remaining bodies are still listed, with no collision hull.
- **Ok** — everything above held.

The container is never refused before it is read. `HavokPhysicsExtractor.TryExtract` always attempts `PackfileImage.Read` and the extraction, and the strict container facts are only used to classify: a thrown exception chooses WrongContainer when the container is unusual and ParseError otherwise, an unusual container with no usable ragdoll chooses WrongContainer even without an exception, and a normal container with no ragdoll is `SkeletonOnly`, not a failure; while a usable ragdoll with an unusual container keeps the status its physics deserves and gets `PhysicsLoadFacts.ContainerNotice`, which `Classify` appends as an extra Info finding. The same principle keeps the measurement visible whatever the primary status is: `AddExtraFindings` adds Info findings for unsupported-shape bodies, skipped body entries and skipped constraint entries unless the primary finding already states them, and a readable ragdoll that also carries hkp objects gets the legacy Info finding on `Ok` and on `NoBodies` — Fallout 4 loads the hknp ragdoll, so those objects are ignored rather than a downgrade.

`HavokPhysicsExtractor.TryExtract(byte[]? hkx, out PhysicsLoadReport report)` is the reporting overload; the one-argument overload and its null contract are unchanged, and `PhysicsLoadClassifier.Inspect` is the model-free entry point the census uses. The container rule above is the only copy of it: this overload adds no second one. The shape/body/constraint/mapper work is wrapped in the `ragdoll` stage so nothing escapes as an unhandled exception, and `PhysicsLoadReport.StatusLabel`/`StatusSlug`/`Summary`/`Diagnostics` render the outcome for logs, machine output and the clipboard.

Playback resolves a fully packed actor's sibling skeleton by its own entry name: `TryLoadRagdoll` asks `GameData.DataRelativePath` for the opened animation's data-relative name only when the file itself has no bodies and no loose sibling was found, then `GameData.SkeletonEntryName` for the packed `CharacterAssets` skeleton that name points at, and passes THAT name to `ChooseRagdoll` as the sibling key. The bytes are fetched lazily inside the extract callback by `MainWindow.PackedSkeleton`, which resolves the sibling name again with `GameData.SkeletonEntryName`, reads that entry once with `GameData.TryReadSkeletonEntry` when it still matches the key, and otherwise reads the key as a loose file. Before the fix the sibling key was the animation's own path, so a packed sibling was reported, and its physics report cross-referenced, under the animation's name. The Animations tab opens an archive entry as a `%TEMP%` copy and a mod-loose file by its own path, so neither has a data-relative name on its own; `BGS/app/MainWindow.Index.cs:MainWindow.OpenIndexEntryAt` therefore carries the entry's virtual path as a one-shot hint that `BGS/app/MainWindow.Open.cs:MainWindow.Load` consumes and clears, `BGS/app/MainWindow.Playback.cs:MainWindow.PackedAnimationKey` uses it instead of `DataRelativePath` when it is a safe virtual path, and every other open leaves it null, so a copy or a mod file resolves the same packed sibling the engine would, and `SkeletonBytes`/`TrySkeletonEntry` need an entry name containing `/Animations/` — a path without it is refused rather than searched for across every archive. Regression coverage is `BGS/tools/tests/RagdollSiblingTests.cs:ThePackedSkeletonComesFromTheSkeletonEntryRatherThanTheAnimation`, joined by `ATransientCopyOfAPackedClipResolvesItsPackedSkeletonFromTheVirtualPathHint` for the hint and `BGS/tools/tests/GameDataAssetsTests.cs:ASkeletonEntryOverTheCapIsNotReadButItsNameStillResolves` for the name-only lookup.

UI: `BGS/app/MainWindow.Playback.cs:ShowPhysicsReport` stores a non-Ok sentence in `_physicsReportNote` and colours it by severity. `BGS/app/MainWindow.Playback.cs:ChooseRagdoll` is the sibling-skeleton seam `TryLoadRagdoll` uses: it prefers the sibling's report for every status except `SkeletonOnly` (so the sibling's `ParseError`, `WrongContainer`, `NoBodies` and `MapperMissing` are shown, not only `LegacyPhysics`/`UnsupportedShapes`), adopts the sibling model and names it as the source when it has bodies, and turns a sibling read `IOException`/`UnauthorizedAccessException`/`InvalidDataException` into a `ParseError` whose failing stage is exactly `sibling read` instead of swallowing it. `BGS/app/MainWindow.Open.cs:AppendPhysicsNotes` (the renamed `AppendRagdollSourceNote`) appends each note at most once — both the source note and the physics note are checked against `_summaryBaseText`, so it is idempotent — and it is called again at the end of `LoadPose` so the notes come back after `LoadPoseFromSelection` overwrites the summary. `ClearPhysicsReport` resets the report, its path and the note, disables Copy diagnostics, and is called from `ClearPose` and from a `LoadPose` whose key is not the loaded document, so Copy diagnostics can never describe a different file. "Copy diagnostics" is enabled only once a report exists and disabled when a load measures nothing; with no report it says so, and with no clipboard it says the environment has none instead of throwing. `BGS/app/RigAuthoringWindow.Physics.cs:OpenPhysics(string path, out PhysicsLoadReport report)` returns whether a usable ragdoll was loaded, always stores the report for the path it was asked to open (a `ReadHkx` failure becomes a `ParseError` whose stage is `read`), and keeps the previously loaded ragdoll on a failed open with a status line that says the previous ragdoll kept in the preview is unchanged. `BGS/app/MainWindow.AssistantFiles.cs` refuses the assistant's `ragdoll` file operation with `report.Summary` when no usable ragdoll was loaded.

Coverage: `BGS/tools/tests/PhysicsLoadReportTests.cs` (every status, the unusual-container notice, the binary tagfile, the `container` ParseError, the diagnostics dump, the extractor paths), `BGS/tools/tests/RagdollSiblingTests.cs` (the `ChooseRagdoll` seam: every sibling status except `SkeletonOnly`, the `sibling read` ParseError, adoption and own-ragdoll priority), and `BGS/tools/uismoke/RigAuthoringSmoke.cs` (the rig status line and the assistant refusal) with `BGS/tools/uismoke/RagdollLifecycleSmoke.cs` (Playback note and Copy diagnostics button).

## BGS/app/ModlistScan.SkeletonPhysics.cs — --scan-skeleton-physics (#659)

`ModlistScan.ScanSkeletonPhysics` (a partial `ModlistScan`, dispatched from `BGS/app/Program.cs`) censuses the physics of a folder. Discovery and every read go through `GameData` (#696): `Mo2ScanData.OpenScanData(root)` builds one `GameData` for the folder — the `GameData` `Mo2ScanData.TryOpen` already discovered with the instance's mod roots when the folder is an MO2 instance, otherwise `GameData.Discover(folder)` — and `EnumerateWinners("", GameData.IsSkeletonName, warnPath)` returns the winners in the game's own precedence order. The old code walked `*.ba2` recursively and called `Ba2.Open` itself; its two name rules (`IsSkeletonEntry` for entries, `IsLooseSkeleton` for files: a `.hkx` whose last path component starts with `skeleton`) are exactly `GameData.IsSkeletonName`. The scope is narrower than that walk, deliberately and visibly: `GameData.Discover` indexes the `.ba2` files directly under the Data folder, ordered by `plugins.txt`, plus the enabled mod roots' top-level archives, so an archive nested in a Data subfolder is no longer censused and the order is the game's rather than a directory listing's. Winners are read with `GameData.TryRead(GameAsset, out bytes, out warning)`, which routes a packed winner through the held-open archive cache and a loose winner through its own file. Both enforce the `InputFilePolicy` 512 MB HKX cap, so the census has one read path that refuses an oversized loose row and an oversized packed row alike; a refusal is still an `unreadable` row, now carrying `TryRead`'s own sentence. A loose skeleton that overrides a packed copy is therefore one row — the winner — and the copies it hides are the winner's `Shadowed` list, rendered by `ShadowDetails` as one `shadowed: <virtual path> [<source>]` line in the finding's `details` (JSON only) and counted by the `shadowed` summary key, while `warnPath` hands a failed archive its full path as its own argument so `skippedArchives` counts distinct failed archive paths instead of splitting a message on a `:` that a `D:\...` path also contains. `RecordSkeletonPhysics` turns each winner into a `PhysicsLoadReport` and one row with its status, bodies, shapes, constraints, physics/shape/constraint classes, mapper and material. A skeleton with no ragdoll is a row, not a failure — this is a census, not a pass/fail list.

Under `--json` the output is a `ScanReport` named `scan-skeleton-physics` with summary keys `archives`, `scanned`, `ok`, `skeletonOnly`, `legacyPhysics`, `unsupportedShapes`, `mapperMissing`, `noBodies`, `wrongContainer`, `parseError`, `unreadable`, `skippedArchives` and `shadowed`; each file's finding kind is the report's status slug. Human mode prints one `STATUS LABEL path [source]` line per file plus its detail lines, and its `done` line reports `OK`, preview gaps, skeleton-only files, files with no usable ragdoll and unreadable entries separately.

Exit code 0 when every scanned file is `Ok`, `UnsupportedShapes` or `SkeletonOnly` and nothing was unreadable; 1 only when a file is `LegacyPhysics`, `MapperMissing`, `NoBodies`, `WrongContainer` or `ParseError`, or something could not be read; 2 for a missing or nonexistent directory, through the shared usage path. `SkeletonOnly` is 154 of the 195 vanilla skeletons, so it is reported separately and is never part of the "no usable ragdoll" count. A `.ba2` that cannot be opened reaches the `warn` callback (`BGS/app/ModlistScan.SkeletonPhysics.cs:ScanSkeletonPhysics`) as its own `(archivePath, message)` pair: `skippedArchives` counts each distinct failed path once, the `skip` finding carries the path relative to the scanned folder and the message, and it does not change the exit code. A winner that `GameData.TryRead` cannot read is an `unreadable` finding and does force exit 1.

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

**The sweep reads through `GameData` (#696).** `MapperSweepData` builds one `GameData` for `--data`: the
`GameData` `Mo2ScanData.TryOpen` already discovered with the instance's mod roots when the folder is an
MO2 instance, `GameData.Discover(folder)` otherwise. Candidates come from `EnumerateWinners("",
IsMapperCandidate)` — the same `.hkx` under `CharacterAssets` whose last path component contains
`skeleton`, with `GameData.IsSkeletonName` as the first test — and each winner is read with
`GameData.Read(asset)`, so an archive is opened once through the held-open cache instead of `Ba2.Open` per
archive group, and a candidate a higher-priority copy hides is not read twice. `archives` counts the
archives that held a candidate, winner or shadowed. Re-run on the same vanilla install after the change:
7 archives, 43 file(s), 86 mappers, 43 of 43 byte-identical, 43 of 43 add-then-remove (41 duplicated a
bone pair), 0 unreadable, 0 failures, exit 0 — identical to the pre-refactor sweep.

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

## BGS/src/Hkx/RagdollDocument.cs — ragdoll value write-back (#671)

`RagdollDocument` edits the values inside a file's `hknpRagdollData` ragdolls and writes them back into the
packfile in place. `symrm ragdoll <file> [--set <field>:<index>=<values>] [--set ragdoll=<i>] [--out <file>] [--force]
[--allow-shared-material] [--allow-shared-shape] [--data <folder>]` is its user-facing surface
(`BGS/tools/symrm/RagdollCli.cs:Ragdoll`), the rig physics tab's masses, material friction and joint limits
save through it (`BGS/app/RigAuthoringWindow.Physics.cs:SaveRagdollValues`), and
`BGS/tools/tests/RagdollDocumentTests.cs` (25 tests) with `BGS/tools/tests/RagdollShapeTests.cs` (`13`
tests), `BGS/tools/tests/RagdollCapsuleResizeTests.cs` (`11` tests) and `BGS/tools/tests/RagdollCliTests.cs` (`10` tests)
covers it. The model and the class-table offset helpers live in `BGS/src/Hkx/RagdollModel.cs`, the
packfile reader in `BGS/src/Hkx/RagdollReader.cs` and the value writer in `BGS/src/Hkx/RagdollWriteback.cs`.

**Layout provenance.** The offsets below come from the Fallout4.exe 1.10.163 class dumps
`hkclass-field-layouts-1.10.163.part-001.txt` and `...part-002.txt`
(`E:\RE-work\F4RR-co\ResearchEvidence\subsystems\havok`). Every one was compared field by field with
`BGS/src/Hkx/HavokClassLayouts.json` and `BGS/src/Hkx/HavokClassTypes.json` before any code was written:
all three agree on every member, size and type, with **no disagreements**. Honouring the "fail closed on
disagreement" rule, the shipped JSON class table is the only source of offsets at run time —
`RagdollLayout.Field`/`Stride`/`AtomOffset` read it, exactly as `HavokPhysicsExtractor.MemberReader` and
`SkeletonMapperWriteback.Offsets` do — and the numbers here are provenance for this note, never literals.

An `hkArray<T>` in a 64-bit packfile is a data pointer at the member offset with the `int32` count at
`+8` and the capacity/flags word at `+12`, so `hknpPhysicsSystemData` (120 B) holds `materials` @16,
`motionProperties` @32, `motionCinfos` @48, `bodyCinfos` @64 and `constraintCinfos` @80
(`hknpRagdollData` (144 B) adds `skeleton` @120 and `boneToBodyMap` @128). Element sizes are
`hknpMaterial` 80, `hknpMotionProperties` 64, `hknpMotionCinfo` 112, `hknpBodyCinfo` 96 and
`hknpConstraintCinfo` 24. The members that matter: `hknpMotionCinfo.inverseMass` real @4,
`inverseInertiaLocal` vector4 @32, `centerOfMassWorld` vector4 @48; `hknpBodyCinfo.shape` ptr @0,
`motionId` u32 @12, `qualityId` u8 @16, `materialId` u16 @18, `collisionFilterInfo` u32 @20;
`hknpMaterial.dynamicFriction` half @18, `staticFriction` half @20, `restitution` half @22 and
`isShared` bool @72; `hknpShape.convexRadius` real @20 and `properties` pointer @32;
`hknpConvexShape.vertices` relarray @48; `hknpConvexPolytopeShape.planes` relarray @64, `faces` relarray @68
and `indices` relarray @72 (`hknpConvexPolytopeShapeFace` 4 B, `firstIndex` u16 @0, `numIndices` u8 @2,
`minHalfAngle` u8 @3); `hknpCapsuleShape.a` vector4 @80 and `b` @96.
A ragdoll constraint's atoms are an embedded struct at @32: `angFriction.maxFrictionTorque` at atoms+256+8,
`twistLimit` min/max at atoms+272+8 and +12, `coneLimit` at atoms+304+8 and +12, `planesLimit` at
atoms+336+8 and +12. A limited hinge's atoms are at @32 too, with `angFriction.maxFrictionTorque` at
atoms+200+8 and `angLimit` min/max at atoms+216+4 and +8. One naming detail rather than a disagreement:
neither table has an `hkpPlanesLimitConstraintAtom` class, because `hkpRagdollConstraintDataAtoms.planesLimit`
is typed `hkpConeLimitConstraintAtom`; its min/max are still the `ConeLimit` atom's, so the code asks the
class table for the member's own ctype instead of assuming a class name.

**A capsule is not only `a`, `b` and a radius, and now it is derived in place.** Every one of the `933` capsule
shapes on the vanilla install carries a non-empty polytope: `7` vertices, `7` planes, `5` faces and `23` indices
each, with `0` empty arrays anywhere. The measurement behind the derivation is
`E:\GardenEngine\docs-drafts\bgs-roadmap\CAPSULE-POLYTOPE-SPEC.md`, taken over the vanilla install (Fallout 4
1.10.163 `Data`, read through `GameData`): `41` files carry an `hknpRagdollData` holding those `933` capsules,
`882` of them point `hknpShape.properties` at an `hkRefCountedProperties` container whose single entry is an
`hknpShapeMassProperties` object, and `51` point it at nothing. `BGS/src/Hkx/RagdollCapsule.cs` implements the
rules below; every one of them was measured rather than guessed, and each carries the tolerance it was measured
with.

**The polytope rules.** An array member is a relative-array header: one `uint32` whose low 16 bits are the count
plus one and whose high 16 bits are the payload's byte offset from the member's own address, which is what
`PackfileObjects.RelArrayAt` already read. A vertex is four floats: `xyz` and a `w` lane whose bits are
`0x3F000000` plus its own index (`933/933`), so `w` is a tag, not geometry. Each vertex belongs to the endpoint
it is nearest to (`3` to `a`, `4` to `b`, `933/933`) and sits `convexRadius / 99 * sqrt(3)` from it (worst
relative error `4.3e-4`). A resize that keeps the direction moves every vertex as
`v' = newEnd + (s'/s)(v - oldEnd)`, with `s = convexRadius / 99`, `oldEnd` and `newEnd` the endpoint that vertex
belongs to, and its `w` bits copied from the stored vertex instead of recomputed, which the `w`-tag check above has already proven equal. Planes `0-5` keep their normal bits and take `d = -max(n . v')` over the
transformed vertices; that is the convention vanilla stores (`933/933` within `5.2e-5`, and the opposite sign is
off by up to `637`). Plane `6` is the sentinel `(0, 0, 0, -3.40282e38)` and is never touched. The `20` face
bytes and the `23` index bytes are constants (the fourth face byte is `4` in `882` capsules and `128` in the
`51` with no properties) and are never written. Measured on `14,089` same-direction ordered pairs at
`|cross(u, u')| <= 1e-6` (`14,025` of them at `<= 1e-7`): the transform reproduces the other capsule's stored
vertices within `1.96e-4` absolute, which is `1.30e-3` of `radius/99`, and its planes within `3.67e-4`. A
direction change beyond `1e-6` rad is refused, because the frame the arrays were built in would no longer be the
one they encode, and so is any stored payload that does not match the rules above - the four counts, the `w`
tags, the `3`/`4` split, each vertex's distance to its endpoint, the unit normals, `d`, the sentinel, and the
face and index payloads - so a file whose derived data this build does not recognise is refused rather than
reinterpreted.

**The mass properties rules.** `hknpShapeMassProperties` is `48` bytes with `hkCompressedMassProperties` at
`+16`: `centerOfMass` `int16 x4` at `+0`, `inertia` `int16 x4` at `+8`, the undecoded `majorAxisSpace`
`int16 x4` at `+16`, `mass` at `+24` and `volume` at `+28`. `volume = pi r^2 L + 4/3 pi r^3` reproduces all
`882` stored volumes within `2.7e-7` relative. `mass` is a user-set value (the corpus stores `1, 2, 3, 4, 5, 8,
10, 15, 20, 25, 97.2, 108` and others) and is **kept**; the inertia is re-encoded from the new geometry and that
kept mass. `inertia_i = clamp(round(K * (mass/V) * I_unit_i))` with `I_unit` a cylinder plus two hemispheres
(`axial = 1/2 mc r^2 + 2*(2/5) mh r^2`, `transverse = mc(3r^2+L^2)/12 + 2*(83/320 mh r^2 + mh (L/2 + 3r/8)^2)`,
`mc = pi r^2 L`, `mh = 2/3 pi r^3`), `K = 3*2^n`, `n = floor(log2(32767/(3*Imax)))` settled so that
`3*2^n*Imax <= 32767 < 3*2^(n+1)*Imax` with `Imax` the largest scaled `I_unit`, the axial lane in the
world-dominant-axis slot of the **old** direction `u = norm(bOld - aOld)`, taken once by the derivation and by
the check below so that a near-tie cannot flip it between them, and the transverse value in the other two, and the `w` lane at
`+14` equal to `14080 - 128n`. That reproduces all `882` stored inertias within `1` LSB and all `882` `w` words
exactly. `centerOfMass_i = clamp(round(mid_i * 2^p))` with `mid = (a + b)/2`, `p = floor(log2(32767/max|mid|))`
settled the same way, and the `w` lane at `+6` equal to `14208 - 128p`: `877` of `882` within `1` LSB, and the
`9` exceptions are near-zero centres whose stored lane is off by at most `2.9e-7` in absolute terms - `5` of
them have `mid == (0,0,0)` exactly and store `w = 6528`, which is `p = 60` and what the encoder writes for a
zero centre. A stored payload is therefore checked two ways: each exponent word must be the one its geometry
gives (a word that does not name an exponent is refused), and each lane must be within `1` LSB of the lane that
geometry gives, or within `1e-6` of the reconstructed value for a near-zero centre. Re-measured on the same
corpus after the gates were tightened from `4` and `2` LSB: of the `2646` centre lanes `216` are covered by the
`1e-6` absolute branch (the near-zero and zero centres above), every remaining centre lane is within `0` LSB, and
all `2646` inertia lanes are within `1` LSB, so no vanilla capsule depended on the looser gates. The rounding
mode was checked the same way: `229` lanes (`93` centre, `136` inertia) carry an unrounded value within `0.02` of
an `x.5` boundary, `223` of them are reproduced by both half-away-from-zero and half-to-even and `6` (near-zero
centres) by neither, so no lane tells the two modes apart and the encoder keeps
`Math.Round(value, MidpointRounding.AwayFromZero)`.

**A resize also has to know who else reads the same object.** Three counts gate `SetCapsule`: the bodies that name
the shape, the capsule shapes that name the same `hkRefCountedProperties` container, and the capsule shapes whose
chain reaches the same `hknpShapeMassProperties` object. `RagdollReader.ReadShape` records both object indices
through `RagdollCapsule.MassPropertiesSiteAt` and `RagdollDocument` counts them per file; every vanilla capsule is
named by exactly `1` body, no container and no mass-properties object is shared, so the counts are `1` across the
corpus and the refusal path is exercised by tests instead. A resize is refused when any count is above `1` unless
`--allow-shared-shape` (API `allowSharedShape`) is passed, while `--allow-shared-material` (API
`allowSharedMaterial`) covers only material friction and restitution. Passing the shape flag only clears that
gate: the writer still re-reads every shape and refuses a file whose shared mass properties no longer describe
each shape that names them, which is the honest answer when two shapes of different geometry share one container.

**`majorAxisSpace` is not decoded, and it is kept.** It is a function of the direction, not of the length or the
radius: all `14,025` parallel same-direction pairs at `<= 1e-7` rad store byte-identical `majorAxisSpace` while
`L` differs in `14,007` of them and `convexRadius` in `14,008`, and it is coarse - the `882` capsules carry
`333` distinct values and the largest group (`322` capsules) spans directions up to `1.0` rad. At `<= 1e-6` rad
only `32` of `14,626` pairs differ, each by one ULP in a saturated lane. A same-direction resize therefore keeps
the stored `majorAxisSpace` unchanged, and a direction change, which would leave it stale, is refused instead.
, and what is refused.** Body mass, centre of mass, material friction and restitution, and
a constraint's twist, plane and cone limits and angular friction. The mass is stored as the file's inverse
mass and the body's `inverseInertiaLocal` is **scaled by the mass ratio** `newInverseMass / oldInverseMass` —
that is `oldMass / newMass` — because a rigid body's inertia tensor is proportional to its mass while its
shape is unchanged; a body whose stored inverse mass is zero or non-finite is refused rather than given an
inertia that no measurement supports. `SetMaterialFriction` takes the dynamic and static values separately
(`symrm ragdoll --set material:<i>=<dynamic>,<static>`; one value sets both) and writes only the lane that
actually changed, because the model stores the half-rounded value each lane already holds and the writer
compares field by field; `--set restitution:<i>=<value>` is its own field rather than a second material
value. A material that more than one body in the same ragdoll names, or that the file itself flags `isShared`,
is refused unless `allowSharedMaterial` is passed, because the change would reach every body that names it.
`SetConstraintLimits` writes **only the twist atom** of a ragdoll constraint (`twistLimit`) or the angular
limit of a limited hinge (`angLimit`); plane limits are written only by `SetConstraintPlaneLimits` and
`--set plane:<i>=<min,max>`, and the UI text says the same.

**Cone and plane atoms are written only in the modes the game uses.** All `666` `coneLimit` atoms on the
vanilla install carry `angleMeasurementMode` `0` and all `666` `planesLimit` atoms (typed
`hkpConeLimitConstraintAtom` as well) carry mode `1`, with `0` of either using anything else. A cone edit is
refused unless the stored mode is `0`, and a plane edit unless it is `1`, in the document and again in the
writer, naming the mode it found; `symrm ragdoll` prints both modes for every constraint it lists. The cone's
`minAngle` is left exactly as the file has it: all `666` vanilla cones store `-100` there, a sentinel rather
than an angle, so the editor writes only `maxAngle`. The plane's minimum and maximum are real values in every
vanilla file and are not mirrored around zero (the distinct `minAngle` values reach `-1.5708` and the distinct
`maxAngle` values reach `1.5708`), so both are written when that limit is edited.

Everything else is refused by construction or by the writer: a capsule whose direction changes, a capsule whose stored polytope or mass properties do not match the rules above, and a capsule shape that more than one body names, or that shares its properties container or mass properties
with another capsule shape, without `allowSharedShape`; nothing adds or removes a body, shape or constraint — the writer separately refuses a model whose array count differs from the
file's, with "rows are never added or removed" — and no method writes `collisionFilterInfo`, `qualityId`,
`motionId`, `reservedBodyId`, body flags, position, orientation, `localFrame`, `motionPropertiesId`,
linear or angular velocity, a constraint's frames or its motors. Limits must be finite and ordered within
`-pi` and `pi`, a cone within `0` and `pi`, a mass and a radius positive and finite, and an index outside
the model is refused with the count it was checked against.

**Write-back, one path.** `RagdollWriteback.Write` locates the five arrays through the same
`PackfileObjects.ArrayAt` the reader used, runs `SkeletonMapperWriteback`'s shared-storage test over each
run, patches the exact bytes of every field that differs, records the `(start, length)` range it owns, and
then refuses the result unless `RequireConfined` finds every changed byte inside a recorded range,
`RequireOtherInstancesIntact` finds every object instance outside those ranges byte-identical, and
`PackfileImage.Rebuild` reproduces the file. The fixup order is never rewritten — there is no
`FixupOrder.Reorder` call and nothing is appended or relocated, because a value edit does not move
anything — and the produced bytes are re-read through the reader and must match the model, field for
field, or nothing is returned at all. `WriteShape` re-derives a capsule's payload from the file's own original one instead of trusting the model, and refuses unless the model's vertices, planes and mass properties are byte for byte what that derivation produces; it then writes the vertex payload, the plane payload, `convexRadius`, `a`, `b` and the three mass-property fields that changed (`centerOfMass`, `inertia`, `volume`), owns one range per field, and never writes `mass` or `majorAxisSpace`. The read-back verify re-derives the expected payload from the original and compares it with what the produced file actually holds, and `RagdollShape.SameAs` compares the payload bytes too, so a stale or doctored array cannot pass

`RagdollDocument.ForceRebuild` is the honest test of that path: it hands the writer a no-op change for
every ragdoll and every shape, so storage location, the field pass, the confinement checks, the rebuild and
the read-back verify all run while nothing is meant to change, and the result must equal the input byte for
byte. `Write` refuses `force` combined with real edits. An unedited `Serialize` returns the source clone,
which is why the forced rebuild, not that shortcut, is what the corpus figure measures. Saving goes through
`FileSafety.ReplaceChecked` with the source stamp, like the other documents, so the original is kept as a
`.bak` and an external change is refused.

**One command, one document.** `symrm ragdoll` selects the ragdoll with `--set ragdoll=<i>`. When the file
holds exactly one ragdoll the selector is optional; with more than one, an edit that names a row refuses until
it is given, with the count in the message. Every edit goes into one `RagdollDocument` and the produced bytes
are read back **once**: `VerifyRagdollEdits` builds a single document and compares every ragdoll's motions,
materials and constraints and every shape, so a file with several ragdolls is checked in full rather than only
at index `0`. `--out` equal to the file being read is still refused without `--force`; with `--force` it goes
through `FileSafety.ReplaceChecked` with the source stamp, exactly like the UI, so the version that was read
is kept as `<name>.bak` and an external change is refused rather than overwritten. `--data` sweeps a whole
Data folder the same way and uses the first ragdoll that actually holds the row it edits, reporting skipped
rows rather than assuming index `0`.

**The window's own paths.** The physics tab writes masses, material friction and joint limits through the
document's bulk methods `SetBodyMasses` and `SetConstraintLimits(int, IReadOnlyList<RagdollLimitEdit>)`, which
validate every row before changing anything and report the rows the ragdoll does not hold as `Skipped` instead
of failing halfway; the window uses the ragdoll its own selector shows, adds a row to the simulation only when
it was applied, and says how many were skipped. Material friction is its own row with dynamic and static
fields and an `Allow shared material` box, echoing the half-rounded values stored. `Revert ragdoll values`
reloads the document's own source bytes (`RagdollDocument.SourceBytes`), clears the simulation masses and
preview joints and rebuilds the physics preview from those bytes, and its message says the bytes it went back
to rather than a file on disk it may never have written. Opening another skeleton or skin handles the
skeleton, the skin and the ragdoll independently: only the document being replaced is saved or dropped, and a
dirty ragdoll is reverted rather than left half-edited by a dialog that only mentioned the other document.

**Measured.** On the vanilla install (`D:\SteamLibrary\steamapps\common\Fallout 4\Data`, read-only), through
`GameData` (#696): `7` archives, `41` files carry an `hknpRagdollData` — all `41` with at least one body,
`950` bodies, `897` limit-bearing constraints and `933` capsule shapes between them — and `154` more
skeleton-named files carry none, which with the census's `195` gives the same population
`BGS/docs/research/fo4-skeleton-physics-census.md` reports. **`41 of 41` unedited forced rebuilds were
byte-identical**, `41 of 41` mass edits and `40 of 41` limit edits read back as edited (one rig carries no
limit-bearing constraint at all, so there was nothing to edit), `0` unreadable and `0` failures, exit `0`.
The capsule and cone figures above come from the same sweep: `933` capsule shapes with `7`/`7`/`5`/`23`
vertices, planes, faces and indices and `6531` checked vertices, `666` cone atoms in mode `0` and `666` plane
atoms in mode `1`, `666` cones storing `minAngle` `-100`, and `897` limit-bearing constraints (`666` ragdoll
and `231` limited hinge) between the `41` files.

**The capsule round trip.** `symrm ragdoll --data` also scales every capsule's length by `1.1` and back on every
file it reads: of the `933` capsules, `931` resized and came back within `2e-3` of `radius/99` (worst
`1.52e-3`; `10` of them byte-identical and `0` files byte-identical, because the endpoints themselves round at
single precision and the derived vertices are recomputed from them), `2` were refused because a near-zero-length
capsule's direction cannot be held within `1e-6` rad once its own endpoint is scaled, and `0` capsule shapes were
found that more than one body names, so `allowShared` never had to be passed.

**Two lanes this build does not touch.** A mass edit writes the first three lanes of `inverseInertiaLocal` and
leaves the fourth exactly as the file has it: on the vanilla install that lane holds `1` in `939` of the `943`
motion rows, `0.02262` in three and `0.00759` in one, so it is not a lane this build interprets or rewrites.
`centerOfMassWorld` is written the same way, three lanes only, and its fourth lane takes `789` distinct values
across the `943` rows (a first pass saw extremes of `-140.86` and `138.52`), which is padding rather than
data. Rescaling a mass also leaves `hknpMotionProperties` alone, and the assumption behind that is measured:
the shipped class has no mass or mass-factor member at all — its members are `isExclusive`, `flags`,
`gravityFactor`, `timeFactor`, `maxLinearSpeed`, `maxAngularSpeed`, `linearDamping`, `angularDamping`, the two
solver-stabilization speeds, `maxDistSqrd`, `maxRotSqrd`, `invBlockSize`, the pathing thresholds and the
deactivation fields — so there is nothing there for a mass change to rescale, and the row is never written.
Before this was measured the only evidence was the committed human fixture
(`BGS/tools/tests/fixtures/vanilla/Meshes/Actors/Character/CharacterAssets/skeleton.hkx`: `1` ragdoll,
`18` bodies, `18` motions, `18` materials, `17` constraints of which `9` are `hkpRagdollConstraintData` and
`8` `hkpLimitedHingeConstraintData`, and `19` capsule shapes), which the forced rebuild also reproduces
byte for byte.

`BGS/tools/tests/RagdollDocumentTests.cs` pins the no-op identity on every committed ragdoll fixture, each
field's edit and its read-back, the byte confinement of each edit (the differing `__data__` offsets must be
a subset of the field's own range), that every other object instance survives an edit unchanged, that the
local, global and virtual fixup tables are untouched, the half-precision rounding of a material value, the
mass-ratio inertia scaling, and every refusal above — including a doctored model whose row count differs
and a forced rebuild asked to carry an edit. The rig smoke
(`BGS/tools/uismoke/RigAuthoringSmoke.cs`) saves a mass and the joint limits through the window's own
controls, checks the file is untouched until Save, that the `.bak` holds the original, that the values read
back, and that a refused mass leaves the file alone. It also checks that saving limits leaves the plane limit
alone, that the material row stores and echoes the half-rounded values without touching the file, and that
`Revert ragdoll values` clears the edits, releases the simulation and rebuilds the preview from the reloaded
bytes. `BGS/tools/tests/RagdollShapeTests.cs` adds the capsule and mode coverage: every capsule in every
committed fixture derives a new polytope and reads it back, a doctored shape or constraint cannot
be written by `RagdollWriteback` at all (which is the test that fails if a derived array could be left stale),
a changed derived count is a different shape, the fixture uses the verified cone and plane modes, an
unverified mode refuses, and the bulk edits validate everything before they change anything and report skipped
rows. `BGS/tools/tests/RagdollCliTests.cs` drives the command line: no selector for a single ragdoll, an
out-of-range selector refused, the selector choosing the ragdoll, the material echo, `restitution:`, a capsule edit derived and read back, a shared capsule shape refused, and `--out` on the source with and without `--force`.

## BGS/src/Archive/GameData.cs — shared read-by-entry and precedence API (#696)

The audit behind #696 found BGS features opening `.ba2` files and deciding loose-versus-archive precedence themselves, because `GameData` had no public API for it. The rule now lives in one place: `BGS/src/Archive/GameData.Assets.cs` (a partial `GameData`) exposes `ReadEntry`, `TryReadEntry`, `Read(GameAsset)`, `TryRead(GameAsset, out bytes, out warning)`, `EnumerateWinners`, `EnumerateLoose`, `FromParts`, `DataRelativePath`, `SkeletonBytes`, `SkeletonEntryName`, `TrySkeletonEntry`, `ModlistPath`, `IsSkeletonName`, `IsCharacterAssetPath` and `IsSkeletonPath`, plus the `GameAsset` record, `GameLooseFile` and the `GameAssetOrigin` enum. `GameData` is the game's data tree as the engine sees it: the `Data` folder's loose files plus the `.ba2` archives directly under it — `Discover` indexes top-level archives only, ordered by `plugins.txt` through `OrderByPlugins`, and never walks a subfolder for archives — consulted in load order, with the enabled Mod Organizer 2 roots layered over it (`DiscoverModded`, `Mo2ScanData`). Loose files win over archives, and within the archives the later-loaded archive wins. A project chain declares animation paths relative to its root, which is normally a folder somewhere inside the data tree (for Fallout 4's vanilla character: `Data\Meshes\Actors\Character`); the same path inside an archive appears with that data-relative prefix, so an animation that is not loose can still exist inside e.g. `Fallout4 - Animations.ba2`. `FromParts(dataFolder, archivePaths, modRootsLowToHigh)` builds that same view from strings a caller already resolved (`AnimationIndexRequest.OpenGameData`), de-duplicating both lists with `Path.GetFullPath`. `Mo2ScanData.OpenScanData(folder)` is the one place that answers "is this folder an MO2 instance or a plain Data folder": the census, the mapper sweep and `--scan-index`'s plain-folder fallback all use it, so none of them carries its own `TryOpen`-or-`Discover` rule. Everything there goes through `Ba2` (Radicle.Interop) and `GameData`'s held-open archive cache; no BA2 byte parsing was added. Follow-up work moves the features onto this surface. `GameData` is single-threaded: it carries no synchronisation, and the held-open archives, the mod-loose index and the per-archive lookup are only safe from one thread at a time.

- **Read-by-entry.** `ReadEntry(archivePath, entryName)` returns `byte[]?`; `TryReadEntry(...)` is the same read with `out byte[] bytes, out string? warning`. Both use `GameData.Open`, so an archive's index is parsed once per run and reused (the test counts `Ba2.OpenCountForTest`: two reads, one open). Matching prefers an EXACT name first — the request flattened with `'\\'` → `'/'` and compared to the archive's own `Ba2.Entry.Name` with `OrdinalIgnoreCase` — and only falls back to the `Normalize` key (extension dropped, `'.'`/`'..'` collapsed) when no entry has that exact name. That is what lets `skeleton.hkx` and `skeleton.hkt` in one archive each read their own bytes: the old normalize-only match collapsed both onto one key and always returned whichever came first in index order. `Read(GameAsset)` passes the asset's exact `EntryName` (the name `EnumerateWinners` took from the archive), so a winner reads its own entry. A missing entry is `false` with a null warning. A DX10 archive is `false` with a null warning, preserving the wording contract `BGS/src/Archive/Ba2.cs:Ba2.Open` documents. A skippable failure (missing, unreadable, truncated) is `false` with `"<archive path>: <first line>"` — the archive's full path, not only its file name, so a caller can key a failure by the archive it happened in. Native-load failures still propagate (`IsSkippableArchiveFailure`).
- **Per-archive lookup.** Each archive gets one `ArchiveIndex` built on first use: the exact `Ba2.Entry.Name` → entry index, and the `Normalize` key → the index of the FIRST entry with that key. An archive that cannot be indexed is remembered together with its warning (null for DX10), so a texture archive is opened once per run instead of on every lookup. It is built once with the held-open archive and cleared in `Dispose`, so `ReadEntry`/`Read` are O(1) rather than a scan of every entry, and `FindEntry` (and therefore `ReadAnimation`) resolves a declared path by one dictionary hit instead of walking the entry list. `FindEntry`'s suffix fallback picks the SMALLEST matching index, which is the entry the old first-match-in-archive-order scan returned. The index replaces the old `GameData.Names` normalized-name set, which `Subfolders` still reads through.
- **Precedence.** `EnumerateWinners(virtualPrefix, nameFilter, warnPath)` walks, in order: mod roots highest to lowest (`ModRoots` reversed), the Data folder's loose files, then `ArchivePaths` in its current order. That archive order is unchanged and stays `OrderByPlugins`' business (#695). Candidates are filtered first: `virtualPrefix` matches the `'/'`-normalized *full* path with its extension, so the directory `Meshes/Actors/Turret` never claims `Meshes/Actors/Turret.hkx`, and `nameFilter` receives the `VirtualPath` as spelled by the source. Precedence is then keyed by `GameData.Normalize`, which is the cross-source collapse: the first candidate for a key wins, and every later candidate for that key in a lower-precedence source becomes one entry of that winner's `Shadowed` list, in the same source order. Distinct files *within one source* are never dropped — a second distinct file with the same key inside the winning source is returned as its own winner (the `skeleton.hkx`/`skeleton.hkt` case on a case-sensitive filesystem), and every winner in that same-source group receives the same lower-priority shadows, so no caller loses the loser list by picking the other twin. A caller counting winners for one key can therefore see more than one top-level asset. `warnPath` is an `Action<string archivePath, string message>?`, so the failing archive's path arrives as its own argument; the older `Action<string>? warn` signature survives as an overload on the same name that folds the two into `"<archive path>: <reason>"`, and `List<string>.Add` still binds to it unambiguously. `MergeWinners(orderedSources, token, progress)` is the same rule for a caller that already holds per-source lists: it takes `(source, entries)` pairs in the caller's own order and returns the same winners, which is how the animation index merges its cached lists without re-walking a loose root or reopening an archive, and `WinnerSourceOrder()` publishes the order (mod roots highest to lowest, then the Data folder, then `ArchivePaths`) so a caller can line its own lists up with it. `EnumerateWinners` and `MergeWinners` share one local `Offer`, so the precedence rule exists once.
- **Archive warnings.** `warnPath` (optional) is fed from the same `TryArchiveEntries` handling the read API uses: any archive that raises a skippable failure contributes its full path and its reason as two arguments and is skipped, instead of disappearing from the result silently. The path stays separate from the reason because a caller counting "how many archives could not be read" needs a distinct path, not a string it would have to split on a `:` that also appears in `D:\...`. A DX10 texture archive returns `false` with a null warning, so it stays silent; a native-load failure still propagates rather than being downgraded to a warning.
- **Loose walking.** Each root is walked by hand rather than with `SearchOption.AllDirectories`, which follows links. Reparse points (symlinks, junctions) are skipped for files and directories below the root, and every candidate must satisfy `IsInsideRoot(root, full)`. A root that is ITSELF a junction is deliberately followed: only links below it are refused, so a Data folder reached through a junction still enumerates. `IsInsideRoot` is the one containment rule (it replaced the old private `IsUnder`), so a path that escapes through `..` or through a link target is refused rather than indexed. The walk resolves the root once and checks containment once per file, yielding already-validated full paths, so `OfferLoose` does not repeat `Path.GetFullPath`/`IsInsideRoot`. The walk covers each root's whole subtree — the prefix prunes candidates, not directories — so callers in a hot loop should cache the result. A loose file whose size cannot be read gets `Size == -1`; an archive asset's `Size` is `Ba2.Entry.Unpacked`, the decompressed size.
- **Lifetime.** `GameData` owns the held-open archives, so a caller that keeps entries for a later read must keep the `GameData` instance alive. `Dispose` closes every opened archive and clears the archive index caches (successes and failures) and the mod-loose cache; it is idempotent. Any archive call after `Dispose` throws `ObjectDisposedException` — `ReadEntry`, `TryReadEntry`, `Read` of an archive asset, `EnumerateWinners`, and through them `FindEntry`/`ReadAnimation` once an archive has to be opened or indexed — instead of silently reopening a fresh native handle.
- **`Read(GameAsset)` and `TryRead(GameAsset, out bytes, out warning)`** dispatch to the loose file or to `ReadEntry`; `Read` is the try-shape with the warning dropped and returns null whenever `TryRead` refuses. Both enforce the HKX input cap that `InputFilePolicy` owns, which is the cap that was lost when the census and the mapper sweep moved onto this API: the asset's declared `Size` is checked first (skipped when it is negative), a loose file's own length is checked from its `FileInfo` before the bytes are read, and an archive entry's declared unpacked size is checked before and after the entry is read, and `TryReadAt` re-checks the LIVE `Ba2.Entry`'s unpacked size before it decompresses, so a stale or tampered cached entry index cannot force a large allocation, so a 512 MB+ entry is refused instead of allocated. The refusal is `InputFilePolicy`'s own sentence, naming the asset, the HKX kind and the 512 MiB maximum. `ReadLimitBytes` is the same check with an injectable limit — it defaults to `InputFilePolicy.MaximumHkxBytes` and only a test ever sets it. A loose read that fails on IO or permission is `false` with `"<virtual path>: <IO message>"`, so a census or a sweep reports why a file could not be read instead of a generic sentence. `SkeletonEntryName(animationEntryName)` is a name-only lookup that decompresses nothing, `TryReadSkeletonEntry(entryName)` is the capped read by name, and `TrySkeletonEntry` is the two of them in one call. The name lookup needs an entry name with an `/Animations/` segment, because that segment is what locates the sibling `CharacterAssets/` folder, and that is what lets a packed sibling skeleton be keyed by its own name instead of by the animation's, and to be decompressed exactly once.
- **`ModlistPath(modsFolder, profile)`** is the single MO2 modlist rule extracted from `DiscoverModded`, which now calls it: `<modsFolder>/profiles/<profile>/modlist.txt`, or `<modsFolder>/modlist.txt` when no profile is named, and null when that file does not exist. `BGS/src/Archive/Mo2ScanData.cs:Mo2ScanData.TryProfile` calls it for the selected profile, the instance root and the Default profile, and `BGS/src/Archive/AnimationIndex.cs` calls it for its modlist; selection order, error text and profile enumeration stay in `TryProfile`, because each failure is reported differently.
- **The skeleton predicates are split.** `IsSkeletonName(fileName)` is the name-only rule: `.hkx` (OrdinalIgnoreCase) whose last path component starts with `skeleton` (`skeleton_physics.hkx` yes, `skeletons/rifle.hkx` no). `IsCharacterAssetPath(virtualPath)` is the folder rule alone: any `CharacterAssets` component except the last. `IsSkeletonPath(virtualPath)` is the documented combination — the `.hkx` gate, then `IsSkeletonName` or `IsCharacterAssetPath` — and keeps its previous behaviour exactly. A caller that only needs "is this file a skeleton by name" should use `IsSkeletonName`: that is the R0 archive scan and the mapper sweep, which sweep entries one file name at a time and must not treat every `.hkx` under a `CharacterAssets` folder as a rig. A caller that wants the folder convention keeps `IsCharacterAssetPath`/`IsSkeletonPath`; `GameData.SkeletonBytes` still asks `IsSkeletonPath` for the name test after it has already required the `<prefix>/CharacterAssets/` entry prefix, and `BGS/app/MainWindow.Animation.cs:SiblingSkeletonPath` still scans a `CharacterAssets` folder for the first `.hkx` that parses. Accepting any `.hkx` under `CharacterAssets` is intentional, because that is what `SkeletonBytes` already did. `BGS/app/ModlistScan.cs:ModlistScan.IsBehaviourPath` also matches `/characterassets/`, but for behaviour files, so it stays separate.

`SkeletonBytes`, `SkeletonEntryName`, `TrySkeletonEntry` and the archive branch of `ReadAnimation` were refactored onto these members without a behaviour change, and repeated `//`-free prose that used to sit in a class summary on `GameData` now lives in this section. Regression coverage is `BGS/tools/tests/GameDataAssetsTests.cs`: read hits/misses, exact-name reads when two extensions share a normalize key, DX10 and skippable archives, the truncated-archive warning, the failed archive's path arriving separately from its reason, the HKX-cap refusals (a faked oversized asset, a sparse oversized loose file, an injectable cap on a real archive entry, a `ReadEntry` refused from the live entry size before it decompresses, a skeleton entry whose name still resolves while its bytes over the cap are refused, and the IO detail for a loose file that vanished), precedence across mod/Data/archive sources, archive order, shadow lists, within-source pairs and their shared shadows, reparse-point skip, outside-root refusal, `ModlistPath` parity with `DiscoverModded`, and the `IsSkeletonName`/`IsCharacterAssetPath`/`IsSkeletonPath` cases.

`BGS/tools/tests/TestArchives.cs:WriteGeneralBa2` is a hand-rolled copy of the GNRL record layout — the 16-byte chunk header with the `0x00100100` size word and the `0xBAADF00D` sentinel, entry data before the length-prefixed name table — kept in step with `Radicle/src/archive/ba2.cpp:write_ba2_general` by hand; Radicle #694 exports the real writer, and this fixture should be pointed at it instead of carrying its own copy. `BGS/tools/tests/GameDataAssetsTests.cs` carries the same kind of hand-rolled damage fixtures next to it: it writes a BTDX header by hand for the DX10 case and truncates a just-written archive in half for the damaged-archive cases, so those cases keep their exact bytes whatever the shared writer does.

Radicle #692/#693 should replace this loose walk and precedence rule with the native resolver's own enumeration; until then `EnumerateWinners` and the `MergeWinners` it calls are the only implementation of the rule in BGS.

## `BGS/src/Archive/AnimationIndex.cs` — animation index (#664)

`BGS/src/Archive/AnimationIndex.cs:AnimationIndex.Build` answers "every animation, behaviour graph and project file this load order can reach" once, and `AnimationIndexEntry` is the row: `VirtualPath` (data-relative, `/`-separated), `Source` (`loose:Data`, `loose:<mod folder>`, or the archive's file name), `Kind`, `Project`, `Race`, `Character`, `Folder`, `Size` and `Location` (the absolute loose file, or the archive the entry is packed in). `AnimationIndexRequest` carries the load order: `DataFolder`, `ModRootsHighToLow`, `ArchivePaths` and the modlist path and profile. `ForGameData` takes those from the same `GameData` the rest of BGS uses, so archive order, mod precedence and the MO2 overwrite folder are exactly the engine's view; `ForDataFolder` and `ForModsFolder` remain the folder-only factories. The GUI and `--scan-index` share one discovery entry point, `AnimationIndexRequest.FromLoadOrder(dataFolder, mo2Folder)`: an MO2 instance root resolves through `Mo2ScanData.TryOpen`, the same adapter the other scans use, and a folder that is not an instance falls back to `ForModsFolder` or to `Mo2ScanData.OpenScanData`. `AnimationIndexRequest.OpenGameData` rebuilds the same view from the request's own strings (`GameData.FromParts`) for the build and for opening an indexed entry, so the index never constructs a load order of its own. `GameData` is disposed inside those factories because the request only holds strings, which is what lets the build run on a background thread.

Classification is path-only, so the index never decodes a file: `AnimationIndex.Classify` splits the virtual path, finds the first `Actors` segment and the first role folder (`Behaviors`, `Animations`, `Characters`, `CharacterAssets`), and reads kind, project, race and character name from those. A `.hkx` under `Actors` with no role folder in its path is the project-file guess (`Meshes/Actors/Character/Character.hkx`), `CharacterAssets` is the skeleton kind including non-skeleton assets such as hair, and a path with no `Actors` at all (a `.hkt` under `Meshes/Animations/...`) still classifies by its role folder alone. Only `.hkx` and `.hkt` names are indexed at all (`IsIndexedName`), so `Meshes/AnimTextData` manifests and textures are never enumerated. `LocationOf` rebuilds the absolute path when an entry is read back out of the cache, so the cached rows and freshly scanned rows are indistinguishable — but the cache file is not trusted input, so `FromCache` re-validates every cached row before it can become a filesystem path. `IsSafeVirtualPath` drops a row whose name is rooted, carries a `:`, has a `..` segment, or is not an indexed Hk name, a loose row must additionally resolve inside its own root (`GameData.IsInsideRoot`), and the count of rows dropped that way is reported in `CacheNote` and on `AnimationIndex.RejectedCacheEntries`.

Precedence is not the index's own rule. `Build` indexes one source at a time — `GameData.EnumerateLoose` for a loose root, `GameData.TryArchiveEntries` for an archive, both under the shared safety rules — and then hands those per-source lists to `GameData.MergeWinners` in the order `GameData.WinnerSourceOrder` publishes: mod roots highest first, then Data loose, then `GameData.ArchivePaths`. Each winner carries `GameAsset.Source` in `GameData`'s own spelling (`archive:<path>` or `loose:<root>`), and the merge keys the per-source row tables on exactly that, so the collapse is per source and two distinct files inside one source (`Foo.hkx` and `Foo.hkt` of one stem, or the same name listed twice in one archive) both stay. A row is emitted only when the winner names a source and a spelling the index actually indexed; a row table is looked up by the winner's own spelling first and by `GameData.Normalize` second, so an archive that spells a path differently is not silently dropped. An archive `GameData` cannot read adds its warning to the build report (`CacheNote`) instead of failing the build. The losing rows are not thrown away: the cache stores each source's own entry list, so deleting an overriding mod file restores the archive entry on the next build without rescanning the archive. No loose root is walked twice: the walk the stamp already needed is the list the merge is offered, and an archive is opened only when its stamp does not match, so a fully warm build reads no archive at all and the merge is the only work left. Duplicate archive warnings, from the per-source scan and from the merge, are collapsed before they reach `CacheNote`. `AnimationIndex.Label`/`GroupLabel` are the single source of the kind strings that the CLI summary keys, the UI group headings and the kind filter all compare against.

`Build` reports `AnimationIndexProgress(done, total, phase)` for every source and checks `CancellationToken` before each source, between the source scan and the merge, and every 1024 archive entries it indexes; a cancelled build throws and writes nothing, so the previous cache stays valid. The walks it uses are `GameData`'s: `EnumerateLoose` and `WalkLoose` skip reparse points (`FileAttributes.ReparsePoint`, on files and on directories) and refuse a path that resolves outside the root, so a junction or symlink inside a Data or mod folder is never followed and cannot loop the walk or pull a row in from outside the root. `BuildAsync` is the `Task.Run` wrapper for a caller that already holds a request; the Animations tab builds its request and the index in one background step instead, because the load order has to be discovered there too. `Elapsed` is measured inside `Build` so the CLI, the UI report and the timing measurement all read the same number.

## `BGS/src/Archive/AnimationIndexCache.cs` — the persistent index cache

The default cache file lives in `AnimationIndex.CacheFolder`, `%AppData%\BehaviourGraphStudio\cache`, next to `settings.cfg` (`BGS/app/Settings.cs`) and the assistant cache folders, and it is named per key: `DefaultCachePathFor` produces `animation-index-<16 hex digits>.json` from the same data folder, profile and modlist path that `MatchesKey` compares, so the UI and `--scan-index` no longer overwrite each other when they index different load orders. `--cache <file>` still names an explicit file. The document is one JSON document (`AnimationIndexCacheData`): a version, the key (`DataFolder`, `Profile`, `ModlistPath`, `ModlistStamp`), and one `AnimationIndexCacheSource` per source with its own stamp plus parallel `Paths` and `Sizes` arrays. Only the paths and sizes are stored; kind, project, race, character and folder are re-derived by `Classify` on load, so improving the classifier takes effect on the next start without invalidating the file. `AnimationIndexCache.Load` returns null for a missing, unparsable, structurally incomplete or differently versioned file, refuses a file larger than `MaxCacheBytes` (256 MB) before it is read, and discards a document whose source count, per-source path count or path length is over its cap or whose `Paths` and `Sizes` arrays disagree, so a tampered or runaway cache cannot be turned into unbounded work. `Save` writes a temporary file beside the target and moves it into place, so a crash or a concurrent reader never sees a half-written cache. `Build` records a discarded or rejected cache in `CacheNote`, which the UI shows in amber and the CLI prints, and then rewrites the file.

Staleness is per source, not per document: `AnimationIndexCache.MatchesKey` accepts the cached document when the data folder, the profile and the modlist path are the ones in the request, and then each source is reused only when its stamp matches. An archive's stamp is its size and last-write time. A loose root's stamp is a count, a byte total, the newest write time and an FNV-1a 64 digest over `relative path|size|mtime` for every indexed file under it, because a directory's own write time does not change when a file deep inside it is edited, while a file list digest does. Changing the ordered modlist therefore does not force a full rebuild: the sources that are still present are reused, only the added or removed roots are rescanned, and the merge re-runs from the per-source entry lists so the new precedence still wins. `ModlistStamp` is stored for the note only and deliberately not part of the key, for that reason. `AnimationIndex.ReusedSources` and `ScannedSources` expose what a build reused, which is how the tests and the report measure warm starts.

## `BGS/src/Archive/AnimationIndexView.cs` — grid budget and filter caps

The Animations browser needs two pieces of arithmetic that are worth testing without a window, so they live here as plain data, with no Avalonia type in sight. `AnimationIndexView.Groups(shown, budget)` buckets the filtered entries by `AnimationKind` in enum order, skips the empty buckets, and then hands the `MaxGridRows` (2000) budget out one row at a time round-robin across every group that still has rows to give. That is the fair share: a group only ends up short when the budget is exhausted, the leftover goes to whichever groups still have rows, and no group can eat the whole list. Each group's heading is `AnimationIndex.GroupLabel`, the total, and — only when the group is cut — `(showing X of Y)`. `ShownCount` totals the painted rows, which is what the summary pill reports. `CappedValues` backs the kind, project and folder combo boxes: it always keeps the `all kinds`/`all projects`/`all folders` sentinel, keeps at most `MaxFilterValues` (400) values, and appends the single note row `(+N more, use search)` when it had to cut, so the cap is visible instead of silent. `FilterValue` treats a selected value that is empty or starts with `(` as the sentinel, which is how picking that note row behaves as no filter.

## `BGS/app/MainWindow.Index.cs` — the Animations browser

`BGS/app/MainWindow.Index.cs:MainWindow.BuildAnimationIndexTab` is the `Animations` tab, added to `BGS/app/MainWindow.cs` after `Playback` and to the `Animation` workspace in `BGS/app/EditorShell.cs:EditorShell.Workspaces`, so it sits beside Animation and Playback and inherits the animation tool strip. Its toolbar is three groups in the `ToolbarGroup` style: the Data folder field with `Browse...`, `Index now`, `Rebuild` and `Cancel`; the search field with the kind, project and folder combo boxes; and `Open selected`. The body is a `HkGrid` with `Path`, `Source`, `Kind` and `Project` columns, two pills (`_indexSummary` for the count, `_indexProgress` for the indexing state and the last build report) and one heading row per kind that has entries, which is the grouping. `Index now` reads `gameDataFolder` and `gameModsFolder` from `Settings` and writes the data folder back, so the tab and the Chain tab's Game data field stay on the same setting.

Filtering (`ApplyIndexFilter`) is the archive browser's rule — every search word must appear somewhere in the entry, in any order (`BGS/app/ArchiveBrowser.cs` uses `Ba2.Matching` the same way) — combined with exact kind, project and folder matches from the combo boxes, which are refilled from the index on every build and capped at 400 values so a large load order cannot stall the combo; when a list is cut, its last row is the `(+N more, use search)` note and picking that row counts as no filter. The paint budget is `AnimationIndexView.MaxGridRows`, split fairly across the non-empty kind groups with the leftover redistributed, so the first group cannot eat the whole list; a cut group's heading carries `(showing X of Y)` and the count pill says how many rows are painted. The index itself is never truncated. `StartIndexBuild` reads the Data and mods settings on the UI thread and then builds the whole request — `GameData.Discover` or `GameData.DiscoverModded` through `DiscoverIndexRequest` — together with the per-key default cache path and the index inside one `Task.Run`, so reading the load order never happens on the UI thread; the failure string and any exception come back through the await and land in the progress pill without touching the previous index. One cancellation source and one build generation are kept: `ShowIndexProgress` ignores a post from any generation but the current one, so progress queued by a build that already finished cannot overwrite its own report, and closing the window (`CancelIndexOnClose` from `MainWindow.OnClosed`) cancels a running build.

Opening reuses the existing open path exactly. `OpenIndexEntry` branches on the source label: a `loose:` entry opens its own file, and an archive entry is extracted to the temp folder keyed by `TempDirKey` under `Ba2.FlatFileName` and loaded read-only (both through `MainWindow.OpenIndexEntryAt`, which passes the entry's `VirtualPath` on as the one-shot hint the packed-sibling lookup reads), which is the same sequence `BGS/app/MainWindow.Pose.cs:MainWindow.OpenArchive` performs from the archive browser, including the note that nothing writes back into an archive. The entry itself is read through `GameData.TryReadEntry` on the request's own `GameData` (`AnimationIndexRequest.OpenGameData`) rather than by opening the archive here, and both the declared entry size and the bytes that came back go through `InputFilePolicy.EnsureHkx`, so an oversized or vanished entry is a warning in the status line instead of an exception.

## `BGS/app/IndexScan.cs` — `--scan-index`

`BGS/app/IndexScan.cs:IndexScan.Run` follows the `--scan-*` contract of `BGS/app/ModlistScan.cs` but lives in its own file, because `ModlistScan.cs` is already close to the 750-line project limit. It takes a game Data folder or a Mod Organizer 2 instance root and asks `AnimationIndexRequest.FromLoadOrder` for the load order, so the enabled mods, the profile and the archives are the ones MO2 has — the same discovery the Animations tab runs, and the same `Mo2ScanData` adapter the other scans use. `--json` prints one `ScanReport` (`mode: scan-index`, summary keys `animation`, `behaviour`, `project`, `character`, `skeleton`, `other`, `entries`, `sources`, `reusedSources`, `scannedSources`, `cacheHit`), progress stays on stderr, `--cache <file>` points the cache elsewhere, `--no-cache` disables it and `--rebuild` ignores it. Without `--cache` the per-key default file for the resolved request is used, so the CLI and the UI keep separate caches per load order. Exit is 0 with entries, 1 with none and 2 for a missing folder or an unreadable load order. `BGS/app/Program.cs:Program.Main` dispatches it beside the other `--scan-*` commands.

## `BGS/tools/tests/AnimationIndexTests.cs` and `BGS/tools/uismoke/AnimationIndexSmoke.cs`

`AnimationIndexClassifyTests` covers the path-only taxonomy (each kind, the DLC03 two-segment project, backslashes, the character-asset name, the indexed-name filter). `AnimationIndexBuildTests` builds a real temp Data folder with a real GNRL archive written by `BGS/tools/tests/TestArchives.cs:WriteGeneralBa2` and with genuine `.hkx` payloads copied from `fixtures/vanilla`, then asserts the load-order winner, that every loose fixture file is indexed with its provenance, that two distinct files of one stem in one source both stay while a mod file still shadows the source below it, that a self-referential directory symlink inside a loose root neither loops the walk nor adds a row (skipped where the platform refuses to create the link), that a warm build reuses every source and scans none, that rewriting or merely touching one loose file rescans only that source, that adding and removing a mod file reveals and re-shadows the archive entry, that a corrupt cache and an old cache version are discarded and rewritten, that a cache from another data folder is not reused, the key's profile and modlist behaviour, and progress plus cancellation. `AnimationIndexCacheTests` tampers with a cached source's `Paths` (a `..` segment, a drive-qualified name, and a `..` buried mid-path) with its stamp still matching and asserts all three rows are dropped with a note and nothing outside the root, checks the per-key default cache name, and checks the load caps (the 256 MB size guard, too many sources, an overlong path, and mismatched `Paths`/`Sizes`). `AnimationIndexViewTests` covers the fair grid budget and the `(showing X of Y)` heading, the capped filter lists with their `(+N more, use search)` note, and the note row behaving as no filter. `AnimationIndexScanCliTests` joins the existing `ScanCli` collection so its console capture does not race the other scan tests, and checks the JSON contract and the warm rebuild through `IndexScan.Run`.

The UI check is `BGS/tools/uismoke/AnimationIndexSmoke.cs:AnimationIndexSmoke.Run`, registered from `BGS/tools/uismoke/RagdollLifecycleSmoke.cs:LifecycleSmoke.Run` rather than from `Smoke.Main`. `BGS/tools/uismoke/Smoke.cs` is over the 750-line project limit, and the line-limit guard blocks any change that leaves an oversized file at or above its current size; the two assertions that had to move with the new tab (the tab header list and the `HkGrid` count) are edits to existing lines, and the new workload is a separate file. The check drives the production tab: empty state, entries from loose files, an archive and a mod root, grouping headings, the search narrowing to one entry and matching nothing, the kind filter, and both open paths (a loose entry reaches `PathFieldForTest`, an archive entry is extracted to a real temp file).

## BGS/src/Hkx/FixupOrder.cs — fixed-array fixup walk and the declared-site guard (#671)

Measured on 17,469 vanilla `.hkx` files resolved through GameData from `D:\SteamLibrary\steamapps\common\Fallout 4\Data` (8 archives, 17,511 entries, 42 skipped) plus the human skeleton fixture `BGS/tools/tests/fixtures/vanilla/Meshes/Actors/Character/CharacterAssets/skeleton.hkx`; provenance is the measured packfile-order rules and Fallout4.exe 1.10.163 decompile facts.

`FixupOrder.Walk` expands a member with `HavokClassTypes.Member.ArrSize > 0` into `max(1, ArrSize)` sites in place: pointer slots stride by the active pointer width for `TYPE_POINTER` (the global table) and for `TYPE_STRINGPTR`/`TYPE_CSTRING` (the local table), a `TYPE_STRUCT` member recurses at the CType's declared size, and `TYPE_ARRAY`/`TYPE_SIMPLEARRAY`/`TYPE_RELARRAY` headers expand at 16 bytes each. The shipped schema has 196 `arrsize > 0` members; two are pointer members — `hkpRagdollMotorConstraintAtom.motors` (3 slots at +64, reached from `hkpRagdollConstraintData.atoms` at +32 through `hkpRagdollConstraintDataAtoms.ragdollMotors` at +160) and `hknpLodShape.shapes` (8 slots at +168) — and 14 are struct members. Before the change the human fixture's global table came out 40 positions and 104 bytes away from the file's own order and its walk placed only 122 of the 140 global entries; after it, reordering reproduces the global, local and virtual tables byte for byte in all 17,469 checked files, and an unedited rebuild reproduces the original file bytes in 17,469 of 17,469.

`Reorder` no longer sorts an entry the walk cannot place to the end of the table. It refuses with `InvalidDataException` naming the offset when the entry sits on a site the class layout declares for that table's own kind — a pointer slot for the global table, an array header or string site for the local table. The guard is an independent declaration-only model of the same classes (it does not consult the walk), so a future walk that loses a case fails instead of quietly dropping the entry. Entries on no declared site of that kind keep their previous last-place order, because the class declarations say nothing about where they belong: that covers BGS's crafted test fixtures and an intermediate `NativeAppend` state, and a global fixup a tool aimed at an array header rather than at a pointer, which 23 existing tests exercise. The schema-unknown pass-through is unchanged and explicit: `Reorder` returns without touching either table when any instance class is absent from the class table (the `hcl*` cloth classes), which is 42 of the 17,511 corpus files.

## BGS/src/Hkx/CanonicalPackfileWriter.cs — canonical packfile re-serializer (#671)

Provenance: measured on the vanilla files the game data resolves (17,511 `.hkx` entries in 8 archives under `D:\SteamLibrary\steamapps\common\Fallout 4\Data`, 17,469 checked, 42 skipped) and Fallout4.exe 1.10.163 decompile facts.

`CanonicalPackfileWriter.Write` re-emits `__data__` from the object graph instead of from the old byte layout. Objects are visited in the order the virtual table states, which is the depth-first pre-order from the root for every vanilla file measured; each object's members are visited in declared offset order, array element pointers are expanded in place, and a member with `ArrSize > 0` expands to its slot count. Per object it emits, in that order, the body (the class's declared size) and then its extra data: an array payload for each `TYPE_ARRAY`/`TYPE_SIMPLEARRAY`/`TYPE_RELARRAY` header (stride by `VSub`: the active pointer width for pointers, strings and `TYPE_ULONG`, `2 * pointer` for variants, `HavokClassTypes.Width` for values, the CType's declared size for struct arrays), the text of each `TYPE_STRINGPTR`/`TYPE_CSTRING` member, and the element texts of a string array, deduplicated by text offset.

Placement is 16 bytes for object bodies and array payloads and 2 bytes for strings, which is what the measurement found across 15,320 vanilla files. Any run of source bytes that no part accounts for is carried as an opaque part at its own offset, so a file this build cannot fully explain is re-emitted rather than corrupted: the human skeleton fixture has nineteen unaccounted 320-byte runs between a capsule shape and its properties object plus a two-byte tail, and it still comes back byte for byte.

The three tables are rebuilt from the same walk. The global and local tables list their sites in walk order (`FixupOrder.Sources`) with each destination remapped through the new layout, and an entry no walk site covers keeps its place at the end of the table; the virtual table is one entry per object in object order pointing into the rebuilt `__classnames__`. `__classnames__` is rebuilt with the four meta classes first and then the object classes in first-appearance order, each as `[u32 signature][0x09][name][NUL]`. Section padding and headers come from `PackfileImage.Rebuild`, which already reproduces vanilla exactly.

`Resize(payloadAt, elements)` grows or shrinks one array payload. A grown array appends copies of its last element, bytes and fixups both: the copied element's table entries are spliced into the table order directly after the template element's, the header's element count is rewritten at its new offset, and a shrink drops the entries of the elements it removed. Relative arrays and payloads whose header the walk does not reach are refused rather than guessed at, as is a graph with a class this build has no definition for or a destination that cannot be mapped.

Measured after this change: re-serializing each of the 17,469 checked vanilla files unchanged reproduces its bytes exactly, 0 exceptions; the human skeleton fixture round-trips byte for byte, one inserted hkaSkeleton bone row (a copy of the last row) re-reads with one more row and re-serializes to identical bytes twice, and removing that row returns the file to its original bytes.

## BGS/app/MainWindow.Open.cs — a packed animation keeps its virtual path into the pose path (#699)

The Animations index and the Bridge's `From archive...` both extract an archive entry to a flat copy under `%TEMP%\BehaviourGraphStudio\<key>` and call `Load()`. Before this change only the ragdoll path was told which virtual path the copy came from: `Load` read `_pendingVirtualPathHint` and passed it to `TryLoadRagdoll`, then called the two-argument `LoadPose(path, fileName)`. That overload resolved the pose skeleton with `PoseSkeleton(key)`, which looks for a skeleton beside the file it is handed — beside a flat copy there is none — so `_poseSkeleton` stayed null, `AnimationPose.WhyNotPosable` refused, `LoadPose` set `_poseAnimation` to null, and `TogglePlay` and `ShowFrame` returned early. Play and every step button therefore did nothing for an animation opened straight from an archive, through either path.

The hint now travels with the open. `Load` captures both `_pendingVirtualPathHint` and a second hint, `_pendingSourceFolder`, which the index and the archive browser set to the folder the archive itself lives in (the game Data folder, normally), and it hands both to `BuildAnimation` and to `LoadPose`. `MainWindow.Index.cs:OpenIndexEntryAt` takes that folder as an optional third argument so a loose index entry passes none, and `MainWindow.Pose.cs:OpenArchiveEntry` is the shared body of the browser path and its test hook `OpenArchiveForTest`.

`PackedAnimationKey` returns the hint before it needs a `GameData`, so a window that has never attached game data still knows which virtual path the copy came from. The packed fallback itself is one method, `MainWindow.Playback.cs:ResolveAnimationSkeleton`, used by both the Playback pose path and the Animation tab: the project chain's skeleton first, then a sibling `.hkx` that parses as a skeleton — only when the path handed in is a file that exists, so a virtual animation name can never be resolved against the process working directory — and then the packed sibling through `GameData.SkeletonEntryName` and `TryReadSkeletonEntry` (`SkeletonBytes`). When no game data is attached it discovers one from the source folder hint or from the saved `gameDataFolder` and disposes exactly what it opened, so the index path works without the Chain tab having run. `ArchiveSkeleton` takes that `GameData` as a parameter and is static; `SiblingSkeletonPath` returns null for a folder that does not exist instead of letting the enumeration throw.

The Animation tab resolves its skeleton the same way, which is what stops it from reporting `no sibling skeleton, tracks are numbered` for a file whose packed sibling Playback had already resolved. Its `BuildAnimation` takes the same two hints and calls the same resolver.

Refusals are no longer thrown away. `LoadPose` keeps the reason in `_poseRefusal` (`label: why`), `TogglePlay` shows that text instead of the generic `Nothing loaded to play`, and `UpdateTransportState` disables the transport with a tooltip that carries the same reason: Play is enabled only when the clip can actually play (`PlaybackSession.CanPlay`, which needs more than one frame), the four step buttons when there is a posed animation at all, and a single-frame clip says so in the tooltip rather than looking dead. `ClearPose` clears the reason and refreshes the buttons.

Coverage: `BGS/tools/uismoke/Smoke.PackedAnimation.cs:PackedAnimationOpensFromIndexAndArchive` packs the genuine `TurretIdleWeapReady.hkx` and `TurretStandingSkeleton.hkx` samples into a synthetic `Fallout4 - Animations.ba2` under `Meshes/Actors/Turret/Animations/Mounted/` and `Meshes/Actors/Turret/CharacterAssets/`, opens the entry through the Animations index and, in a second window, through the archive browser's own path, and asserts for both that `_poseAnimation` is loaded, that the pose skeleton came from the packed sibling, that Play sets `IsPlaying` and that a step moves the frame. It also asserts that the Animation tab names the sibling skeleton and that the read-only archive copy keeps Save changes disabled. `PlaybackReadsPackedClipsFromArchives` still covers the behaviour-plus-clip path with attached game data.

## BGS/app/MainWindow.Animation.Tools.cs and MainWindow.Playback.Tools.cs — tool-selector toolbars (#699)

Both tabs now use one shape: a compact top bar and, under it, exactly one tool panel. The top bar is a `WrapPanel` of `ToolRow`s — an uppercase row title followed by that row's controls — and each tool panel is a `Ux.Panel` whose first child is the same kind of row title inside a `Ux.Wrap`, so a panel wraps instead of scrolling. The host `StackPanel` is capped at `MainWindow.ToolPanelWidth` (1180), which is what keeps the whole ribbon inside the viewport at 1280 px and above; the panel's fields are laid out with `Ux.Labelled`, which puts the caption beside the field and sets `AutomationProperties.LabeledBy` on it.

The Animation tab's top bar carries frame navigation (First, Earlier frames, Later frames, Last and the frame count), the `Tool` `ComboBox` (`Find frame`, `Edit frame`, `Trim`, `Retime`, `Loop`) and `Save changes`. The selected tool's panel holds that tool's fields, its Preview/Apply buttons and its answer line; the other four panels are collapsed, so their controls stay in the tree (the assistant's `editor_state` still finds them by name) but out of the layout and out of the accessibility tree. The Playback tab's top bar carries the transport (Play/Pause, `|<`, `<`, `>`, `>|`, Fit), the timeline (the scrub slider, the frame readout and the scale and frame-distance pills) and the `Tool` `ComboBox` (`Overlays`, `Scene tools`, `Diagnostics`); the panels hold the eight overlay checkboxes, the scene tools (Drop, From selected node, Mesh..., No mesh, Clear measure) and the physics report text with `Copy diagnostics`. The `Skeleton / skin authoring` button is untouched: it lives in `EditorShell.AnimationTools` rather than in a tab toolbar, so it stays reachable from the Animation workspace.

Every control and handler is the one the tab already had; only the parent changed. `ShowAnimationTool` and `ShowPlaybackTool` switch visibility and remember the choice in `Settings` under `animation_tool` and `playback_tool`, falling back to `Edit frame` and `Overlays` when the saved name is not one the build offers. `Ux.Field` takes an optional name and now always sets `AutomationProperties.Name` (the watermark when no name is given), `Ux.Panel` and `Ux.Group` name their border, and the tool `ComboBox`es, the timeline slider and the rig window's four bone fields are named, so the assistant's control discovery (`BGS/app/AssistantEditor.cs`, which labels a control from its automation name first) finds them under the same labels as before or a clearer one.

Coverage: `BGS/tools/uismoke/ToolSelectorSmoke.cs` selects every tool on both tabs and asserts that its own panel is visible, that the other tools' panels are not, and that a representative field of that tool is effectively visible; that the trim, retime and loop fields carry their visible captions; that the save button follows the edits; that both tabs reopen on the remembered tool; and, in a 1280 px window, that every one of the six tool panels needs no horizontal scrollbar and leaves more than 400 px of the window to the editor. `BGS/tools/uismoke/WorkspaceSmoke.cs:LayoutAndHandlersRemainConnected` asserts the new section names (`TRANSPORT`, `TIMELINE`, `TOOL`, `OVERLAYS`, `FRAME NAVIGATION`, `EDIT FRAME`), and the repo's existing `TopToolbarsLeaveTheWorkspaceClear` still holds both tabs under its 160 px ribbon budget at 1000 px.

`BGS/tools/uismoke/Smoke.ToolbarPng.cs:ToolbarPng` renders the toolbars to PNG through the headless Skia backend (`--toolbar-png <folder>`), which is how the screenshots of this design were taken: a 1280x860 window on the 19 frame spline fixture for the Animation tab on the Trim tool and the Playback tab on the Overlays tool, and a third on the packed turret clip for the same Playback tab with its transport enabled.

## BGS/app/MainWindow.Document.cs — SaveAnimation refuses a clean clip, and the save button follows the edits (#699)

`SaveAnimation` was a one-line pass-through, and the Animation tab's `Save changes` was built with `Ux.Primary` and never disabled, so it was the blue, clickable primary action on a tab that had nothing to write. It is now built secondary and repainted by `RefreshSaveAnimationState` (`BGS/app/MainWindow.Animation.Tools.cs`): enabled, accent-filled and white-texted only when `_animationEdited && !_readOnly && _animationData != null`, and otherwise a plain card button with a tooltip that says why — no unsaved changes, a read-only archive copy with the reason it cannot be written, or no animation at all. `_animationEdited` is a property whose setter refreshes the button, so every existing assignment (a frame edit, a trim, a retime, a loop, a save that cleared it, and the test hook) moves the button without a second call; `Load`, the index's read-only branch and the archive browser's read-only branch refresh it explicitly because they change `_readOnly` or `_animationData` around the open.

`SaveAnimation` itself now refuses early and says so in the frame-edit answer line: no animation in this file, a read-only source with `_readOnlyWhy`, or nothing edited. The pending-changes path (`SavePendingChanges`) still calls `WriteAnimation` directly, so the discard dialog's `Save` can commit a dirty clip without going through the guard.

Coverage: `BGS/tools/uismoke/ToolSelectorSmoke.cs:SaveStateFollowsTheEdits` opens a copy of the spline fixture, checks that a clean clip keeps the button out of reach and unpainted, edits one frame, checks that the button turns on and becomes primary, saves through the guarded path and checks that it goes quiet again. The packed-open smoke checks the read-only case, and `AnimationGeometrySmoke` keeps calling `SaveAnimationForTest` on a dirty clip for trim, retime and loop.

## BGS/app/MainWindow.Index.cs — building the index opens nothing (#699)

It was reported that pressing `Index now` opened the first entry by itself. The static reading found no such path — `StartIndexBuild` builds on a worker, `ApplyIndex` resets the filters and repaints the grid, and the only two open triggers are the grid's `DoubleTapped` and its `Enter` key handler — and the runtime check now says the same: `BGS/tools/uismoke/Smoke.PackedAnimation.cs:PackedAnimationOpensFromIndexAndArchive` builds an index over a synthetic data folder, asserts that the window still has no pose source, no frame count, no loaded XML and no decoded animation, and that the path field does not name the indexed entry, and only then selects and opens the entry deliberately. A row that the repaint selects fires `UpdateIndexStatus`, which writes the status line and nothing else. The report is therefore most likely an Enter or a double-click from the automation that drove the button, and the new check is what would catch a real auto-open if one were ever added.

## BGS/app/Ux.cs — one disabled look for the command pills, the tool ribbons and the two tool pickers

Every control that carries its own colours did so with local values: `Ux.Primary` and `Ux.Secondary` set `Background` and `Foreground` on the button, the Animation tab's `RefreshSaveAnimationState` repainted `Save changes`, and the old `Ux.Style` set the same two brushes on every large button. A local value beats a style, so the Fluent theme's own `:disabled` rows could never reach them, and an out-of-reach control drew the theme's near-black disabled label on the app's near-black surfaces. The header pill row (Undo, Redo, Save to .hkx), the Playback transport and the six overlay toggles that wait for a ragdoll were the visible cases.

`Ux.AppStyles` now carries the skin. `Button.primary` and `Button.secondary` set the background, the border and the text; `Button:disabled` sets that trio to the new `OffSurface` (`#2B2B2B`), `OffEdge` (`#3A3A3A`) and `OffText` (`#9AA0A6`); a second style reaches the button template's `ContentPresenter` through its own `Background`, `BorderBrush` and `TextElement.Foreground`, because Fluent paints the presenter for a disabled button and a setter on the button cannot override that. `CheckBox:disabled` does the same for a toggle's label and its box border, and the disabled label clears 4.5:1 against the card surface. `Ux.Install` adds these styles after the Fluent theme, and both hosts call it: `App.Initialize` (`BGS/app/Program.cs`) and the smoke host, which moved out of `BGS/tools/uismoke/Smoke.cs` into `BGS/tools/uismoke/HeadlessApp.cs` because `Smoke.cs` is already over the 750-line project limit and may not be left at its current size. The smokes render the same styles the app ships, which is what the disabled look had been missing. `Primary` and `Secondary` now add a class and the geometry only, the "a dirty clip makes Save changes primary" state is `Classes.Set("primary", dirty)`, and `SaveAnimationButtonIsPrimary` reads that class instead of the background brush. `Ux.Combo` builds a tool picker with the card background, the border brush and the title foreground, plus a `ComboBoxItem` style, replacing the code-blue text on grey both tabs carried.

## BGS/app/MainWindow.Playback.cs — the packed sibling skeleton is resolved before the shared extract folder

`OpenIndexEntryAt` and `OpenArchiveEntry` copy an animation out of a `.ba2` into `%TEMP%\BehaviourGraphStudio\<key of the archive>` and open that copy. Every entry of one archive shares that folder, so when the copy carried a virtual path, `ResolveAnimationSkeleton`'s loose-sibling step (`SiblingSkeletonPath`) parsed whichever `.hkx` sorted first in the folder — another creature's extracted skeleton, or another clip's embedded one — and returned it before the archive was consulted at all. The order now depends on where the file came from: a virtual path hint, or a path under the `BehaviourGraphStudio` temp root, tries the packed sibling first and falls back to the loose scan only when the archive holds no skeleton for that entry, while a file the user opened from disk keeps the old order.

`DataFolderGameData` became `DataFolderCandidates`, which tries the hint folder and then the `gameDataFolder` setting, catches `InvalidDataException` beside the IO and access errors `GameData.Discover` can raise, keeps every discovery in `_scopedGameData` so one open discovers each folder once, and skips a folder twice named. `PackedSkeletonFromDataFolders` asks each candidate for the entry, so a hint folder that cannot serve it falls back to the configured game data folder. `Load` (the document open) and `LoadPose` release that scope in a `finally`, so the archives a lookup opened are closed at the end of the load instead of at the next collection.

## BGS/app/MainWindow.Animation.Tools.cs and BGS/app/MainWindow.Playback.Tools.cs — ribbon captions, spacing and panel names

The two ribbons read as one run of words. Every `ToolRow` now keeps an 18 px gap to the next group, the frame count sits in `ToolCount` with a left margin, and the Animation tab's `SAVE` caption is gone, leaving `Save changes` alone at the end of its row. The gap is width only, because the Playback ribbon's 160 px budget at 1000 px (`BGS/tools/uismoke/WorkspaceSmoke.cs:TopToolbarsLeaveTheWorkspaceClear`) has no room for a vertical margin on every group; the groups stay vertically centred as before. Each panel's `RowTitle` is the tool's own name (`Find frame`, `Edit frame`, `Trim`, `Retime`, `Loop`, `Overlays`, `Scene tools`, `Diagnostics`), which is also the text the picker shows, and `Ux.Panel` is now named `<tool> tool panel`, so a panel no longer shares its automation name with a button inside it (`Find frame` was both). `ShowAnimationTool` and `ShowPlaybackTool` take a `remember` flag and write the remembered tool only when the selection changes, because both run once while the window is built and used to write settings for the construction alone.

## BGS/app/EditorShell.cs — the workspace scrollbar takes its row only when it is needed

`HorizontalScrollBarVisibility` on the workspace toolbar's scroll viewer is `Auto` instead of `Visible`, so a ribbon that fits gives its scrollbar row back to the editor. `BGS/tools/uismoke/WorkspaceSmoke.cs:TopToolbarsLeaveTheWorkspaceClear` asserts the Auto mode for every header, checks that the scrollbar has its own space only while the bar is visible, and still scrolls an overflowing ribbon into view. `BGS/tools/uismoke/ToolSelectorSmoke.cs` keeps its 1280 px no-overflow check for all six tool panels and now also asserts that each selected tool captions its panel with its own name and owns a `<tool> tool panel` automation name.

## BGS/tools/uismoke/Smoke.SkeletonSibling.cs — two creatures in one extract folder

The new check packs the genuine `TurretIdleWeapReady.hkx` and `TurretStandingSkeleton.hkx` samples into a synthetic archive, copies the clip into the folder the app extracts archive entries into, and lays a second creature's `TurretMountedSkeleton.hkx` beside it. The mounted name sorts first, and the check first proves `MainWindow.SiblingSkeletonPath` picks it, so the resolution checks cannot pass by accident. It then asserts that a hinted resolution and a real `OpenArchiveForTest` open both take the packed standing skeleton, that the Animation tab names it by its bone count, that a hint folder which lacks the entry falls back to the settings game data folder, that `MainWindow.DataFolderDiscoveriesForTest` counts one `GameData.Discover` for the whole open, and that the folder of the archives that open discovered can be deleted again once the window has closed, which it cannot while a lookup still holds them open. It runs in the default uismoke mode from `RagdollLifecycleSmoke.Run`. `BGS/tools/uismoke/Smoke.ToolbarPng.cs` now also renders `ui-playback-enabled.png` beside the two toolbar shots, on the packed turret clip, so the transport and the tool panel are seen enabled as well as out of reach.

## `BGS/app/Viewport3D.cs` — GPU playback viewport (#668)

The Playback tab can draw the skinned mesh as shaded triangles. `Viewport3D` derives from
`Avalonia.OpenGL.Controls.OpenGlControlBase` and binds OpenGL through `Silk.NET.OpenGL` 2.23.0,
loaded from the context Avalonia hands to `OnOpenGlInit` with `GL.GetApi(gl.GetProcAddress)`.

Vertex and fragment programs live as C# string constants in `Viewport3D.cs`, with a desktop
(`#version 330 core`) and a GLES (`#version 300 es`) variant of each. `OnOpenGlInit` reads
`GL_VERSION` and picks the pair that matches the context Avalonia created, because the Windows
backend usually hands out ANGLE's GLES. The shaded program lights the surface with one key and one
fill Lambert term in world space, leaves culling off, and flips the normal on `gl_FrontFacing` so
double-sided Fallout 4 shapes light correctly from either side.

### One camera for both renderers

`ViewportCamera` is the single description of the orbit state: yaw, pitch, zoom, pan in pixels,
centre and fit. `SkeletonView` still owns those fields and exposes them as `CameraState`, so its
`Project` and the GL matrices are built from the same numbers. `Project` is
`width / 2 + x * scale + panX`, `height / 2 - up * scale + panY` with
`scale = min(width, height) / (fit * 2.6) * zoom`, which is orthographic: depth never moves a
screen position. The GL side therefore uses an orthographic projection too, not a perspective one,
packed into `ViewportCamera.View()` and `ViewportCamera.Projection()` with pan folded into the
off-centre bounds and a depth range of `fit * 32` either way. `Viewport3DTests` proves the two
paths agree on sample points, so the 2D overlays (bones, labels, bodies, constraints, frames and
the mesh wireframe) land exactly on the shaded surface.

### Mesh upload

`MeshGeometry` and `MeshPart` are the renderer-facing abstraction: positions, triangle indices and
optional normals and uvs. Radicle can supply normals and uvs per part later without touching the
upload path; a part that carries no normals gets area-weighted vertex normals computed from its own
skinned triangles (`MeshGeometry.ComputeNormals`). Skinning stays where it was,
`SkinnedMesh.Pose` in CPU linear-blend form, and `MainWindow.UpdateMesh` refreshes one reused
`MeshGeometry` from the same posed vertex arrays that feed the 2D wireframe, so each shape is
skinned once per frame and nothing on that path is allocated per frame: the part vertex arrays, the
interleaved pack buffer, the normal scratch array and the 2D segment array are fields sized once by
the vertex count, `SkinnedMesh.Pose` fills the array it is handed, `MeshGeometry.Refresh` copies
into its own arrays and only reallocates when the vertex or index count changes, and the `List` to
`ToArray` copy for the 2D edges is gone. The GL control uploads positions with `BufferSubData` while
the packed size is unchanged, uploads the index buffer only while the geometry hands it the same
index array instance (so a new mesh, a vertex count change or an index count change re-uploads it
and a moving frame does not), and packs positions and normals interleaved into a reused buffer. The Overlays status line
names the shaded path `Shaded (GL) on <GL_VERSION>` so it cannot be confused with the wireframe
fallback, and its timings separate CPU skin plus normals from the GL upload and draw.

A `GC.GetAllocatedBytesForCurrentThread` run around `UpdateMesh` on this machine (the technical
`Stand_to_Walk` clip at frame 12, a larger turret mesh for contrast, headless) measured the
following before and after: `MaleBody.nif`, one shape, 1523 vertices and 2698 triangles, went from
627567 to 575 bytes per frame, and `TurretMounted.nif`, 2291 vertices and 2946 triangles, from
686544 to 575 bytes per frame. The residual 575 bytes do not depend on the mesh: they are the
viewport scene hand-off through `MainWindow.SyncViewport` plus a fixed bookkeeping cost in the
refresh. `AnimationPose.At` allocates 22584 bytes per frame on its own and is accounted for
separately, because it builds the frame's whole pose rather than the mesh.

### Fallback and overlays

`Viewport3D.GlReady` only turns true after a successful `OnOpenGlInit`. Shaded mode uses the GL
surface while it is true; when it is not (headless uismoke, no driver, a lost context) the
`SkeletonView` overlay keeps drawing the existing 2D wireframe, the status line in the Overlays
panel says so, and the overlay paints its normal card backdrop and ground grid again. The GL layer
sits under `SkeletonView` in the same grid, and the overlay's backdrop and ground plane are
switched off only while GL is actually drawing, so nothing double-draws.

The Overlays panel gained "Mesh mode" (Shaded, Wireframe, Off; remembered as
`playback_mesh_mode`) and a "Mesh wireframe" overlay toggle (remembered as
`playback_mesh_wireframe`) that draws the 2D edges over the shaded surface. The Mesh... picker
stayed in Scene tools. `MeshViewportSmoke` runs headless and asserts the fallback is chosen, that
the status line explains it, and that Wireframe and Off mode behave.

### Skinning cost

A vanilla human body plus a walk clip loads its shaded geometry in about a millisecond of CPU skin
plus normal work per frame on this machine, well under the display refresh, so the mesh path keeps
the CPU linear-blend skinning from `SkinnedMesh` and uploads the result. GPU skinning (bone
matrices as a uniform array with weights and indices as vertex attributes) was not added here: the
body arrives as several shapes with separate bindings, so the uniform-array path would need its own
per-shape attribute buffers kept in step with the CPU path, which the measured cost does not
justify yet.

### Sibling note for animations

The classifier's "This file holds no skeleton and therefore no ragdoll" line is written for
skeleton files. `MainWindow.NoteAnimationRagdoll` now replaces that status for animation files with
where the ragdoll could have come from: the skeleton sibling that was found but carries none, or
the fact that no sibling was found at all.

### Rendering mode and the frame pump

The viewport needs a real OpenGL context, and the Windows composition backend only offers one when
a GL-capable rendering mode is in play. `Program.BuildAvaloniaApp` keeps the default Windows order
for the whole app, `AngleEgl` then `Wgl` then `Software`, so a build without an override behaves the
way any Avalonia Windows app does. `BGS_RENDERING=angle|wgl|software` is the opt-in override:
`Program.RenderingModeOrderFor` moves the named mode to the front and keeps the other two behind it
as fallbacks, so `BGS_RENDERING=wgl` asks for a driver OpenGL context first (a remote desktop
session whose ANGLE cannot start is the common case) while a machine without either still falls back
to software and the 2D wireframe. An unset or unrecognised value leaves the default order alone, and
`RenderingModeTests` pins the default, each override and the unknown value.
`Viewport3D` itself draws nothing until `OnOpenGlInit` has run, which Avalonia only calls once the
compositor can hand out GPU interop for the control's drawing surface.

Frames are requested with `RequestNextFrameRendering`. A 100 ms `DispatcherTimer` in the control
keeps asking while the viewport is visible, which repaints the surface after a resize and keeps the
upload and draw timings on the status line fresh; the scene setter itself also requests a frame.
The timings on the Overlays status line are the measured ones for a vanilla human body
(`MaleBody.nif`, one shape, 2698 triangles) with the character `Stand_to_Walk` clip playing at
frame 12: CPU skinning plus vertex normals land between 1 and 2.5 ms, the dynamic buffer upload is
about 0.9 ms and the draw is 0.01 to 0.03 ms on an NVIDIA driver. The captured viewport lives at
`docs-drafts/bgs-roadmap/ui-shaded-mesh.png` in the roadmap drafts folder, the upright re-check at
`ui-shaded-mesh-fixed.png` and the allocation and context-rebuild re-check at
`ui-shaded-mesh-fixed2.png` beside it.

### Skin bone names

`MainWindow.ApproximateHelperBoneInterim` runs before each shape is bound: a mesh bone named
`Chest_skin`
takes the name of the skeleton bone `Chest` when no bone of the exact name exists. Fallout 4
character meshes name their skin vertices after the skeleton bone plus `_skin`, and the shipped
`skeleton.hkx` carries only the plain names, so `MaleBody.nif` dropped from 45 unmatched bones to
22 (the rest are fat, breast and belly bones the skeleton does not carry at all). The 2D wireframe
and the shaded surface both benefit because they share the binding, and the resolution sits in the
app's mesh loading step, so `SkinnedMesh.Bind` keeps its exact-name contract and the symrm check
that a helper bone does not match is untouched. `Viewport3DTests` covers the resolution.

### Upright GL viewport

The orthographic projection now points the GL clip Y axis the same way as the 2D mapping:
`M22 = -2 / (top - bottom)` and `M42 = (top + bottom) / (top - bottom)`. With the earlier
`+2 / (top - bottom)` and `-(top + bottom) / (top - bottom)` a larger world Z produced a larger clip
Y, and because the GL window origin is bottom-left while Avalonia's is top-left, the shaded mesh and
the GL ground grid rendered upside down against the upright 2D overlays (bones, labels, bodies,
constraints, frames). The old `Viewport3DTests.GlMatrixAgreesWithTheTwoDimensionalProjection`
compared `(ndc.y + 1) * height / 2` with the 2D screen Y, so it read the flipped NDC as if it were
already a top-left window coordinate and passed while the captured image was wrong.
`ProjectThroughMatrix` now returns final Avalonia window pixels (`x = (ndc.x + 1) * width / 2`,
`y = (1 - ndc.y) * height / 2`), the agreement test is
`GlMatrixAndTheTwoDimensionalProjectionAgreeInWindowPixels` and it also pins the window centre with
pan applied, and `LargerWorldZDrawsHigherOnScreenInBothPaths` asserts the observable directly for
every camera in the table: the same point at Z 22 lands at a smaller screen Y than at Z -8 in both
the 2D and the GL path. `Viewport3D` keeps `CullFace` disabled, so the Y flip cannot change what is
visible through triangle winding.

### Rebuilding the GL context

`Viewport3D` keeps every GL identifier, every uniform location and the upload bookkeeping in one
`ViewportGlState`, and `OnOpenGlDeinit` and `OnOpenGlLost` delete what they can and then reset the
whole state to zero (uniform locations to -1). A re-created context therefore starts from nothing,
and `Viewport3D.Build` compiles through `ViewportGlState.EnsureProgram`, which returns the existing
program only while its id is not zero and otherwise compiles and links again, so a new context can
never be handed a stale program id. `Viewport3DTests.TheGlStateResetZeroesEveryIdentifierAndLocation`
and `Viewport3DTests.AZeroProgramIdentifierIsRebuiltInsteadOfReused` pin both halves without a GL
driver. `_scene` is volatile, so the render call always sees one whole camera and mesh snapshot
rather than a torn one, and the uploaded vertex count and index array live in the same state that
the reset clears, so a rebuilt context re-uploads geometry before it draws it.

### Init failures and glGetError

`OnOpenGlInit` now wraps its program and buffer setup in a try/catch and reports the real compile or
link message through `Fail`, which sets `LastError`, so the Diagnostics panel names the error
instead of claiming OpenGL failed for an unstated reason. After a successful init the control reads
`glGetError` once, and it reads it once more after the first draw; the Overlays status line carries
`Viewport3D.GlErrorNote`, which names the error code and the stage, or says the check was clean, so
a shader that links but draws nothing is not silent.

Making ANGLE the default exposed a real bug that the WGL-only default had hidden: every shader
constant began with a newline, so `#version` sat on line 2 and ANGLE's ES compiler refused the
source with `version directive must occur on the first line of the shader`. That is exactly the
message the try/catch above now shows in place of an unstated failure, and it is why the default
order has to be exercised rather than assumed. The eight constants are now written flush against
the start of their verbatim string, so the viewport draws through ANGLE's `#version 300 es` pair on
this machine (OpenGL ES 3.0, ANGLE 2.1.1) and through the desktop `#version 330 core` pair under
`BGS_RENDERING=wgl` (NVIDIA 4.0.0). Both runs report `Shaded (GL)`, and both report a clean
`glGetError` at init and on the first draw.

### Full skin weights

`SkinnedMesh.Pose` divides by the full sum of the positive weights. An influence whose bone did not
match the skeleton contributes the vertex through `SkinnedMesh.Placement` for the frame being drawn
(the `bind * posedTransform` of the first matched bone), instead of being dropped. Taking that
placement from the reference pose made a travelling clip stretch the mesh: the unmatched share
stayed behind at the rest spot while the matched share rode the root. The current frame's placement
puts both shares on the same bone, so the vertex keeps its offset through root motion, and
`Viewport3DTests.AHalfMatchedVertexTravelsWithThePosedBone` pins that a half-matched vertex moves
with the frame. Previously `total` counted only the matched weights, so a vertex weighted half to a
matched bone and half to an unmatched one rode the matched bone completely, and a vertex weighted
only to unmatched bones was moved rigidly by the first matched bone's motion. A weight that is not
finite (`NaN` or an infinity) is treated as zero before it is summed, because one such weight would
otherwise turn the whole vertex into `NaN`; `Viewport3DTests.NonFiniteWeightsAreTreatedAsZero`
covers it. The mesh status line says the unmatched bones keep their whole weight, so a vertex
weighted only to them holds this frame's placement instead of riding the bones that did match.

### Helper bones are an interim approximation

`MainWindow.ApproximateHelperBoneInterim` still maps a shape bone named `Chest_skin` onto the
skeleton bone `Chest`, but it is one seam now: `SkinnedMesh.ResolveSkinBoneName` decides, and it
only rewrites a name when the stripped parent exists in the skeleton. The method is named for its
interim nature so no caller mistakes it for a final mapping, and `.NET` has no compile-time way to
say that here, so the name carries it. The approximation ignores a helper bone's own offset from its
parent, and the mesh status line reports how many bones went through it ("helper bones approximated
by their parent bone"). TODO: a Radicle NIF node-hierarchy ABI is coming that will drive the helper
bones from `skeleton.nif` / the body NIF properly; it replaces this one seam, so the string
rewriting stays out of `SkinnedMesh.Bind`, which keeps its exact-name contract, and the symrm check
that a helper bone does not match is untouched.

### Overlay defaults with a mesh

`MainWindow.TryLoadRagdoll` still enables the ragdoll overlays when a file carries bodies, but when
a mesh is loaded as well it defaults Bodies, Bone labels, Physics skeleton and Body frames to off,
so the shaded body is not buried under body boxes, bone labels, the physics skeleton and the frame
triads; Constraints keeps its old default. `MainWindow.LoadMesh` applies the quieter set too when
the mesh arrives after the ragdoll. Every one of those toggles records that the user moved it
(`MarkOverlayChoice`), and `ApplyOverlayDefault` never writes a toggle the user has set, so an
explicit choice survives the next file. The programmatic writes (`ClearPose`, the Frames-only
toolbar and the defaults themselves) run under `_overlayDefaultsApplying` so they are not mistaken
for user choices. The animated bone skeleton itself stays on.

### Mesh frames across the UI and render threads

`BGS/app/MeshGeometry.cs:MeshGeometry` is refilled on the UI thread every frame and read on the render thread. Every write (`Refresh`) and the renderer's read (`PackSnapshot`) run under one lock owned by the geometry, so the renderer always packs positions and normals from a single frame; the scene published to `Viewport3D.SetScene` carries only the reference and version. `PackSnapshot` resizes the renderer's pack buffer inside that lock, so a mesh swap between sizing and packing cannot overrun it, and returns the index array it packed against. A published index array is never edited: `Refresh` keeps the instance only when the topology is unchanged and allocates a new one otherwise, which is what lets `UploadMesh` skip the element upload by reference. A lock was chosen over double buffering because packing a skinned body takes microseconds and one owner for the arrays is simpler to reason about. `BGS/tools/tests/ViewportMeshThreadingTests.cs` runs a producer and a reader on two threads and checks every packed frame is whole, and pins that all eight shader sources start with `#version` (ANGLE rejects anything before the directive). The upload timer is a reused `Stopwatch` field.

## BGS/src/Archive/GameData.Entries.cs:GameData.TryReadEntry

`BGS/src/Archive/GameData.Entries.cs:GameData.TryReadEntry(string archivePath, Ba2.Entry entry)` reads an entry the caller already holds, so it skips the name scan that the name-based `TryReadEntry` in `BGS/src/Archive/GameData.Assets.cs` does. It reads through `Open(archivePath)`, the held-open archive cache, so each archive is opened once per GameData. A skippable archive failure returns null, and a native-load failure propagates (`IsSkippableArchiveFailure`). `BGS/src/Archive/SubgraphIndex.cs:SubgraphIndex.ReadEntry` is its only caller, so one `SubgraphIndex.Discover` opens each archive once.

`BGS/src/Archive/GameData.Entries.cs:GameData.LooseEnumeration` holds the options that `BGS/src/Archive/GameData.cs:GameData.ModLooseIndex` uses for its recursive mod-root walk. This file keeps only those two members. The name-based read and winner surface (`ReadEntry`, the four-argument `TryReadEntry`, `Read`, `EnumerateWinners`) lives in `BGS/src/Archive/GameData.Assets.cs`, so the duplicate `ReadEntry`, two-name `TryReadEntry`, `EnumerateWinners`, `AssetWinner`, `AssetCopy`, `FlatName` and `LooseFiles` were removed rather than kept beside it.

## BGS/src/Archive/SubgraphIndex.cs:SubgraphIndex.ReadEntry

Reads one manifest or offset entry through the entry overload `GameData.TryReadEntry(archivePath, entry)` and maps a null (a skippable archive failure) to an empty array, as before. It used to open and dispose its own `Ba2` per entry, so one `Discover` opened a large archive 1+N times. It now reads through the GameData handle cache, which `EnumerateEntries` already fills for every archive, so each archive is opened once per GameData and stays open until `GameData.Dispose`. The lock lifetime during `Discover` is unchanged, because `EnumerateEntries` already held those handles; what went away is the extra handle per entry. `BGS/tools/tests/GameDataSharedPathTests.cs:GameDataSharedPathTests.SubgraphIndexReadsEveryManifestFromOneOpenArchive` pins the single open.

## BGS/src/Archive/SubgraphIndex.cs:SubgraphIndex.Discover - why the overlay is not on EnumerateWinners (#696)

The manifest overlay, the loose scan and `Mo2ScanData.ModHkxPaths` stay off `GameData.EnumerateWinners`, because its semantics differ:
- Manifests merge by content id (`byId[sub.Id] = sub`, last entry wins), while `EnumerateWinners` keeps one winner per path.
- The loose AnimTextData scan is top-level only (`LooseManifestFiles`, `TopDirectoryOnly`), while `EnumerateWinners` recurses into subfolders.
- `Mo2ScanData.ModHkxPaths` is per root (loose, then archive) and covers mod roots only, so it does not match the Data-plus-archives order.
- `Mo2ScanData.DescribeSource` would call the winner lookup per row, making the work O(rows x files).
- `ModlistScan --scan-archives` scans a user-picked tree independent of load order, so there are no winners to take.

## BGS/tools/tests/Ba2RadicleTests.cs:Ba2RadicleTests

`BGS/tools/tests/Ba2RadicleTests.cs:Ba2RadicleTests` keeps four Facts: `NativeLoadFailureIncludesRadicleAbiMismatch`, `GameDataArchiveFilterPropagatesWrappedRadicleAbiMismatch`, `PlaybackSkeletonLookupReportsNativeLoadFailureButSkipsCorruptArchives` and `Ba2WrapperReadsGeneralArchivesThroughRadicleAndRefusesTextureArchives`. The winner, discovery and denied-listing regressions that were written against the dev API now live in the #699 suites:
- `BGS/tools/tests/GameDataAssetsTests.cs:GameDataAssetsTests.LaterPluginsBaseArchiveWinsOverEarlierOne` (#804) detects a plugin-order reversal that lets the earlier plugin's base archive win.
- `BGS/tools/tests/GameDataAssetsTests.cs:GameDataAssetsTests.ModArchiveOutranksBaseArchivesWithTheSamePath` detects a mod's top-level BA2 listed after base archives with the same path.
- `BGS/tools/tests/GameDataAssetsTests.cs:GameDataAssetsTests.ModRootWithoutMeshesFolderStillWinsItsSoundPrefix` detects a `Meshes` gate applied to winner enumeration.
- `BGS/tools/tests/GameDataAssetsTests.cs:GameDataAssetsTests.EnumerateWinnersListsHiddenLooseFiles` detects a loose walk that skips hidden files.
- `BGS/tools/tests/GameDataDeniedListingTests.cs:GameDataDeniedListingTests` holds the three denied-listing tests and the denied-read `DiscoverModdedFallsBackToPlainArchivesWhenTheModlistCannotBeRead` (#752 Low), and the `icacls` / mode-0 helpers they share.
- `BGS/tools/tests/GameDataSharedPathTests.cs:GameDataSharedPathTests.SubgraphIndexFindsLooseManifestUnderLowercaseAnimTextData` (#752 Low) is covered in its own section below.

Dev tests that #699 already covers were not copied: `TryReadEntryReusesTheOpenArchiveHandleInsteadOfReopening` is `GameDataAssetsTests.ReadEntryReadsThroughTheHeldOpenArchiveCache`; `IsSkeletonPathAcceptsOnlyHkxUnderCharacterAssets` is `GameDataAssetsTests.IsSkeletonPathCoversCharacterAssetsAndSkeletonNames`; mod-root precedence and the loose-over-archive shadow order are `GameDataAssetsTests.EnumerateWinnersAppliesLoadOrderPrecedenceAcrossSources`.

## BGS/src/Archive/GameData.cs:GameData.ModLooseIndex - mod root index options (#752 Low)

`BGS/src/Archive/GameData.cs:GameData.ModLooseIndex` lists each mod root with `Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories)`, the legacy overload, which already includes hidden files. The shared `GameData.LooseEnumeration` options from `BGS/src/Archive/GameData.Entries.cs:GameData.LooseEnumeration` change one thing for it: `IgnoreInaccessible = true`. Before, the enumeration stopped at the first unreadable subfolder: files already yielded stayed in the index and the rest of that root was lost. Now only the unreadable subtree is skipped. This half has no regression test, because a test needs an ACL-denied folder.

The `Meshes` gate in `BGS/src/Archive/GameData.cs:GameData.ModLooseIndex` now calls `CaseInsensitivePath` instead of `Directory.Exists`. On a case-sensitive filesystem a mod that ships `meshes` in lower case is no longer skipped. On Windows the checks agree for a `Meshes` folder; a file named `Meshes` is the one difference. `BGS/src/Archive/SubgraphIndex.cs:SubgraphIndex.Discover` resolves its loose AnimTextData folders the same way (see its note below). In `GameData.ModLooseIndex`, enumeration still uses the mod root as given, so a root whose own casing differs from disk is still skipped on Linux.

`BGS/tools/tests/GameDataAssetsTests.cs:GameDataAssetsTests.ModLooseIndexFindsFilesUnderLowercaseMeshesRoot` reads a `.hkx` from a lower-case `meshes` mod folder through `GameData.ReadAnimation`, which reaches `ModLooseIndex`, and expects its bytes. Before the fix it returns null on Linux, where the `Meshes` gate is case-sensitive. The test is not distinguishing on Windows, where `Directory.Exists` already matches `Meshes` case-insensitively, so it passes there with or without the fix.

## BGS/src/Archive/SubgraphIndex.cs:SubgraphIndex.Discover - loose AnimTextData lookup (#752 Low)

`BGS/src/Archive/SubgraphIndex.cs:SubgraphIndex.Discover` reads loose manifests from `Meshes/AnimTextData/AnimationFileData` and `Meshes/AnimTextData/AnimationOffsets` under the data folder and under each mod root. Those three folders used to be tested with a case-sensitive `Directory.Exists`, so on a case-sensitive filesystem a mod that ships `meshes/animtextdata/animationfiledata` in lower case was skipped. Each folder now resolves through `BGS/src/Archive/GameData.cs:GameData.CaseInsensitivePath`, the helper `GameData.ModLooseIndex` already uses, and a name that does not resolve is skipped, and each resolved folder is also checked with `Directory.Exists` after resolving, because the helper returns a path for a file too and `Directory.EnumerateFiles` on a file would throw out of `Discover`. The whole path goes through the helper, so the mod root's own casing is resolved here too.

`BGS/tools/tests/GameDataSharedPathTests.cs:GameDataSharedPathTests.SubgraphIndexFindsLooseManifestUnderLowercaseAnimTextData` builds a mod root whose `meshes/animtextdata` subtree is lower-case (the root folder itself is mixed case) and asserts that the manifest's primary behavior and the offsets entry are found. On Windows the file system is case-insensitive, so the old `Directory.Exists` check also found those folders and the test passes with or without the fix. It fails without the fix only on a case-sensitive filesystem such as Linux.

On a case-sensitive filesystem the helper stops at the deepest folder that exists, so a root holding both `Meshes` and `meshes/animtextdata`, or both `AnimTextData` and `animtextdata`, resolves only the first match; that layout was already unsupported (#752 follow-up).

## BGS/src/Archive/SubgraphIndex.cs:SubgraphIndex.Discover - unreadable loose manifests (#752 Low)

`BGS/src/Archive/SubgraphIndex.cs:SubgraphIndex.LooseManifestFiles` lists the loose AnimationFileData and AnimationOffsets folders with the same top-directory `*.txt` call as before and returns no files when the listing is denied. Before, the `UnauthorizedAccessException` from `Directory.EnumerateFiles` in `BGS/src/Archive/SubgraphIndex.cs:SubgraphIndex.Discover` (its `ScanLoose`) escaped and aborted the whole index. The call is now inside an `IOException` or `UnauthorizedAccessException` catch.

`BGS/tools/tests/GameDataDeniedListingTests.cs:GameDataDeniedListingTests.SubgraphIndexKeepsReadableLooseManifestsWhenAModRootListingIsDenied` denies the current user's list right on one mod root's AnimationFileData (`icacls /deny ...:(RD)` on Windows, mode 0 elsewhere; a failing `icacls` throws) and restores it in `finally` (Unix mode 0700). It returns without asserting if a probe shows the denial did not take effect, so on such a machine it proves nothing. Otherwise `Discover` must not throw (an exception fails the Fact), and it asserts that a manifest and an offsets entry in the readable data folder are found. It fails on the unfixed code with `UnauthorizedAccessException` from `ScanLoose`, and passes with the fix. A denied AnimationOffsets folder and the per-file `FileInfo.Length` guard in the offsets loop have no test.

## BGS/src/Archive/GameData.cs:GameData.Discover - unreadable mod root top level (#752 Low)

`BGS/src/Archive/GameData.cs:GameData.Discover` lists each mod root's top-level `.ba2` files with `Directory.EnumerateFiles(..., SearchOption.TopDirectoryOnly)`. An access-denied listing of one root used to throw `UnauthorizedAccessException` out of `Discover`, so the whole GameData failed to open. That listing is now inside a catch for `UnauthorizedAccessException` and for `IOException` other than `DirectoryNotFoundException` (the siblings' filter minus `DirectoryNotFoundException`, so a missing root still throws): the root stays in `ModRoots` and is still counted by the modded summary in `BGS/app/MainWindow.Chain.cs`, but contributes no archives, so a path only that root's archives override resolves to a lower-priority copy with no warning. Its loose files are still left to the loose paths (`BGS/src/Archive/GameData.cs:GameData.ModLooseIndex`, which uses `LooseEnumeration` from `BGS/src/Archive/GameData.Entries.cs`, and `BGS/src/Archive/GameData.Assets.cs:GameData.WalkLoose`, which skips a folder it cannot list), which skip what they cannot read. The base Data folder listing is not guarded, so an unreadable Data folder still throws, and a missing mod root still throws `DirectoryNotFoundException`. `BGS/src/Archive/GameData.cs:GameData.DiscoverModded` has no listing of its own and calls `Discover`, so it behaves the same way. `BGS/src/Archive/GameData.cs:GameData.ModlistProfile` guards its `<mods>/profiles` listing with a catch for `IOException` and `UnauthorizedAccessException` (the Mo2ScanData filter, so unlike Discover a vanished profiles folder also returns null; see its section below) (#752 Low).

`BGS/tools/tests/GameDataDeniedListingTests.cs:GameDataDeniedListingTests.DiscoverSkipsAModRootWhoseTopLevelListingIsDeniedAndKeepsTheOtherArchives` denies the current user's list right on the first mod root (`icacls`, or mode 0 elsewhere, through the shared `DenyListing` / `RestoreListing` helpers) and runs `Discover` with that root and a readable root holding `Readable.ba2`. It asserts that `Discover` does not throw and that `EnumerateWinners` finds the readable root's archive entry. It returns without asserting if a probe shows the denial did not take effect, so on such a machine it proves nothing. It fails on the unfixed code with `UnauthorizedAccessException` at the top-level listing in `Discover`, and passes with the fix. It does not cover an unreadable Data folder, a missing mod root, or a denied archive inside a root that is listed.

## BGS/src/Archive/GameData.cs:GameData.ModlistProfile - unreadable profiles folder (#752 Low)

`BGS/src/Archive/GameData.cs:GameData.ModlistProfile` picks the first profile (alphabetically, case-insensitive) under `<mods>/profiles` that ships a `modlist.txt`. The `Directory.EnumerateDirectories` call and the LINQ chain are unchanged, but the whole chain now runs inside a catch for `IOException` or `UnauthorizedAccessException`, and a failure returns null (no profile). Before, a denied profiles listing threw `UnauthorizedAccessException` out of `ModlistProfile`, and `BGS/app/MainWindow.Chain.cs:MainWindow.BuildChainCore` (the modded-open try) reported "game data could not be read" and the whole modded open failed. The catch filter is the one `BGS/src/Archive/Mo2ScanData.cs:Mo2ScanData.EnumerateDirectoriesSafe` uses; that helper is private to `Mo2ScanData`, so the filter is repeated here rather than shared. A denied profiles folder now reaches the "no modlist.txt under that folder, or its profiles folder is unreadable" status and a plain `Discover`. A profile whose own `modlist.txt` is denied is not covered here: `DiscoverModded` reads it under a guard that treats an unreadable file as empty (see its section below), and the profile is skipped if `File.Exists` reports it absent.

A null profile with no root `modlist.txt` makes the caller skip the layering and run a plain `GameData.Discover(folder)` with the summary "no modlist.txt under that folder". So a denied profiles folder silently drops the mods rather than failing, and the user sees only that summary line.

`BGS/tools/tests/GameDataDeniedListingTests.cs:GameDataDeniedListingTests.ModlistProfileNamesTheProfileUnlessTheProfilesListingIsDenied` creates `mods/profiles/Default/modlist.txt` and asserts `ModlistProfile` returns "Default" while the folder is readable. It then denies the current user's list right on `profiles` through the shared `DenyListing` / `RestoreListing` helpers (`icacls` on Windows, mode 0 elsewhere) and asserts null, restoring the folder in `finally` before the temp folder is deleted. It returns without asserting if a probe shows the denial did not take effect. It fails on the unfixed code with `UnauthorizedAccessException` from `ModlistProfile`'s profiles listing, and passes with the fix. It does not cover a denied profile folder's own `modlist.txt` or an `IOException` raised mid-enumeration.

## BGS/src/Archive/GameData.cs:GameData.OrderByPlugins - later plugin's archive wins

`BGS/src/Archive/GameData.cs:GameData.OrderByPlugins` now reverses the plugin-matched base archives before the unplaced ones are appended. `ArchivePaths` is highest priority first, so the archive of the last-loaded plugin is now first and wins. Before, the first plugin listed in plugins.txt came first, and its archive won, which is the reverse of the game.

This follows the Radicle load-order rule in `docs/code-notes/GARDEN-TOOLS.md` under "**Load order.**" and "**Precedence.**": INI-listed archives load first, then each plugin's archives in load order, and the last-mounted archive wins.

Archives that match no plugin, such as `Fallout4 - Animations.ba2` when Fallout4.esm is not in plugins.txt, stay after every plugin-matched archive, as the lowest priority. They stand in for INI-listed archives, which load before any plugin archive. The early `return archives;` for an unreadable plugin list is unchanged, so that case keeps the alphabetical order. Mod-root archives are unchanged.

Tests: `BGS/tools/tests/GameDataAssetsTests.cs:GameDataAssetsTests.LaterPluginsBaseArchiveWinsOverEarlierOne` covers A, B and Fallout4 archives with plugins.txt `*A.esm`, `*B.esm`. It expects B's archive to win and `ArchivePaths` to be B, A, Fallout4. `BGS/tools/tests/GameDataPluginOrderTests.cs:PluginListOrdersBaseArchivesWithRadicleRules` expects the new order. The symrm test `BGS/tools/symrm/Tests.cs:GameDataLoadOrderFollowsPlugins` was deleted because it duplicated `PluginListOrdersBaseArchivesWithRadicleRules`.

## BGS/src/Archive/GameData.cs:GameData.DiscoverModded - unreadable modlist.txt (#752 Low)

`BGS/src/Archive/GameData.cs:GameData.DiscoverModded` reads the chosen modlist.txt inside a catch for `UnauthorizedAccessException` and for `IOException` other than `FileNotFoundException` or `DirectoryNotFoundException`. An unreadable file counts as empty: no mod roots come from it, only the overwrite root (when present) is layered, and the archives are those a plain `GameData.Discover` finds. Before, the exception escaped and the modded open failed. A file that vanishes between the `File.Exists` check in `GameData.ModlistPath` and the read still throws.

`BGS/tools/tests/GameDataDeniedListingTests.cs:GameDataDeniedListingTests.DiscoverModdedFallsBackToPlainArchivesWhenTheModlistCannotBeRead` denies the file's read (`DenyReading`) and asserts `ArchivePaths` and `ModRoots` equal a plain `Discover`. `BGS/tools/tests/GameDataAssetsTests.cs:GameDataAssetsTests.ModlistPathMatchesTheFileDiscoverModdedReads` pins `GameData.ModlistPath`: a named profile uses its own modlist, and a missing profile returns null rather than the root modlist, even once a root modlist exists.

## BGS/tools/symrm/Program.cs:Frames - unsupported tally by animation class

The directory branch of `Frames` counts animations the reader does not decode by `HkxAnimationData.AnimationClass`, taken from `HkxBinaryReader.TryReadAnimation`, not by splitting the `NotSupportedException` message. The message now starts with `Unsupported compression (quantized)` or `Unsupported compression (predictive)` for the two compressed classes, so splitting on `.` gave mixed keys. Keying on the class also gives every unsupported class the same raw class name as its key.

## BGS/app/MainWindow.Pose.cs:ClearPose - pose skeleton cleared

`ClearPose` also sets `_poseSkeleton` to null. Its callers all want the pose gone: `BGS/app/MainWindow.Open.cs:Load` and the failure paths in `BGS/app/MainWindow.Playback.cs:LoadPose`. `LoadPose` assigns `_poseSkeleton` before it reads the animation, so without this a later unreadable or unsupported animation left the previous skeleton set with no animation to pose.

## `src/Hkx/NifCollisionDecoder.cs` — hknp collision from a NIF system blob

`BGS/src/Hkx/NifCollisionDecoder.cs:TryDecode(ReadOnlySpan<byte>, NifCollisionAttribution?, out NifCollisionModel?, out string)` reads the raw `bhkPhysicsSystem`/`bhkRagdollSystem` payload that Radicle already hands BGS through `NifFile.Collisions[*].Bytes`; the two-argument overload passes no attribution and keeps every caller that only wants the payload unchanged. It accepts a bare packfile or the same packfile behind a four-byte length prefix, and refuses anything whose first word is neither `0x57E0E057` nor a prefix followed by it.

The blob is read with the existing `PackfileImage` + `PackfileObjects` pair, so its own header supplies the pointer width and layout rules; every member offset is then resolved by name through `HavokClassTypes.Shipped` + `LayoutWalker.Active` at that width. The decoder hardcodes no member offset: every offset it reads comes from the table. Literal offsets appear only in the tests — the matrix translation lane (`+48`) the fixture writes inside `hknpShapeInstance.transform`, the packfile header probe (`0x40 + 0x14`) in the hostile-count test, and the sixteen values `TableOffsetsMatchTheMeasuredCollisionLayout` pins against the table, so a table edit that moved those members would fail a named assertion instead of moving the fixture with it. `PackfileImage` already refuses any layout-rules combination other than reuse-padding off, empty-base-class on, so a blob built with a different packing is rejected rather than read at the wrong offsets.

What the class table proves, and where it comes from: `HavokClassTypes.json` carries `hk_2014.1.0-r1` member names, types and offsets, with instance sizes read out of `Fallout4.exe` 1.10.163 through the `hkClass` initializers the game registers (`HavokClassLayouts.json` records that source). The fields used here, with the table's offsets, are: `hknpPhysicsSystemData.materials@16`, `.bodyCinfos@64`; `hknpBodyCinfo.shape@0`, `.materialId@18`, `.flags@24`, `.position@48`, `.orientation@64`; `hknpMaterial.dynamicFriction@18`, `.staticFriction@20`, `.restitution@22`, `.friction/restitutionCombinePolicy@24/25`; `hknpBSMaterial.uiMaterialCRC@20` behind `hknpBSMaterialProperties.MaterialA@16`; `hknpShape.convexRadius@20`; `hknpConvexPolytopeShape.vertices@48`, `.faces@68`, `.indices@72`; `hknpConvexPolytopeShapeFace.firstIndex@0`/`.numIndices@2`; `hknpShapeInstance.transform@0` (matrix4, translation in floats 12..14), `.scale@64`, `.shape@80`; `hknpCompoundShape.instances@96` as the `hkFreeListArray.elements` header; `hknpCompressedMeshShape.data@96`; `hknpCompressedMeshShapeData.meshTree@16`; `hkcdStaticMeshTreeBase.sections@64`, `.primitives@80`, `.sharedVerticesIndex@96`; the template parent's `.packedVertices@112`, `.sharedVertices@128`; `hkcdStaticTreeTree.domain@16`; `hkcdStaticMeshTreeBaseSection.codecParms@48`, `.firstPackedVertex@72`, `.sharedVertices@76`, `.primitives@80`, `.numPackedVertices@88`, `.numSharedIndices@89`, `.page@92`.

The embedded blob is also asked whether it agrees with that table: `Decode` compares the signature the blob's own `__classnames__` section declares for `hknpPhysicsSystemData`/`hknpRagdollData` and `hknpBodyCinfo` against `HavokClassTypes.Shipped[name].Signature`, and returns a named error on disagreement instead of decoding at the table's offsets. That check only proves class identity: a matching signature is necessary but not sufficient, and a 2014 layout that reordered members while keeping the signature would still pass it. A corpus manifest proves the decode is stable, not correct; only decoded output compared with an independent oracle can settle the layout question.

Bodies are emitted in `bodyCinfos` order. `materialId` indexes both the `hknpMaterial` array (response) and the `hknpBSMaterial` array (CRC), because the class data carries no cross-reference and the two arrays are authored in the same order; a missing index yields the default response and hash 0 rather than a guessed one. Shape identity is the packfile offset of the referenced object, so a shape shared by several bodies is decoded once; indices are assigned in depth-first body order, which is what the tests pin.

Shapes: a sphere's radius is `hknpShape.convexRadius`; a polytope reads its relative vertex/face/index arrays and fan-triangulates each closed face ring; a compound reads the `elements` header inside its `hkFreeListArray` at stride 128, skips free-list slots whose shape pointer is null, and recurses with a depth cap of 8; a compressed mesh reads its data object's embedded tree. Mesh vertices come from two codecs, both read from the SDK layout only for their bit split and parameter order: 32-bit packed vertices use 11/11/10 bits with `codecParms[0..2]` as origin and `[3..5]` as scale, and 64-bit shared vertices use 21/21/22 bits with the tree's AABB minimum as origin and `(max-min)/mask` as scale. Primitive entries are four u8 indices; `b == d` is a custom, external or invalid primitive and is skipped with a note when it is custom or external, `c == d` is one triangle `(a,b,c)`, and otherwise the entry is the quad `(a,b,c)+(a,c,d)`.

The remaining links that the checked-in table does not settle, and which therefore stay assumptions until the corpus run: that this 2014 section stores `firstSharedVertexIndex`, `firstPrimitiveIndex` and `firstDataRunIndex` as plain u32 (three four-byte structs at 76/80/84, which is what the measured size chain from 72 to 88 implies); that a section's primitive count can be taken from the next section's first index (the measured section exposes no count member); that the shared-index block begins at `firstSharedVertexIndex` and the shared vertex array is indexed with `page * 65536 + index`; and that `codecParms` is origin then scale (the 2018 `VertexCodecParameters::load` confirms the order, but FO4's 2014 packer is still unconfirmed). The decoder records a note only for a refused primitive range, a compound free-list hole, a skipped custom/external primitive, or a cap; the section-field readings themselves are not range checks and stay open until the corpus run.

Caps are 4096 bodies and materials, 8192 shapes, recursion depth 8, 1024 sections, and 1M vertices/triangles per shape; a cap is recorded in `NifCollisionModel.Notes`, which is part of the hash, so a capped decode can never collide with an uncapped one. Any exception inside the reader becomes `TryDecode == false` with the exception name in `error`, which is what the truncated and random-byte tests exercise.

## `src/Hkx/NifCollisionModel.cs` — deterministic manifest

`NifCollisionModel.Describe` writes one LF-terminated record per fact: a `layout` line (pointer width, prefix form, system class), `bodies N`, `b` lines (index, shape index, material id, CRC, flags, position, orientation, response), `shapes N`, one `s` line per shape (index, class, kind, radius, vertex/triangle/instance counts), `v`/`t`/`i` lines, then one `note` line per cap or refused range. Floats pass through `HkxNumber.Text`, so the text is the shortest round-trip form at the invariant culture. `Sha256Hex` digests those UTF-8 bytes. Two decodes of the same blob must agree byte for byte (`DescribesTheSameBytesTheSameWay`), and a corpus manifest can be compared across machines.

`BGS/src/Hkx/NifCollisionModel.cs:Attribution` is the one part of the model that does not come from the blob. Radicle's `NifCollisionBlob` already resolves the `bhkNPCollisionObject` that names a system — owner block, `bhkCOFlags` word, the object's `Data` reference, Body ID, `Target` block and the target's node index and name — and the decoder takes those as a `NifCollisionAttribution` argument instead of deriving them, because the 14-byte object layout belongs to the NIF reader (piece A) and a second reading in C# would put two implementations of one layout between `nif.xml` and the export. The argument is nullable by design: a system no object names decodes with `Attribution == null` rather than with zeroes that a consumer could mistake for an owner of block 0, and `flags`, `system_ref`, `body_id` and the target stay at the reader's "none" values for exactly the same reason. Because attribution can differ for identical bytes, `Describe` and `Sha256Hex` deliberately exclude it: the digest identifies the decoded payload, so two NIFs naming one system share a hash, and the corpus manifest records the attribution columns beside it instead of inside it.

## `tools/tests/CollisionPackfile.cs` and `NifCollisionFixture.cs` — synthetic system blob

`CollisionPackfile` builds a real packfile: two sections, `__data__` and `__classnames__`, an object per class with a virtual fixup to a signature-plus-name entry, local fixups for array and pointer members, and 16-byte-aligned payloads. Every size, member offset and signature comes from `HavokClassTypes` + `LayoutWalker`, so the fixture cannot drift from the table the decoder reads. `NifCollisionFixture.Build` lays out one `hknpPhysicsSystemData`, one `hknpBSMaterialProperties` with two CRCs, two `hknpMaterial`s, three bodies (sphere, compound, compressed mesh), a polytope inside the compound, and a one-section mesh tree with three packed and one shared vertex. It also exposes `Prefix`, the same bytes behind four zero bytes, for the prefix path.

## `tools/tests/NifCollisionDecoderTests.cs` — decoder contract and vanilla manifest

The tests cover bodies/materials/hashes/shapes on the synthetic blob, the measured offsets as literals, `hknpDynamicCompoundShape`, a compound free-list hole, determinism of `Describe`/`Sha256Hex`, the four-byte prefix, refusal of zeroed/non-packfile blobs, refusal when the system class is renamed away, refusal when the system signature disagrees with the table, every truncation of the fixture (byte by byte, plus 64 tail cuts) through `TryDecode` without an escaping exception (with an error string whenever it refuses), and 200 seeded random blobs with only the magic planted. `VanillaNifCollisionBlobsDecodeToStableHashes` is the Radicle-to-BGS path: with `FO4_NIF_DIR` set it reads every `*.nif`, takes each `NifFile.Collisions[*].Bytes`, decodes it, and writes `nif-collision-manifest.txt` (relative path, block, system class, body/shape counts, hash) sorted; it fails if any blob fails to decode and compares the file with a committed `fixtures/nif-collision/manifest.txt` when one exists. Without `FO4_NIF_DIR` a custom `NifCorpusFact` attribute reports the test as skipped, not passed, so a machine with no game data cannot show a green corpus check; with data it writes the manifest and compares a committed one when present. No manifest is committed yet, so the hash half stays unenforced until the first data run.

## `tools/export/Program.cs` — the export CLI

`Program.Run(args, output, error)` is the whole command surface: `--out <dir>` plus file inputs, `--batch` to walk directories for `*.hkx` and `*.nif`, and `--schema-only` to write `schema.json`. It parses arguments itself, prints the usage text on `--help` or on a missing `--out` (an `--out` followed by another option is refused the same way, `BGS/tools/export/Program.cs:Program.Run`, test `BGS/tools/tests/ExportRecoveryTests.cs:ExportRecoveryTests.AnOptionCannotBeConsumedAsTheOutValue`), and returns 0 when every input exported; 2 when an input failed or when a run's output directory cannot be created (that row is reported on the log only, because `errors.jsonl` cannot be written); 1 for usage errors and for `--schema-only` output failures. `NotSupportedException` and `SecurityException` from the output path are handled rather than escaping the process, and `BGS/tools/export/ExportRunner.cs:TryPrepareOutput` records the `output directory` row and returns 2; the `index.json` and `errors.jsonl` writes still throw and reach the exit-1 path, so the no-throw guarantee covers the output-directory case only. The CLI is the only entry point; every decision lives in `ExportRunner`.

## `tools/export/ExportRunner.cs` — what one run writes

`ExportRunner.Run` collects inputs, sorts them by full path with `StringComparer.Ordinal`, and exports one at a time so the output cannot depend on directory enumeration order. Batch roots go through `ExportRunner.Collect`, which catches a failing walk, keeps the files it already found, and records one row whose `source` is that directory, so an unreadable subtree can no longer end the run with no rows. Per file it records the input SHA-256, then dispatches on extension: an `.hkx` is read once through `PackfileImage.Read` + `PackfileObjects`, and the classes present decide which documents are attempted — `hkaSkeleton` gives a skeleton, any `HkxAnimationData.DecodedAnimationClasses` member gives an animation, and `hkbBehaviorGraph` or `hkbStateMachine` gives a behaviour graph. A `.nif` is read by `BGS/tools/export/ExportRunner.cs:ExportNif` through Radicle's `NifFile.FromBytes`, whose `Collisions` list already carries the `bhkNPCollisionObject` that names each system; that object becomes a `NifCollisionAttribution`, and every payload is decoded by `NifCollisionDecoder.TryDecode`, reusing piece B rather than reimplementing it. The switch replaces this tool's own `NifFile.Parse` + `NifPhysics.Read` pair: one parse now supplies both the payload and its owner, so the 14-byte object layout stays in Radicle and no C# copy of it exists to drift, and `NifPhysics.Read` keeps its other caller, `NifEmbeddedPhysicsProofTests`, untouched. The one behaviour difference is version reach: BGS's container reader accepted any `BSVersion >= 130`, while Radicle's NIF reader accepts Fallout 4's 20.2.0.7 / user version 12 / BSVersion 130 only, so a later game's NIF becomes an error row instead of a document — no such NIF is a supported input here, and the collision decoder would refuse those systems anyway. Each kind runs through `BGS/tools/export/ExportRunner.cs:AttemptKind`, which catches every non-cancellation exception: a failure becomes an error row prefixed with the kind and the remaining kinds still run. `BGS/tools/export/ExportRunner.cs:ExportCollisionBlobs` decodes and writes one collision blob at a time, so a NIF holding several systems exports the good ones and reports only the blobs that fail. A file that yields nothing raises instead of being skipped, and `ExportOne` catches every failure that is not cancellation for that input, so a defect in one reader cannot end a batch.

Targets are `<kind>/<stem>.<kind>.json` under `--out`, where the stem is the input path below the batch root, so two inputs cannot silently overwrite each other (`_targets` refuses a repeated target). `index.json` lists every document this run wrote, sorted by target, with `kind`, `source`, `target`, `inputSha256`, `sha256` and `bytes`; `errors.jsonl` holds one single-line JSON object per failure, sorted by source then message and written by the `ExportJson` class (`BGS/tools/export/ExportJson.cs:ExportJson`) in its `singleLine` constructor mode like every other document, and is deleted when a run has none. The output directory is never pruned, so `index.json` describes the current run rather than the directory contents.

Byte-identical output rests on three things: `StringComparer.Ordinal` sorting everywhere order is not already structural, `ExportJson` as the only serializer, and `HkxNumber.Text` for every float, which is the same shortest-round-trip text the collision record hashes.

## `tools/export/GraphPlan.cs` — the native plan

`GraphPlan.Build(BehaviourGraphModel)` walks the model that `NativeXml.From(PackfileObjects, PackfileImage)` produced and `BehaviourGraphModel.Parse` read; no Java tool, converted XML or external process is involved. It reads `hkbBehaviorGraphStringData` for event and variable names, takes `hkbBehaviorGraph.rootGenerator` as the entry point, and resolves generators with an ancestor set, so a generator shared by two parents is expanded in full under each of them: clips, reference poses, behaviour references, BGS sequences, state machines, blenders and pose matchers, manual selectors, layers, and the pass-through classes (`hkbModifierGenerator`, `BSiStateTaggingGenerator`, `BSCyclicBlendTransitionGenerator`, `DynamicAnimationTaggingGenerator`, `BSOffsetAnimationGenerator`, `BSBehaviorGraphSwapGenerator`, `BSBoneSwitchGenerator`). A generator met again only while it is still being resolved above it is a true cycle and becomes a childless `shared` node with a detail string, bumping `revisitedGenerators`; stubbing a legitimate second occurrence is what would leave a consumer unable to reconstruct that branch, so a cycle is the only reason to stop expanding a subtree that is still reachable. A state machine object met twice is written once: `_machinesById` turns the second occurrence into a `machine` node carrying the existing index, and consumers follow `machines[index]`; that lookup runs before the budget check, so a reference to a machine already in `machines[]` keeps its index even after truncation instead of becoming a stub that orphans it. The same lookup runs inside the cycle guard of `BGS/tools/export/GraphPlan.cs:Resolve`, because a machine is registered in `machines[]` before its states resolve: a state machine that closes an ancestor cycle is therefore a `machine` reference whose detail says the reference closes a cycle, not a `shared` stub, and a consumer can follow the recursion through the written machine. Only a non-machine cycle becomes a `shared` stub and bumps `revisitedGenerators`, so that count stays equal to the number of shared cycle stubs the document holds rather than counting references that lose nothing. `MaximumResolvedNodes` bounds the re-expansion at 200,000 expanded nodes; past it the plan stops expanding, returns a `shared` stub for each node it does not expand, adds one to the `plan-node-budget` gap per stub, and `GraphExport` repeats that count in the document's `notes`, so a short plan is measurable rather than merely present. `HkObject.Int` in `BGS/src/Hkx/BehaviourGraphModel.cs` deliberately keeps parsing graph scalars with the current culture, because `FieldRender` formats those scalars with the current culture too (`Narrow`, the enum `printed.ToString()`, `TYPE_INT64`/`TYPE_ULONG`, `Shortest`): the two sides are written and read on one machine, so they agree by construction, and moving either side to `CultureInfo.InvariantCulture` alone breaks a negative value on a locale whose minus sign is not ASCII `-`. A locale-independent reader has to move with an invariant writer and every other reader of rendered text (`GraphRun`, `GraphValidator`, `StateEditor`, `BindingEditor`, `NativeAuthoring`, `NativeSave`, `PredefinedTemplates`, `ElementSummary`, `BlendWeights`, `HkxTextEdit`) as one testable change. State machines append to `Machines` before their states resolve, so `RootMachine` is an index into that list and nested machines keep discovery order. Transitions come from each state's `transitions` rows plus the machine's `wildcardTransitions`, with the event name from the string table, the condition text from `expression`, `conditionString` or the class name, and the duration from the transition effect. `BGS/tools/export/GraphPlan.cs:StatesWithGenerator` counts a state only when its generator is a real generator, so an unmapped placeholder or a `shared` stub is not counted. A blend child or selector child whose generator reference does not resolve becomes a `BGS/tools/export/GraphPlan.cs:NoteMissingChild` gap row with a warning instead of disappearing from its parent, and a state generator that does not resolve keeps its `missingGenerators` count.

The plan carries the fields the consumer contract expects: node kind, class, name, animation name, detail, speed, weight, machine index and children; state id, name and generator; transition endpoints, wildcard flag, event id and name, condition and duration; machine name and start state; event and variable names; the gap table; unmapped generator counts; warnings; and the stats counts. Gap keys are `blend-branch-not-wired`, `blend-weight-ignored`, `generator-unmapped`, `generator-reference-unresolved` (which also counts a blend or selector child that did not resolve, warns at most five times so dangling children cannot crowd out later warnings, and keeps the gap count uncapped) and `plan-node-budget`, with counts and an explanation each. `BGS/tools/export/GraphPlan.cs:NoteMissingChild` names the target the child referenced; for a blend child whose wrapper block is absent it names the wrapper id instead, so the warning points at what the file failed to contain. They describe what a consumer would drop, not what this tool failed to read: the plan itself keeps every branch, weight and generator it resolved.

## `tools/export/CollisionExport.cs` — the attributed collision document

`BGS/tools/export/CollisionExport.cs:Json` writes the decoded payload as before and, between `systemClass` and `bodies`, an `attribution` object with the owner block, the `bhkCOFlags` word, the object's `Data` reference, Body ID, `Target` block, target node index and target node name, or `null` when no collision object named the system. The null is deliberate: zeroes would let a consumer read "owned by block 0, body 0" where the file says nothing, and every field the reader leaves at "none" would arrive as a real-looking value. Attribution is stated once per document rather than repeated on every body and shape, because this document holds exactly one system: every shape in it belongs to the system the object named. `flags` is copied as stored and never renamed into bits, because whether a bit disables a body is a consumer decision, not a reading decision. The document's `sha256` still hashes only the decoder's record, so two NIFs naming one system share a hash and differ only in `attribution`; a consumer comparing payloads across files can therefore ignore which object named them.

## `tools/export/ExportJson.cs`, `ExportFormats.cs` and `SchemaDocument.cs` — the document contract

`ExportJson` is the only writer: two-space indentation, LF, ASCII-only strings with every character outside printable ASCII escaped, `HkxNumber.Text` floats, and an exception rather than `null` or `0` for a non-finite value. `ExportFormats` holds the format names and versions, all at version 1. `BGS/tools/export/SchemaDocument.cs:Json` writes `schema.json`, which lists every format's fields with a type and a note, including `bgs.schema` itself and `bgs.export-errors`, so every document the tool writes is described by its own file; `SchemaDocumentsEveryTopLevelFieldOfEveryWrittenDocument` fails if a document grows a top-level field the schema does not describe. The `singleLine` constructor of `BGS/tools/export/ExportJson.cs:ExportJson` drops the pretty-print separators, which `errors.jsonl` uses, so its rows share the document writer's escaping and stay one object per line.

## `tools/tests/ExportCorpusTests.cs` and the rest of the export suite

`EveryManifestFileIsAttemptedAndTwoRunsAreByteIdentical` reads `app.Config/manifest.json`, asserts the cached and checked-in copies agree, exports `fixtures/vanilla` twice into separate temporary directories, compares every output byte, and then requires every manifest path to be either exported or reported in `errors.jsonl`, with `inputSha256` equal to the manifest's SHA-256. Files whose manifest fidelity is `byte` must export at least one document; a kind that fails on such a file is still reported as a row, and the `structural` and `refused` entries may be reported instead, because those are the ones BGS's save path already treats as special. `CharacterSkeletonExportsExactlyNinetyFiveBones` asserts the exported `CharacterAssets/skeleton.hkx` skeleton has 95 bones and matches `HkxBinaryReader.ReadSkeleton` bone by bone; the number itself is corroborated by `SkeletonMapperAuthorTests` on the same fixture, not measured by this suite. `SplineAnimationExportsDecodedFramesEventsAndBinding` compares the exported frames, duration, events, skeleton name and track mapping against `HkxBinaryReader.ReadAnimation` for `TurnLeft90.hkx`, so the test proves the export carries what the decoder produced, not that the decoder is right about vanilla data. `ExportGraphPlanTests` builds the plan from a hand-written XML graph and pins states, transitions, weights, gaps and stats without any game data, and separately pins that a clip shared by two blend children keeps its animation name in both occurrences, that a shared state machine is written once and referenced by index, that a non-machine generator which is its own ancestor becomes one `shared` stub instead of recursing while a state machine that closes a cycle keeps the index of the copy already written (`MachineCycleReferenceCarriesItsWrittenIndex`), that a plan truncated at the node budget still carries the index of a machine it already wrote and counts every stub in the gap, and that the truncation count reaches the document's `notes`. `ExportCollisionTests` decodes the synthetic packfile from `NifCollisionFixture`, writes the document twice and compares it, checks that a supplied attribution reaches `BGS/tools/export/CollisionExport.cs:Json` field by field while an unattributed decode writes `null` (`AttributionReachesTheDocumentOnlyWhenAnObjectNamedTheSystem`), and checks that a NIF without a collision system, unreadable bytes and a missing path all become error rows rather than exceptions. The NIF-without-a-system row now comes from Radicle's reader, so that fact also covers the tool's new NIF entry point; it does not prove the sample would be read by the native library on a machine where the library is missing, which would surface as a row naming the load failure. A `byte` file whose classes are none of the four exported kinds (the radroach character data file) is reported instead of exported, because the export tool never claims to read that kind. The recovery tests pin the export gaps: `AnOutputDirectoryThatCannotBeCreatedIsAnErrorRowAndExitCode`, `ANonInputFailureInOneKindStillRunsTheNextKind`, `AMultiSystemNifExportsItsGoodBlobsAndReportsTheBadOne`, `ErrorRowsUseTheDocumentWriterAndStayOnOneLine`, `SchemaListsItselfAndEveryDocumentFormat`, `AStateWhoseGeneratorIsASharedStubIsNotCounted`, `ADanglingBlendChildBecomesAGapRow` and `ADanglingSelectorChildBecomesAGapRow`.

What these tests do not establish: that any of it compiles or passes (nothing was built or run), that the decoded PSD layout matches Fallout 4's own classes (piece B's class table is measured, but no RE session re-read it and no vanilla collision blob has been decoded here), that the 95-bone count holds the day the suite runs (no file was counted here), and that the spline math is right (the export test only shows it carries the decoder's frames).
`BGS/tools/tests/NifCollisionDecoderTests.cs` covers bodies/materials/hashes/shapes on the synthetic blob, the measured offsets as literals, `hknpDynamicCompoundShape`, a compound free-list hole, determinism of `Describe`/`Sha256Hex`, attribution passed in and returned unchanged with the digest unaffected by it (`KeepsTheAttributionItsCallerSupplies`), the four-byte prefix, refusal of zeroed/non-packfile blobs, refusal when the system class is renamed away, refusal when the system signature disagrees with the table, every truncation of the fixture (byte by byte, plus 64 tail cuts) through `TryDecode` without an escaping exception (with an error string whenever it refuses), and 200 seeded random blobs with only the magic planted. `VanillaNifCollisionBlobsDecodeToStableHashes` is the Radicle-to-BGS path: with `FO4_NIF_DIR` set it reads every `*.nif`, takes each `NifFile.Collisions[*]`, hands its owner flags, system reference, Body ID and target to the decoder, and writes `nif-collision-manifest.txt` (relative path, block, system class, body/shape counts, hash, then owner block, flags, system reference, Body ID, target block and target node) sorted; it fails if any blob fails to decode and compares the file with a committed `fixtures/nif-collision/manifest.txt` when one exists. The attribution columns are there so the first run on installed data answers the 14-byte `bhkNPCollisionObject` question from a record rather than from a failing assertion. Without `FO4_NIF_DIR` a custom `NifCorpusFact` attribute reports the test as skipped, not passed, so a machine with no game data cannot show a green corpus check; with data it writes the manifest and compares a committed one when present. No manifest is committed yet, so the hash and attribution halves stay unenforced until the first data run.
## BGS/app/MainWindow.Open.cs:LoadDocument - read-only only after the new document loads

`LoadDocument` returns true once it commits the new document, after the discard prompt and the source stamp. Every earlier return (empty path, not found, refused file, a discard the user cancelled, an unreadable stamp) returns false. `Open` and `Load` pass that result back. The archive callers `BGS/app/MainWindow.Index.cs:OpenIndexEntry` and `BGS/app/MainWindow.Pose.cs:OpenArchiveEntry` set `_readOnly` and print "Opened ... read only" only when it returns true. Before, they did this after any return, so a cancelled discard left the still-loaded, possibly edited document read-only and Save refused it. The two returns after the commit point (the non-8-byte pointer layout and a root that did not resolve) return true, because the archive copy is already the open document and must still be read-only. Regression test: `BGS/tools/uismoke/Smoke.PackedAnimation.cs:CancelledArchiveOpenKeepsTheEditedDocumentWritable` (edit, cancelled archive open, then Save still writes the edit).

## BGS/tools/symrm/MapperCli.cs:MapperOne - overwriting the source keeps a backup

When `--out` names the file that was read, `MapperOne` writes with `FileSafety.ReplaceChecked` and the hash of the bytes that were read, the same as `BGS/tools/symrm/RagdollCli.cs:RagdollOne`. That keeps the previous version as `<name>.bak` and refuses the write if the file changed on disk after it was read. Before, `FileSafety.Replace` overwrote the source with no backup and no changed-on-disk check. Regression test: `BGS/tools/tests/MapperCliTests.cs:MapperCliTests.OverwritingTheSourceWithForceKeepsTheOriginalAsABackup`.

## `tools/export/GraphPlan.cs:GraphPlan.ResolveOnce` — plan depth limit (deep chains overflowed the stack)

`Resolve` and `ResolveOnce` recurse through blenders, pass-throughs, selectors and state machines, and the only bound was the 200k node budget, which counts nodes and not depth. A chain of a few thousand nested generators overflowed the thread stack. A stack overflow cannot be caught, so it killed the whole batch, although `ExportRunner.ExportOne` promises that one failure never stops the run. `MaximumResolveDepth` (256) now caps that recursion. The depth is `path.Count`, because `path` holds exactly the ancestors on the stack. Past the cap a generator becomes a `Shared` stub with detail `plan depth limit reached`, counted under the gap `plan-depth-limit`, with one warning per plan. A machine that was already written still comes back as a reference past the cap, the same as with the budget. Regression test: `BGS/tools/tests/ExportGraphPlanTests.cs:ExportGraphPlanTests.ADeepGeneratorChainStopsAtTheDepthLimitAsAGap`, a 5000-level chain that must complete and report the gap. That test parses the written document with `JsonDocumentOptions.MaxDepth` 1024, because the default 64 is far below the depth a 256-level plan writes.

## `tools/export/GraphExport.cs:GraphExport.Node` — bounded by the plan, not capped here

`Node` recurses through `PlanNode.Children`. Only `GraphPlan.Resolve`, `WrappedNode` and `SelectorNode` build those children, and each level is one `Resolve` call, so the depth cap in `GraphPlan.ResolveOnce` bounds this recursion too. A second cap here could never fire. The chain test in `BGS/tools/tests/ExportGraphPlanTests.cs` runs `GraphExport.Json` over the capped plan, which exercises this recursion on the deepest tree the plan can hold.

## `src/Hkx/NifCollisionDecoder.cs:ShapeReader.Section` — section index validation

`Section` read `firstPackedVertex` and `sharedVertices` as `int` and computed `firstPacked + i` and `(firstShared + i) * 2` in `int`. A field of 2^30 wrapped `* 4` to 0, so the decoder read the first packed vertices as if they were this section's own. The packed range is now checked against `packed.Count` in `long` arithmetic before any read. A range that does not fit refuses the section with the note `section N packed vertex range not decoded`, the same way an unvalidated primitive range is refused. Each shared slot is checked against `sharedIndex.Count` in `long` before it is read, and a slot past the end stops the shared loop, as a failed read already does. Regression test: `BGS/tools/tests/NifCollisionDecoderTests.cs:NifCollisionDecoderTests.RefusesSectionIndexesWhoseSlotArithmeticWraps`, which sets `firstPackedVertex` to 2^30 (refused, zero vertices) and `sharedVertices` to 0x7FFFFFFF (the three packed vertices, no shared one) in the synthetic mesh from `BGS/tools/tests/NifCollisionFixture.cs:NifCollisionFixture.BuildWithMeshSectionField`. 2^30 is not used for `sharedVertices`: `(2^30) * 2` wraps to `int.MinValue`, the slot falls outside the file, and the case passed without the guard. 0x7FFFFFFF wraps `* 2` to -2, which reads the zero padding before `sharedVerticesIndex` as index 0 and adds one shared vertex without the guard.

## `src/Hkx/NifCollisionDecoder.cs:ShapeReader.Compound` — total instance cap

The 8192-instance limit applied to each compound, not to the decode. Up to 8192 compounds of 8192 instances each, about 67 million records with their transform and scale arrays, were allowed. `ShapeReader` now counts instances across one decode and stops at `MaxInstances` (65536), recording `instances capped at 65536` in `NifCollisionModel.Notes`. Like the other caps, that note is part of the manifest hash, so a capped decode cannot collide with an uncapped one.

## `BGS/src/Nif/NodeTree.cs:NodeTree.Resolve` — skin bones outside the Havok skeleton

A skin bone that the Havok skeleton does not name is looked up in the NIF node trees, in order: `skeleton.nif` (the skeleton's own NIF, added by `BGS/app/MainWindow.Skinning.cs:MeshNodeTree`) and then the body mesh. From the named node the walk goes up through parents until a node names a skeleton bone, and the local transforms passed on the way are kept as `Link.LocalChain`, so the bone is posed with the offset its NIF node carries. A parent cycle stops after one pass over the tree. Only when neither tree yields a skeleton bone does `Resolve` fall back to `SkinnedMesh.ResolveSkinBoneName` (`X_skin` to `X`) with an identity chain, reported as the source `interim approximation`. That fallback places the bone where dev's helper approximation did.

## `BGS/app/MainWindow.Pose.cs:ApproximateHelperBoneInterim` — kept for its tests and the corpus baseline

`ApproximateHelperBoneInterim` is not deleted. `BGS/tools/tests/Viewport3DTests.cs:SkinBoneNamesResolveAgainstTheSkeleton` pins its rename, and `BGS/tools/tests/SkinCorpusTests.cs:ReferencePoseCorpusComparesActualInterimAndNodeResolver` uses it as the baseline the resolver is measured against. `BGS/app/MainWindow.Pose.cs:LoadMesh` no longer calls it, because renaming a helper bone first would hide its NIF node offset from the resolver.

## `BGS/app/MainWindow.Skinning.cs:RebindMesh` — a mesh follows the skeleton it is shown on

A mesh chosen before any skeleton exists is kept as `_pendingSkinMesh` and drawn when an animation supplies one, from `BGS/app/MainWindow.Playback.cs:LoadPoseBody`. When an animation with a skeleton opens, `RebindMesh` rebinds every loaded shape against it, so a binding never outlives the skeleton it was resolved against. `BGS/app/MainWindow.Pose.cs:ClearMesh` drops the pending mesh, so choosing no mesh, opening a document and a successful load all cancel it.

## `BGS/app/MainWindow.Playback.cs:SkeletonBytesOrNull` — Radicle native-load failures stay visible

Dev's `SkeletonBytesOrNull` (#700) swallows only what `GameData.IsSkippableArchiveFailure` accepts. That predicate lets a Radicle native-load failure through, so `LoadPose` reports it. `BGS/tools/tests/Ba2RadicleTests.cs:PlaybackSkeletonLookupReportsNativeLoadFailureButSkipsCorruptArchives` pins that split. Catching `InvalidDataException` directly, as the salvaged `ArchiveSkeleton` did, would hide the native-load failure, so dev's helper stays.


## `BGS/src/Hkx/RootMotion.cs:RootMotion.Read` — provenance and frame kinds

Provenance: Fallout4.exe 1.10.163, GT #520 comments 16593 and 16778 (RE-025). Default reference frames with frameType 0 contain 16-byte samples: xyz translation and w, an unwrapped angle in radians about up, sampled at 30 Hz. Queries clamp and interpolate components; positive w turns left. `RootMotion.Read` accepts frameType 0 and 1, and only on the class `hkaDefaultAnimatedReferenceFrame`: all 15,193 shipped default frames store 0, and tag 1 is the same DEFAULT payload under the other enum name, so refusing it would make trim, loop and save read such a frame as no motion at all, while frameType 2 (PARAMETRIC), every other tag and every other reference-frame class are refused and named in the unsupported reason as `<class>, frameType <n>`. No windowed/blended query is inferred.

## `BGS/app/MainWindow.RootMotion.cs:UpdateRootMotionView` — placement, camera and loop

Playback places the pose with the sampled translation and rotation. Follow travel OFF keeps the camera fixed; ON offsets its centre by the sampled translation, holding travel in the viewport without changing world placement. Loop queries restart the clip rather than accumulating its transform; during playback with Follow travel OFF a loop offset, not the base centre, is moved backwards by the clip travel on each wrap so the pose does not jump in the viewport, and Stop, a scrub, a pause and a fresh open clear that offset back to the base centre (Stop calls `ResetRootTravelLoop` and `ResetRootMotionView` clears it on every open). The ground overlay shows the path, one-second ticks and the final facing arrow.

## `BGS/tools/symrm/MotionCli.cs:MotionData` — archive census

Installed archive census through GameData/Ba2: 13,990 animation entries, 13,162 carrying samples, 828 without samples, zero unsupported or unreadable. Three carrying clips exceed the preview bound of 1,000,000 units: Character/Animations/Weapon/Pistol/Stand_to_Run_L180.hkx (131,548,976 units), Character/Animations/Stand_to_Run_L180.hkx and its Weapon/GripRifleStraight variant (78,007,424 each). The next largest distance is Vertibird/Animations/TakeoffUntrained.hkx at 7,544.326 units. Final sample angles are reported unwrapped in degrees, ranging from -372.605 to 360.000; multi-turn angles are valid.

## `BGS/tools/symrm/MotionCli.cs:DumpMotionFrame` — the pistol outlier

The pistol outlier is stored data, not a wrong pointer or stride: its default frame is at data-section offset 0x9ed0, array header 0x9f18 has a local fixup to 0x9f30 (frame + 0x60), and its 31 contiguous vec4 samples end exactly at the next object, hkaAnimationBinding at 0xa120. Sample 1 raw bytes are B57384485DF8184A8062F6CA00000000; xyz decode to 271261.66, 2506263.2, -8073536. Sample 6 z is 17265380 while w advances smoothly to pi. These values genuinely reside in the reference-frame payload. How the game treats this broken-looking content is not established here.

## `BGS/app/MainWindow.RootMotion.cs:ResetRootMotionView` — camera framing limit

With Follow travel OFF the camera centre is the one captured when the file opened (the frame-0 pose centre) and the fit is that pose's radius, so framing comes from the frame-0 pose alone: a clip whose path travels more than a few body radii walks out of the viewport, and the ground path with its ticks is drawn beyond the framing. `SkeletonView` has no fit-to-arbitrary-bounds helper - `SkeletonView.Frame()` reframes on the current pose, the mesh or the measured bodies - so widening the fit to the path bounds needs a new helper; until then the fixed camera is the manual answer and the drawn path shows where the clip goes.

## `BGS/src/Hkx/RootMotion.cs:RootMotion.Motion` — invalid travel

Raw distance and samples remain reportable. Non-finite samples/metadata or total travel above 1,000,000 units show `root motion looks invalid (x units)`, are neither applied to the pose/camera nor drawn, and are counted separately (within the carrying total) by the corpus report. This is a viewer sanity bound, not a claimed game limit.

## `BGS/src/Hkx/AnimationEdit.cs:TrimMotion` — known gap: refused frames are not trimmed or retimed

A frame that `RootMotion.Read` refuses (frameType 2, or a reference-frame class other than the default) reports `Any` false. `BGS/src/Hkx/AnimationEdit.cs:TrimMotion` and `RetimeMotion` return such a motion unchanged, and the `replacingMotion` test in `BGS/src/Hkx/NativeAnimation.cs:Interleave` and `BGS/src/Hkx/NativeAnimation.cs:Recompress` then keeps the original reference frame rather than rewriting it. No bytes are lost, but a trimmed or retimed clip keeps its untrimmed root motion, so the root travel no longer lines up with its frames. `Loop` leaves root motion as it is by design. The census above found no shipped clip in that state; a modded clip can be. Not guarded in this change.

## `BGS/tools/tests/RootMotionTests.cs` — the regression each test detects

- `RootMotionTests.cs:InterpolatesComponentsAndAccumulatesCurvedDistance` — component interpolation, curved (summed) distance and the unwrapped turn.
- `RootMotionTests.cs:QueriesClamp` — queries outside the clip clamp to its ends.
- `RootMotionTests.cs:ExplicitLoopQueriesWrapWithoutAccumulatingPastCycles` — loop queries wrap instead of accumulating cycles.
- `RootMotionTests.cs:PositiveAngleTurnsForwardLeftAboutUp` — the sign of a positive turn (left about up).
- `RootMotionTests.cs:SyntheticDefaultFrameParsesThenPlacesWithoutMutatingPose` — a parsed default frame places bones without changing the source pose.
- `RootMotionTests.cs:FrameKindsHaveExplicitRefusals` — only frameType 0 and 1 of the default class are read; every other kind is refused with its reason.
- `RootMotionTests.cs:NullExtractedPointerDoesNotBorrowAnUnrelatedFrame` — a null `extractedMotion` pointer does not pick up an unrelated frame object.
- `RootMotionTests.cs:CommittedTurningFixtureHasPositiveAngle` — the committed RadRoach turn clip reads as a positive turn.
- `RootMotionTests.cs:InvalidTravelIsRetainedButNeverPlaced` — travel above 1,000,000 units, infinite or NaN is kept but never placed.
- `RootMotionTests.cs:FiniteTravelDoesNotHideNonFiniteAngle` — a finite distance does not hide a NaN angle from the previewable check.
- `RootMotionTests.cs:MetricsFollowSamplesAddedAfterAQuery` — the cached distance is recomputed after samples are added.
- `RootMotionTests.cs:MultiTurnAnglesStayUnwrapped` — multi-turn angles stay unwrapped.

## `BGS/tools/tests/NodeTreeTests.cs` — the regression each test detects

- `NodeTreeTests.cs:ResolverUsesNearestExactAncestorAndOrderedSources` — a farther ancestor, or the body tree ranked above `skeleton.nif`, would move a helper bone.
- `NodeTreeTests.cs:DisconnectedAndCyclicTreesFallBackWithoutChangingExactBind` — a cyclic parent chain must end, and an unresolved name must fall back without changing the exact-name bind.
- `NodeTreeTests.cs:VanillaMaleRestPoseReproducesVerticesAfterOutputPlacementRemoval` — on the vanilla male body the reference pose reproduces the mesh vertices; needs `BGS_SKIN_DATA`.

## `BGS/tools/tests/SkinCorpusTests.cs:ReferencePoseCorpusComparesActualInterimAndNodeResolver` — resolver accuracy gate

Compares the interim rename and the node resolver against the vanilla rest pose over 55 files. It asserts that exactly 55 files were measured, that every compared vertex error is finite, and that the resolver never leaves more bones unmatched than the exact bind; it writes each file's interim and resolver errors to test output, and does not assert a threshold on them. Needs `BGS_SKIN_DATA`. `BGS/tools/tests/TestSupport.cs:SkinDataFactAttribute` marks it and the vanilla `NodeTreeTests` case as skipped, not passed, when that variable is unset.

## `BGS/tools/uismoke/Smoke.RootMotion.cs:RootMotionSmoke` and `RootMotionLoopSmoke` — UI regressions

`RootMotionSmoke` detects Follow travel OFF moving the camera, Follow travel OFF leaving the placed skeleton still, Follow travel ON changing the placed skeleton, Follow travel ON not holding the travel in the viewport, and the camera not returning at frame 0. `RootMotionLoopSmoke` detects playback across the loop wrap not moving the fixed camera, and a scrub to frame 0 not clearing the loop offset. Both run in the ragdoll lifecycle smoke runner (`BGS/tools/uismoke/RagdollLifecycleSmoke.cs:LifecycleSmoke`).

## `BGS/tools/uismoke/Smoke.Skinning.cs:SkinningSmoke` — skin binding through the UI

Detects a mesh chosen before an animation failing to bind when the skeleton arrives, a mesh that does not rebind when the animation changes, and a malformed `skeleton.nif` failing to fall back to the body tree. Returns 0 without `BGS_SKIN_DATA`, as the smoke runner has no skip state.
## BGS/src/Hkx/QuantizedAnimationDecoder.cs — quantized sample decoding (#683)

Provenance: Fallout4.exe 1.10.163; GT #520 comment 16773; RE-026 README game-decoder runtime section 1 and independent review items 2–6, 13. The review corrects the earlier table: w9 is static float tracks, w10+w12 dynamic transform scalars, w11 dynamic rotations, w13 dynamic float tracks; w18 is offsets and w19 scales. Only the sums w6+w8 and w10+w12 are consumed, so their unresolved individual meanings are not guessed. Packed rotations use the eight independently executed spare-bit patterns. All declared nonempty regions must fit without overlapping; scatter destinations must be unique, in range and fully define the meaningful pose components. Omitted defaults/reference reconstruction and nonzero endian are refused. The header duration determines uniform sample timing.

The packfile reader dispatches into the same HkxTrackData lists as spline playback; FloatTracks also reach AnimationPose.Pose.Floats. The existing Animation tab, Playback and symrm readers consume that shared path without a separate codec branch. Synthetic packfile fixtures and test-only inverse value packing exercise parsing, static/dynamic translation/rotation/scale, float tracks, pose construction and PlaybackSession. Primitive tests additionally cover random quaternion/scalar quantization round trips. This is application-path evidence, not game-runtime verification.

Runtime-only unverified: no shipped file uses this codec; a hand-built file carrying every bit-15 pattern still needs sampling in the game. No claim of runtime conformance or complete R2 support is made.

Layout assumptions, labelled: the record does not state two things the reader enforces fail-closed. The rotation scatter index is a multiple of four inside the twelve float slots per bone (inferred from the slot bound the game uses), and a packed rotation whose stored components overshoot unit length by more than a quantisation slack of 0.001 is refused (smaller overshoot rounds the omitted component to zero). Both are assumptions, not facts from the RE record; a file that violates them is refused with the message "layout assumption violated" or "does not describe a rotation". The omitted component otherwise follows the record exactly: sign times the square root of one minus the sum of squares.

## BGS/src/Hkx/PredictiveCompressedAnimationDecoder.cs — bounded dynamic-bone playback (#683)

Provenance: Fallout4.exe 1.10.163 game-decoder statements in GT #520 comment 16773 and RE-026 README independent review items 7, 9, 10, 11 and 13 (the game-decoder statements only). The bone-group nesting is settled: one 16-descriptor decode per group of up to 16 scalar lanes. Each descriptor nibble is a half-payload byte count minus one; each half reads a full 16-byte reverse look-behind, including preceding bytes, and extracts four pairs of signed w-bit fields multiplied by four. Widths 0..14 are supported; width 15 is refused. Three int16-wrapping prefix sums followed by arithmetic shift-right two reconstruct the samples. BLOCK_OFFSETS are u32 byte offsets stored in a u16 array with u16-element section offsets. Blocks contain 16 samples at a 15-frame stride, not 15 samples plus an unused pad.

The reader dispatch supports only fully defined dynamic bone components: every translation, quaternion and scale component must be explicitly scattered once (padding components are optional). All used lanes must be homogeneous ranged or fixed. Ranged lanes use per-lane float scales/offsets; fixed lanes use the exact float bit pattern 0x39000400 with zero bias. Mixed fixed/ranged cursor association, any float-track/boundary table or selector, static value/index walks, reference/default reconstruction and quaternion-renormalization lists are refused with the unresolved feature named. Truncated arrays, bad section/block bounds, duplicate/out-of-range scatter slots and nonfinite dequantization are rejected. Sample 15 of a block is real data and the next block starts at frame 15, so no equality between sample 15 and the next block's sample 0 is required: the record does not state one. AnimationPose.At accepts fractional frames and interpolates local transforms before hierarchy composition. PlaybackSession remains integer-frame based.

Test-only inverse packing was derived by reversing the stated game-decoder operations, not imported from a production compressor. Tests cover all widths 0..14 (450 randomized field blocks), 100 wrap-heavy random sample blocks, descriptor nibbles, reverse look-behind contamination, u32 offsets in u16 units, finite arithmetic, one/two-bone groups, homogeneous fixed/ranged playback, nonmultiple-of-15 frame counts, and explicit unsupported-site refusals. Synthetic packfiles exercise parse, decode, AnimationPose and PlaybackSession, including a 31-frame clip at 14.5 and 15. Their successful application sampling does not establish game conformance.

Layout assumptions, labelled: the block-offset table must hold at least two entries per block, the dynamic scale and bias arrays at least one entry per dynamic lane, and IS_ANIMATED_BITMAP is only size-checked (its contents are not consumed because the record does not say how they select tracks). These are minimum-size checks that fail closed; they are assumptions, not facts from the RE record.

Padding lanes: the lanes of the last group beyond the dynamic lane count are never field-decoded, only skipped by their descriptor-implied byte count, because the game decodes junk there and a junk width nibble must not refuse a legitimate file; width 15 is refused only on a used lane.

Runtime-only unverified: no shipped file uses this codec; sampling a hand-built 31-frame predictive file in the game at 14.5 and 15 still needs doing. The quantized eight-pattern game-runtime check above also remains unverified. Float/boundary consumers, STATIC_* walks, RENORM consumption and mixed-range cursor association remain unresolved and are not claimed implemented. Complete R2 support is not claimed.

## BGS/src/Hkx/GltfAnimationImport.cs — glTF animation import (#1069)

- **Coordinate frame of the Havok data** (proven on the vanilla character skeleton, `BGS/tools/tests/GltfAnimationImportTests.cs:FixtureSkeletonIsZUpForwardYAtHumanScale`): the head bone sits 120.84 units above the foot, the toe is ahead of the foot on +Y, and the left hand is on -X. So the Havok frame is X right, Y forward, Z up. `BGS/src/Hkx/RootMotion.cs:Motion` already uses the same Up/Forward defaults.
- **glTF convention assumed**: the Blender glTF exporter's. A Havok (Blender) point (x, y, z) is glTF (x, z, -y), so glTF forward is -Z. The import reads glTF (x, y, z) as Havok (x, -z, y).
- **Root-only conversion, no conjugation**: `BGS/src/Hkx/GltfAnimationImport.cs:GltfAnimationImport.HavokPose` right-multiplies only root joints (no joint parent) by `ToHavok` (`Matrix4x4.CreateRotationX(pi/2)`, row-vector form). A change of world basis left-multiplies the root's world transform, and that carries down the chain. A child's local transform is already relative to a parent frame that carries the change, so it is unchanged. Conjugating children would turn each bone's local axes too, which is wrong. A Blender-style armature node with the -90 degree X export rotation is an ancestor that gets folded in, so it cancels the conversion and the bones land in the Havok frame (`BGS/tools/tests/GltfAnimationImportTests.cs:BlenderArmatureYUpRotationCancelsTheRootConversion`). The NifSkope fork's glTF importer applies the same +90 degree X undo on the left, and its unit scale divides metres by metres-per-unit, which matches this convention (read for convention only).
- **Scale**: `BGS/src/Hkx/GltfAnimationImport.cs:GltfAnimationImport.MetresPerUnit` is 0.0142875 m per Havok unit, the NIF-to-Havok factor recorded in `PluginEditTool/docs/internal/NIF_TOOLCHAIN.md` (box collision, "HK scale 0.0142875"). Cross-check from the fixture: the head at 120.84 units is 1.73 m and the pelvis at 68.9 units is 0.98 m. That is human scale, but it is not a measured metric reference. The game must confirm playback scale.
- **Channels are absolute local transforms**: `BGS/src/Hkx/GltfAnimationImport.cs:GltfAnimationImport.Bake` takes the glTF value of a channel as the bone's local TRS, as glTF defines it. A component no channel animates is taken from the skeleton's reference pose (`GltfAnimationImport.Reference`). A bone with no channel at all is held at its reference pose.
- **Rest pose check**: `BGS/src/Hkx/GltfAnimationImport.cs:GltfAnimationImport.Build` lists animated bones whose glTF rest pose differs from the skeleton reference by more than `RestPositionTolerance` (0.01 units) or `RestAngleTolerance` (0.001 rad) in `Report.RestMismatch`. These are reported, not refused. Absolute channel values are only correct when the rig matches the skeleton's rest pose.
- **Helper nodes**: `BGS/src/Hkx/GltfAnimationImport.cs:GltfAnimationImport.HavokPose` multiplies the static non-joint nodes between a bone and its nearest joint parent into the bone's local transform. Animated non-joint nodes above bones are refused in `GltfAnimationImport.Build`, because their motion has no bone track to land on.
- **Parent check**: `BGS/src/Hkx/GltfAnimationImport.cs:GltfAnimationImport.Build` requires an animated bone's nearest glTF joint parent to have the same name as its skeleton parent. Otherwise it refuses, because a helper bone in between would change the frame.
- **Frame rate and span**: `GltfAnimationImport.Build` uses 30 fps, or `ceil(1 / smallest key step)` if the source is faster. Frame f is at time f / fps. The clip ends at the first frame at or after the last key, and times outside the keys hold the end values. Two frames are the minimum, and `GltfAnimationImport.MaximumFrames` caps the length.
- **Interpolation**: LINEAR (lerp, shortest-path slerp for rotation) and STEP are read. CUBICSPLINE is refused with its reason in `BGS/src/Hkx/GltfAnimationImport.cs:GltfAnimationImport.Build`, because the tangent handling is not implemented.
- **Root motion is replaced by a stationary frame**: `BGS/src/Hkx/GltfAnimationImport.cs:GltfAnimationImport.Write` passes a two-sample zero-travel `RootMotion.Motion` as the timeline motion to `BGS/src/Hkx/NativeAnimation.cs:NativeAnimation.Recompress`. The existing `replacingMotion` branch replaces a template's extracted-motion reference frame when a motion with samples is given, so the clip gets no travel. A template with no motion gets none. `GltfAnimationImport.Encode` refuses an output whose motion still has travel or an unsupported frame. Measured: the paired kill (79 template samples) and TurnLeft90 (19) both write a frame with 2 zero samples and travel 0.
- **Track map**: `BGS/src/Hkx/GltfAnimationImport.cs:GltfAnimationImport.TemplateBones` takes the template's track-to-bone map (`TrackToBoneIndices`, or the identity when the template names none and its track count equals the bone count). A template may drive a subset of the bones: the paired kill drives 94 of 95, with CamTarget undriven. `GltfAnimationImport.Project` writes its tracks in the template's own order, and `GltfAnimationImport.Encode` checks that the read-back map equals the template's. `GltfAnimationImport.Write` refuses, by name, an animated glTF bone that has no track in the template.
- **Annotations are cleared**: `NativeAnimation.Recompress` takes `keepAnnotations` (default true, so existing callers are unchanged). `GltfAnimationImport.Encode` passes false, so the template's events are not copied, and `Written.AnnotationsCleared` counts them (paired kill 7, TurnLeft90 4). Byte identity of the default path is pinned by `BGS/tools/tests/AnimationSaveTransactionTests.cs:AnimationSaveTransactionTests.RecompressDefaultOutputIsByteIdenticalToTheVerifiedBuild`, with hashes taken from the untouched HEAD build.
- **Verification and rotation budget**: `GltfAnimationImport.Encode` reads the recompressed bytes back and compares every track and frame to the intended clip. `GltfAnimationImport.Attempts` lists the encoder settings tried in order: the `SplineEncoder.Options` defaults, then rotation format 1 with `RotationTolerance` 0.00025 and 0.000125, then format 2 with 0.00025 and 0.0000625. The acceptance budget stays the encoder default (0.01 units, 0.001 rad, 0.0005 scale). Measured on the real fixtures: format 1 at the default tolerance misses (paired kill 1.585e-3 rad, TurnLeft90 1.634e-3). Format 1 at 0.00025 passes (paired kill 9.74e-4 rad, a 2.6 percent margin; TurnLeft90 5.7e-4 rad), so the fit tolerance is the lever and format 1 is preferred. Format 2 alone also misses (1.700e-3, 1.550e-3). Format 2 at 0.00025 gives 9.7e-5 and 8.1e-5 rad, but it is only a fallback: the committed vanilla spline clip (TurnLeft90) uses format 1 in every track block, and the game is not shown to read format 2.
- **Rotation error metric**: `GltfAnimationImport.Angle` uses `2 atan2(|vector part of conj(a) b|, |w|)`. The `2 acos(dot)` form reads about 7e-4 rad of noise for two float quaternions that differ by 1e-7, because acos is ill-conditioned near 1. A rotation test built on acos therefore fails on float noise.
- **Round trip evidence**: `BGS/tools/tests/GltfAnimationImportTests.cs:VanillaClipRoundTripsPerBoneAndFrame` exports the paired kill (94 tracks) with the full 95-bone skeleton, re-imports it as .gltf and .glb, and compares `AnimationPose.Local` per bone and frame. Measured worst error: 1.14e-5 units, 3.0e-6 rad, 7.7e-7 scale. The tolerances are 1e-3 units, 1e-4 rad and 1e-4 scale.
- **Reader**: `BGS/src/Hkx/GltfAnimationReader.cs:GltfScene.Parse` reads .gltf (data: or relative buffers) and .glb (JSON and BIN chunks) with System.Text.Json. Refused: sparse accessors, non-float or normalized animation data, byte strides shorter than the element, external buffers that are absolute or URLs, skins other than one, and animations other than one (the file must name exactly one).
- **Dispatch**: `BGS/tools/symrm/CliEntry.cs:CliEntry.Run` routes `gltf` to `BGS/tools/symrm/GltfCli.cs:GltfCli.Run`, not `Program.Main`. `BGS/tools/symrm/Program.cs` is already over the 750-line limit (7,435 lines), so the new command lives in its own file. The help catalog entry is `BGS/tools/symrm/SymrmHelp.cs:SymrmHelp.Catalog`.
- **CLI output**: `BGS/tools/symrm/GltfCli.cs:GltfCli.Run` refuses an output that already exists or equals an input, writes through `BGS/src/Hkx/FileSafety.cs:FileSafety.Replace`, and prints the counts in `Report`. `--root-motion` is refused with a message, not ignored.
