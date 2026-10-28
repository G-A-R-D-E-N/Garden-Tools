# SWF and ABC real-data gates: data-machine runbook

Verified against `origin/nomad/dev` at 1896d4752 (the base this worktree was created from). Every test is in the `fo4_tests` gtest binary (`Radicle/CMakeLists.txt:546`, `add_executable(fo4_tests`; sources at lines 915, 934, 948, 970-973). Nothing was built or run on the machine that wrote this file. Items marked UNVERIFIED were not found in the repo or were not observed.

The Papyrus gates are in `docs/papyrus-real-data-gates.md`. The ground rules there (skip is not pass, `;` separator on Windows, the binary path) apply here too, with the differences below.

## Ground rules

- Every gate here reads one variable: `FO4_DATA_DIR`, the Fallout 4 `Data` folder itself. Repo examples use `D:\SteamLibrary\steamapps\common\Fallout 4\Data` (`docs/code-notes/BGS.md:562`). Unset means `GTEST_SKIP`. A set value with no `Fallout4 - Interface.ba2` in it also skips, through the `std::filesystem::exists(archive_path)` check in each test. Gate 1 skips only when it finds neither loose movies nor the archive. Every SKIPPED line needs a note.
- No Papyrus variable is read. `FO4RE_*` and `FO4_PSC*` do not apply.
- The census manifest path is compile-time, not env: `FO4_TESTDATA_DIR="testdata"` (`Radicle/CMakeLists.txt:1010`). It is relative, so the working directory must be `<repo>\Radicle`. `gtest_discover_tests` sets that (`Radicle/CMakeLists.txt:1019`), and the manual template sets it too.
- The game build comes from the file version of `<FO4_DATA_DIR>\..\Fallout4.exe` (`Radicle/tests/real_data_build.cpp:82-92`). 1.10.163.0 is OG, 1.10.980 or 1.10.984 is NG, and 1.11.x is AE (`real_data_build.cpp:76-78`). `exact_counts_known` is true only for AE 1.11.240.0 (`real_data_build.cpp:95-96`). Each test records the build as the `game_build` property.
- Archives: the gates read `Fallout4 - Interface.ba2`, through `fo4::archive::read_ba2_general_file` or, in gates 7 and 8, through the C ABI (`radicle_ba2_open`). Gate 1 also reads the three DLC main archives. Loose `Interface\*.swf|.gfx` files are used first where a test reads them.
- CI: the real-data job `native-windows-real-data` (`.github/workflows/native-ci.yml:492`, runner label `fo4-data`) reads `FO4_DATA_DIR` from the runner environment. The workflow does not set it. `vars.FO4_DATA_DIR_LINUX` is for the Linux job only (lines 136, 164).
- CI's `REAL_DATA_TEST_REGEX` (`native-ci.yml:66`) matches all eight tests below. Checked by running the regex with `grep -E` against each test name.
- No `$groups` entry in the path filter names a SWF or ABC test (the workflow has no other `swf` or `abc` text), so the filter never excludes them.
- The `fo4_tests.exe` location is derived from CMake, as in the Papyrus runbook (`build\native\bin\fo4_tests.exe`). Not built here. UNVERIFIED.

## Command template (PowerShell, data machine)

```powershell
$env:FO4_DATA_DIR = 'D:\SteamLibrary\steamapps\common\Fallout 4\Data'   # the Data folder itself
Set-Location <repo>\Radicle
& <repo>\build\native\bin\fo4_tests.exe --gtest_filter=SwfRealData.InterfaceCorpusDoActionCountAndNoLzmaCompression
Remove-Item Env:FO4_DATA_DIR
```

For every gate below at once, use this filter:

```
--gtest_filter=SwfRealData.*:SwfCensusReal.*:SwfFontsReal.*:AbcCorpusReal.*:AbcAsmReal.*:CApiRuntimeReal.ReplaysTheSevenProgramsDeterministically
```

Add `--gtest_output=xml:<dir>\<gate>.xml` to keep `RecordProperty` values. Each gate's properties are listed under "What to record".

## Gates

### 1. Whole-archive parse and byte round trip

