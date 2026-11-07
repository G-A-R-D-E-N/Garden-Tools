# PluginEditTool code notes

## FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument (SWF editor document)

`SwfDocument` is the editor's document object over the managed Radicle edit surface: `OpenBytes`/`OpenFile`
build one `SwfFile` and one `SwfEditHistory`, and every edit goes through the history so undo/redo is the
only writer. `Characters`, `FrameLabels`, `Timeline` and `Frame` are the read side the UI binds; `Apply`
takes a `SwfPlanSpec` built from the flat `radicle_swf_tag_at` indices and a refusal throws the
`RadicleException` verbatim with the Radicle message. `Dirty` is `Revision != 0`, `Save` copies the
existing asset to `<path>.bak` before writing the new bytes, and `WriteBytes` returns the current movie.
The service is intentionally path-agnostic: loose files and archive-resolved bytes both enter through
`OpenBytes`, so the archive/asset-resolution wiring stays in the host and the PET Core project only needs
the `Radicle.Interop` project reference.

Tests: `FO4RecordEditor.Core.Tests/SwfDocumentServiceTests.cs` and `SwfFixture.cs` build a hand-authored
movie (edit text, root label, placement, sprite label) and pin characters, labels, the first frame,
edit/undo/redo counts, the backup-before-save behaviour and a refused edit leaving revision and history
untouched. Nothing was built or run locally; CI is the first compiler.
## FO4RecordEditor/Services/SwfInterop.cs:SwfInterop and web/src/SwfPanel.tsx (SWF editor panel)

`SwfInterop` is the host bridge the panel calls through `chrome.webview.hostObjects.swf`: `Open` builds the
one `SwfDocument`, `Characters`/`FrameLabels`/`Frame` return the read side as JSON (`FrameLabels` is per timeline),
`RenameLabel`/`RemoveLabel` pass the timeline's DefineSprite character id and the label index to the Radicle verbs (`SwfInterop.cs:SwfInterop`), and `Timeline` maps a scrubber index to that id (`SwfInterop.cs:SwfInterop.Timeline`),
`Undo`/`Redo`/`State` drive the history, and `Save` writes
through the document. Every method returns JSON, and a `RadicleException` comes back as `{ error, status }`
so the panel can show the Radicle message verbatim next to the field. `SwfPanel` renders the character and
label lists on the left, the frame scrubber and display-list table in the centre, the history/save controls
and dirty marker in the header, and binds Ctrl/Cmd+Z / Shift+Z / Y to undo/redo. The web test stubs the
`hostObjects.swf` bridge exactly like the other PET panels and drives open → select label → rename → undo →
redo → save plus the verbatim-refusal path. The panel is a full-page overlay mounted by `MainShell`, with a Movie and
an ABC scripts tab (`SwfPanel.tsx:SwfPanel` renders the tab row and the `hidden` page wrappers).
## web/src/MainShell.tsx (SWF panel registration)

The Interface (SWF) panel is registered the same way as the other asset panels: `showSwf` state, an activity-bar
button with the `Boxes` icon after Papyrus, and a conditional `SwfPanel` render as a full-page `position: fixed`
overlay like `PapyrusPanel`, not inside the shell's layout.
The host bridge is picked up by the existing `[RpcExpose]` scan as `hostObjects.swf`, which is the name the panel
test stubs. Archive-resolved open and the semantic edit verbs are still deferred.
## Radicle/bindings/csharp/Radicle.Interop/SwfEditText.cs:SwfEditTextCodec (edit-text fields)

`SwfEditText.cs:SwfEditTextCodec.Parse` reads bounds (RECT bits), both flag bytes, font id/class, height, colour, max length,
layout, variable and initial text from the decoded tag payload, and `SwfEditText.cs:SwfFile.EditText` exposes it. It is read-only:
`SwfDocumentService.cs:SwfDocument.EditEditText` writes through `SwfVerbs.ApplyEditText`, and label add, rename and remove write through the label verbs.
Tests in `SwfEditTextTests.cs` pin the full read and the wrong-tag refusal. The depth and
transform edits still compose payloads in PET (`SwfPlacementCodec.WithDepth`, `SwfPlacementMatrixCodec.Write`).
## FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.EditEditText (edit-text through the history)

`SwfDocumentService.cs:SwfDocument.EditEditText` calls `SwfVerbs.ApplyEditText` on the document's own history (through
`Verb`), so the DefineEditText rewrite is owned by Radicle and an edit-text change is one history command with the same
undo/redo/refusal behaviour. `SwfDocumentService.cs:SwfDocument.EditText` reads through `SwfVerbs.ReadEditText` on the same
history, so the read and the write share Radicle's codec; `SwfDocumentServiceTests.EditsAnEditTextPastASpriteAtItsFlatIndex`
reads the flat index past a DefineSprite before and after the edit.
`SwfInterop` exposes `ReadEditText`/`EditEditText` for the panel (colour/rect/text are flattened JSON; the colour stays
`0xAARRGGBB`). The read reply has no `variable`: the native read does not export it and the panel never read it.
`SwfDocumentServiceTests.EditsEditTextThroughTheSameHistory` pins the edit plus its undo, and checks `variable` through the
managed `SwfFile.EditText` oracle.
## web/src/SwfPanel.tsx (edit-text properties)

Selecting a character whose kind is EditText calls `ReadEditText` through the host bridge and fills the right-hand
fields (text, colour, bounds rect); Apply sends `EditEditText` with those values and folds the returned state
back into the header. The field-level refusal banner is the same one all commands use, so a Radicle refusal
appears next to the panel verbatim. The web test drives open → select edit text → change text → apply and asserts
the bridge call.
## web/src/SwfPanel.tsx:SwfPanel.loadLabels (frame label list)

The left list shows the selected timeline's frame labels. `SwfPanel.loadLabels` asks `Timeline(index)` for the DefineSprite
character id (0 for root) and then calls `FrameLabels` with that id, because the label verbs take the character id while the
scrubber and `Frame` take the timeline index. The id is kept as `labelOwner`, and add, rename and remove send it with the
label index. Each load takes a request number from `labelRequest` and drops a reply that is no longer the newest, so a late
reply for an earlier timeline cannot overwrite the list on screen. A picked label is kept as `{ ...row, timeline }` and
survives a reload of the same timeline after a rename only; a timeline change, Add, Remove, Undo, Redo and Save clear it
(`web/src/SwfPanel.tsx:SwfPanel.refresh` clears it before reloading). Undo, redo, save and each label edit reload the
characters and the labels from the history (`SwfPanel.refresh`, `SwfPanel.loadCharacters`). Tests in
`web/src/SwfPanel.test.tsx` pin the index-to-id mapping, the dropped stale reply, the kept selection and the reload after undo.
## FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.AddLabel (label insertion)

`SwfDocumentService.cs:SwfDocument.AddLabel` calls `SwfVerbs.AddFrameLabel` on the document's history. Radicle resolves
the timeline (0 for root, a DefineSprite id otherwise), refuses a duplicate name on that timeline and a frame the
timeline does not reach, and commits one history command that undo removes. `SwfInterop.AddLabel` exposes it, and the
panel's "Add at frame" button passes the label name field, the scrubber frame and the timeline's character id held by `web/src/SwfPanel.tsx:SwfPanel.loadLabels`.
## Radicle/bindings/csharp/Radicle.Interop/SwfPlacements.cs:SwfPlacementCodec (depth edit)

Placement tags have no C ABI getter, so `Placements()` parses the root and sprite placement payloads
(PlaceObject 4, PlaceObject2 26, PlaceObject3 70) for depth/character, and `SetDepth` patches the depth bytes in
place for a sprite child and sends the payload through the tag plan, keeping every other byte; a root placement goes
through the native depth verb (`PluginEditTool/FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.SetDepth`, see the tail section). `SwfDocument.SetDepth` and
`SwfInterop.Placements`/`SetDepth` expose it. `SwfPlacementTests` pins the read, the in-place patch, the undo and
the wrong-tag refusal. Transform/keyframe composition is the next placement step.
## web/src/SwfPanel.tsx (placement depth properties)

