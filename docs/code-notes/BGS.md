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

## BGS/tools/tests/Ba2RadicleTests.cs:WriteGeneralBa2

Radicle requires each GNRL record to declare a 16-byte chunk header (the count and size word is `0x00100100`) and the `0xBAADF00D` chunk sentinel. Fixtures that wrote zeros there passed only because the previous reader ignored those fields. `BGS/tools/tests/AuditRegressionTests.cs:WriteBa2` uses the same layout.

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

`BGS/src/Archive/GameData.cs:GameData.ModlistProfile` picks the first profile (alphabetically, case-insensitive) under `<mods>/profiles` that ships a `modlist.txt`. The `Directory.EnumerateDirectories` call and the LINQ chain are unchanged, but the whole chain now runs inside a catch for `IOException` or `UnauthorizedAccessException`, and a failure returns null (no profile). Before, a denied profiles listing threw `UnauthorizedAccessException` out of `ModlistProfile`, and `BGS/app/MainWindow.Chain.cs:MainWindow.BuildChainCore` (the modded-open try) reported "game data could not be read" and the whole modded open failed. The catch filter is the one `BGS/src/Archive/Mo2ScanData.cs:Mo2ScanData.EnumerateDirectoriesSafe` uses; that helper is private to `Mo2ScanData`, so the filter is repeated here rather than shared. A denied profiles folder now reaches the "no modlist.txt under that folder, or its profiles folder is unreadable" status and a plain `Discover`. A profile whose own `modlist.txt` is denied is not covered here: `DiscoverModded` reads it under a guard that treats an unreadable file as empty (see its section below), and the profile is skipped if `File.Exists` reports it absent.

A null profile with no root `modlist.txt` makes the caller skip the layering and run a plain `GameData.Discover(folder)` with the summary "no modlist.txt under that folder". So a denied profiles folder silently drops the mods rather than failing, and the user sees only that summary line.

`BGS/tools/tests/Ba2RadicleTests.cs:ModlistProfileNamesTheProfileUnlessTheProfilesListingIsDenied` creates `mods/profiles/Default/modlist.txt` and asserts `ModlistProfile` returns "Default" while the folder is readable. It then denies the current user's list right on `profiles` through the shared `DenyListing` / `RestoreListing` helpers (`icacls` on Windows, mode 0 elsewhere) and asserts null, restoring the folder in `finally` before the temp folder is deleted. It returns without asserting if a probe shows the denial did not take effect. It fails on the unfixed code with `UnauthorizedAccessException` from `ModlistProfile`'s profiles listing, and passes with the fix. It does not cover a denied profile folder's own `modlist.txt` or an `IOException` raised mid-enumeration.

### src/Archive/GameData.cs — OrderByPlugins (later plugin's archive wins)

`BGS/src/Archive/GameData.cs:GameData.OrderByPlugins` now reverses the plugin-matched base archives before the unplaced ones are appended. `ArchivePaths` is highest priority first, so the archive of the last-loaded plugin is now first and wins. Before, the first plugin listed in plugins.txt came first, and its archive won, which is the reverse of the game.

This follows the Radicle load-order rule in `docs/code-notes/GARDEN-TOOLS.md` under "**Load order.**" and "**Precedence.**": INI-listed archives load first, then each plugin's archives in load order, and the last-mounted archive wins.

Archives that match no plugin, such as `Fallout4 - Animations.ba2` when Fallout4.esm is not in plugins.txt, stay after every plugin-matched archive, as the lowest priority. They stand in for INI-listed archives, which load before any plugin archive. The early `return archives;` for an unreadable plugin list is unchanged, so that case keeps the alphabetical order. Mod-root archives are unchanged.

Tests: `BGS/tools/tests/Ba2RadicleTests.cs:LaterPluginsBaseArchiveWinsOverEarlierOne` covers A, B and Fallout4 archives with plugins.txt `*A.esm`, `*B.esm`. It expects B's archive to win and `ArchivePaths` to be B, A, Fallout4. `BGS/tools/tests/GameDataPluginOrderTests.cs:PluginListOrdersBaseArchivesWithRadicleRules` expects the new order. The symrm test `BGS/tools/symrm/Tests.cs:GameDataLoadOrderFollowsPlugins` was deleted because it duplicated `PluginListOrdersBaseArchivesWithRadicleRules`.
## BGS/src/Archive/GameData.cs:GameData.DiscoverModded - unreadable modlist.txt (#752 Low)

