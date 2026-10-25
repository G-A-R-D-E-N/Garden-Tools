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
one `SwfDocument`, `Characters`/`FrameLabels`/`Frame` return the read side as JSON, `RenameLabel` composes a
one-replacement tag plan from the flat tag index, `Undo`/`Redo`/`State` drive the history, and `Save` writes
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

Tag 37 has no ABI 67 getter, so the managed layer parses and re-encodes the decoded tag payload itself:
`SwfEditTextCodec.Parse` reads bounds (RECT bits), both flag bytes, font id/class, height, colour, max length,
layout, variable and initial text, and `Write` reproduces the decoder order and reuses the original id/RECT
prefix unless the bounds are edited, so a text or colour edit keeps every earlier byte. `SwfFile.EditText` and
`SwfFile.EditEditText` expose it and send the rewritten payload through the normal tag plan, so history,
revision and refusal behaviour are unchanged. Tests in `SwfEditTextTests.cs` pin the full read, the
prefix-preserving text edit, colour and bounds edits, the no-op plan, and the wrong-tag refusal. This is the
"compose from tag payloads in PET" path for edit text; the remaining property edits (transform/depth/keyframe,
label add/remove) still need either the same treatment or new native exports.
## FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.EditEditText (edit-text through the history)

`SwfDocument.EditText` reads the managed tag-37 record and `EditEditText` writes the re-encoded payload through
`Apply`, so an edit-text change is one history command with the same undo/redo/refusal behaviour as any other
plan. `SwfInterop` exposes `ReadEditText`/`EditEditText` for the panel (colour/rect/text are flattened JSON; the
colour stays `0xAARRGGBB`), and `SwfDocumentServiceTests.EditsEditTextThroughTheSameHistory` pins the edit plus
its undo. The panel fields for this verb and the placement/label property edits are still next.
## web/src/SwfPanel.tsx (edit-text properties)

Selecting a character whose kind is EditText calls `ReadEditText` through the host bridge and fills the right-hand
fields (text, colour, bounds rect); Apply sends `EditEditText` with those values and folds the returned state
back into the header. The field-level refusal banner is the same one all commands use, so a Radicle refusal
appears next to the panel verbatim. The web test drives open → select edit text → change text → apply and asserts
the bridge call. Placement transform/depth/keyframe editing and label add/remove are still out of the panel.
## FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.AddLabel (label insertion)

`AddLabel` finds the tag index of the requested root ShowFrame, refuses a duplicate root-timeline name and a frame
the timeline does not reach, then inserts a new FrameLabel tag through the plan, so the label add is one history
command that undo removes. `SwfInterop.AddLabel` exposes it to the panel and the panel's "Add at frame" button
uses the label name field and the current scrubber frame. Removal and sprite-timeline adds still need payload
composition or a native removal verb.
## Radicle/bindings/csharp/Radicle.Interop/SwfPlacements.cs:SwfPlacementCodec (depth edit)

Placement tags have no ABI 67 getter, so `Placements()` parses the root and sprite placement payloads
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

A first keyframe form: for a root PlaceObject2 placement, `InsertKeyframe` copies its payload, forces the Move flag and
clears the character bit, and inserts the copy before the requested frame's root ShowFrame through the plan, so it is
one history command that undo removes. A sprite placement or a non-PlaceObject2 tag is refused this round;
`SwfInterop.InsertKeyframe` and the panel's "Keyframe at frame" button (scrubber frame) use it.
## FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.InsertKeyframe (sprite timelines)

For a placement inside a DefineSprite, `InsertKeyframe` finds the sprite frame's ShowFrame child, splices a new
Move-flagged placement tag (short or long header as the length needs) into the sprite payload at that child's header
offset, and replaces the DefineSprite tag through the plan. The fixture's sprite now carries a placement, so the
service test pins the insertion and undo on that timeline too.
## FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.OpenResolved (archive/asset open)

`OpenResolved` takes the logical asset path and a resolver delegate (the host's existing asset resolution), reads the
bytes and builds the same document as a loose file. The document deliberately keeps no filesystem path for this
form, so Save requires an explicit target; the host can therefore open BA2-resolved bytes and write back through its
own asset-write path. The service test drives a resolver over the fixture bytes and asserts the reads.
## FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.RemoveLabel (label removal)

The C plan now carries a removal list (`radicle_swf_tag_removal`, regenerated interop), so `RemoveLabel` verifies the tag
is a FrameLabel and applies a one-removal plan; undo replays the command list from the original bytes and restores the
label. The panel's Remove button calls it for the selected label. The removal verb stays at ABI 67 because that surface
was never pushed before this round.
## FO4RecordEditor.Core/Services/SwfDocumentService.cs:SwfDocument.AddLabel (sprite timelines)

`AddLabel` now takes the timeline id: root uses a plan insertion before the frame's ShowFrame, a sprite timeline finds
the DefineSprite tag, splices the FrameLabel tag into the sprite payload at the target child's header offset and
replaces that tag through the plan. Duplicate names are checked per timeline; the service test pins sprite add + undo.
The panel passes its current timeline scrubber value.
## FO4RecordEditor/Services/SwfInterop.cs:SwfInterop.Open (loose or archive)

Open checks for a loose file first; otherwise it asks the existing PET `AssetResolver` (`Resolve` + `Materialize`) and
opens the materialized bytes, so a BA2-resolved Interface movie enters the same document/history path. A path that is
neither loose nor resolvable comes back as a JSON error. Saving a resolved document back into its archive is not wired
yet because PET has no generic asset-write path; the document save writes a target path with a `.bak`.
## FO4RecordEditor.Core/Services/WriteService.Asset.cs:WriteService.SaveAsset (SWF document save)

The SWF document save now goes through the core `WriteService` static partial: `SaveAsset(path, bytes)` copies the
existing file to `<path>.bak` and writes the new bytes, matching the backup-first rule the other write paths follow.
`SwfDocument.Save` delegates to it, so the editor's Save and a future host asset-write hook share one implementation.