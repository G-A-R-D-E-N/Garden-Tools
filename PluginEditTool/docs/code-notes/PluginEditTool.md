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
redo → save plus the verbatim-refusal path. Wiring the panel into `MainShell` and the backend type surface is
deferred to the next round; nothing was built or run locally.
## web/src/MainShell.tsx (SWF panel registration)

The Interface (SWF) panel is registered the same way as the other asset panels: `showSwf` state, an activity-bar
button with the `Boxes` icon after Papyrus, and a conditional `SwfPanel` render beside `PapyrusPanel`/`NifPanel`.
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

## PluginEditTool/native/src/graph/GraphCompiler.cpp:GraphCompiler (compileScript stays on PET)

The graph compile still calls PET's `papyrus::compileScript` with PET's `ScriptIndex` and returns PET's `PexFile`. Radicle's
`fo4::papyrus::compileScript` has the same pass order but returns Radicle types, so moving it means moving the graph's script index
and PEX type too. It is left for the graph lift.

## PluginEditTool/native/tests/unit/PapyrusTargetTests.cpp:PapyrusTarget analysis RPCs match the PET oracle on real sources

Regression detected: a rewired answer that drifts from PET's, such as a dropped import root, an offset or path argument passed
through wrong, a changed JSON key, or a compile whose `.pex` bytes differ. Each section runs one RPC on real sources
(`GraphScripts/FixtureEventSource.psc`, `BaseStubs/Actor.psc`, and a broken `Bad.psc` written to the temp root) and requires the
same string PET's `AnalysisService.cpp:AnalysisService` returns. Compile also requires the written `FixtureEventSource.pex` bytes to match PET's.

## FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.ApplyNativeEdit (ABC instruction and glyph edits)

The native instruction and glyph verbs mutate a handle directly through `radicle_swf_apply_plan`, so running them on `Movie` would skip the history and vanish on the next reopen. `ApplyNativeEdit` runs the verb on a scratch `SwfFile` opened from the history bytes and lands the changed tag payload as one `Replace` plan. A refusal throws before `Apply`, so the revision, the undo stack and the dirty flag stay put. Covered by `SwfDocumentServiceTests.AnAbcInstructionEditIsAHistoryCommandAndUndoRedoFollowIt` and `SwfDocumentServiceTests.ARefusedAbcInstructionLeavesTheHistoryUntouched`.

## FO4RecordEditor/Services/SwfInterop.cs:SwfInterop.AbcBlocks and SwfInterop.Disassemble (ABC read side)

`AbcBlocks` lists every ABC block of the open movie with its class and script names and its method pool, read from `SwfFile.AbcBlocks` (already parsed) and `SwfFile.Methods(abc)`. The method pool index is the index `Disassemble(abc, methodIndex)` expects, so the panel passes `(abc, index)` straight back. An unparsed block reports zero pool methods (`radicle_swf_abc_method_count` returns 0 when `!view->parsed`), so it lists with no methods instead of failing the whole listing. A method with no body is disabled in the panel instead of being sent to `Disassemble`, which would refuse it. Covered by `FO4RecordEditor.Tests/SwfInteropTests.cs:SwfInteropTests.AbcRpcsReturnTheFieldsThePanelReadsAndTheRefusalShape`.

## web/src/SwfPanel.tsx:SwfPanel (ABC scripts view)

The "Scripts (ABC)" section sits at the foot of the right aside. `open` (`web/src/SwfPanel.tsx:SwfPanel.open`) clears the ABC listing, the selected method and `selectedMethodKey` before the host `Open` call, so a refused re-open leaves no stale listing and a late `pickMethod` reply from the previous document is dropped; it then loads `AbcBlocks` after the placement list. `pickMethod` (`web/src/SwfPanel.tsx:SwfPanel.pickMethod`) shows `Disassemble` output in the `instructions` preformatted block. It records the picked method key in `selectedMethodKey` before the await and drops a reply whose key no longer matches, so an out-of-order reply cannot overwrite the listing of the method now selected; a rejected host call goes into the section alert. An `AbcBlocks` refusal goes into the section's own alert rather than the header error bar, and the "No ABC scripts in this SWF." message only shows when the listing succeeds with zero blocks. The view is display-only: no ABC edit verb is wired, and the listing is not refreshed by undo or redo because no panel control edits ABC yet.

Tests: `web/src/SwfPanel.test.tsx` extends the existing `stubBridge` with `AbcBlocks` and `Disassemble` defaults and adds the listing, empty-state, refusal, stale-reply and refused-re-open cases (`web/src/SwfPanel.test.tsx:SwfPanel`).

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