`BGS/src/Archive/GameData.cs:GameData.DiscoverModded` reads the chosen modlist.txt inside a catch for `UnauthorizedAccessException` and for `IOException` other than `FileNotFoundException` or `DirectoryNotFoundException`. An unreadable file counts as empty: no mod roots come from it, only the overwrite root (when present) is layered, and the archives are those a plain `GameData.Discover` finds. Before, the exception escaped and the modded open failed. A file that vanishes between the `File.Exists` check in `GameData.ModlistPath` and the read still throws.

`BGS/tools/tests/Ba2RadicleTests.cs:DiscoverModdedFallsBackToPlainArchivesWhenTheModlistCannotBeRead` denies the file's read (`DenyReading`) and asserts `ArchivePaths` and `ModRoots` equal a plain `Discover`. `BGS/tools/tests/Ba2RadicleTests.cs:ModlistPathUsesTheNamedProfileAndNeverFallsBackToTheRootModlist` pins `GameData.ModlistPath`: a named profile uses its own modlist, and a missing profile returns null rather than the root modlist.
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

`BGS/tools/tests/NifCollisionDecoderTests.cs` covers bodies/materials/hashes/shapes on the synthetic blob, the measured offsets as literals, `hknpDynamicCompoundShape`, a compound free-list hole, determinism of `Describe`/`Sha256Hex`, attribution passed in and returned unchanged with the digest unaffected by it (`KeepsTheAttributionItsCallerSupplies`), the four-byte prefix, refusal of zeroed/non-packfile blobs, refusal when the system class is renamed away, refusal when the system signature disagrees with the table, every truncation of the fixture (byte by byte, plus 64 tail cuts) through `TryDecode` without an escaping exception (with an error string whenever it refuses), and 200 seeded random blobs with only the magic planted. `VanillaNifCollisionBlobsDecodeToStableHashes` is the Radicle-to-BGS path: with `FO4_NIF_DIR` set it reads every `*.nif`, takes each `NifFile.Collisions[*]`, hands its owner flags, system reference, Body ID and target to the decoder, and writes `nif-collision-manifest.txt` (relative path, block, system class, body/shape counts, hash, then owner block, flags, system reference, Body ID, target block and target node) sorted; it fails if any blob fails to decode and compares the file with a committed `fixtures/nif-collision/manifest.txt` when one exists. The attribution columns are there so the first run on installed data answers the 14-byte `bhkNPCollisionObject` question from a record rather than from a failing assertion. Without `FO4_NIF_DIR` a custom `NifCorpusFact` attribute reports the test as skipped, not passed, so a machine with no game data cannot show a green corpus check; with data it writes the manifest and compares a committed one when present. No manifest is committed yet, so the hash and attribution halves stay unenforced until the first data run.
The tests cover bodies/materials/hashes/shapes on the synthetic blob, the measured offsets as literals, `hknpDynamicCompoundShape`, a compound free-list hole, determinism of `Describe`/`Sha256Hex`, the four-byte prefix, refusal of zeroed/non-packfile blobs, refusal when the system class is renamed away, refusal when the system signature disagrees with the table, every truncation of the fixture (byte by byte, plus 64 tail cuts) through `TryDecode` without an escaping exception (with an error string whenever it refuses), and 200 seeded random blobs with only the magic planted. `VanillaNifCollisionBlobsDecodeToStableHashes` is the Radicle-to-BGS path: with `FO4_NIF_DIR` set it reads every `*.nif`, takes each `NifFile.Collisions[*].Bytes`, decodes it, and writes `nif-collision-manifest.txt` (relative path, block, system class, body/shape counts, hash) sorted; it fails if any blob fails to decode and compares the file with a committed `fixtures/nif-collision/manifest.txt` when one exists. Without `FO4_NIF_DIR` a custom `NifCorpusFact` attribute reports the test as skipped, not passed, so a machine with no game data cannot show a green corpus check; with data it writes the manifest and compares a committed one when present. No manifest is committed yet, so the hash half stays unenforced until the first data run.

## `tools/export/Program.cs` — the export CLI

`Program.Run(args, output, error)` is the whole command surface: `--out <dir>` plus file inputs, `--batch` to walk directories for `*.hkx` and `*.nif`, and `--schema-only` to write `schema.json`. It parses arguments itself, prints the usage text on `--help` or on a missing `--out`, and returns 0 when every input exported, 1 for usage or output failures, 2 when at least one input failed. The CLI is the only entry point; every decision lives in `ExportRunner`.

