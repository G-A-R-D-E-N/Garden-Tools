# SWF and ABC real-data gates: data-machine runbook

Anchors name a file and a symbol (test name, function, constant or manifest key), not a line. Names and the CI regex were checked on 2026-10-31 against `nomad/gt-807-replay-pins` (base `f1152a80b`, plus the items recorded in the code notes). Nothing ran on the data runner. The checks behind this update were WSL Linux builds, and the real-data runs noted in the code notes used the Steam install on the authoring PC with a placeholder build label. Items marked UNVERIFIED were not found in the repo or were not observed.

Every test is in the `fo4_tests` gtest binary (`add_executable(fo4_tests` in `Radicle/CMakeLists.txt`). Test sources are listed in the same call.

The Papyrus gates are in `docs/papyrus-real-data-gates.md`. The ground rules there (skip is not pass, `;` separator on Windows, the binary path) apply here too, with the differences below.

## Ground rules

- Every gate here reads `FO4_DATA_DIR`, the Fallout 4 `Data` folder itself. The other variables are the optional `FO4_GAME_BUILD` override (see the build bullet below) and, for the captured-sequence comparison only, `FO4_SWF_CAPTURE_DIR` (a folder of `<Program>.tsv` captures; unset means that test skips before any exe read). Repo examples use `D:\SteamLibrary\steamapps\common\Fallout 4\Data` (see `docs/code-notes/BGS.md`). Unset means `GTEST_SKIP`. A set value with no `Fallout4 - Interface.ba2` in it also skips, through the `std::filesystem::exists(archive_path)` check in each test. Gate 1 skips only when it finds neither loose movies nor the archive. Every SKIPPED line needs a note.
- No Papyrus variable is read. `FO4RE_*` and `FO4_PSC*` do not apply.
- The census manifest path is compile-time, not env: `FO4_TESTDATA_DIR="testdata"`, set by `target_compile_definitions(fo4_tests ...)` in `Radicle/CMakeLists.txt`. It is relative, so the working directory must be `<repo>\Radicle`. The `gtest_discover_tests(fo4_tests ... WORKING_DIRECTORY ...)` call in the same file sets that, and the manual template sets it too.
- The game build comes from the file version of `<FO4_DATA_DIR>\..\Fallout4.exe` (`Radicle/tests/real_data_build.cpp:detect_game_build`), unless `FO4_GAME_BUILD` is set and non-empty. That override replaces the exe probe, and `game_build` then records the override value, not the exe's. A wrong override changes which branch runs: gate 3 row-checks only when the build is 1.10.163.0 or 1.10.984.0, gate 5 requires zero LZMA only when `exact_counts_known` is true, and gate 2 pins `do_action_tags == 82` only when `exact_counts_known` is true too. `game_build_from_version` maps 1.10.163.0 to OG, 1.10.980 or 1.10.984 to NG, and 1.11.x to AE. `exact_counts_known` is true only for AE 1.11.240.0. Each test records the build as the `game_build` property.
- Archives: the gates read `Fallout4 - Interface.ba2`, through `fo4::archive::read_ba2_general_file` or, in gates 7, 8 and 10, through the C ABI (`radicle_ba2_open`) or the same archive reader. Gate 1 also reads the three DLC main archives. Loose `Interface\*.swf|.gfx` files are used first where a test reads them.
- CI: the real-data job `native-windows-real-data` in `.github/workflows/native-real-data.yml` runs on the host labelled `fo4-data` and reads `FO4_DATA_DIR` from the runner environment. The workflow does not set it. No workflow references `FO4_DATA_DIR_LINUX` (checked 2026-10-31).
- CI's `REAL_DATA_TEST_REGEX` in `.github/workflows/native-real-data.yml` matches every gate below. Checked 2026-10-31 by running the regex with `grep -E` against each gate's test name.
- No `$groups` entry in the path filter names a SWF or ABC test (the workflow has no other `swf` or `abc` text), so the filter never excludes them.
- The `fo4_tests.exe` location is derived from CMake, as in the Papyrus runbook (`build\native\bin\fo4_tests.exe`). UNVERIFIED on a data runner.

## Command template (PowerShell, data machine)

```powershell
$env:FO4_DATA_DIR = 'D:\SteamLibrary\steamapps\common\Fallout 4\Data'   # the Data folder itself
Set-Location <repo>\Radicle
& <repo>\build\native\bin\fo4_tests.exe --gtest_filter=SwfRealData.InterfaceCorpusDoActionCountAndNoLzmaCompression
Remove-Item Env:FO4_DATA_DIR
```

