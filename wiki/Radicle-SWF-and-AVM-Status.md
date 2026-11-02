# Radicle SWF, AVM1 and AVM2: current status

Audience: developers new to the project. Source: `nomad/dev` checkout, read only. Nothing here was built or run. "Tests that prove it" means test files that exist and what their names or notes say they check, not a pass result. Items I could not confirm are marked "unverified".

Main evidence file: `Radicle/docs/code-notes/Radicle.md` (sections on SWF, ABC, AVM1, AVM2). Quotes of behaviour below come from there unless a source file is named.

## 1. What an SWF is

An SWF is a Flash movie file. Fallout 4 uses Scaleform, which plays Flash movies for its menus and HUD. Files come as `.swf` or Scaleform `.gfx`. A movie is a short header (signature, version, frame size, frame rate, frame count) followed by a list of tags. Tags define shapes, bitmaps, fonts, text, sounds and sprites, place them on a display list, mark frames, and carry code. Code is either AVM1 (old ActionScript, in `DoAction` tags) or AVM2 (ActionScript 3 bytecode called ABC, in `DoABC` / `DoABC2` tags). Radicle reads, edits, writes and partly runs these files in C++.

Scope note: the SWF editor UI lives in PluginEditTool. The C# GardenEngine `SwfTools` is a parity oracle, not shipped here (the code-notes name `SwfTools/ArtworkStyles.cs:ColorTransform.Compose` as the reference for colour composition).

## 2. The layers

```
bytes -> container + tags -> display list / timeline -> SVG render
                                   |                         |
                                   +--- editing (tag plan, history, verbs)
                                   |
                         AVM1 (decode/run/encode)   AVM2 (ABC model, text, editing, runtime)
```

Public C++ headers are under `Radicle/include/fo4/swf`, `.../avm1`, `.../avm2`, `.../abc`. Sources are under `Radicle/src/swf`, `Radicle/src/avm1`, `Radicle/src/avm2`, `Radicle/src/abc`. The C ABI is `Radicle/include/fo4/c_api_swf.h` and `c_api_swf_verbs.h` (about 89 and 10 `RADICLE_API` entries) with sources `Radicle/src/c_api_swf*.cpp`. AVM2 runtime entry points are in `Radicle/src/c_api_runtime.cpp`.

### 2.1 Container and tags

Works:
- `Radicle/src/swf/swf.cpp:read_swf` reads `FWS`, `CWS` (zlib) and the Scaleform `GFX` / `CFX` variants. A zlib body is capped at 256 MiB (`Radicle/src/swf/swf_internal.hpp:kMaxBodyBytes`). A wrong declared length is reported, not fatal.
- `ZWS` (LZMA) read and write: in-house codec in `Radicle/src/swf/swf_lzma.cpp`, no linked library. The earlier "unsupported compression" status no longer comes out of `read_swf`. Test: `Radicle/tests/test_swf_lzma.cpp`.
- Tag stream decode into typed payloads (`Radicle/src/swf/swf_tags.cpp`), with a byte-exact round trip for unedited movies. Tests: `Radicle/tests/test_swf.cpp`, `test_swf_tags.cpp`.
- Batch tag edits: `Radicle/src/swf/swf.cpp:apply_tag_plan` replaces payloads and inserts tags in one all-or-nothing pass. A refused plan leaves the file untouched. Test: `Radicle/tests/test_swf_plan.cpp`.
- Real-data checks: `Radicle/tests/test_swf_real.cpp` (`ParsesEveryShippedMovieAndRoundTripsByteIdentically`, `InterfaceCorpusDoActionCountAndNoLzmaCompression`). These need the Fallout 4 data runner and skip without `FO4_DATA_DIR`.
- Corpus census: `Radicle/include/fo4/swf/swf_census.hpp:census_movie`, tests `test_swf_census.cpp` and `test_swf_census_real.cpp`.

Not covered or unverified:
- The code-note for the container states the semantics of the sprite control tags were not part of the container lane (they are handled in the display list layer below).
- The code-notes record that most of the build and test claims were "not run locally; CI is the first compiler". No CI claims are made here.

### 2.2 Display list and timeline