## `tools/export/ExportRunner.cs` — what one run writes

`ExportRunner.Run` collects inputs, sorts them by full path with `StringComparer.Ordinal`, and exports one at a time so the output cannot depend on directory enumeration order. Batch roots go through `ExportRunner.Collect`, which catches a failing walk, keeps the files it already found, and records one row whose `source` is that directory, so an unreadable subtree can no longer end the run with no rows. Per file it records the input SHA-256, then dispatches on extension: an `.hkx` is read once through `PackfileImage.Read` + `PackfileObjects`, and the classes present decide which documents are attempted — `hkaSkeleton` gives a skeleton, any `HkxAnimationData.DecodedAnimationClasses` member gives an animation, and `hkbBehaviorGraph` or `hkbStateMachine` gives a behaviour graph. A `.nif` is parsed by `NifFile.Parse`, its embedded systems are read by `NifPhysics.Read` — which piece C widened from `bhkRagdollSystem` alone to `bhkPhysicsSystem` too; its only caller before piece C was `NifEmbeddedPhysicsProofTests`, by reflection and with ragdoll-only fixtures, so that proof is unaffected — and every blob is decoded by `NifCollisionDecoder.TryDecode`, reusing piece B rather than reimplementing it. Each kind has its own try/catch: a failure becomes an error row prefixed with the kind, and the other kinds still export. A file that yields nothing raises instead of being skipped, and `ExportOne` catches every failure that is not cancellation for that input, so a defect in one reader cannot end a batch.

Targets are `<kind>/<stem>.<kind>.json` under `--out`, where the stem is the input path below the batch root, so two inputs cannot silently overwrite each other (`_targets` refuses a repeated target). `index.json` lists every document this run wrote, sorted by target, with `kind`, `source`, `target`, `inputSha256`, `sha256` and `bytes`; `errors.jsonl` holds one compact JSON object per failure, sorted by source then message, and is deleted when a run has none. The output directory is never pruned, so `index.json` describes the current run rather than the directory contents.

Byte-identical output rests on three things: `StringComparer.Ordinal` sorting everywhere order is not already structural, `ExportJson` as the only serializer, and `HkxNumber.Text` for every float, which is the same shortest-round-trip text the collision record hashes.

## `tools/export/GraphPlan.cs` — the native plan

`GraphPlan.Build(BehaviourGraphModel)` walks the model that `NativeXml.From(PackfileObjects, PackfileImage)` produced and `BehaviourGraphModel.Parse` read; no Java tool, converted XML or external process is involved. It reads `hkbBehaviorGraphStringData` for event and variable names, takes `hkbBehaviorGraph.rootGenerator` as the entry point, and resolves generators with an ancestor set, so a generator shared by two parents is expanded in full under each of them: clips, reference poses, behaviour references, BGS sequences, state machines, blenders and pose matchers, manual selectors, layers, and the pass-through classes (`hkbModifierGenerator`, `BSiStateTaggingGenerator`, `BSCyclicBlendTransitionGenerator`, `DynamicAnimationTaggingGenerator`, `BSOffsetAnimationGenerator`, `BSBehaviorGraphSwapGenerator`, `BSBoneSwitchGenerator`). A generator met again only while it is still being resolved above it is a true cycle and becomes a childless `shared` node with a detail string, bumping `revisitedGenerators`; stubbing a legitimate second occurrence is what would leave a consumer unable to reconstruct that branch, so a cycle is the only reason to stop expanding a subtree that is still reachable. A state machine object met twice is written once: `_machinesById` turns the second occurrence into a `machine` node carrying the existing index, and consumers follow `machines[index]`; that lookup runs before the budget check, so a reference to a machine already in `machines[]` keeps its index even after truncation instead of becoming a stub that orphans it. `MaximumResolvedNodes` bounds the re-expansion at 200,000 expanded nodes; past it the plan stops expanding, returns a `shared` stub for each node it does not expand, adds one to the `plan-node-budget` gap per stub, and `GraphExport` repeats that count in the document's `notes`, so a short plan is measurable rather than merely present. `HkObject.Int` in `BGS/src/Hkx/BehaviourGraphModel.cs` deliberately keeps parsing graph scalars with the current culture, because `FieldRender` formats those scalars with the current culture too (`Narrow`, the enum `printed.ToString()`, `TYPE_INT64`/`TYPE_ULONG`, `Shortest`): the two sides are written and read on one machine, so they agree by construction, and moving either side to `CultureInfo.InvariantCulture` alone breaks a negative value on a locale whose minus sign is not ASCII `-`. A locale-independent reader has to move with an invariant writer and every other reader of rendered text (`GraphRun`, `GraphValidator`, `StateEditor`, `BindingEditor`, `NativeAuthoring`, `NativeSave`, `PredefinedTemplates`, `ElementSummary`, `BlendWeights`, `HkxTextEdit`) as one testable change. State machines append to `Machines` before their states resolve, so `RootMachine` is an index into that list and nested machines keep discovery order. Transitions come from each state's `transitions` rows plus the machine's `wildcardTransitions`, with the event name from the string table, the condition text from `expression`, `conditionString` or the class name, and the duration from the transition effect.

