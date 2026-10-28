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
undo/redo/refusal behaviour. `SwfDocument.EditText` still reads the managed tag-37 record with `SwfEditTextCodec.Parse`;
`SwfInterop` exposes `ReadEditText`/`EditEditText` for the panel (colour/rect/text are flattened JSON; the colour stays
`0xAARRGGBB`), and `SwfDocumentServiceTests.EditsEditTextThroughTheSameHistory` pins the edit plus its undo.
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
place and sends the payload through the tag plan, keeping every other byte. `SwfDocument.SetDepth` and
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

For a PlaceObject2 placement, `InsertKeyframe` copies its payload, forces the Move flag, clears the character bit and drops
the two character-id bytes that followed the depth, then inserts the copy before the requested frame's root ShowFrame
through the plan, so it is one history command that undo removes. A non-PlaceObject2 tag is refused.
`SwfInterop.InsertKeyframe` and the panel's "Keyframe at frame" button (scrubber frame) use it.
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
(`SwfDocumentService.cs:SwfDocument.EditEditText`). The managed write was removed from `SwfEditText.cs:SwfEditTextCodec` (`Write`) and `SwfEditText.cs:SwfFile.EditEditText`. PET does not call the native read yet:
`SwfDocument.EditText` reads through the managed `Parse`.
## FO4RecordEditor/Services/SwfInterop.cs:SwfInterop.Run (one lock per call)

`Run` takes the static `Gate` around every action, so the document reference, its history and the lazy character cache are
touched by one call at a time. `Open` runs inside `Run` and takes no lock of its own.

## FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.ApplyNativeEdit (ABC instruction and glyph edits)

The native instruction and glyph verbs mutate a handle directly through `radicle_swf_apply_plan`, so running them on `Movie` would skip the history and vanish on the next reopen. `ApplyNativeEdit` runs the verb on a scratch `SwfFile` opened from the history bytes and lands the changed tag payload as one `Replace` plan. A refusal throws before `Apply`, so the revision, the undo stack and the dirty flag stay put. Covered by `SwfDocumentServiceTests.AnAbcInstructionEditIsAHistoryCommandAndUndoRedoFollowIt` and `SwfDocumentServiceTests.ARefusedAbcInstructionLeavesTheHistoryUntouched`.

## FO4RecordEditor/Services/SwfInterop.cs:SwfInterop.AbcBlocks and SwfInterop.Disassemble (ABC read side)

`AbcBlocks` lists every ABC block of the open movie with its class and script names and its method pool, read from `SwfFile.AbcBlocks` (already parsed) and `SwfFile.Methods(abc)`. The method pool index is the index `Disassemble(abc, methodIndex)` expects, so the panel passes `(abc, index)` straight back. An unparsed block reports zero pool methods (`radicle_swf_abc_method_count` returns 0 when `!view->parsed`), so it lists with no methods instead of failing the whole listing. A method with no body is disabled in the panel instead of being sent to `Disassemble`, which would refuse it. Covered by `FO4RecordEditor.Tests/SwfInteropTests.cs:SwfInteropTests.AbcRpcsReturnTheFieldsThePanelReadsAndTheRefusalShape`.

## web/src/SwfPanel.tsx:SwfPanel (ABC scripts view)

The "Scripts (ABC)" section sits at the foot of the right aside. `open` (`web/src/SwfPanel.tsx:SwfPanel.open`) clears the ABC listing, the selected method and `selectedMethodKey` before the host `Open` call, so a refused re-open leaves no stale listing and a late `pickMethod` reply from the previous document is dropped; it then loads `AbcBlocks` after the placement list. `pickMethod` (`web/src/SwfPanel.tsx:SwfPanel.pickMethod`) shows `Disassemble` output in the `instructions` preformatted block. It records the picked method key in `selectedMethodKey` before the await and drops a reply whose key no longer matches, so an out-of-order reply cannot overwrite the listing of the method now selected; a rejected host call goes into the section alert. An `AbcBlocks` refusal goes into the section's own alert rather than the header error bar, and the "No ABC scripts in this SWF." message only shows when the listing succeeds with zero blocks. The view is display-only: no ABC edit verb is wired, and the listing is not refreshed by undo or redo because no panel control edits ABC yet.

Tests: `web/src/SwfPanel.test.tsx` extends the existing `stubBridge` with `AbcBlocks` and `Disassemble` defaults and adds the listing, empty-state, refusal, stale-reply and refused-re-open cases (`web/src/SwfPanel.test.tsx:SwfPanel`).

## FO4RecordEditor/Services/SwfInterop.cs:SwfInterop.ShapeSvg (shape preview RPC)

`ShapeSvg(characterId)` runs `Document().Movie.ShapeSvg` (the Radicle `radicle_swf_shape_svg` wrapper, `Radicle/bindings/csharp/Radicle.Interop/SwfFile.cs:SwfFile.ShapeSvg`) and replies `{svg}`. An undefined id, a non-shape id or no open document throws, and `Run` turns that into the same `{error, status}` shape every other RPC returns, so there is no separate error path. The SVG content (fills as `<path>`) is pinned by Radicle's `test_c_api_swf_characters.cpp`; the PET side only proves the RPC wiring.

## web/src/SwfPanel.tsx:SwfPanel.pickCharacter (shape preview selection)

Every character pick increments `shapeRequest` and clears `shapeSvg` before it awaits, so picking a non-shape character leaves no preview and sends no `ShapeSvg`. A `ShapeSvg` reply is applied only while its request number is still current, the same request-number guard the ABC view uses with `selectedMethodKey`, so a late reply for an earlier shape cannot replace the one picked last. `SwfPanel.open` also increments the counter, so a reply from the previous document cannot land on a newly opened one. The preview is an `<img>` whose `src` is `data:image/svg+xml;utf8,` plus `encodeURIComponent` of the SVG text; the SVG is never assigned to `innerHTML`, so markup inside it is not parsed into the page.

## FO4RecordEditor.Core.Tests/SwfFixture.cs:SwfFixture.ShapeTag (opt-in shape character)

`SwfFixture.Movie(withShape: true)` adds an empty DefineShape (id 5, no fills, no lines, an end record). It is opt-in so the document-service tests keep their two-character count. The empty shape is enough to prove `ShapeSvg` returns a `<svg` document; a filled-shape path check would duplicate Radicle's own test.

Tests: `FO4RecordEditor.Tests/SwfInteropTests.cs:SwfInteropTests.ShapeSvgRpcReturnsTheSvgForAShapeAndTheRefusalForANonShape` (shape returns `<svg`, EditText returns an error), `web/src/SwfPanel.test.tsx:SwfPanel` (shape selection requests `ShapeSvg` and renders the image, a late reply for an earlier shape is ignored, and a non-shape pick clears the preview).