- Test: `SwfRealData.ParsesEveryShippedMovieAndRoundTripsByteIdentically` (`Radicle/tests/test_swf_real.cpp:502`).
- Env: `FO4_DATA_DIR`. Loose `Interface\` movies are read first. The archive is read only when no loose movie and no LZMA movie was seen (lines 526-534). `DLCCoast - Main.ba2`, `DLCNukaWorld - Main.ba2` and `DLCRobot - Main.ba2` are scanned when present. An absent one records `absent` and is not a failure (lines 540-556).
- Pass: `movies > 0`; `movies + lzma == total`; `art_failures == 0`; `character_failures == 0`; `edit_text_tags > 0`; shape, bitmap, morph, fill-style and line-style counts `> 0` (lines 558-570). Each parsed movie must satisfy `write_swf(read) == original bytes` (`check_movie`, `test_swf_real.cpp:461`).
- Proves: every non-LZMA movie in the scanned sources parses, builds a character model, decodes its art, and writes back unchanged.
- Unverified or not proved: an LZMA (`ZWS`) movie is counted in `lzma` and returns from `check_movie` before any parse or round trip (`test_swf_real.cpp:416-420`). The round trip proves that Radicle's writer reproduces the input bytes. It says nothing about how the game renders the movie. No counts are pinned.

### 2. Interface corpus: DoAction count and no LZMA

- Test: `SwfRealData.InterfaceCorpusDoActionCountAndNoLzmaCompression` (`test_swf_real.cpp:613`).
- Env: `FO4_DATA_DIR`. Reads only `Fallout4 - Interface.ba2`. No loose files, no DLC.
- Pass: `movies > 0`; `lzma_movies == 0`; `do_action_tags == 82` (lines 658-660). The count walks every `DefineSprite` child recursively (`count_do_action`, lines 643-653).
- Proves: the Interface archive has 82 AVM1 `DoAction` tags and no ZWS movie.
- Caveat: `82` is an unconditional literal (line 660). No build check guards it, unlike gates 3 and 5. A movie that fails `read_swf` is skipped silently (lines 640-642), so a parse failure is caught only by gate 1.
- Unverified: whether 1.10.163.0 or 1.10.984.0 also gives 82. `Radicle/docs/code-notes/Radicle.md:5731` says the earlier 1.10.984 zero count was not reproduced, and no OG or NG scan is recorded. On a build where the count differs, this gate fails. It does not skip.

### 3. Program census against the committed manifest

- Test: `SwfCensusReal.MatchesTheCommittedProgramManifest` (`Radicle/tests/test_swf_census_real.cpp:104`).
- Env: `FO4_DATA_DIR`, plus the compile-time `FO4_TESTDATA_DIR` (see ground rules). Manifest: `Radicle/testdata/swf/program_census.txt`, with `# game_build 1.10.984.0` on line 3.
- Scope: every archive entry whose path contains `programs\` and ends `.swf` (`is_programs_movie`, line 22).
- Pass, every build: each Programs movie parses (`ASSERT_TRUE`, line 128); `blocks == parsed` (line 144); the seven names `AtomicCommand`, `GJ_Defrag`, `Grognak`, `PipFall`, `RedMenace`, `TurretHack` and `ZetaInvaders` are present (lines 145-154); every movie with ABC blocks references engine names (lines 155-160).
- Pass, row check: only when the detected build is in `k_verified`, which is 1.10.163.0 or 1.10.984.0 (lines 170-171). The census text must equal the manifest byte for byte after its `# game_build` line is rewritten to the detected build (`with_game_build`, lines 179-183).
- Caveat: on any other build, including AE 1.11.240.0, the test records `manifest_build_unverified` and returns (lines 174-178). It passes without comparing a row.
- Proves: on OG or NG, Radicle's census equals the committed manifest. `Radicle/docs/code-notes/Radicle.md:5356-5358` says the manifest was produced by a scratch Python reader, not by Radicle's code, so this is agreement between two readers, not a check against the game.
- Build-label discrepancy, not resolved: the manifest header says `1.10.984.0`. The brief also cited `1.11.240.0` at `Radicle.md:~501`. No `1.11.240.0` appears on line 501 of `Radicle/docs/code-notes/Radicle.md`. The only SWF-census-area mention of `1.11.240.0` is `Radicle.md:5727`, for the DoAction evidence. Which build the census header should name is UNVERIFIED.

### 4. Embedded fonts