For every gate below at once, use this filter:

```
--gtest_filter=SwfRealData.*:SwfCensusReal.*:SwfFontsReal.*:AbcCorpusReal.*:AbcAsmReal.*:CApiRuntimeReal.ReplaysTheSevenProgramsDeterministically:CApiRuntimeReal.RunsAProgramMovieDeterministically:CApiRuntimeReal.ComparesTheCapturedBgsSequenceForEachProgram
```

Add `--gtest_output=xml:<dir>\<gate>.xml` to keep `RecordProperty` values. Each gate's properties are listed under "What to record".

## Gates

### 1. Whole-archive parse and byte round trip

- Test: `SwfRealData.ParsesEveryShippedMovieAndRoundTripsByteIdentically` (`Radicle/tests/test_swf_real.cpp`).
- Env: `FO4_DATA_DIR`. Loose `Interface\` movies are read first. The archive is read only when no loose movie and no LZMA movie was seen. `DLCCoast - Main.ba2`, `DLCNukaWorld - Main.ba2` and `DLCRobot - Main.ba2` are scanned when present. An absent one records `absent` and is not a failure.
- Pass: `movies > 0`; `movies + lzma == total`; `art_failures == 0`; `character_failures == 0`; `edit_text_tags > 0`; shape, bitmap, morph, fill-style and line-style counts `> 0`. Each parsed movie must satisfy `write_swf(read) == original bytes` (`check_movie` in `Radicle/tests/test_swf_real.cpp`).
- Proves: every non-LZMA movie in the scanned sources parses, builds a character model, decodes its art, and writes back unchanged.
- Unverified or not proved: an LZMA (`ZWS`) movie is counted in `lzma` and returns from `check_movie` before any parse or round trip. The round trip proves that Radicle's writer reproduces the input bytes. It says nothing about how the game renders the movie. No counts are pinned.

### 2. Interface corpus: DoAction count and no LZMA

- Test: `SwfRealData.InterfaceCorpusDoActionCountAndNoLzmaCompression` (`Radicle/tests/test_swf_real.cpp`).
- Env: `FO4_DATA_DIR`. Reads only `Fallout4 - Interface.ba2`. No loose files, no DLC.
- Pass: `movies > 0`; `lzma_movies == 0`; `do_action_tags == 82` only when `exact_counts_known(build)` (AE 1.11.240.0). On any other build the count is recorded as the `do_action_tags_unverified` property and does not fail. The count walks every `DefineSprite` child recursively (`count_do_action`).
- Proves: on the verified build, the Interface archive has 82 AVM1 `DoAction` tags and no ZWS movie.
- Caveat: `82` is still a literal, now guarded the same way as gate 5's LZMA check. A movie that fails `read_swf` is skipped silently, so a parse failure is caught only by gate 1.
- Unverified: whether 1.10.163.0 or 1.10.984.0 also gives 82. The code notes no longer contain the earlier 1.10.984 zero-count note that an older version of this runbook cited (searched 2026-10-31). No OG or NG scan is recorded there. On a build where the count differs, this gate fails. It does not skip.

### 3. Program census against the committed manifest

- Test: `SwfCensusReal.MatchesTheCommittedProgramManifest` (`Radicle/tests/test_swf_census_real.cpp`).
- Env: `FO4_DATA_DIR`, plus the compile-time `FO4_TESTDATA_DIR` (see ground rules). Manifest: `Radicle/testdata/swf/program_census.txt`, whose `# game_build` line records the build it was taken from (`1.10.984.0`).
- Scope: every archive entry whose path contains `programs\` and ends `.swf` (`is_programs_movie`).
- Pass, every build: each Programs movie parses (`ASSERT_TRUE`); `blocks == parsed`; the seven names `AtomicCommand`, `GJ_Defrag`, `Grognak`, `PipFall`, `RedMenace`, `TurretHack` and `ZetaInvaders` are present; every movie with ABC blocks references engine names.
- Pass, row check: only when the detected build is in `k_verified` (1.10.163.0 or 1.10.984.0). The census text must equal the manifest byte for byte after its `# game_build` line is rewritten to the detected build (`with_game_build`).
- Caveat: on any other build, including AE 1.11.240.0, the test records `manifest_build_unverified` and returns. It passes without comparing a row.
- Proves: on OG or NG, Radicle's census equals the committed manifest. The code notes say the manifest was not produced by Radicle's own code: it came from a scratch independent Python reader (search the code notes for "the manifest was not produced by Radicle's own code"). So this is agreement between two readers, not a check against the game.
- Build label: the manifest's `# game_build` line is `1.10.984.0`. `k_verified` is `{1.10.163.0, 1.10.984.0}` in `Radicle/tests/test_swf_census_real.cpp`. Every other build, including AE 1.11.240.0, is not row-checked.