What works:
- `Radicle/include/fo4/swf/swf_timeline.hpp` and `Radicle/src/swf/swf_timeline.cpp:build_movie` build a movie model (root and sprite timelines).
- `Radicle/src/swf/swf_display.cpp` expands a frame into a flat, depth-ordered row list (`SwfFrameInstance`, with world transform, colour transform, clip depth and morph ratio). Placement decode is in `Radicle/src/swf/swf_placement.cpp` (PlaceObject 1/2/3, matrices, colour transforms, filters, blend modes).
- Character model: `Radicle/src/swf/swf_characters.cpp:build_character_model` gives one row per character id with kind (shape, morph, bitmap, text, edit text, font, sprite, button, sound, video, imported), defining tag index, bounds, bitmap pixel size and names from `SymbolClass` and `ExportAssets`. It refuses duplicate ids, short payloads and name-table ids that name nothing, and clears the whole model on a refusal. Tests: `test_swf_characters.cpp`, `test_c_api_swf_characters.cpp`.
- Depth and frame control: `Radicle/src/swf/swf_depth.cpp`, `swf_frame_control.cpp`, `swf_keyframe.cpp`. Tests: `test_swf_depth.cpp`, `test_swf_frame_control.cpp`, `test_swf_keyframe.cpp`, `test_swf_display.cpp`, `test_swf_place.cpp`, `test_swf_place_filters.cpp`.

Limits:
- Video characters are listed in the model but video rows draw nothing in the frame SVG (code-note for `frame_to_svg`).

### 2.3 Shapes, bitmaps, text, fonts, filters (decode and SVG render)

Shapes:
- `Radicle/src/swf/swf_shape.cpp:decode_shape_tag` decodes DefineShape (2), 2 (22), 3 (32) and 4 (83): fill styles, line styles, edge and style-change records.
- Morph shapes: `Radicle/src/swf/swf_morph.cpp` decodes DefineMorphShape (46) and 2 (84) as start/end pairs and interpolates by ratio. In the frame SVG, a morph row with a non-solid style was not drawn at the time of the first morph note (`interpolate_morph_shape` returned nullptr). Whether that is still true: unverified. Tests: `test_swf_morph_tween.cpp`, `test_c_api_swf_morph.cpp`.
- Shape to SVG: `Radicle/src/swf/swf_svg.cpp:shape_to_svg`, C verb `radicle_swf_shape_svg` in `Radicle/src/c_api_swf_characters.cpp`. Bitmap fills become SVG repeating patterns with an embedded PNG. Tests: `test_swf_svg.cpp`, `test_swf_art.cpp`.

Frame SVG:
- `Radicle/src/swf/swf_frame_svg.cpp:frame_to_svg` draws one root frame in paint order, with sprite transform chains, colour transforms as `feColorMatrix`, sprite and shape clip masks (a mask uses fill geometry only), hidden mask rows, button UP-state records, static text as glyph paths, nine-slice scaling, and filter and blend wraps. C verb: `radicle_swf_frame_svg` (`Radicle/src/c_api_swf_display.cpp`).
- Tests: `test_swf_display.cpp`, `test_swf_svg_buttons.cpp`, `test_swf_svg_text_clip.cpp`, `test_swf_svg_text_matrix.cpp`, `test_c_api_swf_display.cpp`. Real data: `test_swf_real.cpp:ExportsEveryShapeToWellFormedSvg`.
- Edit-text (DefineEditText) rows: There is no edit-text handling in `Radicle/src/swf/swf_frame_svg.cpp`. Whether they are drawn anywhere else: unverified.
- Button rows: only UP records are drawn. Record colour transforms are ignored. Hit-only and over-only records draw nothing.

Filters (`Radicle/src/swf/swf_svg_filters.cpp`, ids as in `swf_placement.cpp:read_filter`):

| Filter | Status in SVG |
|---|---|
| DropShadow, Glow, Blur, ColorMatrix | drawn |
| Bevel, GradientGlow, Convolution, GradientBevel | listed as skipped, not drawn |
| Blur `passes` | not rendered; a single gaussian stands in |

Angle, strength and ColorMatrix scaling choices are "not checked in a Flash Player" per the code-notes. Test: `test_swf_place_filters.cpp`, `SwfSvgFilters` cases in `test_swf_svg_buttons.cpp`.