Open also fetches `Placements`, the left column lists root placements (depth/character), and selecting one fills the
right-hand depth field; Apply sends `SetDepth` and refreshes the placement list and history header. The web test
drives open → select placement → change depth → apply and asserts the bridge call. Transform/keyframe fields are
still the remaining placement properties.
## Radicle/bindings/csharp/Radicle.Interop/SwfPlacementMatrix.cs:SwfPlacementMatrixCodec (transform edit)

The MATRIX block of a placement is bit-packed between byte-aligned fields; `Read` decodes the scale/rotate/translate
values and their span, and `Write` re-encodes only that span (adding scale/rotate when an edit asks for them) and
keeps the prefix and suffix bytes. `SwfFile.PlacementMatrix`/`SetPlacementTransform`, `SwfDocument` and `SwfInterop`
expose the read and the plan edit; `SwfPlacementMatrixTests` pin identity read, translate edit with other bytes
equal, adding scale to a matrix that had none, and the wrong-tag refusal.
## web/src/SwfPanel.tsx (placement transform properties)

Selecting a placement now also reads its MATRIX through `PlacementMatrix`; the right panel exposes X/Y, ScaleX/ScaleY
and Rotate0/Rotate1 fields and Apply sends `SetTransform`, keeping depth and every other byte (the codec replaces only
the matrix span). The web test drives select → apply depth → apply transform and asserts both bridge calls.
## web/src/SwfPanel.tsx (bounds boxes)

The centre now draws a bounds box per display-list instance whose frame expansion carried bounds: `SwfInterop.Frame`
adds the bounds rect/flags/clip depth to each row, and the panel positions a dashed absolutely-positioned box in a
relative stage with the depth label. This is the round's rendering substitute: no shape canvas, only the placement
bounds. The web test asserts the box test id exists for the fixture instance.
## FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.InsertKeyframe (keyframe at frame)

For a sprite-child PlaceObject2 placement, `PluginEditTool/FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.InsertKeyframe`
copies its payload, forces the Move flag, clears the character bit and drops the two character-id bytes that followed the
depth, then splices the copy into the sprite frame through the plan, so it is one history command that undo removes. A
non-PlaceObject2 tag is refused. Root placements use the native keyframe verb instead (the root keyframe section at the end
of this file). `SwfInterop.InsertKeyframe` and the panel's "Keyframe at frame" button (scrubber frame) use it.
## FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.InsertKeyframe (sprite timelines)

For a placement inside a DefineSprite, `InsertKeyframe` finds the sprite frame's ShowFrame child, splices a new
Move-flagged placement tag (short or long header as the length needs) into the sprite payload at that child's header
offset, and replaces the DefineSprite tag through the plan. The splice is the shared `SwfDocument.InsertSpriteChild`
helper, also used by `AddLabel`; the copied Move row drops its character-id bytes the same way as the root form. The
fixture's sprite carries a placement, so the service test pins the insertion and undo on that timeline too.
## FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.OpenResolved (archive/asset open)

