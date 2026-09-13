# BGS audit — 12 September 2026

Audited revision: `d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958` (`work/native-port`). This is a historical checkout audit, not an audit of current `main`. The evidence-recording branch starts at `8d4c6685aa76a65e2a8e9ed408472769dc6aa3d9`; its BGS tree differs substantially. Findings require re-triage on a current implementation before being treated as open defects.

**Result: 12 findings. Four warrant priority fixes because they concern lost edits, bypassed save validation, or unbounded allocations from imported files.** Seven functional findings were reproduced with synthetic files or controls; the remaining findings are identified as source-traced or build/coverage evidence below.

This record contains no production fixes. The original audit used synthetic files and isolated preferences; the existing primary-checkout changes were preserved. The companion reproducer creates a unique temporary directory and leaves its inputs there for inspection.

**1. P1 — “Save and continue” bypasses graph validation. Reproduced.**

The normal Save action calls `GraphValidator.SaveRefusal`, but closing or opening another file with “Save and continue” routes through `SavePendingChanges` to `SaveCurrent`, which commits without that check. A generated two-state graph with duplicate state IDs was refused by the toolbar, then written successfully by choosing Save on close. Reopening the bytes still reports duplicate IDs. This is a proven inconsistency with BGS's own validation rules; no claim about a particular game crash is needed.