Bitmaps:
- `Radicle/src/swf/swf_bitmap.cpp:decode_bitmap_tag` decodes DefineBitsLossless (20) and 2 (36) and inflates the image. JPEG tags are in `Radicle/src/swf/swf_jpeg` related code (`Radicle/src/c_api_swf_jpeg.cpp`). PNG export: `Radicle/src/swf/swf_bitmap_png.cpp`, C verbs `radicle_swf_bitmap_png` and `radicle_swf_bitmap_jpeg`.
- Edit: `Radicle/include/fo4/swf/swf_bitmap_edit.hpp:replace_bitmap_lossless`. The C ABI list reviewed (`c_api_swf.h`) has no bitmap replace verb: unverified whether another header exposes one.

Fonts and text:
- Glyph outlines: `Radicle/src/swf/swf_glyph.cpp:read_glyph_shapes`, edit by `swf_glyph_edit.cpp:replace_glyph_payload`, C verb `radicle_swf_font_replace_glyph`. Tests: `test_swf_glyph.cpp`, `test_c_api_swf_glyph.cpp`, `test_swf_fonts.cpp`, `test_swf_fonts_real.cpp`.
- DefineText / DefineText2 glyph records decode in `Radicle/src/swf/swf_text.cpp`. Tests: `test_swf_text_glyph.cpp`, `test_c_api_swf_text.cpp`.

### 2.4 Sounds