`OpenResolved` takes the logical asset path and a resolver delegate (the host's existing asset resolution), reads the
bytes and builds the same document as a loose file. `Path` keeps the logical asset path (never the materialized cache
file) and the document is flagged resolved, so `Save()` without an explicit target throws a read-only message, while an
explicit target writes there. The service test drives a resolver over the fixture bytes, asserts the reads, and asserts
that `Save()` refuses.
## FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.RemoveLabel (label removal)

`SwfDocumentService.cs:SwfDocument.RemoveLabel` calls `SwfVerbs.RemoveFrameLabel(timeline, labelIndex)`. The index is the
row index from `FrameLabelRows` for that timeline, not a tag index, and undo restores the label through the history. The
panel's Remove button sends the timeline's character id held by `web/src/SwfPanel.tsx:SwfPanel.loadLabels` and the selected row's label index.
## FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.AddLabel (sprite timelines)

`AddLabel` takes the timeline id and the sprite splice happens inside Radicle (`SwfVerbs.AddFrameLabel`), not in PET; the
service test pins sprite add + undo. The panel passes the DefineSprite character id held by `web/src/SwfPanel.tsx:SwfPanel.loadLabels`, not the scrubber index.
## FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.RenameLabel and SwfDocument.FrameLabelRows (label rows and rename)

`FrameLabelRows(timeline)` wraps `SwfVerbs.FrameLabelRows` and returns `label_index`, frame and name per timeline;
`SwfInterop.FrameLabels(timeline)` sends them to the panel. `RenameLabel(timeline, labelIndex, name)` calls
`SwfVerbs.RenameFrameLabel`, and Radicle keeps the bytes after the name's NUL (the named-anchor flag) and refuses a
duplicate name on that timeline. `SwfDocumentServiceTests` pins the root and sprite row listing, the rename with undo and
the out-of-range refusal.
## FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.Verb (verb commits reopen the movie)

`Verb` runs one Radicle label or edit-text verb on the document's `_history` and then reopens `Movie` from the history
bytes, the same as `Apply`. The history the document holds is the `SwfEditHistory` the verbs take, so no
`SwfEditing.UseForVerbs` bridge is needed in PET.
## FO4RecordEditor/Services/SwfInterop.cs:SwfInterop.Open (loose or archive)

Open takes a loose path only when it exists, and then only under the configured Data or project folders
(`ToolPaths.RequireDataOrProjectPath`); any other loose path is refused. Anything else goes through
`SwfDocument.OpenResolved` with a byte resolver built on the PET `AssetResolver` (`Resolve` then `Materialize`), which
reads the materialized file. The cache file is only read, so Save never writes into the cache. Saving a resolved
document back into its archive is not wired, and the UI Save has no target to pass, so it is refused.
## FO4RecordEditor.Core/Services/WriteService.Asset.cs:WriteService.SaveAsset (SWF document save)

`SaveAsset(path, bytes)` copies the existing file to `<path>.bak` only when that backup does not exist, so later saves never
replace the original bytes. The new bytes go to a temp file beside the target (`.<name>.<guid>.tmp`) that is moved over
the target with `File.Move(..., overwrite: true)`; the temp is deleted in a finally. `SwfDocument.Save` delegates to it.
## web/src/SwfPanel.tsx (display-list row selection)

A display-list row is now a selection affordance: clicking it matches the row's depth (and character when present) to
the placement list and selects that placement as the edit target, so the properties panel follows the centre table as
well as the explicit placement list. The web test clicks the fixture row and asserts `PlacementMatrix` was requested.
## Radicle native delivery for the managed editor (read-only trace, item 1)

The native PET app builds Radicle from the sibling checkout: `PluginEditTool/native/CMakeLists.txt:53` sets
`RADICLE_DIR` to `../../Radicle`, `:63` pulls it in with `add_subdirectory`, and `:72-73` clear the
`VERSION`/`SOVERSION` properties so the build emits a plain `libradicle.so` (the ntfs3 volume cannot create the
version symlinks). That library is the C ABI: `Radicle/CMakeLists.txt:458` defines the shared `radicle_c` target and
`:478` adds `src/c_api_swf.cpp`, `src/c_api_swf_art.cpp`, `src/c_api_swf_characters.cpp`,
`src/c_api_swf_display.cpp`, `src/c_api_swf_edit.cpp` and `src/c_api_swf_history.cpp`, with the SWF models at
`:332-346`; `Radicle/include/fo4/c_api.h:16` is `RADICLE_ABI_VERSION 78u` (the SWF entry points were added at ABI 71). So the SWF entry points do ship in
the library when the tree is this one — nothing was missing from the build.

The managed side resolves the library in `Radicle/bindings/csharp/Radicle.Interop/Core.cs`: `:29` installs the
`DllImportResolver`, `:31` exposes `SetPath`, `:49` prefers `RADICLE_NATIVE_PATH`, and `:53` falls back to
`NativeLibrary.TryLoad` beside the assembly. Before this change no file under `PluginEditTool` set
`RADICLE_NATIVE_PATH`, called `RadicleLibrary.SetPath`, or copied the library next to the managed output (a grep of
every PET `.cs`/`.csproj`/packaging file found none), so the managed Core/Server/Test host had no delivery path.

Wired now: `FO4RecordEditor.Core/Services/RadicleNative.cs` (`EnsureLoaded`/`Configure`/`CandidatePaths`) prefers
`RADICLE_NATIVE_PATH`, then probes the deployed layout (app dir, `native/`, `radicle/`, `runtimes/<rid>/native/`)
and no build folders (the cwd-ancestor `Radicle/build` and `build` walk was removed, so a dev build needs
`RADICLE_NATIVE_PATH` or a copy in the app dir), and both managed entry points
(`SwfDocument` and `SwfInterop`) call it in their static constructors; `RadicleNativeTests` pin the candidate order
and the found/missing probe. Packaging still has to place the library where the probe looks (or set the variable):
the Linux deb ships only the native host (`packaging/linux/build-deb.sh:68` says services run in the native
backend), and that native host links the C++ `fo4` target (`PluginEditTool/native/src/CMakeLists.txt:60`), not
`radicle_c`. The native production host therefore needs no change for its own Radicle use, but a native host object
bridge is still required if the SWF panel is to run inside that native host rather than the managed
Core/Server/Test host; that bridge is not part of this round.
## FO4RecordEditor.Core/Services/ToolPaths.cs:ToolPaths.RequireUnderRoots (loose-path confinement)

`RequireUnderRoots` resolves the path with `Path.GetFullPath` and accepts it only when it starts with a root plus a
directory separator, compared case-insensitively. The separator refuses a sibling such as `<root>2\...`, and the full-path
step refuses `..` escapes. An empty root list refuses everything. `RequireDataOrProjectPath` supplies the configured roots
(DataFolder, Fallout4Path, Mo2InstancePath, OutputFolder) and `SwfInterop.Open` uses it for loose paths. `ToolPathsTests`
pins the accepted path, the sibling, the escape and the empty list.
## Radicle/bindings/csharp/Radicle.Interop/SwfEditText.cs:SwfEditTextCodec (duplicate of the C++ edit-text codec)

`SwfEditTextCodec.Parse` and `Write` re-implement the DefineEditText (tag 37) field walk that the C++ side already has in
`Radicle/include/fo4/swf/swf_text_edit.hpp` (`replace_edit_text`, `apply_edit_text`). The C ABI now exports the edit-text verbs `radicle_swf_edit_text_read` and `radicle_swf_edit_text_apply`
(`Radicle/include/fo4/c_api_swf_verbs.h`). The PET document's write goes through `SwfVerbs.ApplyEditText`
(`SwfDocumentService.cs:SwfDocument.EditEditText`). The managed write was removed from `SwfEditText.cs:SwfEditTextCodec` (`Write`) and `SwfEditText.cs:SwfFile.EditEditText`. PET's read now goes through
`SwfVerbs.ReadEditText` (`SwfDocumentService.cs:SwfDocument.EditText`), so `Parse` only backs `SwfFile.EditText`.
## FO4RecordEditor/Services/SwfInterop.cs:SwfInterop.Run (one lock per call)

`Run` takes the static `Gate` around every action, so the document reference, its history and the lazy character cache are
touched by one call at a time. `Open` runs inside `Run` and takes no lock of its own.

## PluginEditTool/native/src/app/PapyrusTarget.cpp:PapyrusTarget::invoke (Check, Analyze, SymbolAt and Compile call the Radicle service)

These four RPCs call `fo4::papyrus::AnalysisService` (`Radicle/include/fo4/papyrus/analysis_service.hpp`) with the same arguments
they passed to PET's copy, `fo4ide::papyrus::AnalysisService`. PET's copy stays in
`PluginEditTool/native/src/papyrus/AnalysisService.cpp` as the parity oracle until the real-data gates pass. `Decompile` still uses
PET's `papyrus::Decompiler` and `papyrus::PexFile`, because it is not an analysis call. Result shapes are unchanged: the service
strings and the analyze and symbol JSON are returned as they were.

## PluginEditTool/native/src/papyrus/AnalysisService.cpp:AnalysisService (lift audit against Radicle)

A line-level comparison of PET's `src/papyrus/*.cpp` against the Radicle files found no PET-only logic. The differences are
renames and moves: `offsetOf` is `lineColumnToOffset` in Radicle (PET calls it only inside the service and its own tests); the
case-insensitive `.psc` walk is `fo4::papyrus::enumeratePapyrusFiles` with the same rules (symlinked directories skipped, ignore-case
sort); PET's `compileFile` is file-local in Radicle; the Radicle parser and code generator are split into several files, with the
ASCII `lower` and `iequals` helpers in `Radicle/src/papyrus/parser_detail.hpp` and `codegen_detail.hpp`. Only comments and blank
lines differ otherwise.

## PluginEditTool/native/src/app/GraphTarget.cpp:GraphTarget::compiler

`GraphTarget` keeps one `fo4::graph::GraphCompiler` and one `fo4::papyrus::ScriptIndex` per distinct base-import list, keyed on the
joined roots, and rebuilds both when that list changes; `Refresh` drops them. The compile itself is
`Radicle/src/graph/graph_compiler.cpp:GraphCompiler::compile`, so PET holds no graph or F4SE code: `PluginEditTool/native/src/graph/`,
`src/f4se/` and their headers are deleted, and the Graph RPCs, `LoadScript`, the palette reads and the pinned-radicle graph test
helper all call `fo4::graph`.

## PluginEditTool/native/tests/unit/PapyrusTargetTests.cpp:PapyrusTarget analysis RPCs match the PET oracle on real sources

Regression detected: a rewired answer that drifts from PET's, such as a dropped import root, an offset or path argument passed
through wrong, a changed JSON key, or a compile whose `.pex` bytes differ. Each section runs one RPC on real sources
(`GraphScripts/FixtureEventSource.psc`, `BaseStubs/Actor.psc`, and a broken `Bad.psc` written to the temp root) and requires the
same string PET's `AnalysisService.cpp:AnalysisService` returns. Compile also requires the written `FixtureEventSource.pex` bytes to match PET's.

## FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.ApplyNativeEdit (ABC instruction and glyph edits)

The native instruction and glyph verbs mutate a handle directly through `radicle_swf_apply_plan`, so running them on `Movie` would skip the history and vanish on the next reopen. `ApplyNativeEdit` runs the verb on a scratch `SwfFile` opened from the history bytes and lands the changed tag payload as one `Replace` plan. A refusal throws before `Apply`, so the revision, the undo stack and the dirty flag stay put. Covered by `SwfDocumentServiceTests.AnAbcInstructionEditIsAHistoryCommandAndUndoRedoFollowIt` and `SwfDocumentServiceTests.ARefusedAbcInstructionLeavesTheHistoryUntouched`.

## FO4RecordEditor/Services/SwfInterop.cs:SwfInterop.AbcBlocks and SwfInterop.Disassemble (ABC read side)

`AbcBlocks` lists every ABC block of the open movie with its class and script names and its method pool, read from `SwfFile.AbcBlocks` (already parsed) and `SwfFile.Methods(abc)`. The method pool index is the index `Disassemble(abc, methodIndex)` expects, so the panel passes `(abc, index)` straight back. An unparsed block reports zero pool methods (`radicle_swf_abc_method_count` returns 0 when `!view->parsed`), so it lists with no methods instead of failing the whole listing. A method with no body is disabled in the panel instead of being sent to `Disassemble`, which would refuse it. Covered by `FO4RecordEditor.Tests/SwfInteropTests.cs:SwfInteropTests.AbcRpcsReturnTheFieldsThePanelReadsAndTheRefusalShape`.

## web/src/SwfAbcPage.tsx:SwfAbcPage (ABC scripts page)

The ABC scripts page is its own tab, mounted by `web/src/SwfPanel.tsx:SwfPanel` inside a `hidden` wrapper and keyed by `documentId`, which `SwfPanel.open` sets to 0 at the start and to a new value only on a successful open, so a refused re-open starts with an empty page. The page's left column lists the blocks with their classes, scripts and methods from `AbcBlocks` (`web/src/SwfPanel.tsx:SwfPanel.loadAbc`, which `open`, `refresh` and an apply all call, so undo and redo update the list). The main area holds the whole block text from `AbcText` in one textarea; `Apply script` sends the draft with `ApplyAbcText`, a refusal is shown verbatim and keeps the draft, and a success passes the reply to `SwfPanel.abcApplied`, which sets the revision, redraws the preview and reloads the list. The text reloads when the block, the list length or the revision changes, and a reply that is no longer current is dropped by a `live` flag, so a late load cannot replace the text of a newer block. A draft belongs to the block it was loaded from, so a block switch shows no text until its load arrives. The picked method's `Disassemble` listing is an effect over the selected method and the revision with the same guard, so an out-of-order reply cannot overwrite the method now selected. `SwfPanel` ignores Ctrl/Cmd+Z and Ctrl/Cmd+Y whose target is a `HTMLTextAreaElement`, so the script box keeps its own undo.

Tests: `web/src/SwfPanel.test.tsx` covers the tab switch that hides the movie page, the apply that redraws without a `State` read, the verbatim refusal that keeps the draft, Ctrl+Z staying in the box, and a draft that survives a tab switch, alongside the existing listing, empty-state, refusal, stale-reply and refused-re-open cases.
## FO4RecordEditor/Services/SwfInterop.cs:SwfInterop.BrowseForFile (picked path outside the roots)

`SwfInterop.BrowseForFile` returns the host dialog's path and remembers its full path in `_browsedPath`. `SwfInterop.Open` accepts exactly that path (OrdinalIgnoreCase) without `ToolPaths.RequireDataOrProjectPath`; every other loose path is still checked against the roots, so a typed outside path stays refused. The native target mirrors this: `PluginEditTool/native/src/app/SwfTarget.cpp:SwfTarget::invoke` runs the `BrowseForFile` branch before `mutex_` (the dialog is modal) and stores the weakly canonical pick in `browsed_`, and `PluginEditTool/native/src/app/SwfTargetFile.cpp:SwfTarget::requireUnderRoots` accepts that one path. Only the dialog call writes the remembered path, so the page cannot forge a pick. A cancelled dialog returns an empty string and remembers nothing. Tests: `FO4RecordEditor.Tests/SwfInteropTests.cs:SwfInteropTests.OpenRefusesAnOutsideFileUntilTheHostDialogPicksIt` and `PluginEditTool/native/tests/unit/SwfTargetTests.cpp:SwfTarget` (`opens an outside file only after the native dialog picks it`).

## FO4RecordEditor/Services/SwfInterop.cs:SwfInterop.ShapeSvg (shape preview RPC)

`ShapeSvg(characterId)` runs `Document().Movie.ShapeSvg` (the Radicle `radicle_swf_shape_svg` wrapper, `Radicle/bindings/csharp/Radicle.Interop/SwfFile.cs:SwfFile.ShapeSvg`) and replies `{svg}`. An undefined id, a non-shape id or no open document throws, and `Run` turns that into the same `{error, status}` shape every other RPC returns, so there is no separate error path. `FO4RecordEditor/Services/SwfInterop.cs:SwfInterop.ShapeSvg` refuses an id outside 0..65535 before the `(ushort)` cast, so it cannot wrap onto a real character (70000 would land on 4464, -1 on 65535); the check throws `ArgumentOutOfRangeException`, which `Run` returns as `{error}`. The SVG content (fills as `<path>`) is pinned by Radicle's `test_c_api_swf_characters.cpp`; the PET side only proves the RPC wiring.

## web/src/SwfPanel.tsx:SwfPanel.pickCharacter (shape preview selection)

Every character pick increments `shapeRequest` and clears `shapeSvg` before it awaits, so picking a non-shape character leaves no preview and sends no `ShapeSvg`. A `ShapeSvg` reply is applied only while its request number is still current, the same request-number guard the ABC view uses with `selectedMethodKey`, so a late reply for an earlier shape cannot replace the one picked last. `web/src/SwfPanel.tsx:SwfPanel.pickCharacter` applies the same counter check to its EditText branch, after the `ReadEditText` await and before any state is set, so a late edit-text reply cannot open its form over a newer selection. `SwfPanel.open` also increments the counter, so a reply from the previous document cannot land on a newly opened one. The preview is an `<img>` whose `src` is `data:image/svg+xml;utf8,` plus `encodeURIComponent` of the SVG text; the SVG is never assigned to `innerHTML`, so markup inside it is not parsed into the page.

## FO4RecordEditor.Core.Tests/SwfFixture.cs:SwfFixture.ShapeTag (opt-in shape character)

`SwfFixture.Movie(withShape: true)` adds an empty DefineShape (id 5, no fills, no lines, an end record). It is opt-in so the document-service tests keep their two-character count. The empty shape is enough to prove `ShapeSvg` returns a `<svg` document; a filled-shape path check would duplicate Radicle's own test.

Tests: `FO4RecordEditor.Tests/SwfInteropTests.cs:SwfInteropTests.ShapeSvgRpcReturnsTheSvgForAShapeAndTheRefusalForANonShape` (shape returns `<svg`, EditText returns an error, ids 70000 and -1 are refused with `0..65535` and no svg), `web/src/SwfPanel.test.tsx:SwfPanel` (shape selection requests `ShapeSvg` and renders the image, a late reply for an earlier shape is ignored, a non-shape pick clears the preview, and a late `ReadEditText` reply after a newer shape selection opens no edit form).

## PluginEditTool/FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.SetDepth (root placements)

A root placement moves through `Radicle/bindings/csharp/Radicle.Interop/SwfVerbs.cs:SwfVerbs.ReassignInstanceDepth`, not the in-place patch. `SwfDocument.RootInstance` finds the root-timeline frame row whose `CreateTagIndex` (flat `radicle_swf_tag_at` index, `Radicle/bindings/csharp/Radicle.Interop/SwfEditing.cs:SwfFrameInstanceRecord`) equals the tag, and refuses when the matching rows name more than one instance. The native verb rewrites the depth of the creating placement and of every move row at that depth, and inserts a RemoveObject2 at the lifetime's terminator when one exists. Tags from the terminator on shift by one (`Radicle/src/swf/swf_depth.cpp:reassign_instance_depth`), so the panel refetches the placement list after an edit, which it already does. Refusals the in-place patch allowed now refuse: an occupied destination, a destination inside a mask range, and a destination equal to the source. The verb returns the moved instance id; `SwfInterop.SetDepth` does not expose it, so its reply is unchanged. A move row (a placement with no character) creates no instance, so `SetDepth` on it refuses with "no root-timeline instance is addressed by that placement".

## PluginEditTool/FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.SetPlacementTransform (root placements)

A root placement's transform goes through `SwfVerbs.ApplyInstanceTransforms` on the row whose `TransformTagIndex` equals the tag. The native verb takes points and unit-one scale, so `SwfDocument.ToVerbTransform` divides MATRIX twips by 20 and 16.16 fixed point by 65536, and an absent scale or rotate is the identity. One twip therefore reaches the verb as 0.05 points and is rounded back to 1 twip in `Radicle/src/swf/swf_edit.cpp:matrix_from_transform`. A root placement with no transform row (no authored MATRIX on its path, or a sprite child) falls back to `SwfPlacementMatrixCodec.Write` in `PluginEditTool/FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.SetPlacementTransform`, which adds the MATRIX to the tag itself as before. A tag whose MATRIX a later move on the same frame overrides also has no row, so the fallback edits that tag's own matrix, the previous behaviour and invisible to the user; a later-frame override still leaves rows on the earlier frames and takes the native path. Several instances can share one transform tag (a move row with a character inherits it); the first row decides the frame and instance, and the verb edits the shared tag, so the choice does not change the result. `SwfDocument.RootRows` reads only the first frame that names the placement, and `SetDepth` and `InsertKeyframe` still refuse two instances on that frame.

## PluginEditTool/FO4RecordEditor/Services/SwfInterop.cs:SwfInterop.PlacementMatrix

The matrix RPC reports an absent scale as identity (`0x10000`) and an absent rotate as 0, since the panel sends all six fields back on apply and a 0 scale would collapse the clip. `PluginEditTool/FO4RecordEditor.Tests/SwfInteropTests.cs:SwfInteropTests.PlacementMatrixRpcReportsIdentityScaleForAnAbsentScale` pins it.

## PluginEditTool/FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.InsertKeyframe (root placements)

`SwfDocumentService.cs:SwfDocument.InsertKeyframe` sends a root keyframe through `SwfVerbs.InsertInstanceTransformKeyframe` with no transform, so the native verb copies the instance's authored transform at that frame. It rebuilds the tag as flags 0x05, the depth and a MATRIX, so name, ratio and colour transform are not copied and carry over from the earlier tags. The verb refuses a frame that already sources the transform explicitly, which includes the frame where the placement was created, and a frame where the instance is not live. The root keyframe test therefore inserts on frame 1 of a two-frame fixture.

## FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.InspectText and SwfDocument.Avm1Disassemble (read-only SWF text)

`InspectText` returns the Radicle inspection listing of the open movie, named by `ToString()` (the asset path, or `<bytes>` for a document opened from bytes). Radicle refuses a null name, so the heading is never null. `Avm1Disassemble` passes a flat tag index to `SwfFile.Avm1Disassemble` (`radicle_swf_avm1_disassemble`); only DoAction (12) and DoInitAction (59) are accepted. Radicle's refusal (an out-of-range index, another tag code, or an undecodable stream) surfaces as its `RadicleException`. Both read `Movie` and never enter the history, so neither changes `Revision` or `Dirty`.

## FO4RecordEditor/Services/SwfInterop.cs:SwfInterop.InspectText, SwfInterop.ActionTags and SwfInterop.Avm1Disassemble (inspect RPCs)

The three methods are public on the `[RpcExpose]` class, so `RpcDispatcher` routes them to the WebView2 host object and the server without registration. `InspectText` replies `{text}`. `ActionTags` replies `{tags: [{tagIndex, code, insideSprite}]}` for the code 12 and 59 tags in flat order; the panel passes those indices to `Avm1Disassemble`, which replies `{text}`. Refusals go through `Run` as `{error, status}`, the same shape as every other RPC.

## web/src/SwfPanel.tsx:SwfPanel (Inspect section)

The "Inspect" section sits above the ABC section in the right aside. `open` clears the inspect text, the action listing, the picked tag and its error before the host `Open` call, then loads `InspectText` and `ActionTags` after the ABC listing, so a refused re-open leaves no stale listing. `pickActionTag` (`web/src/SwfPanel.tsx:SwfPanel.pickActionTag`) increments `avm1Request` before its await and applies a reply only while its number is still current, the same guard the ABC view uses, so an out-of-order disassembly cannot replace the one picked last. A refused `InspectText` or `ActionTags` goes into the section alert; a refused `Avm1Disassemble` goes into the disassembly alert. The view is display-only and edits no SWF bytes.

Tests: `web/src/SwfPanel.test.tsx:SwfPanel` picks `DoAction @9` and asserts `Avm1Disassemble(9)` and the listing text in the `avm1 disassembly` block. Regression: the disassembly call or the listing being dropped from the panel, or the pick sent with a different index.

## FO4RecordEditor.Core.Tests/SwfFixture.cs:SwfFixture.ActionTag (opt-in DoAction)

`SwfFixture.Movie(withAction: true)` adds a root DoAction whose bytes are Play, Stop, Trace, End (the bytes Radicle's `action_movie` uses in `Radicle/tests/test_c_api_swf_text.cpp`). It is opt-in so the other document-service tests keep their flat tag indices.

## FO4RecordEditor.Core.Tests/SwfDocumentServiceTests.cs:SwfDocumentServiceTests.InspectTextHeadsTheMovieWithItsAssetPathAndListsItsRootLabels

Regression: the inspect text read from the wrong movie, or its `## <asset path>` heading lost or replaced by the file name. It asserts the heading and the root label line only, not the full listing, which Radicle's own tests pin.

## FO4RecordEditor.Core.Tests/SwfDocumentServiceTests.cs:SwfDocumentServiceTests.Avm1DisassemblesADoActionTagAndRefusesANonActionTag

Regression: a DoAction listed from a wrong flat index or payload offset (the expected text is exact), a non-action tag (a FrameLabel, code 43) decoded as bytecode instead of refused by `SwfDocumentService.cs:SwfDocument.Avm1Disassemble`, or a refused disassembly bumping `Revision`.

## FO4RecordEditor/Services/SwfInterop.cs:SwfInterop.BitmapPng (bitmap preview RPC)

`SwfInterop.BitmapPng(tagIndex)` calls `Document().Movie.BitmapPng`, which is the Radicle `radicle_swf_bitmap_png` wrapper (`Radicle/bindings/csharp/Radicle.Interop/SwfFile.cs:SwfFile.BitmapPng`), and replies `{png}` with the PNG bytes as base64. The index is the flat tag index that `Characters` reports as `tagIndex` for a bitmap, so the RPC needs no range check of its own: a negative index becomes an out-of-range `nuint` and Radicle refuses it as INVALID_ARGUMENT, and a non-bitmap tag is refused as NOT_FOUND. Both reach the panel as `{error, status}` through `Run`, the same shape as `ShapeSvg`. Radicle refuses a bitmap whose format is not 3 or 5 as INVALID_ARGUMENT. A JPEG-defined character has no decoded bitmap, so `BitmapPng` refuses it as NOT_FOUND; the panel falls back to `BitmapJpeg` only on that NOT_FOUND status and shows any other error as is.

## FO4RecordEditor/Services/SwfInterop.cs:SwfInterop.BitmapJpeg (JPEG bitmap preview RPC)

`SwfInterop.BitmapJpeg(tagIndex)` calls `Document().Movie.BitmapJpeg`, the Radicle `radicle_swf_bitmap_jpeg` wrapper (`Radicle/bindings/csharp/Radicle.Interop/SwfFile.cs:SwfFile.BitmapJpeg`), and replies `{jpeg}` with the JPEG bytes as base64. It takes the same flat tag index as `BitmapPng`. A non-JPEG tag and a DefineBitsJPEG3/4 image with alpha are refused as NOT_FOUND and reach the panel as `{error, status}` through `Run`. Regression: a JPEG bitmap previewed as an error, or a refused alpha JPEG served as an empty image (`FO4RecordEditor.Tests/SwfInteropTests.cs:BitmapJpegRpcReturnsTheBase64JpegForAJpegBitmapAndTheRefusalForANonBitmap`).

## web/src/SwfPanel.tsx:SwfPanel.pickCharacter (bitmap preview selection)

A `Bitmap` pick in `web/src/SwfPanel.tsx:SwfPanel.pickCharacter` sends `character.tagIndex` to `BitmapPng`, not `character.id`, because the Radicle export is keyed by the flat tag index. It shares the `shapeRequest` counter with the shape branch, so a late reply cannot replace the preview of a newer pick, and every pick clears `bitmapSrc` before it awaits. A `NotFound` from `BitmapPng` sends the same tag index to `BitmapJpeg`, and that reply is checked against `shapeRequest` again. A refusal goes to the `swf-error` bar through `setError`, the same as a refused `ShapeSvg`. The image is an `<img alt="Bitmap preview">` whose `src` is the ready `bitmapSrc` data URL: `data:image/png;base64,` plus the PNG reply, or `data:image/jpeg;base64,` plus the JPEG reply. `web/src/SwfPanel.css` gives `.swf-bitmap` `max-width: 100%` and `image-rendering: pixelated` so small bitmaps scale up without smoothing.

Tests: `web/src/SwfPanel.test.tsx:SwfPanel` picks two bitmap rows and asserts `BitmapPng` is called with tag 4 then tag 5, the PNG data URL is rendered for the first, and the refusal for the second shows its message with no image left over. It also covers a `NotFound` PNG refusal that falls back to `BitmapJpeg` and renders an `image/jpeg` data URL, a PNG success that never calls `BitmapJpeg`, and a refusal of both exports that shows the JPEG message. Regression: a bitmap previewed from its character id, a PNG or JPEG served under the wrong MIME type, a PNG refusal shown for a JPEG bitmap, or a refused export swallowed into a stale preview.

## PluginEditTool/FO4RecordEditor.Core.Tests/SwfFixture.cs:SwfFixture.BitmapTag (opt-in lossless2 bitmap)

`SwfFixture.Movie(withBitmap: true)` adds a DefineBitsLossless2 (36) character 20 at tag index 0: format 5, 2x2, zlib-compressed 16 zero bytes, built in `PluginEditTool/FO4RecordEditor.Core.Tests/SwfFixture.cs:SwfFixture.BitmapTag`. It is opt-in so the other document tests keep their character counts and flat indices.

## PluginEditTool/FO4RecordEditor.Core.Tests/SwfDocumentServiceTests.cs:SwfDocumentServiceTests.ExportsABitmapCharacterAsAPngOfItsAuthoredSizeFromItsFlatTagIndex

Regression: a PNG encoded from a different tag than the one `PluginEditTool/FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.Characters` reports, or encoded with the wrong width and height. `PluginEditTool/FO4RecordEditor.Core.Tests/SwfDocumentServiceTests.cs` reads the PNG signature and the IHDR width and height bytes (offsets 16 to 23) from `SwfDocument.Movie.BitmapPng`.

## PluginEditTool/FO4RecordEditor.Tests/SwfInteropTests.cs:SwfInteropTests.BitmapPngRpcReturnsTheBase64PngForABitmapAndTheRefusalForANonBitmap

Regression: a `png` field dropped or not base64 of the PNG bytes in `PluginEditTool/FO4RecordEditor/Services/SwfInterop.cs:SwfInterop.BitmapPng`, a bitmap exported from the character id instead of the `tagIndex` from `PluginEditTool/FO4RecordEditor/Services/SwfInterop.cs:SwfInterop.Characters`, or an edit-text tag exporting as a PNG instead of replying the error shape.

## PluginEditTool/FO4RecordEditor/Services/SwfInterop.cs:SwfInterop.FrameSvg (root frame preview RPC)

`SwfInterop.FrameSvg(frame)` calls `Document().Movie.FrameSvg` with the frame as a `uint`, which is the Radicle `radicle_swf_frame_svg` wrapper `Radicle/bindings/csharp/Radicle.Interop/SwfFile.cs:SwfFile.FrameSvg`, and replies `{svg}` with the composited root frame, the same shape as `PluginEditTool/FO4RecordEditor/Services/SwfInterop.cs:SwfInterop.ShapeSvg`. A negative frame is refused in `PluginEditTool/FO4RecordEditor/Services/SwfInterop.cs:SwfInterop.FrameSvg` before the cast, since `(uint)` of a negative `int` would read as a huge frame and come back as an empty stage with no error. A frame past the root timeline is not a refusal: Radicle draws an empty, well-formed stage, so `PluginEditTool/FO4RecordEditor/Services/SwfInterop.cs:SwfInterop.FrameSvg` never replies an error for it.

## PluginEditTool/web/src/SwfPanel.tsx:SwfPanel.showFrame (frame preview selection)

`PluginEditTool/web/src/SwfPanel.tsx:SwfPanel.showFrame` clamps the requested frame to `0..rootFrames-1` and sends it to `FrameSvg`. `rootFrames` is stored by `PluginEditTool/web/src/SwfPanel.tsx:SwfPanel.open` from `Timeline(0)`, not on every frame change, because only an Open replaces the document, and `PluginEditTool/FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.InsertKeyframe` refuses any frame past the root show-frame count, so no edit grows the root timeline. Every frame request takes the `frameRequest` counter, the same pattern as `shapeRequest` in `PluginEditTool/web/src/SwfPanel.tsx:SwfPanel.pickCharacter`, so a late reply for an earlier frame cannot replace the frame shown last. Every request clears `frameSvg` before it awaits, so a refusal goes to the error bar through `setError` and leaves no image behind.

The image is an `<img alt="Frame preview">` with the `data:image/svg+xml;utf8,` URL that `PluginEditTool/web/src/SwfPanel.tsx:SwfPanel` builds for the shape preview, so the SVG is never injected as markup. `PluginEditTool/web/src/SwfPanel.css` gives `.swf-frame-image` `max-width: 100%`.

## PluginEditTool/web/src/SwfPanel.test.tsx:SwfPanel (frame preview and stale-reply tests)

`PluginEditTool/web/src/SwfPanel.test.tsx:SwfPanel` has three new cases. The frame-preview case in `PluginEditTool/web/src/SwfPanel.test.tsx:SwfPanel` asserts `FrameSvg` is called once with 0 on Open, is bounded to 0..2 for a three-frame root, is clamped for `9` and `-3`, and that a refused frame shows its message with no image. Regression: a frame sent past the root timeline or below zero, a sprite frame count used as the bound, or a refused frame left on screen as a stale image. The FrameSvg stale case holds the reply for frame 1 until frame 2 has resolved and asserts the frame 2 image survives. Regression: an out-of-order `FrameSvg` reply overwriting the newer frame. The BitmapPng stale case does the same for two bitmap picks on the bitmap branch of `PluginEditTool/web/src/SwfPanel.tsx:SwfPanel.pickCharacter`, which had no stale-guard test before. Regression: an out-of-order `BitmapPng` reply overwriting the newer bitmap preview.

## PluginEditTool/FO4RecordEditor.Tests/SwfInteropTests.cs:SwfInteropTests.FrameSvgRpcReturnsTheRootFrameAndAnEmptyStageBeyondTheTimeline

Regression: a `svg` field dropped from the `PluginEditTool/FO4RecordEditor/Services/SwfInterop.cs:SwfInterop.FrameSvg` reply, a frame past the root timeline surfaced as an error instead of an empty stage, or a negative frame reaching `Radicle/bindings/csharp/Radicle.Interop/SwfFile.cs:SwfFile.FrameSvg` as a wrapped `uint` and exporting as svg. The fixture is `PluginEditTool/FO4RecordEditor.Core.Tests/SwfFixture.cs:SwfFixture.Movie` with `withShape: true`.

## PluginEditTool/web/src/SwfPanel.tsx:SwfPanel (frame preview on Open and after edits)

`PluginEditTool/web/src/SwfPanel.tsx:SwfPanel.open` calls `showFrame(0)` after it stores `rootFrames` from `Timeline(0)`, guarded by the local root count so a zero-frame timeline sends no `FrameSvg` call. A `Timeline(0)` refusal goes to the error bar through `setError` in `PluginEditTool/web/src/SwfPanel.tsx:SwfPanel.open` instead of leaving `rootFrames` at 0 with no message.

Every document change redraws the preview through one effect in `PluginEditTool/web/src/SwfPanel.tsx:SwfPanel`, not through each edit handler. The effect compares the `revision` from the `State` reply and calls `showFrame(previewFrame)` when it moves. Every edit, undo and redo bumps it, because `Radicle/src/swf/swf_history.cpp:SwfEditHistory::replay` increments `revision_` and `SwfEditHistory::undo` and `SwfEditHistory::redo` both call it. `PluginEditTool/web/src/SwfPanel.tsx:SwfPanel` sets `syncedDoc` when Open replaces the document, so a re-open does not trigger a second redraw. A Save does not move the key and does not redraw. The `syncedDoc` ref stops a change of `previewFrame` or `rootFrames` from redrawing the frame, so a navigation still makes exactly one `FrameSvg` call.

The preview's number input in `PluginEditTool/web/src/SwfPanel.tsx:SwfPanel` ignores an empty or non-finite value, so clearing the field does not jump to frame 0. The test `PluginEditTool/web/src/SwfPanel.test.tsx:SwfPanel` for an edit asserts that a depth change re-requests the preview frame, not frame 0. The frame-0 `FrameSvg` RPC test in `PluginEditTool/FO4RecordEditor.Tests/SwfInteropTests.cs:SwfInteropTests.FrameSvgRpcReturnsTheRootFrameAndAnEmptyStageBeyondTheTimeline` does not assert a `<path` element: the `withShape` fixture in `PluginEditTool/FO4RecordEditor.Core.Tests/SwfFixture.cs:SwfFixture.Movie` defines the shape with tag 2 but places only the edit-text character on the root timeline.

## PluginEditTool/web/src/SwfPanel.tsx:SwfPanel (preview redraw through an effect event)

`PluginEditTool/web/src/SwfPanel.tsx:SwfPanel` redraws the preview through `syncFramePreview`, a `useEffectEvent` wrapper around `showFrame`. `showFrame` sets state synchronously (`setPreviewFrame`, `setFrameSvg`) before its first `await`, so calling it directly in the redraw effect trips the `react-hooks/set-state-in-effect` lint error; the event wrapper is the sanctioned indirection and is exempt from the effect dependency list. The base bridge stub in `PluginEditTool/web/src/SwfPanel.test.tsx:stubBridge` carries default `ShapeSvg`, `BitmapPng`, and `BitmapJpeg` mocks matching the `PluginEditTool/web/src/SwfPanel.tsx:SwfHost` interface, so per-test overrides only replace behavior while the shared type keeps every call site checked; a missing entry surfaces as TS2339 where the test reads it, which is how the bitmap and shape preview tests broke the `tsc -b` build step.

## PluginEditTool/web/src/SwfPanel.tsx:SwfPanel (preview redraw from document-change handlers)

`PluginEditTool/web/src/SwfPanel.tsx:SwfPanel` no longer redraws the preview from an effect: even through the `useEffectEvent` wrapper, calling `showFrame` in the effect body trips `react-hooks/set-state-in-effect` (the v6 rule flags the wrapped call too). The seven edit handlers in `PluginEditTool/web/src/SwfPanel.tsx:SwfPanel` (`renameLabel`, `applyEditText`, `addLabel`, `applyTransform`, `insertKeyframe`, `applyDepth`, `removeLabel`) and `PluginEditTool/web/src/SwfPanel.tsx:SwfPanel.refresh` on the undo/redo/save path now call `PluginEditTool/web/src/SwfPanel.tsx:SwfPanel.applyRevision` after `setState`; it redraws through `PluginEditTool/web/src/SwfPanel.tsx:SwfPanel.redrawPreview` only when the `revision` moved, and `PluginEditTool/web/src/SwfPanel.tsx:SwfPanel.open` seeds `previewRevision` so a re-open does not double-draw. Regression: a moved `showFrame` call back inside any effect, or a new edit handler that sets state without calling `applyRevision` (the preview would keep the pre-edit image).

## PluginEditTool/native/src/app/SwfTarget.cpp:SwfTarget (native swf RPC target)

Files: `PluginEditTool/native/src/app/SwfTarget.cpp` holds the method dispatch, the argument table and the edit verbs; `PluginEditTool/native/src/app/SwfTargetFile.cpp` holds `swfRootsFor`, `open`, `save`, `snapshot`, `refreshFromHistory` and `requireUnderRoots`; `PluginEditTool/native/src/app/SwfTargetSupport.hpp` holds the error, text and byte helpers; `PluginEditTool/native/include/fo4ide/app/SwfTarget.hpp` declares the class and the `SwfRootsProvider` type.

`PluginEditTool/native/src/app/SwfTarget.cpp:SwfTarget` implements every `PluginEditTool/FO4RecordEditor/Services/SwfInterop.cs:SwfInterop` method with the same names, argument order and JSON response shapes, so `PluginEditTool/web/src/SwfPanel.tsx:SwfPanel` works unchanged against FO4IDE native. The target holds one open document (a Radicle SWF handle plus a `radicle_swf_history`) and serialises every call on a mutex, matching the C# `Gate` lock. Reads go straight to the `radicle_swf_*` getters; the ABC instruction edits go through the one history verb `Radicle/src/c_api_swf_abc_write.cpp:radicle_swf_history_abc_text_edit`, which resolves the ABC tag inside the history's own movie, so the reader handle keeps no scratch copy and a stale tag index cannot land the edit on the wrong tag. A failed edit captures the Radicle message before any handle destroy or free, because `Radicle/src/c_api_swf.cpp:radicle_swf_destroy` and `Radicle/src/c_api.cpp:radicle_free_bytes` clear it. Every edit, undo and redo re-reads the history bytes and reopens the movie view, matching the `SwfDocumentService.cs:SwfDocument` reopen invariant (`Movie` is reopened after every Apply, Undo, Redo and Verb); when the new bytes do not decode, the target undoes the history step so the history and the movie agree again; if that undo also fails, the history stays one step ahead and Save refuses until a later step resyncs it (see "Refresh after a history step" below). `Open` refuses paths outside the roots from the provider given to the constructor, and the provider runs on every Open: `PluginEditTool/native/src/app/SwfTargetFile.cpp:swfRootsFor` returns the four roots that `PluginEditTool/FO4RecordEditor.Core/Services/ToolPaths.cs:ToolPaths.RequireDataOrProjectPath` checks (the Data folder, the Fallout 4 folder itself, the MO2 instance folder and the output folder), and `PluginEditTool/native/apps/fo4ide/main.cpp:main` passes a provider over `currentSettings`, so a `--mo2` or settings change applies to the next Open without a restart. `SwfTarget::requireUnderRoots` accepts a path only when it is a strict descendant of a non-blank root, after `weakly_canonical` on both sides (the `IsUnder` rule of `ToolPaths.RequireUnderRoots`). `Open` closes the current document before it checks the new path, as the C# host disposes its document, so a refused Open leaves nothing open and later edits cannot reach the old one. The stored path is the canonical one, so `Save` writes where `Open` read. A file over `core::kMaxSwfFileBytes` (256 MiB, `PluginEditTool/native/include/fo4ide/core/PayloadLimits.hpp`, the Radicle `kMaxBodyBytes` cap) is refused from `fs::file_size` before any read. The RPC and HTTP layers have no path guard of their own. All JSON dumps use the replace error handler, so file bytes that are not UTF-8 (label names, text, disassembly) serialise with U+FFFD instead of throwing. Only file paths open; the C# archive-resolver branch (`SwfInterop.Open` falling back to the PET asset resolver) is refused with an error, because `Open` in the native target has no asset resolver; `OpenArchived` covers game archives. Refusals answer `{"error", "status"}` with the `Status.ToString` names the panel's `status` checks read; every method's arguments are checked before dispatch against one table, `PluginEditTool/native/src/app/SwfTarget.cpp:expectedArguments` (`s` a string, `i` and `u` integers that must not be negative, `h` an integer from 0..65535, `z` any integer that fits 32 bits), by `SwfTarget.cpp:argumentRefusal`. A missing, mistyped, non-integer, out-of-range or negative argument answers `{"error"}` with "argument N is missing", "must be a string", "must be an integer", "must be an integer from 0..65535" or "must not be negative", with no status, and the document does not change. The table follows the `PluginEditTool/FO4RecordEditor/Services/SwfInterop.cs:SwfInterop` signatures.

The three placement edits, the two placement readers, the action-tag list and the ABC text edit are new Radicle verbs (ABI 96, see the `Radicle/src/swf/swf_placement_edit.cpp:set_placement_depth_flat` note), not target-side logic: `SetDepth`, `SetTransform` and `InsertKeyframe` forward the panel ints (`argumentRefusal` refuses negative and out-of-range indices before they reach Radicle), `Placements` and `ActionTags` reshape the Radicle snake-case JSON into the camelCase `SwfInterop` shape (the DoAction/DoInitAction code filter lives in Radicle), and the unscaled-matrix 0x10000 default lives in `Radicle/src/c_api_swf_verbs.cpp:radicle_swf_placement_matrix_at`. The encoder-flag deviation is recorded on the Radicle side and is rendering-neutral.

Refresh after a history step: `PluginEditTool/native/src/app/SwfTargetFile.cpp:SwfTarget::refreshFromHistory` reopens the movie from the history bytes (`reopenHistory`). When the reopen fails it takes the opposite step, undo after an edit or a Redo and redo after an Undo, then resyncs the movie with an ignored message and returns false with the decode message, so the reply is an error and the movie is the state before the step. A Redo that fails this way leaves its command on the redo stack, so the next Redo fails the same way; that is accepted, and no ABI call drops it. If the opposite step itself fails, the history stays one step ahead of the movie: history-backed reads (FrameLabels, the placement and edit-text reads) show that step, movie-backed reads (Characters, Frame) show the last good movie, and Save refuses because the history bytes do not reopen (`PluginEditTool/native/src/app/SwfTargetFile.cpp:SwfTarget::save`). Test: `SwfTarget steps back a Redo whose result cannot reopen, so Save writes a movie that reopens`, on the tag-cap movie `tagCapMovie` in `PluginEditTool/native/tests/unit/SwfTargetTestSupport.hpp`. The second failure case and the Undo branch have no test: no RPC path makes an undo's result fail to reopen, so that branch is unverified.

## PluginEditTool/web/src/hostbridge.ts:HOST_OBJECTS (swf entry)

`swf` is registered in `PluginEditTool/native/apps/fo4ide/main.cpp:main` and added to `HOST_OBJECTS`, so the HTTP bridge exposes it at `/rpc` like every other target. `PluginEditTool/web/src/hostbridge.test.ts` pins the `swf.State` route. No `SwfPanel.tsx` change was needed: every reply shape already matched.

Tests: `PluginEditTool/native/tests/unit/SwfTargetTests.cpp:SwfTarget` builds the `SwfFixture.Movie(withAbc:true)` bytes in C++ (same tags, same ABC body) and pins open/state, rename plus undo/redo with the state checked after each step, an `InsertAbcInstruction` nop edit visible in `Disassemble`, the unscaled-matrix `PlacementMatrix` defaults, save plus reopen with the edit present, a failing `InsertAbcInstruction` with a non-empty message, a Latin-1 label dumped with U+FFFD, path refusals (empty roots, outside roots, `..` escape) and a passing rooted open, a keyframe insert pinned by the post-insert placement list with undo and frame-range refusals, and the `RenameLabel` out-of-range and `ShapeSvg(-1)` refusal shapes. It is registered as `fo4ide_test_swf_target` in `PluginEditTool/native/tests/CMakeLists.txt`. The colour case `SwfTargetTests.cpp:SwfTarget` refuses `12zz`, `-1` and `1 2` and accepts `0xFF00FF00`: `EditEditText` in `PluginEditTool/native/src/app/SwfTarget.cpp:SwfTarget` now parses the colour with `std::from_chars` in full after an optional `0x`, where `std::stoul` had read `12zz` as `0x12` and `-1` as `0xFFFFFFFF` and written them.

Cases for the roots, size, argument, rollback and canonical-path fixes, each with a `// Constraint:` line that names its regression: `SwfTarget reads the MO2 instance root from settings on each Open`, `SwfTarget treats the configured Fallout 4 folder as a root`, `SwfTarget saves to the canonical path it opened`, `SwfTarget closes the open document when a later Open is refused`, `SwfTarget refuses an SWF over the size limit before reading it`, `SwfTarget refuses malformed arguments without editing the document`, `SwfTarget refuses an edit whose result cannot reopen and keeps the previous movie`, and `SwfTarget does not report a save when the first-save backup cannot be copied`. Round 3 added `SwfTarget steps back a Redo whose result cannot reopen, so Save writes a movie that reopens`, `SwfTarget names an unknown method even when no document is open` and `SwfTarget does not report a save when the atomic write fails`. Revert proofs on the final tree, each rebuilt and restored: without the step-back for Undo and Redo (`reopenHistory` in place of `refreshFromHistory`), the Redo case fails 5 assertions; without the backup-copy check in `save`, the backup-copy case fails 2 assertions. Round 2 mutation runs, taken before the round-3 changes to refresh and save and not re-run: roots captured once at construction (1 failed assertion), the MO2 root dropped from `swfRootsFor` (1), the Fallout 4 folder replaced by its Data subfolder (2), the rollback returning true (1), the refusal ignored (7), a non-integer argument skipped (5), the range check dropped (3), the canonical path replaced by the raw path (1), the close on Open removed (1), the size cap disabled (1), and the backup error ignored with the write skipped (2).

Verified in WSL (GCC 15, Linux): `fo4ide_test_swf_target`, configured outside the clone with the cached Catch2 3.15.3, spdlog, reproc and httplib sources and the Radicle build's nlohmann_json, passes 23 test cases and 353 assertions on the final tree (`All tests passed`), with zero warnings in the build log; `ctest -R SwfTarget` passes 23 of 23. Before the colour fix the same binary failed that one case. Not run: the managed `FO4RecordEditor.Tests` and the web `hostbridge.test.ts`, and nothing on Windows.

## PluginEditTool/native/src/app/SwfTargetFile.cpp:SwfTarget::save (Save, first-save backup, atomic write)

`PluginEditTool/native/src/app/SwfTargetFile.cpp:SwfTarget::save` copies the on-disk file to `<path>.bak` only when that backup does not exist yet (the `WriteService.Asset.cs:WriteService.SaveAsset` shape, so later saves never overwrite the first backup), then writes the history bytes with `fo4::io::write_file_atomic` (see `PluginEditTool/docs/internal/ARCHITECTURE.md:write_file_atomic` and `PluginEditTool/native/src/material/MaterialService.cpp:write_material_file`). `saved_` moves only after the write returns. Save decodes the history bytes before it writes, so bytes that do not reopen are refused with nothing written (one extra decode per save). Regression: a `save` that reports `saved` after a failed backup copy, pinned by `SwfTarget does not report a save when the first-save backup cannot be copied` (a dangling `.bak` symlink, so only the copy fails; POSIX only, skipped on Windows), and a `save` that reports `saved` after a failed atomic write, pinned by `SwfTarget does not report a save when the atomic write fails` (the destination replaced by a directory, `.bak` already present).

Atomicity is not tested. The earlier test that claimed the destination keeps its original bytes was removed: its backup copy failed before `write_file_atomic` ran, so the replace was never reached. No portable way to make the replace fail was found (a read-only directory is not a reliable forced failure on Windows), so a truncating write in place of `write_file_atomic` is not caught by any test.

Tests: `SwfTarget keeps the first-save backup across later saves` (`PluginEditTool/native/tests/unit/SwfTargetTests.cpp:SwfTarget`) pins the `.bak` holding the original bytes after two saves and the reopened second edit. The refusal path is pinned by the backup test named above. Both run with the rest of `fo4ide_test_swf_target` in WSL.

Timing: the two `[slow]` cases (`SwfTarget refuses an edit whose result cannot reopen and keeps the previous movie` and `SwfTarget steps back a Redo whose result cannot reopen, so Save writes a movie that reopens`) take 11 to 43 s across three WSL Release `ctest -R SwfTarget` runs (the reopen case 11 to 22 s, the Redo case 19 to 43 s), over the 10 s `FO4IDE_TEST_TIMEOUT`. `PluginEditTool/native/tests/CMakeLists.txt:fo4ide_add_test` registers the `[slow]` cases with `FO4IDE_SLOW_TEST_TIMEOUT` (300 s), so a slower Debug or sanitizer run has margin; every other case keeps the global timeout. No cheaper reopen failure was found: the reader's tag cap counts children (`Radicle/src/swf/swf.cpp:read_swf`, `kMaxTags`), so the failure needs a 262144-tag base; the plan-level check in `Radicle/src/swf/swf.cpp:apply_tag_plan` counts only top-level tags, so it does not refuse the edit earlier. The seconds are the decode of that base.

Open items, not verified here:
- `PluginEditTool/native/src/app/SwfTargetFile.cpp:SwfTarget::requireUnderRoots`: the containment check compares `fs::path` components, which may be case-sensitive on Windows, while the C# `ToolPaths.IsUnder` uses `OrdinalIgnoreCase`. No Windows build or test was run for this.
- `PluginEditTool/native/src/app/SwfTargetFile.cpp:SwfTarget::open`: accepts file paths only (`OpenArchived` takes an archive entry name). The C# `PluginEditTool/FO4RecordEditor/Services/SwfInterop.cs:SwfInterop.Open` falls back to `SwfDocument.OpenResolved` (the asset resolver) for a path that is not a file; that branch is refused here.
- `PluginEditTool/native/src/settings/Settings.cpp:from_json`: applies the `FALLOUT4_PATH` and `FO4_DATA_PATH` environment variables over settings.json, so the roots follow the environment here. The C# roots (`PluginEditTool/FO4RecordEditor.Core/Services/ToolPaths.cs:ToolPaths.RequireDataOrProjectPath`) come from settings.json only; C# reads environment variables for tool paths such as `NIFTOOL_PATH`, not for the roots.

## PluginEditTool/native/tests/helpers/GraphTestSupport.hpp:fixtureRoot

`PluginEditTool/native/tests/helpers/GraphTestSupport.hpp:fixtureRoot` joins `FO4IDE_TEST_FIXTURES_DIR` with a folder name, which is how `baseStubs()` and `graphScripts()` reach `PluginEditTool/native/tests/fixtures`. The define comes from `PluginEditTool/native/tests/CMakeLists.txt` (`FO4IDE_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"`), and the graph and Papyrus target cases feed those paths into `Settings::papyrusBaseImports`, so a target that drops the define fails to compile here, and a moved or renamed fixtures tree makes those cases resolve imports from the wrong directory. The header carries no comment saying so, because the value lives in CMake where a reader of the header cannot check it.