- Test: `SwfFontsReal.DecodesEmbeddedFontProgramsAndInterfaceFonts` (`Radicle/tests/test_swf_fonts_real.cpp:34`).
- Env: `FO4_DATA_DIR`. Archive only. Movies are entries whose path ends with `fonts_programs.swf` or `fonts_en.swf` (line 53).
- Pass: at least one font movie parses (`ASSERT_GT`, line 86); `fonts > 0`; `layout_fonts > 0`; `total_glyphs > 100` (lines 86-89). For each layout font with a non-empty `advances`, `advances.size() == glyph_count` and `glyph_codes` is non-empty (lines 70-74).
- Proves: the embedded `DefineFont` tags decode with glyph counts and layout data.
- Not proved: glyph outline correctness or rendering. No counts are pinned. A font movie that fails to parse is skipped silently (lines 58-60), so only the `total_glyphs > 100` floor and the `movies > 0` assert catch it.

### 5. ABC corpus: decode, re-encode and verify

- Test: `AbcCorpusReal.DecodesEveryShippedInterfaceAbcBlock` (`Radicle/tests/test_abc_real.cpp:276`).
- Env: `FO4_DATA_DIR`. Loose `Interface\` movies first. The archive is read only if no ABC block was seen (line 301). Skips if neither source exists.
- Pass: `total_movies == movies + lzma + unreadable` (line 316); `blocks == parsed + unsupported_total()` (line 317); `trailing_bytes_blocks == 0`; `serialize_mismatches`, `reencode_mismatches` and `edit_mismatches` each `== 0`; `verified_flows == bodies`; `unsupported_total() == 0`; `parsed > 0`; `bodies > 0` (lines 316-332). `lzma == 0` is required only when `exact_counts_known` is true (lines 328-330), which is AE 1.11.240.0 (`real_data_build.cpp:95-96`).
- Proves: on every block of the shipped Interface movies, the ABC decoder, flow verifier, instruction re-encoder and nop insert/delete round trip all agree with the input bytes, and no block is unsupported.
- Not proved: any specific count. The test asserts no literal count. The AE figures (675 blocks, 30985 bodies) are in `Radicle/docs/code-notes/Radicle.md:4529` and were not re-observed here.

### 6. ABC text dump and assemble-back

- Test: `AbcAsmReal.EveryVanillaInterfaceBlockDumpAssemblesBackByteForByte` (`Radicle/tests/test_abc_asm_real.cpp:114`).
- Env: `FO4_DATA_DIR`. Archive only (`check_archive`, line 127).
- Pass: `movies > 0` and `blocks > 0` (`ASSERT_GT`, lines 136-137); `parse_failures`, `assemble_failures` and `serialize_mismatches` each `== 0` (lines 138-140). Each block must go `render_abc_text` then `assemble_abc_text` then `serialize`, and the result must equal the block payload byte for byte (`check_archive`, lines 53-108).
- Proves: the ABC text form is lossless for every block of the shipped Interface archive.
- Not proved: LZMA movies are counted in `lzma` and skipped (lines 61-63). No golden text file is compared.

### 7. Seven Programs replay (deterministic, not pinned)

- Test: `CApiRuntimeReal.ReplaysTheSevenProgramsDeterministically` (`Radicle/tests/test_c_api_runtime_real.cpp:383`).
- Env: `FO4_DATA_DIR`. Archive only. The seven movies are found by basename, case-insensitively (lines 400-412). A missing movie is a failure, not a skip.
- What it drives: `fo4::avm2::SwfRuntime::create` with `options.bgs_host = true` (lines 340-343), not the C ABI. `Radicle/docs/code-notes/Radicle.md:6202` gives the reason. It runs 180 frames at 1/24 s with the fixed input log in `program_inputs` (lines 238-266): 40 mouse moves, a click, Enter, Space and a second click. Frames are run through `fo4::avm2::replay` (`Radicle/src/avm2/replay.cpp`).
- Pass, per program: two runs, both with `init` on, each reach 180 frames; the movie is still `playing` and the frame hashes are not all equal (lines 296-305); for AtomicCommand, Grognak, PipFall, RedMenace and ZetaInvaders, at least one `bgs.` call reaches the host (`expects_bgs`, lines 414-417, 435-437); the two runs' frame-hash vectors and host-log hashes are equal (lines 457-458). Then `check_pins` runs (lines 463, 369-379), which does nothing while the pins are zero.
- Pass, control: AtomicCommand and PipFall, run with `init` off, must differ from their `init` runs in both frame hashes and host-log hash (lines 471-481).
- Pass, total: `ran == 7` and `mismatches == 0` (lines 483-485).
- Proves: all seven Programs open, run a fixed input log for 180 frames without a script error, and produce identical frame hashes and host logs across two runs in one process. The `init` path changes what is drawn and logged.
- Does NOT prove: that the output matches the game. `kPinned` is all zero (`test_c_api_runtime_real.cpp:232-236`), so `check_pins` compares nothing (lines 372-377). This test proves two runs agree, not what they produce.
- Other unverified points: the `SetPlatform(0.0, false)` and `InitProgram()` argument types are guesses (`Radicle/docs/code-notes/Radicle.md:6205`). Whether `Interpreter` errors inside `replay` reach this test is unverified (`Radicle.md:6214`); the frame count and `playing()` are what is checked.

#### Filling the pins

- Source 1 (stderr): for every program that completes both runs, the test prints `R8_HASH <program> <snapshot> <hostlog> <bgs_calls>` (`test_c_api_runtime_real.cpp:455-456`).
- Source 2 (XML): the `program_<name>` property holds `ran snapshot=<n> hostlog=<n> bgs_calls=<n> asset_misses=<n>` (lines 464-468).
- Map: `<snapshot>` goes to `snapshot_hash`, which is compared with `run.hashes.back()` (line 373). `<hostlog>` goes to `host_log_hash` (line 376).
- Source of the values: a data-runner log only, after the bridge fix on branch `nomad/gt-swf-placement-move` has landed. A pin taken before that fix pins the wrong behaviour. In this checkout the branch is at 1896d4752 with 0 commits beyond `origin/nomad/dev`, so the fix is not in any commit I could see. Its worktree is `E:/Projects/Forgejo/GT-wt/swf-placement-move`.

### 8. Same file, not in the brief list: single-movie C ABI replay

- Test: `CApiRuntimeReal.RunsAProgramMovieDeterministically` (`test_c_api_runtime_real.cpp:130`). It matches the CI regex.
- Env: `FO4_DATA_DIR`. Archive only.
- Drives: one Programs movie (AtomicCommand preferred, lines 157-172) through the C ABI: `radicle_runtime_open_movie_from_ba2` (line 184) and `radicle_movie_replay` (line 120), 24 frames, run twice.
- Pass: first and second hash vectors match, and every hash is nonzero (lines 199-209).
- Skip: `the program movie needs a runtime feature that is still missing: <radicle_last_error>` (lines 195-198). A skip here is a missing runtime feature, not a pass.

## What to record

- The gtest summary lines: `[  PASSED  ]`, `[  FAILED  ]` and `[  SKIPPED ]`. Standard gtest output; the vendored gtest version is UNVERIFIED.
- Every skipped gate and the env var it needed. Every gate skips on a missing `FO4_DATA_DIR` or missing archive. Gate 8 also skips on a missing runtime feature. Gate 7 never skips for a missing feature; a program that fails is a failed test.
- `game_build` from any gate's XML. This value decides whether gate 3 compares rows and whether gate 5 requires zero LZMA.
- Gate 1 properties: `movies`, `lzma_movies`, `art_failures`, `character_model_failures`, `version_histogram`, `dlccoast_movies` and the other DLC keys.
- Gate 2: `do_action_tags` and `lzma_movies`.
- Gate 3: `program_movies`, `abc_blocks`, `abc_parsed`, `manifest_build_unverified` (set when the row check was skipped).
- Gate 4: `fonts`, `layout_fonts`, `total_glyphs`, `decoded`.
- Gate 5: `abc_blocks`, `parsed_blocks`, `bodies`, `verified_flows`, `flow_refusal_histogram`, `unsupported_status_histogram`.
- Gate 7: stderr `R8_HASH` lines, `program_<name>` properties, and `ran`.

## UNVERIFIED

- Whether the `fo4-data` runner has `FO4_DATA_DIR` set and which Fallout 4 build it has. The workflow reads the build in its "Print Fallout 4 build" step, but no value is in the repo.
- The exact `fo4_tests.exe` location (derived from CMake, not built here).
- The gtest summary and `GTEST_SKIP` output format for the vendored gtest version.
- Whether 82 DoAction tags, 675 ABC blocks or the other AE counts hold on OG 1.10.163.0 or NG 1.10.984.0.
- The census header build: `1.10.984.0` on line 3 of `program_census.txt` against the `1.11.240.0` the brief cites at `Radicle.md:~501`. The citation was not found on that line. Not resolved here.
- Whether the seven-program pins can be filled before the bridge fix lands. They cannot be trusted until it does.
- The `SetPlatform` and `InitProgram` argument types (gate 7).
- Whether Radicle's census equals the manifest on a data runner. The row check runs only there and was not run here.