Put the same structural validation in the shared save path used by every entry point. References: [SaveCurrent](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/app/MainWindow.cs#L5374), [toolbar validation](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/app/MainWindow.cs#L5414), [close decision](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/app/MainWindow.cs#L3704).

**2. P1 — Saving metadata and animation edits together discards animation changes. Reproduced.**

When both `_dirty` and `_animationEdited` are true, `SavePendingChanges` chooses only the XML/document save. That save reloads the file and resets the animation editor. The reproduction changes frame 1's translation to `(123, 456, 789)` and changes the animation binding's skeleton name. “Save and continue” closes successfully; the new skeleton name is on disk, but the edited translation is gone.

Preserve both edit sets through a single save operation, or refuse the combined state until it can be saved without loss. References: [save selection](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/app/MainWindow.cs#L3643), [reload after document save](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/app/MainWindow.cs#L5386), [animation reset](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/app/MainWindow.cs#L2480).

**3. P1 — Closing can discard the property field currently being typed. Reproduced in Avalonia headless.**

`OnClosing` checks the dirty flags without calling `CommitPendingFields`. Property text is committed on Enter or LostFocus, so the current text can differ from the model while both dirty flags remain false. In the reproduction, a real playbackSpeed textbox receives focus, changes from 2.5 to 0.75, and the window closes without flushing pending commits or consulting the discard decision. Opening another file already flushes fields; closing does not.

Flush pending fields before deciding whether closing is safe, and prevent closure if committing them fails. Native Windows focus/event timing was not independently exercised, so the demonstrated scope is the headless control lifecycle. References: [OnClosing](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/app/MainWindow.cs#L3680), [field commit triggers](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/app/MainWindow.cs#L4459), [existing flush helper](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/app/MainWindow.cs#L5234).

**4. P1 — A tiny malformed skeleton can trigger huge allocation and loop counts. Reproduced.**

`ParseSkeleton` trusts the declared bone count and fabricates a `Bone_i` name whenever the corresponding fixup is absent. A 480-byte synthetic file was accepted and produced 100,000 bone names. Larger positive counts scale the allocation and UI-thread work independently of file size, so the 512 MiB input-size limit does not prevent resource exhaustion. The annotation-track loop and lossless array readers have related unchecked-count patterns.

Validate count × element size against the referenced section before looping or allocating, and reject incomplete payloads. Reference: [bone loop](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/src/Hkx/HkxBinaryReader.cs#L351), [annotation loop](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/src/Hkx/HkxBinaryReader.cs#L482), [lossless array allocation](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/src/Hkx/HkxBinaryReader.cs#L632). The skeleton case was exercised at a bounded size; an actual out-of-memory condition was not intentionally induced.

**5. P2 — Copy/paste does not capture the selected content. Reproduced.**

Copy reads the saved file rather than the current document. Its clipboard stores a path and subtree IDs, and Paste reads that path again. Two checks demonstrate the effect: a visible unsaved speed of 0.75 pasted as the saved 2.5; changing the source file after Copy then made the same clipboard paste 1.25. Deleting or renumbering source objects can also make the stored IDs stale. Saving a template similarly reads the disk document and ignores unsaved values.

Capture immutable bytes representing the intended document at Copy/template creation time, or explicitly block those actions while edits are pending. References: [CopySubtree](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/app/MainWindow.cs#L1339), [clipboard creation](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/src/Hkx/NativePaste.cs#L30), [source re-read at paste](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/src/Hkx/NativePaste.cs#L128), [SaveTemplate](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/app/MainWindow.cs#L1450).

**6. P2 — Modlist scanning reports success after silently skipping unreadable files. Reproduced.**

`HkxTextEdit.TextOf` returns an empty string on a read/decode failure. The scanner skips it and returns success whenever the missing-reference count is zero. A synthetic enabled mod containing a corrupted character HKX produced “0 character file(s) resolved, 0 ... missing” and exit code 0, without naming the unreadable file. Archive read failures are also swallowed; the clip scanner has the same empty-text behavior.

Track unreadable/skipped candidates separately, report their paths, and return a distinct incomplete/failure result when the requested scan could not be completed. References: [character scan](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/app/ModlistScan.cs#L149), [success result](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/app/ModlistScan.cs#L174), [clip scan](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/app/ModlistScan.cs#L238), [read failure suppression](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/src/Hkx/HkxTextEdit.cs#L68).

**7. P2 — Clip scanning includes overridden copies as though all were loaded. Reproduced.**

`ScanClips` visits every behavior under every enabled mod and archive without selecting one winner per normalized data-relative path. A fixture with the same graph path in High and Low was counted as two loaded behaviors. The High copy's animation was present; the overridden Low copy still generated a missing-animation finding and exit code 1. Unlike this path, `ScanModlist` already has a `seen` set for choosing character-file winners.

Build the effective file set using the intended load order before scanning, including loose/archive precedence. References: [loose behavior traversal](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/app/ModlistScan.cs#L271), [archive traversal](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/app/ModlistScan.cs#L280). This finding concerns duplicate-path handling demonstrated by BGS's own ordering; it does not certify every MO2 loading rule.

**8. P2 — Canceling an archive open marks the current document read-only. Source-traced.**

`OpenFromArchive` calls the void `Load()` method and then unconditionally sets `_readOnly = true` and reports that the archive entry opened. If the user cancels the unsaved-changes prompt, `Load` keeps the existing document, but the caller still marks that document read-only. An ordinary editable file can therefore become unsavable after canceling an unrelated archive open.

Have Load report success/cancellation and apply archive state only after the new document is installed. References: [unconditional archive state](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/app/MainWindow.cs#L2890), [canceled load](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/app/MainWindow.cs#L3732). This was traced through the code, not driven through a native file picker.

**9. P2 — CI omits the large sample-driven UI regression section. Confirmed by workflow and harness.**

The workflow runs `uismoke` with no file arguments. The substantial scenario loop beginning at line 314, and the later edit loop, only execute for existing paths passed as arguments. The standalone animation case also points at a missing `dist/examples/Dogmeat/...` file and explicitly skips. The observed 188 passing checks consequently do not mean the sample-driven graph, playback, editing, and comparison checks ran.

Pass supported fixtures explicitly, create synthetic fixtures for essential scenarios, and make mandatory missing fixtures fail visibly. References: [CI invocation](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/.github/workflows/bgs-ci.yml#L104), [sample loop](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/tools/uismoke/Smoke.cs#L314), [edit loop](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/tools/uismoke/Smoke.cs#L1451), [missing animation fixture](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/tools/uismoke/Smoke.cs#L2076).

**10. P2 — The pinned SDK cannot load the installed Avalonia analyzers/generator. Confirmed by build.**

The pinned .NET SDK 8.0.424 uses compiler 4.11.0, while Avalonia 12.1.1's analyzer and generator assemblies require compiler 4.14.0. The Release build succeeds with CS9057 warnings for both assemblies; their checks/generation are not available in that build. Three obsolete Watermark API warnings also remain, but they are lower priority.

Align the SDK/compiler and Avalonia package versions and verify the analyzer warnings are gone. References: [SDK pin](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/global.json#L3), [Avalonia packages](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/app/BehaviourStudio.csproj#L24). No dependency versions were changed during this audit.

**11. P2 — Editing depends on writing an unused temporary XML file. Source-traced; Ponytail delete finding.**

`PrepareEditing` deletes/recreates a temporary directory and writes `_xmlText` to `_xmlPath`, but no subsequent code reads `_xmlPath`. A failure in that unnecessary filesystem work clears the in-memory XML and leaves editing unavailable even when the source could be read. It also adds another read of the source instead of working entirely from the already-opened bytes.

`delete:` remove the unused GUI XML-file materialization and its path state; retain the in-memory XML and the conversion/mapping checks. The shared ResetDirectory helper still has other callers and should remain. References: [temporary XML path](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/app/MainWindow.cs#L3916), [failure clears the model](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/app/MainWindow.cs#L3933). Approximately seven direct lines can be removed with no new dependency; this is a source-based estimate, not an applied diff.

**12. P3 — Oversized source files exceed the inherited 500-line guardrail. Confirmed inventory.**

Twelve production source files exceed 500 lines. The largest are MainWindow.cs (5,522), GraphView.cs (1,368), NativeSave.cs (1,160), and HkxBinaryReader.cs (1,121). MainWindow combines document lifecycle, save decisions, archive access, graph editing, animation editing, playback, and project analysis. The divergent lifecycle paths in findings 1–3 are concrete reasons to isolate document/save responsibilities when fixing them.

Extract cohesive responsibilities as they are changed; a broad rewrite is unnecessary. References: [MainWindow](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/app/MainWindow.cs#L1), [GraphView](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/app/GraphView.cs#L1), [NativeSave](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/src/Hkx/NativeSave.cs#L1), [HkxBinaryReader](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/blob/d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958/BGS/src/Hkx/HkxBinaryReader.cs#L1). Ponytail estimate: about 7 directly removable lines from finding 11, 0 dependencies; responsibility extraction is not counted as deleted code.

**Validation and scope**

Locked restores succeeded. The actual application Release build passed with five warnings. The xUnit suite passed 395/395 tests. The UI harness passed 188 checks, with the coverage omissions described above. An early concurrent test/UI build hit a shared output-file lock; rerunning the UI build after the tests completed succeeded. A preliminary test command before restore did not execute tests and was not counted as a pass.

The [reproduction instructions](README.md) describe the version-bound harness. The separate reproduction harness passed its checks for findings 1–5, including actual focused textbox setup. Two CLI fixtures reproduced findings 6–7. [Harness results](evidence/results.txt), [unreadable-file scan](evidence/scan-unreadable.txt), and [overridden-file scan](evidence/scan-overrides.txt) are retained beside this report.

The audit covered the build/test wiring and traced loading, archive/project scanning, save transactions/recovery, graph authoring, clipboard/templates, animation decoding/editing, and relevant UI lifecycle paths. Conversion, NIF, project search/report, and graph ownership received bounded static review. This is broad source review and Windows build/headless verification, not exhaustive fuzzing, native visual acceptance, Linux validation, or verification of behavior inside Fallout 4. Lower-confidence candidate findings were excluded.

Fix findings 1–4 first, then clipboard/scanner correctness and coverage. Reuse the reproductions as regression tests when implementing those fixes.
