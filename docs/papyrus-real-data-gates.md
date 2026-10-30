# Papyrus real-data gates: data-machine runbook

Verified against `origin/nomad/dev` at 6add80b69 (re-checked 2026-10-28): every gtest name below exists in `Radicle/tests/`, and the test-definition lines are current. Other in-file line numbers predate the shared-helper refactor and were not re-checked, so treat them as approximate. Every test is in the `fo4_tests` gtest binary (`Radicle/CMakeLists.txt`, target `fo4_tests`). Items marked UNVERIFIED were not found in the repo.

## Ground rules

- Gates read process env vars. Unset or empty means `GTEST_SKIP`: the test is skipped, not run, and is not a pass. Report every SKIPPED line.
- Path lists are split on `;` on Windows (the shared `rootsFromEnv` helper uses `rootListSeparator()`, `;` under `_WIN32`, `:` elsewhere). Never `:` on Windows, which is the drive separator.
- A value that names no existing directory yields no roots or no files, and the gate skips. It does not fail. Set-but-bad paths can silently fall back (parse gate to `FO4RE_PSC_CORPUS`, `PapyrusSourceRealTest` to `testdata/papyrus`).
- None of these gates reads `FO4_DATA_DIR`.
- The shipped `.pex` are inside `Fallout4 - Misc.ba2`, not loose. CI stages them on the data runner with `radicle_parity_dump ba2pex` ("CI staging route" below). The local method is in the "Building the vanilla corpus" section of `PluginEditTool/docs/internal/PAPYRUS.md`, using `PluginEditTool/tools/build-vanilla-pex-corpus.sh` (a bash script that needs the C# server).
- Vanilla `.psc` roots are `<Fallout 4>/Data/Scripts/Source/{User,Base}` (`PAPYRUS.md:543`). Which root set reproduces the documented counts (7,854 files, 18,802 parse corpus, 1,498 decompile sample) is UNVERIFIED.
- Binary location: CI configures `cmake -S PluginEditTool\native -B build\native -G Ninja -DCMAKE_BUILD_TYPE=Release ... -DRADICLE_BUILD_TESTS=ON` (`.github/workflows/native-ci.yml`, job "Native real-data tests (Windows, MSVC, Fallout 4 data)", step Configure). `PluginEditTool/native/CMakeLists.txt:8` sets `CMAKE_RUNTIME_OUTPUT_DIRECTORY` to `${CMAKE_BINARY_DIR}/bin`, so the binary is expected at `build\native\bin\fo4_tests.exe`. This is derived from the CMake files and was not observed on a built tree.
- Working directory must be `<repo>\Radicle`. `gtest_discover_tests` sets `WORKING_DIRECTORY` to the Radicle source dir (`Radicle/CMakeLists.txt`, line 1023), and `FO4_TESTDATA_DIR` is the relative `testdata` (line 1014).
- CI's real-data Test step sets `FO4_PSC_CORPUS`, `FO4RE_PSC_CORPUS` and `FO4RE_PSC_ROOTS` to the vanilla `Scripts\Source\User` and `Scripts\Source\Base` roots. The `Stage vanilla .pex corpus` step in the same job exports `FO4_PEX_CORPUS`, `FO4RE_PEX_CORPUS`, `FO4RE_PEX_RELEASE=1` and `FO4RE_PEX_REQUIRE=1`; it sets no `FO4RE_F4SE_SRC`, so gate 8 still skips. The Linux (`native-linux`) job sets none of the `FO4RE_PEX_*` variables and its PEX gates skip. Whether the `fo4-data` runner's machine env sets anything else is UNVERIFIED.
- CI's `REAL_DATA_TEST_REGEX` (`native-ci.yml:66`) matches every gate below except `PapyrusSourceRealTest.*`, which has `RealTest.` and no `Real.`. Checked by running the regex against each test name.
- The PR path filter in the real-data job (the `$groups` list) does not exclude any Papyrus gate.

## Command template (PowerShell, data machine)

```powershell
$env:FO4RE_PSC_CORPUS = '<psc root>;<psc root>'   # the variable differs per gate: use the one in its Env line
Set-Location <repo>\Radicle
& <repo>\build\native\bin\fo4_tests.exe --gtest_filter=PapyrusSemanticReal.ResolvesAndTypeChecksEveryCompleteCorpusScriptClean
Remove-Item Env:FO4RE_PSC_CORPUS
```

Add `--gtest_output=xml:<dir>\<gate>.xml` to keep `RecordProperty` values. For a whole test group, use `--gtest_filter=PapyrusSemanticReal.*` with every variable in the group set.

## Gates

### 1. Corpus sweep (parse)

- Test: `PapyrusParseReal.ParsesEveryCorpusScriptWithoutCrashing` (`Radicle/tests/test_papyrus_parse_real.cpp`).
- Env: `FO4_PSC_CORPUS`. `FO4RE_PSC_CORPUS` is read only when `FO4_PSC_CORPUS` is unset or empty (`corpus_roots`, lines 55-56). Roots are trimmed. `FO4RE_PSC_STRICT` set to any non-empty value enables the shape check (lines 216-217).
- Pass: 0 crashes, 0 unreadable, and clean / all files > 0.995 (line 219). Strict: every erroring file is one of the two documented malformed shapes, headerless under a `modified` folder or with 2+ `ScriptName` lines (lines 221-225). With strict set and files = 18,802: clean = 18,782, headerless = 19, repeated = 1 (constants lines 21-24, checks 227-231).
- Skip: `no .psc corpus found; set FO4_PSC_CORPUS to the corpus roots` (line 154).
- Run: `$env:FO4_PSC_CORPUS = '<psc root>;<psc root>'`, `$env:FO4RE_PSC_STRICT = '1'`, then the template with this filter.
- Not a gate: `PapyrusSourceRealTest.*` (`Radicle/tests/test_papyrus_source_real.cpp`) reads `FO4_PSC_CORPUS` only and falls back to `Radicle/testdata/papyrus` when it is unset. It is not in CI's regex.

### 2. Resolve and type-check (7,832)

- Test: `PapyrusSemanticReal.ResolvesAndTypeChecksEveryCompleteCorpusScriptClean` (`Radicle/tests/test_papyrus_semantic_real.cpp`).
- Env: `FO4RE_PSC_CORPUS` (`rootsFromEnv`). Trimmed, non-directories dropped.
- Pass: complete > 0 and zero problems (parse errors, resolver diagnostics, type-checker diagnostics on complete scripts) (lines 97-112). When the file count is 7,854, complete must equal 7,832 (`kVanillaFiles` and `kVanillaComplete`, lines 24-25, check 107-109).
- Skip: `FO4RE_PSC_CORPUS is not set; corpus sweep skipped.` (line 82).
- Companion, same env: `PapyrusSemanticReal.YieldsSymbolsAcrossTheCorpusWithoutCrashing` (line 41), passes when symbols > 0.

### 3. Duplicate declaration

- Test: `PapyrusSemanticReal.DuplicateDeclarationCheckFlagsNothingInTheVanillaSources` (`test_papyrus_semantic_real.cpp`, line 105).
- Env: `FO4RE_PSC_ROOTS`.
- Pass: parsed > 0 and `declarationCheck` reports nothing on cleanly parsed files (lines 142-146). Unparseable files are counted, not failed.
- Skip: `FO4RE_PSC_ROOTS is not set; sweep skipped.` (line 117).
- Run: `$env:FO4RE_PSC_ROOTS = '<psc roots>'`, then the template with this filter.

### 4. CK differential (158 of 161 documented)

- Test: `PapyrusCodegenReal.CompiledOutputMatchesCreationKitInstructionForInstruction` (`Radicle/tests/test_papyrus_codegen_real.cpp`, line 229).
- Env: `FO4RE_PEX_CORPUS`, or `FO4_PEX_CORPUS` when that yields no roots (lines 229-230). Extra script roots: `FO4RE_PSC_ROOTS` (lines 235, 257). `FO4RE_PEX_RELEASE` must be exactly `1` for a vanilla corpus (line 236). It switches the mode printed at lines 239-240 and turns off debug and beta emission (lines 244-245).
- Pairing: each `.pex` needs a `.psc` under `Source/User` or `Source` (`sourceFor`, lines 91-116). Only Fallout 4 `.pex` count (`isFallout4`, lines 65-78).
- Pass: compiled > 0 and identical / compiled >= 0.95 (`kMinimumIdenticalFraction`, line 40; check at line 300).
- Skip: `FO4RE_PEX_CORPUS is not set to a directory holding .pex files` (line 232); `FO4RE_PEX_CORPUS holds no .psc/.pex pairs` (line 241).
- Output: `pairs= compiled= refused= identical=` (lines 287-288), then `REFUSED` and `DIFF` lines.
- Run: `$env:FO4RE_PEX_CORPUS = '<staged pex root>'`, `$env:FO4RE_PSC_ROOTS = '<psc roots>'`, `$env:FO4RE_PEX_RELEASE = '1'`, then the template with this filter.
- Documented figure: `PluginEditTool/docs/internal/PAPYRUS.md:102` gives 198 pairs, 161 compiled, 158 identical (98.1%), 37 refused, 3 differing, with `FO4RE_PEX_CORPUS` and `FO4RE_PSC_ROOTS`. The code threshold is 0.95, so 158/161 would pass. The row does not name its test; `PAPYRUS.md:148` names the native PET gate that reads the same variables. Whether this Radicle test reproduces 158/161 is UNVERIFIED.

### 5. Decompile fidelity (>= 0.802)

- Test: `PapyrusCodegenReal.DecompiledCorpusRecompilesToTheSameProgram` (`test_papyrus_codegen_real.cpp`, line 303).
- Env: `FO4RE_PSC_ROOTS` only. It is both the index roots and the sample source.
- Sample: a stratified sample of up to 1,500 `.psc` (`kDecompileSampleLimit`, line 42; `stratifiedSample` in `Radicle/tests/papyrus_corpus_roots.hpp`). Candidates are sorted by lowercase path relative to their root, then every k-th is taken with k = ceil(N / 1,500), so the sample spans the corpus. The `SAMPLE candidates= selected= stride= first= last=` line (line 319) prints what was measured.
- Pass: files non-empty, compiled > 0, and matched / compiled >= 0.802 (`kMinimumDecompileFidelity`, line 41; check at line 404). The `DECOMPILE files= compiled= matched=` line is printed at line 388.
- Skip: `FO4RE_PSC_ROOTS is not set to a directory holding .psc sources` (line 306).
- Documented figure: `PAPYRUS.md:105` gives 1,202 of 1,498 (80.2%) with `FO4RE_PSC_ROOTS`. That figure came from the old first-1,500 prefix, so it is not comparable with the stratified sample. The 0.802 floor is unchanged and needs re-measuring on the data runner.
- Run: `$env:FO4RE_PSC_ROOTS = '<psc roots>'`, then the template with this filter.

### 6. Decompiler no-throw (not in the brief list; in CI's regex)

- Test: `PapyrusDecompilerReal.EveryCorpusPexDecompilesToSourceAndListingWithoutThrowing` (`Radicle/tests/test_papyrus_decompiler_real.cpp`).
- Env: `FO4_PEX_CORPUS` only.
- Pass: no exception, and each output has the `ScriptName ` and `; Disassembly of ` headers. Fallback and unhandled counts are printed (`[decompiler]` line), not failed.
- Skip: unset, `set FO4_PEX_CORPUS to one or more directories of .pex files`; no files, `no .pex files under <roots>`.

### 7. Graph round-trip fixtures (28): not a data gate

- The 28 fixtures are `allFixtures` in `Radicle/tests/graph_fixtures.hpp` (28 `add(` entries). `test_graph_compiler.cpp` and `test_graph_lift.cpp` build `Env(true)` (`Radicle/tests/graph_builder.hpp:Env::Env`), which indexes `Radicle/testdata/graph/BaseStubs`.
- No env var and no game data. They run in every `fo4_tests` run, so there is nothing to set on the data machine.
- The "28 / 28" rows in `PluginEditTool/docs/internal/GRAPH.md:105-109` name `GraphLiftRoundTripTests`, not these gtest names. Not re-verified here.

### 8. F4SE corpus

- Tests in `Radicle/tests/test_f4se_corpus_real.cpp` (filter `F4seCorpusReal.*`): `EveryRegistrationInARealTreeIsRecovered` (line 132), `RecoveredBindingsAreInternallyConsistent` (188), `RecoveredRegistrationsAgreeWithThePapyrusDeclarations` (211), `TheTwoShippedVersionsDifferOnlyInWaysTheDiffCanName` (232).
- Env: `FO4RE_F4SE_SRC`, a directory holding F4SE source trees (`rootsFromEnv`, lines 33-44). It needs F4SE sources, not game data. Where those sources sit on the data machine is UNVERIFIED.
- Pass: per tree natives == present, problems == 0, unmapped == 0, totalPresent > 0, totalRecovered == totalPresent (lines 199-208). Consistency: checked > 0 with field checks (221-231). Agreement: recovered > 0 and no mismatches (250-251). Diff: `newer - older` count equals added minus removed (283-284).
- Skip: four messages starting `FO4RE_F4SE_SRC is not set` (lines 157, 213, 236, 257). A variable pointing at a bad path shows the same message.

### 9. Graph lift sweep (sampled, 1,500 files)

- Test: `GraphLiftReal.LiftsAndRelinearizesSampledCorpusFunctionsToTheirOriginalBodies` (`Radicle/tests/test_graph_lift_real.cpp`). Its suite name ends in `Real`, so CI's `REAL_DATA_TEST_REGEX` (`native-ci.yml:66`) selects it with no regex change.
- Env: `FO4RE_PSC_CORPUS`, falling back to `FO4_PSC_CORPUS` (`rootsFromEnv`). CI sets both to the same `.psc` roots. `FO4_DATA_DIR` is not read.
- Sample: `stratifiedSample` in `Radicle/tests/papyrus_corpus_roots.hpp`, limit 1,500 (`kLiftSampleLimit`). Scripts that do not parse clean, or whose vanilla compile fails, are not counted.
- Per callable: every function, event, and state function or event of a sampled script. The graph lifter (`GraphLifter::lift`) refuses by diagnostic, and the test attributes each error to the callable whose start line is the nearest at or before the diagnostic's `Line N:`. A refused callable is counted under its reason. Otherwise `GraphCompiler::compile` (which runs the linearizer) rebuilds the script, and the callable is a round trip when `functionDifference` finds no difference from the vanilla compile.
- Pass: `attempted > 0` and `lifted >= 0.5 * attempted` (`kMinimumLiftedFraction`). The floor is an unmeasured placeholder and must be re-measured on the data runner. Refusals never fail the gate directly. They only count against the lifted floor.
- Round trip is reported, not asserted: `roundtrip=` in the `GRAPHLIFT` line and the `DIFF` lines. It stays log-only until it is measured on the data runner.
- Detects: a lifter change that refuses most of the vanilla corpus (lifted floor). A linearizer or compiler change that rebuilds a lifted body with different instructions, flags or signature shows as a lower `roundtrip=` count and as `DIFF` lines, but does not fail the gate yet.
- Skip: `FO4RE_PSC_CORPUS is not set to a directory holding .psc sources`.
- Remote events are compared under their pex name `::remote_<Type>_<Name>` (`buildEvent`, `code_generator.cpp`), so they round-trip under the same key.
- Refusal reasons: a lifter message, or `exception` for a callable of a script that threw before its lift finished. Scripts that throw before any callable is known count nothing.
- Output: `SAMPLE candidates= selected= stride=`, then `GRAPHLIFT attempted= lifted= roundtrip= refused=`, then up to 15 `REFUSED <count>  <reason>` lines (quoted names replaced by `'X'`) and up to 20 `DIFF` lines.
- Run: `$env:FO4RE_PSC_CORPUS = '<psc roots>'`, then the template with this filter.

## What to record

- The gtest summary lines (`[  PASSED  ]`, `[  SKIPPED ]`, `[  FAILED  ]`). Standard gtest output; the vendored gtest version is UNVERIFIED.
- The `--gtest_output=xml` file for gate 1, which holds `files`, `clean`, `malformed` and `error_codes`.
- The `pairs=` and `DECOMPILE` stdout lines for gates 4 and 5.
- Each skipped gate, with the env var it needed.

## CI staging route

`native-windows-real-data` builds `radicle_parity_dump` and runs it before the Test step:

```powershell
& .\build\native\bin\radicle_parity_dump.exe ba2pex <Data>\Fallout4 - Misc.ba2 $env:RUNNER_TEMP\vanilla-pex
```

`ba2pex <archive.ba2> <out-dir>` extracts every `.pex` entry with Radicle's own BA2 reader, keeping the archive's relative path, and writes nothing else. It exits non-zero when the archive is missing, when the archive holds no `.pex`, or when an entry path would resolve outside `<out-dir>`. The step removes a stale staging directory before extracting, so a failed run cannot be covered by an earlier one.

The archive names its entries `scripts\<name>.pex`, so the corpus root is `$env:RUNNER_TEMP\vanilla-pex\scripts`: a `.pex` at `<root>\<relative>.pex` pairs with the source at `<root>\Source\User\<relative>.psc` or `<root>\Source\<relative>.psc` (`test_papyrus_codegen_real.cpp:sourceFor`). The step copies `Scripts\Source\Base` into `<root>\Source` and nothing else, because `User` is where F4SE and mods keep edited vanilla scripts and `sourceFor` prefers it; staging `User` would report those edits as DIFFs. It fails when `Scripts\Source\Base` is missing, which is a staging error rather than a data verdict.

The step exports `FO4_PEX_CORPUS` and `FO4RE_PEX_CORPUS` (one root), `FO4RE_PEX_RELEASE=1` and `FO4RE_PEX_REQUIRE=1`, and only prints the `.pex`/`.psc` counts. The judgement lives in the tests: with `FO4RE_PEX_REQUIRE=1`, `papyrus_corpus_roots.hpp:skipOrFailPexCorpus` fails the PEX gates when the roots are unset, hold no `.pex`, or hold no `.psc`/`.pex` pairs, where an unset variable would skip them again.

The staged tree is `$env:RUNNER_TEMP\vanilla-pex`; no game data is copied into the repository and nothing is written under `FO4_DATA_DIR`.

Linux has no `.pex` staging yet: the pairing above resolves the source by its exact path, and case-sensitive pairing has not been measured.

## UNVERIFIED

- The counts the staged corpus produces in CI: the `pairs=` figure of gate 4 and the `DECOMPILE` figure of gate 5 have not been measured through this route.
- Which root sets reproduce 7,854 / 18,802 / 1,498 / 161.
- Whether the `fo4-data` runner has any gate env var set.
- The exact `fo4_tests.exe` location (derived from CMake, not built here).
- The gtest summary and `GTEST_SKIP` output format for the vendored gtest version.
- Whether the Radicle CK test reproduces 158/161.
- Where the F4SE source trees live on the data machine.