The plan carries the fields the consumer contract expects: node kind, class, name, animation name, detail, speed, weight, machine index and children; state id, name and generator; transition endpoints, wildcard flag, event id and name, condition and duration; machine name and start state; event and variable names; the gap table; unmapped generator counts; warnings; and the stats counts. Gap keys are `blend-branch-not-wired`, `blend-weight-ignored`, `generator-unmapped`, `generator-reference-unresolved` and `plan-node-budget`, with counts and an explanation each. They describe what a consumer would drop, not what this tool failed to read: the plan itself keeps every branch, weight and generator it resolved.

## `tools/export/ExportJson.cs`, `ExportFormats.cs` and `SchemaDocument.cs` — the document contract

`ExportJson` is the only writer: two-space indentation, LF, ASCII-only strings with every character outside printable ASCII escaped, `HkxNumber.Text` floats, and an exception rather than `null` or `0` for a non-finite value. `ExportFormats` holds the format names and versions, all at version 1. `SchemaDocument.Json()` writes `schema.json`, which lists every format's fields with a type and a note; `SchemaDocumentsEveryTopLevelFieldOfEveryWrittenDocument` fails if a document grows a top-level field the schema does not describe.

## `tools/tests/ExportCorpusTests.cs` and the rest of the export suite

`EveryManifestFileIsAttemptedAndTwoRunsAreByteIdentical` reads `app.Config/manifest.json`, asserts the cached and checked-in copies agree, exports `fixtures/vanilla` twice into separate temporary directories, compares every output byte, and then requires every manifest path to be either exported or reported in `errors.jsonl`, with `inputSha256` equal to the manifest's SHA-256. Files whose manifest fidelity is `byte` must export at least one document; a kind that fails on such a file is still reported as a row, and the `structural` and `refused` entries may be reported instead, because those are the ones BGS's save path already treats as special. `CharacterSkeletonExportsExactlyNinetyFiveBones` asserts the exported `CharacterAssets/skeleton.hkx` skeleton has 95 bones and matches `HkxBinaryReader.ReadSkeleton` bone by bone; the number itself is corroborated by `SkeletonMapperAuthorTests` on the same fixture, not measured by this suite. `SplineAnimationExportsDecodedFramesEventsAndBinding` compares the exported frames, duration, events, skeleton name and track mapping against `HkxBinaryReader.ReadAnimation` for `TurnLeft90.hkx`, so the test proves the export carries what the decoder produced, not that the decoder is right about vanilla data. `ExportGraphPlanTests` builds the plan from a hand-written XML graph and pins states, transitions, weights, gaps and stats without any game data, and separately pins that a clip shared by two blend children keeps its animation name in both occurrences, that a shared state machine is written once and referenced by index, that a generator which is its own ancestor becomes one `shared` stub instead of recursing, that a plan truncated at the node budget still carries the index of a machine it already wrote and counts every stub in the gap, and that the truncation count reaches the document's `notes`. `ExportCollisionTests` decodes the synthetic packfile from `NifCollisionFixture`, writes the document twice and compares it, and checks that a NIF without a collision system, unreadable bytes and a missing path all become error rows rather than exceptions.

What these tests do not establish: that any of it compiles or passes (nothing was built or run), that the decoded PSD layout matches Fallout 4's own classes (piece B's class table is measured, but no RE session re-read it and no vanilla collision blob has been decoded here), that the 95-bone count holds the day the suite runs (no file was counted here), and that the spline math is right (the export test only shows it carries the decoder's frames).