### 4. Embedded fonts

- Test: `SwfFontsReal.DecodesEmbeddedFontProgramsAndInterfaceFonts` (`Radicle/tests/test_swf_fonts_real.cpp`).
- Env: `FO4_DATA_DIR`. Archive only. Movies are entries whose path ends with `fonts_programs.swf` or `fonts_en.swf`.
- Pass: at least one font movie parses (`ASSERT_GT`); `fonts > 0`; `layout_fonts > 0`; `total_glyphs > 100`. For each layout font with a non-empty `advances`, `advances.size() == glyph_count` and `glyph_codes` is non-empty.
- Proves: the embedded `DefineFont` tags decode with glyph counts and layout data.
- Not proved: glyph outline correctness or rendering. No counts are pinned. A font movie that fails to parse is skipped silently inside the test loop, so only the `total_glyphs > 100` floor and the `movies > 0` assert catch it.

### 5. ABC corpus: decode, re-encode and verify

- Test: `AbcCorpusReal.DecodesEveryShippedInterfaceAbcBlock` (`Radicle/tests/test_abc_real.cpp`).
- Env: `FO4_DATA_DIR`. Loose `Interface\` movies first. The archive is read only if no ABC block was seen. Skips if neither source exists.
- Pass: `total_movies == movies + lzma + unreadable`; `blocks == parsed + unsupported_total()`; `trailing_bytes_blocks == 0`; `serialize_mismatches`, `reencode_mismatches` and `edit_mismatches` each `== 0`; `verified_flows == bodies`; `unsupported_total() == 0`; `parsed > 0`; `bodies > 0`. `lzma == 0` is required only when `exact_counts_known` is true (`Radicle/tests/real_data_build.cpp:exact_counts_known`).
- Proves: on every block of the shipped Interface movies, the ABC decoder, flow verifier, instruction re-encoder and nop insert/delete round trip all agree with the input bytes, and no block is unsupported.
- Not proved: any specific count. The test asserts no literal count. The AE figures (675 blocks, 30985 bodies) are in the code notes under the heading `Radicle/tests/test_abc_real.cpp:AbcCorpusReal`. They were not re-observed here.

### 6. ABC text dump and assemble-back

- Test: `AbcAsmReal.EveryVanillaInterfaceBlockDumpAssemblesBackByteForByte` (`Radicle/tests/test_abc_asm_real.cpp`).
- Env: `FO4_DATA_DIR`. Archive only (`check_archive`).
- Pass: `movies > 0` and `blocks > 0`; `parse_failures`, `assemble_failures` and `serialize_mismatches` each `== 0`. Each block must go `render_abc_text` then `assemble_abc_text` then `serialize`, and the result must equal the block payload byte for byte (`check_archive`).
- Proves: the ABC text form is lossless for every block of the shipped Interface archive.
- Not proved: LZMA movies are counted in `lzma` and skipped (`check_archive`). No golden text file is compared.

### 7. Seven Programs replay (deterministic, not pinned)

- Test: `CApiRuntimeReal.ReplaysTheSevenProgramsDeterministically` (`Radicle/tests/test_c_api_runtime_real.cpp`).
- Env: `FO4_DATA_DIR`. Archive only. The seven movies are found by basename, case-insensitively (`swf_paths_by_basename`). A missing movie is a failure, not a skip.
- What it drives: `fo4::avm2::SwfRuntime::create` with `options.bgs_host = true` (`run_program_body`), not the C ABI. The code notes section `Radicle/tests/test_c_api_runtime_real.cpp:ReplaysTheSevenProgramsDeterministically` under "Fake BGS host and acceptance harness (#807 R8 L4)" gives the reason. It runs 180 frames at 1/24 s with the fixed input log in `program_inputs`: 40 mouse moves, a click, Enter, Space and a second click. Frames are run through `fo4::avm2::replay` (`Radicle/src/avm2/replay.cpp`).
- Pass, per program: two runs, both with `init` on (`drive_movie`), each reach 180 frames; the movie is still `playing` and the frame hashes are not all equal; for AtomicCommand, Grognak, PipFall, RedMenace and ZetaInvaders, at least one `bgs.` call reaches the host (`expects_bgs`); the two runs' frame-hash vectors and host-log hashes are equal. Then `check_pins` runs, which records `pin_<program>` as unpinned when the manifest has no row for the build.
- Pass, control: AtomicCommand and PipFall, run with `init` off, must differ from their `init` runs in the host-log hash. Frame hashes are not compared: they are not an acceptance measure (acceptance is the `bgs.` call sequence).
- Pass, total: `ran == 7` and `mismatches == 0`.
- Proves: all seven Programs open, run a fixed input log for 180 frames without a script error, and produce identical frame hashes and host logs across two runs in one process. The `init` path changes the host log.
- Does NOT prove (yet): that the output matches the game. `check_pins` loads the committed manifest `Radicle/testdata/swf/program_replay_pins.txt` (same `FO4_TESTDATA_DIR` resolution as the gate 3 census) and matches rows by program basename and detected `game_build`. With no rows for the current build it records a `pin_<program>` property of `unpinned for build <version>` and does not fail. With rows, a snapshot-hash, host-log-hash or `bgs_calls` mismatch fails. The manifest currently ships with no rows, so every build is unpinned until the RE request is answered.
- Other unverified points: `SetPlatform(uint, Boolean)` and `InitProgram()` match the ABC of the five program roots that define them, but no root call ran on the branch checked, because those movies fail at creation. `SetPlatform` is skipped when the root lacks it (`drive_movie` checks `Object::lookup` on the root). Whether `Interpreter` errors inside `replay` reach this test is unverified (the code notes section named above); the frame count and `playing()` are what is checked.

#### Filling the pins

- Source 1 (stderr): for every program that completes both runs, `ReplaysTheSevenProgramsDeterministically` prints `R8_HASH <program> <snapshot> <hostlog> <bgs_calls>`.
- Source 2 (XML): the `program_<name>` property holds `ran snapshot=<n> hostlog=<n> bgs_calls=<n> asset_misses=<n>`.
- Map to manifest rows (`Radicle/testdata/swf/program_replay_pins.txt`, read by `load_replay_pins` into `ManifestPin`): `<snapshot>` goes to the row's `snapshot` line (`ManifestPin::snapshot_hash`, compared with `run.hashes.back()` in `check_pins`); `<hostlog>` goes to the `hostlog` line (`ManifestPin::host_log_hash`, compared with `run.host_log`); `<bgs_calls>` goes to the `bgs_calls` line (`ManifestPin::bgs_calls`, compared with `run.bgs_calls`). The compiled-in `kPinned` table is retired, so no other field is filled.
- Row shape: `program`, `build`, `snapshot`, `hostlog` and `bgs_calls` lines, one group per program per build. The manifest's header comment documents the format.
- Source of the values: a data-runner log only, taken after the bridge fix has landed. That fix is `4d2c104b2` (`fix(radicle): keep timeline objects across move placements in the SWF runtime bridge`), merged through PR #914 (merge `562bfac72`). Both commits are in this repo's history (checked 2026-10-31); whether the merge is on `origin/nomad/dev` was not re-checked. A pin taken before that fix pins the wrong behaviour.

### 8. Same file as gate 7, single-movie C ABI replay

- Test: `CApiRuntimeReal.RunsAProgramMovieDeterministically` (`Radicle/tests/test_c_api_runtime_real.cpp`). It matches the CI regex.
- Env: `FO4_DATA_DIR`. Archive only.
- Drives: one Programs movie (AtomicCommand preferred) through the C ABI: `radicle_runtime_open_movie_from_ba2` and `radicle_movie_replay`, 24 frames, run twice.
- Pass: first and second hash vectors match, and every hash is nonzero.
- Skip: `the program movie needs a runtime feature that is still missing: <radicle_last_error>`. A skip here is a missing runtime feature, not a pass.

### 9. Every shape exports to well-formed SVG

- Test: `SwfRealData.ExportsEveryShapeToWellFormedSvg` (`Radicle/tests/test_swf_real.cpp`, `check_svg_movie`).
- Env: `FO4_DATA_DIR`. Reads only `Fallout4 - Interface.ba2`; loose files and DLC archives are not scanned. Skips without it.
- Pass: `svg_shapes > 0` and `svg_failures == 0`. Each `shape_to_svg` output has balanced tags and no `nan` or `inf` text.
- Proves: every DefineShape in the Interface archive exports to a balanced, finite SVG document. It does not prove the SVG draws like the game.

### 10. Captured bgs sequence against game captures

- Test: `CApiRuntimeReal.ComparesTheCapturedBgsSequenceForEachProgram` (`Radicle/tests/test_c_api_runtime_real.cpp`). It uses `parse_capture`, `read_capture_file` and `replay_capture` from `Radicle/tests/bgs_capture.cpp`.
- Env: `FO4_SWF_CAPTURE_DIR`, a folder with one `<Program>.tsv` per program basename (for example `GJ_Defrag.tsv`), and `FO4_DATA_DIR` for the archive. Unset `FO4_SWF_CAPTURE_DIR` skips before any exe read. Unset `FO4_DATA_DIR` skips. A folder with no `<Program>.tsv` skips.
- Capture format, UNVERIFIED against the real logger: six tab-separated columns per row: `seq`, `frame_or_ms` (read as a frame index), `direction` (`to_swf` for game-to-SWF calls, `to_host` for SWF-to-game calls), `method`, `args` (a JSON array of `{type,value}`), and `return` (JSON `null` or one `{type,value}`). Type names are matched case-insensitively against string, number, int, uint, boolean, null and undefined. An optional first line that begins `seq<TAB>` is a header, and a UTF-8 BOM before it is ignored.
- Pass, per program with a capture: the file opens and parses; the SWF replays; every `to_host` row is matched, in seq order, by method and stringified arguments; no captured call is missing and no SWF call is extra; no captured game call fails in the SWF; the movie does not stop on a frame script error; and the capture has at least one `to_host` row. Otherwise the failure names the first difference and any replay error. Captured return values reach the script only as strings.
- Proves (if it passes): the program makes the captured host calls, in the captured order, with the captured arguments. Timing is not compared.
- Does not prove: rendering, state, or `===` and `typeof` results for answers. The host hooks do not see `Math.random`. A movie whose ABC constant pool contains the string `random` gets a "possibly RNG-driven" note on each mismatch, so an RNG divergence is flagged rather than proven.
- What to record: `bgs_<Program>.swf` as `matched N bgs calls` or `first difference: ...`, and `compared_programs`.

## What to record

- The gtest summary lines: `[  PASSED  ]`, `[  FAILED  ]` and `[  SKIPPED ]`. Standard gtest output; the vendored gtest version is UNVERIFIED on the data runner.
- Every skipped gate and the env var it needed. Every gate skips on a missing `FO4_DATA_DIR` or missing archive. Gate 8 also skips on a missing runtime feature. Gate 7 never skips for a missing feature; a program that fails is a failed test. Gate 10 skips on a missing `FO4_SWF_CAPTURE_DIR`.
- `game_build` from any gate's XML. This value decides whether gate 3 compares rows and whether gate 5 requires zero LZMA.
- Gate 1 properties: `movies`, `lzma_movies`, `art_failures`, `character_model_failures`, `version_histogram`, `dlccoast_movies` and the other DLC keys.
- Gate 2: `do_action_tags` and `lzma_movies`, plus `do_action_tags_unverified` on builds where the count is not pinned.
- Gate 3: `program_movies`, `abc_blocks`, `abc_parsed`, `manifest_build_unverified` (set when the row check was skipped).
- Gate 4: `fonts`, `layout_fonts`, `total_glyphs`, `decoded`.
- Gate 5: `abc_blocks`, `parsed_blocks`, `bodies`, `verified_flows`, `flow_refusal_histogram`, `unsupported_status_histogram`.
- Gate 7: stderr `R8_HASH` lines, `program_<name>` properties, `pin_<name>` properties (`unpinned for build <version>` when the manifest has no row), and `ran`.
- Gate 10: `bgs_<Program>.swf` properties and `compared_programs`.

## UNVERIFIED

- Whether the `fo4-data` runner has `FO4_DATA_DIR` set and which Fallout 4 build it has. The workflow reads the build in its "Print Fallout 4 build" step, but no value is in the repo.
- The exact `fo4_tests.exe` location on a data runner (derived from CMake, not checked there).
- The gtest summary and `GTEST_SKIP` output format for the vendored gtest version.
- Whether 82 DoAction tags, 675 ABC blocks or the other AE counts hold on OG 1.10.163.0 or NG 1.10.984.0.
- Whether the seven-program pins can be filled before the bridge fix lands. They cannot be trusted until it does.
- The `SetPlatform` and `InitProgram` argument types at runtime (gate 7). They match the ABC of the five program roots, but no root call ran on the branch checked.
- Whether Radicle's census equals the manifest on a data runner. The row check runs only there.
- Gate 10: the capture column names, the direction words, the frame unit (frame or milliseconds), the `return` encoding and the type names. No real capture was seen, and the logger is not on the authoring PC.