There are two different "sound" topics in Radicle. Do not mix them:
- `Radicle/docs/SOUND.md` is about Fallout 4 sound records (SNDR and similar, #340). It is not about SWF sounds.
- SWF sounds are in `Radicle/include/fo4/swf/swf_sound.hpp` and `Radicle/src/swf/swf_sound.cpp`.

Works:
- Decode of DefineSound, StartSound, StartSound2 and SoundStreamHead payloads (`decode_define_sound`, `decode_start_sound`, `decode_start_sound2`, `decode_sound_stream_head`).
- Edits: `Radicle/src/swf/swf_sound_edit.cpp` with `replace_sound_data` and `retarget_start_sound`. C verbs: `radicle_swf_sound_at`, `radicle_swf_start_sound_at`, `radicle_swf_replace_sound_data`, `radicle_swf_retarget_start_sound` (`Radicle/src/c_api_swf_sound.cpp`). Tests: `test_swf_sound.cpp`, `test_c_api_swf_sound.cpp`.
- PCM decode for the AVM2 side: `Radicle/src/avm2/sound_decode.cpp` handles uncompressed 8 and 16 bit, little-endian 16 bit and SWF ADPCM. MP3 is refused rather than guessed. Test: `test_avm2_sound_decode.cpp`.

Limits:
- The AVM2 runtime queues Play and Stop sound events. It does not mix audio itself (code-note "DefineSound PCM decode").

### 2.5 Editing

What the edit layer offers (all go through plans, so a refused edit leaves bytes unchanged):
- Edit history with undo and redo: `Radicle/src/swf/swf_history.cpp:SwfEditHistory`. Undo replays the first N commands on the original bytes. C handle verbs `radicle_swf_history_*` (`Radicle/src/c_api_swf_history.cpp`). Tests: `test_swf_history.cpp`, `test_c_api_swf_history.cpp`.
- Edit-text field changes (`swf_text_edit.cpp:apply_edit_text`) and frame label add, rename and remove (`add_frame_label`, `rename_frame_label`, `remove_frame_label`; a duplicate name on one timeline is refused). Tests: `test_swf_edit_text.cpp`, `test_swf_label_edit.cpp`, `test_c_api_swf_text.cpp`.
- Placement edits: instance transforms, depth reassignment, keyframe insertion (`radicle_swf_apply_instance_transforms`, `radicle_swf_reassign_instance_depth`, `radicle_swf_insert_instance_transform_keyframe`, in `Radicle/src/c_api_swf_verbs.cpp`). Tests: `test_swf_edit.cpp`, `test_swf_keyframe.cpp`, `test_swf_depth.cpp`, `test_c_api_swf_edit.cpp`.
- Glyph, sound and bitmap replacement: see 2.3 and 2.4.
- ABC and AVM1 code editing: see 2.6 and 2.7.
- Write back: `Radicle/src/swf/swf_write.cpp`, C verb `radicle_swf_write_bytes`.

### 2.6 AVM1 (old ActionScript)

Context: the code-notes record 82 `DoAction` tags in three shipped GFx movies (section "Phase 7 corpus evidence"). So AVM1 matters for shipped content.

Decode and encode (`Radicle/include/fo4/avm1/avm1.hpp`, `Radicle/src/avm1/decode.cpp`, `encode.cpp`, `edit.cpp`):
- `decode_actions` gives typed records. Typed payloads: Frame, GetURL, one-byte records, ConstantPool, WaitForFrame, text records, DefineFunction and DefineFunction2, Try, With, Push, Jump, If, Call, GotoFrame2. Other records keep raw bytes. Bytes after the first End are kept and written back.
- `encode_actions` rebuilds Length, branch offsets and body sizes from the record list, so insert and erase keep jumps correct (`edit.cpp:insert_action`, `erase_action`). `validate_actions` and `encode_actions_checked` refuse a body over 65535 bytes or a branch delta that does not fit int16.
- Tests: `Radicle/tests/test_avm1.cpp`, `test_avm1_encode.cpp`. Real data: `test_avm1_real.cpp:InterfaceGfxAvm1StreamsDecodeAndReencodeByteIdentically` (runner only).

Interpreter (`Radicle/include/fo4/avm1/interpreter.hpp:execute_actions`, `Radicle/src/avm1/*.cpp`):
- Runs on a per-call state: operand stack, constant pool, globals, trace, step limit. Statuses include `Unsupported`, `StepLimit`, `ResourceLimit`, `Thrown`.
- Op families split across files: arithmetic, string, bitwise and comparison in `interpreter.cpp` and `text_ops.cpp`; objects and arrays in `object_ops.cpp`; user functions in `function_ops.cpp`; Try, With, extends, implements, casts in `control_ops.cpp`; random, GetTime and similar in `misc_ops.cpp` (including a no-op for StrictMode 0x89); timeline ops in `timeline_ops.cpp`.
- Timeline ops (GotoFrame, Play, Stop, GotoFrame2, SetTarget, properties) work only through an injected `Avm1TimelineHost`. With no host they are refused by name.
- Tests: `test_avm1_ops.cpp`, `test_avm1_objects.cpp`, `test_avm1_functions.cpp`, `test_avm1_control.cpp`, `test_avm1_timeline.cpp`.

Limits named in the code-notes:
- The first slice's note says several behaviours were "not checked against the SWF spec v19 mirror" (ToInteger not reduced to 32 bits, `parse_number` accepts whatever `std::strtod` accepts, string compares are byte based). Later notes may have changed some of these; which still hold: unverified.
- The interpreter is not wired to the SVG renderer or the AVM2 runtime in the code reviewed. Unverified.

C ABI: only `radicle_swf_avm1_disassemble` (`Radicle/include/fo4/c_api_swf.h`, `Radicle/src/c_api_swf.cpp`). There is no C ABI verb for AVM1 run or encode. So the AVM1 interpreter and encoder are C++ library features, not reachable from C# today.

### 2.7 AVM2 and ABC

ABC model and tools (`Radicle/include/fo4/abc/*.hpp`, `Radicle/src/abc/`):
- Parse and serialize: `AbcFile::parse` and `Radicle/src/abc/abc_write.cpp:AbcFile::serialize`. Opcode decode with reachability (`opcodes.cpp`), summary and literal collection (`summary.cpp`), control flow and verification (`flow.hpp`, `abc_verify.cpp`).
- Text form: `render_abc_text` dumps a whole block as editable text (doubles as exact IEEE bit patterns, references as `@N` pool indexes); `assemble_abc_text` and `assemble_instruction_text` (`abc_asm.hpp`) turn text back into a block or an instruction.
- Instruction edits: `Radicle/include/fo4/abc/edit.hpp` has `insert_instruction`, `delete_instruction`, `replace_instruction`, `replace_body_code`, `add_method`, pool intern helpers and `refresh_body_limits`. Edits are verified before commit.
- C ABI (in `c_api_swf.h`): class, method, script, function, multiname queries; `radicle_swf_abc_method_disassemble`; `radicle_swf_abc_text`, `radicle_swf_abc_assemble`, `radicle_swf_abc_apply_text`, `radicle_swf_abc_insert_instruction`, `radicle_swf_abc_replace_instruction` (write side in `Radicle/src/c_api_swf_abc_write.cpp`). There is no `delete_instruction` verb in the exports I listed.
- Tests: `test_abc.cpp`, `test_abc_asm.cpp`, `test_abc_edit.cpp`, `test_abc_flow.cpp`, `test_abc_pool.cpp`, `test_abc_text.cpp`, `test_abc_verify.cpp`, `test_abc_write.cpp`, `test_abc_summary.cpp`, `test_abc_pushshort.cpp`, `test_swf_doabc_round_trip.cpp`, `test_c_api_swf_abc_sprite.cpp`. Real data (runner): `test_abc_real.cpp`, `test_abc_asm_real.cpp`, `test_abc_text_real.cpp`, `test_c_api_swf_real.cpp:DumpsEditsAssemblesAndWritesARealInterfaceBlock`.
- Corpus numbers recorded in the code-notes for the AE 1.11.240.0 Interface archive: 675 `DoABC2` blocks, all parse Ok, no LZMA movies there, 0 unsupported. These are note claims about the authoring machine, not something I reproduced.

AVM2 runtime (`Radicle/include/fo4/avm2/`, `Radicle/src/avm2/`; started under issue #807):
- Value model and coercions, names and namespaces, class and trait linker (`runtime.hpp:Runtime::link`), an interpreter with opcode dispatch (`interpreter_dispatch_a..e.inc`), natives for core classes (array, string, math, object, error, ByteArray, AMF3, events, system).
- Display runtime: DisplayObject, containers, Sprite, MovieClip, Stage, Shape, Bitmap, TextField, SimpleButton, Graphics (records commands, does not render), timelines with frame scripts. `swf_movie_data.cpp` and `swf_runtime.cpp` bridge real SWF placements into those objects, including symbol classes and the document class.
- Host bridge (`host.hpp`): asset resolver, storage, host-call forwarder; ExternalInterface, URLLoader, Loader, ApplicationDomain; Scaleform/BGS bridge natives (`native_bgs.cpp`). Frame snapshots with a deterministic hash (`snapshot.hpp`, `display_snapshot.hpp`) and an input replay harness (`replay.hpp`).
- Tests: many `Radicle/tests/test_avm2_*.cpp` files (interpreter, display, events, host, snapshot, domain, text, builtins, document class, SWF runtime). Real data (runner): `test_avm2_scripts_real.cpp` and `test_c_api_runtime_real.cpp:ReplaysTheSevenProgramsDeterministically`, which replays seven shipped Program movies twice with a fixed 180-frame input log and requires identical hash sequences.
- C ABI: `radicle_runtime_open`, `radicle_runtime_open_movie` (and `_with_bgs`, `_from_ba2`), `radicle_movie_input`, `radicle_movie_snapshot` in `Radicle/src/c_api_runtime.cpp`.

Limits named in the code-notes (check the notes for newer fixes):
- Hit testing reads only a script-set width and height. Bridge-placed content does not get drawn bounds, so mouse targets fall back to the root (note at "R4 complete" limits).
- Focus Tab traversal is not implemented, so focus-change events are never dispatched. `mouseOver` and `mouseOut` carry no `relatedObject`.
- `localToGlobal` accumulates translation only (note at "R4 start").
- Strict mode stops at an unbound native. The probe mode `InterpreterLimits::lenient_builtins` returns undefined instead and is used by the real-data script walk.
- Graphics calls are recorded, not rendered. Audio is queued, not mixed.
- Whether the runtime renders frames to SVG or pixels: not found; the runtime produces snapshots. SVG comes from the separate `swf_frame_svg.cpp` path.

## 3. C# bindings

`Radicle/bindings/csharp/Radicle.Interop/` wraps the C ABI. SWF files: `SwfFile.cs`, `SwfEditing.cs` (Characters, OpenFrames, OpenHistory, ApplyPlan, WriteBytes), `SwfEditText.cs`, `SwfVerbs.cs`, `SwfPlacements.cs`, `SwfPlacementMatrix.cs`, `SwfArt.cs`, `SwfMorphs.cs`. Tests in `Radicle/bindings/csharp/Radicle.Interop.Tests/`: `SwfTests.cs`, `SwfEditingTests.cs`, `SwfEditTextTests.cs`, `SwfAbcWriteTests.cs`, `SwfPlacementTests.cs`, `SwfPlacementMatrixTests.cs`, with hand-built movies in `SwfFixtures.cs`. The generated interop file `RadicleNative.g.cs` comes from `tools/generate_interop.py`.

## 4. How PluginEditTool uses it

Chain: React panel -> WebView2 host object -> C# service -> Radicle bindings -> C ABI.

- `PluginEditTool/web/src/SwfPanel.tsx` (600 lines) talks to `chrome.webview.hostObjects.swf`. It is opened from `PluginEditTool/web/src/MainShell.tsx` by the "Interface (SWF)" activity button.
- `PluginEditTool/FO4RecordEditor/Services/SwfInterop.cs` is the host object. It returns JSON strings.
- `PluginEditTool/FO4RecordEditor.Core/Services/SwfDocumentService.cs` holds `SwfDocument`: one open movie plus an edit history, with `Dirty`, `Revision`, `UndoCount`, `RedoCount`. It can open bytes, a file, or a path resolved through the host asset resolver. `Save` backs up the previous bytes before writing (test `SavingBacksUpThePreviousBytesBeforeWritingTheEdit`).

What the panel does today (calls found in `SwfPanel.tsx`):
- Open a movie by path; list characters, timelines, frame labels and placements.
- Preview a shape (`ShapeSvg`), a bitmap (`BitmapPng`, falling back to `BitmapJpeg`) and a root frame (`FrameSvg`), redrawn after edits.
- Edit: rename, add and remove frame labels; edit-text fields; placement depth; placement transform; insert a keyframe; undo, redo, save. Radicle refusals show verbatim in an error bar.
- Show the inspect text, ABC blocks with classes and method disassembly, and AVM1 disassembly for a picked `DoAction` tag.

What the host object exposes but the panel does not call (no use found in `SwfPanel.tsx`): `InsertAbcInstruction`, `ReplaceAbcInstruction` (`SwfInterop.cs`). `SwfDocument.ReplaceFontGlyph` exists in the service. No sound or bitmap-replace calls appear in the panel or host object: so those Radicle features have no PET UI as far as I read.

Tests: web `PluginEditTool/web/src/SwfPanel.test.tsx` (selection, rename, undo, redo, stale reply handling, bitmap and frame previews, ABC listing); managed `PluginEditTool/FO4RecordEditor.Core.Tests/SwfDocumentServiceTests.cs` (history, labels, edit text, keyframes, depth, transform, ABC instruction edit, AVM1 disassembly, bitmap PNG export).

## 5. Summary table

| Layer | Read | Edit | Render | Run | In PET panel |
|---|---|---|---|---|---|
| Container, tags (incl. zlib, LZMA, GFX) | yes | tag plan | n/a | n/a | open, save |
| Display list, timeline | yes | placement, depth, keyframe, labels | frame SVG | n/a | yes |
| Shapes, morph | yes | no edit seen | SVG (some filters and morph styles missing) | n/a | preview |
| Bitmaps | yes | lossless replace (C++) | PNG export | n/a | preview only |
| Fonts, text | yes | glyph replace, edit text | glyph paths for static text | n/a | edit text only |
| Sounds | yes | replace data, retarget | n/a | decode PCM, no mixing | none |
| AVM1 | decode | insert and erase (C++) | n/a | interpreter (C++ only) | disassembly |
| AVM2 and ABC | yes | text and instruction edits | n/a | runtime with snapshots | listing and disassembly |

## 6. Known gaps (short list)

- SVG: Bevel, GradientGlow, Convolution and GradientBevel filters not drawn; blur passes ignored; video rows draw nothing; button over and hit states not drawn.
- AVM1: interpreter and encoder not exposed through the C ABI (only disassemble).
- AVM2 runtime: hit testing on placed content, focus traversal, audio mixing, rendering of Graphics commands.
- PET: no UI for sound, bitmap replace, glyph replace or ABC instruction edit (the host object has the ABC edit methods).
- Many real-data tests only run on the Fallout 4 data runner.
