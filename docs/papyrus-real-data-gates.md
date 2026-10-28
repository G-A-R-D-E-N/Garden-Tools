# Papyrus real-data gates: data-machine runbook

Verified against `origin/nomad/dev` at 9ee84f058. Every test is in the `fo4_tests` gtest binary (`Radicle/CMakeLists.txt`, target `fo4_tests`). Items marked UNVERIFIED were not found in the repo. Line numbers cited for the parse, semantic and F4SE tests predate the shared-helper refactor and may be off by a few lines.

## Ground rules

- Gates read process env vars. Unset or empty means `GTEST_SKIP`: the test is skipped, not run, and is not a pass. Report every SKIPPED line.
- Path lists are split on `;` on Windows (the shared `rootsFromEnv` helper uses `rootListSeparator()`, `;` under `_WIN32`, `:` elsewhere). Never `:` on Windows, which is the drive separator.
- A value that names no existing directory yields no roots or no files, and the gate skips. It does not fail. Set-but-bad paths can silently fall back (parse gate to `FO4RE_PSC_CORPUS`, `PapyrusSourceRealTest` to `testdata/papyrus`).
- None of these gates reads `FO4_DATA_DIR`.
- The shipped `.pex` are inside `Fallout4 - Misc.ba2`, not loose. The staging method is in the "Building the vanilla corpus" section of `PluginEditTool/docs/internal/PAPYRUS.md`, using `PluginEditTool/tools/build-vanilla-pex-corpus.sh` (a bash script). A Windows staging route is UNVERIFIED.
- Vanilla `.psc` roots are `<Fallout 4>/Data/Scripts/Source/{User,Base}` (`PAPYRUS.md:518`). Which root set reproduces the documented counts (7,854 files, 18,802 parse corpus, 1,498 decompile sample) is UNVERIFIED.
- Binary location: CI configures `cmake -S PluginEditTool\native -B build\native -G Ninja -DCMAKE_BUILD_TYPE=Release ... -DRADICLE_BUILD_TESTS=ON` (`.github/workflows/native-ci.yml`, job "Native real-data tests (Windows, MSVC, Fallout 4 data)", step Configure). `PluginEditTool/native/CMakeLists.txt:8` sets `CMAKE_RUNTIME_OUTPUT_DIRECTORY` to `${CMAKE_BINARY_DIR}/bin`, so the binary is expected at `build\native\bin\fo4_tests.exe`. This is derived from the CMake files and was not observed on a built tree.
- Working directory must be `<repo>\Radicle`. `gtest_discover_tests` sets `WORKING_DIRECTORY` to the Radicle source dir (`Radicle/CMakeLists.txt`, near line 1018), and `FO4_TESTDATA_DIR` is the relative `testdata` (line 1010).
- CI sets none of these variables. `native-ci.yml` has no `FO4RE_`, `FO4_PSC`, `FO4_PEX` or `FO4RE_F4SE` reference. Whether the `fo4-data` runner has them in its machine env is UNVERIFIED.
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
- Companion, same env: `PapyrusSemanticReal.YieldsSymbolsAcrossTheCorpusWithoutCrashing` (line 51), passes when symbols > 0.

### 3. Duplicate declaration

- Test: `PapyrusSemanticReal.DuplicateDeclarationCheckFlagsNothingInTheVanillaSources` (`test_papyrus_semantic_real.cpp`, line 115).
- Env: `FO4RE_PSC_ROOTS`.
- Pass: parsed > 0 and `declarationCheck` reports nothing on cleanly parsed files (lines 142-146). Unparseable files are counted, not failed.
- Skip: `FO4RE_PSC_ROOTS is not set; sweep skipped.` (line 117).
- Run: `$env:FO4RE_PSC_ROOTS = '<psc roots>'`, then the template with this filter.

### 4. CK differential (158 of 161 documented)

- Test: `PapyrusCodegenReal.CompiledOutputMatchesCreationKitInstructionForInstruction` (`Radicle/tests/test_papyrus_codegen_real.cpp`, line 228).
- Env: `FO4RE_PEX_CORPUS`, or `FO4_PEX_CORPUS` when that yields no roots (lines 229-230). Extra script roots: `FO4RE_PSC_ROOTS` (lines 235, 257). `FO4RE_PEX_RELEASE` must be exactly `1` for a vanilla corpus (line 236). It switches the mode printed at lines 239-240 and turns off debug and beta emission (lines 244-245).
- Pairing: each `.pex` needs a `.psc` under `Source/User` or `Source` (`sourceFor`, lines 91-116). Only Fallout 4 `.pex` count (`isFallout4`, lines 65-78).
- Pass: compiled > 0 and identical / compiled >= 0.95 (`kMinimumIdenticalFraction`, line 39; checks 297-299).
- Skip: `FO4RE_PEX_CORPUS is not set to a directory holding .pex files` (line 232); `FO4RE_PEX_CORPUS holds no .psc/.pex pairs` (line 241).
- Output: `pairs= compiled= refused= identical=` (lines 287-288), then `REFUSED` and `DIFF` lines.
- Run: `$env:FO4RE_PEX_CORPUS = '<staged pex root>'`, `$env:FO4RE_PSC_ROOTS = '<psc roots>'`, `$env:FO4RE_PEX_RELEASE = '1'`, then the template with this filter.
- Documented figure: `PluginEditTool/docs/internal/PAPYRUS.md:86` gives 198 pairs, 161 compiled, 158 identical (98.1%), 37 refused, 3 differing, with `FO4RE_PEX_CORPUS` and `FO4RE_PSC_ROOTS`. The code threshold is 0.95, so 158/161 would pass. The row does not name its test; `PAPYRUS.md:132` names the native PET gate that reads the same variables. Whether this Radicle test reproduces 158/161 is UNVERIFIED.

### 5. Decompile fidelity (>= 0.802)

- Test: `PapyrusCodegenReal.DecompiledCorpusRecompilesToTheSameProgram` (`test_papyrus_codegen_real.cpp`, line 302).
- Env: `FO4RE_PSC_ROOTS` only. It is both the index roots and the sample source.
- Sample: the first 1,500 `.psc` by path (`kDecompileSampleLimit`, lines 41, 318).
- Pass: files non-empty, compiled > 0, and matched / compiled >= 0.802 (`kMinimumDecompileFidelity`, line 40; checks 399-402). The `DECOMPILE files= compiled= matched=` line is printed at line 384.
- Skip: `FO4RE_PSC_ROOTS is not set to a directory holding .psc sources` (line 305).
- Documented figure: `PAPYRUS.md:89` gives 1,202 of 1,498 (80.2%) with `FO4RE_PSC_ROOTS`.
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

- Tests in `Radicle/tests/test_f4se_corpus_real.cpp` (filter `F4seCorpusReal.*`): `EveryRegistrationInARealTreeIsRecovered` (line 155), `RecoveredBindingsAreInternallyConsistent` (211), `RecoveredRegistrationsAgreeWithThePapyrusDeclarations` (234), `TheTwoShippedVersionsDifferOnlyInWaysTheDiffCanName` (255).
- Env: `FO4RE_F4SE_SRC`, a directory holding F4SE source trees (`rootsFromEnv`, lines 33-44). It needs F4SE sources, not game data. Where those sources sit on the data machine is UNVERIFIED.
- Pass: per tree natives == present, problems == 0, unmapped == 0, totalPresent > 0, totalRecovered == totalPresent (lines 199-208). Consistency: checked > 0 with field checks (221-231). Agreement: recovered > 0 and no mismatches (250-251). Diff: `newer - older` count equals added minus removed (283-284).
- Skip: four messages starting `FO4RE_F4SE_SRC is not set` (lines 157, 213, 236, 257). A variable pointing at a bad path shows the same message.

## What to record

- The gtest summary lines (`[  PASSED  ]`, `[  SKIPPED ]`, `[  FAILED  ]`). Standard gtest output; the vendored gtest version is UNVERIFIED.
- The `--gtest_output=xml` file for gate 1, which holds `files`, `clean`, `malformed` and `error_codes`.
- The `pairs=` and `DECOMPILE` stdout lines for gates 4 and 5.
- Each skipped gate, with the env var it needed.

## UNVERIFIED

- A Windows route to stage the `.pex` corpus (the staging script is bash).
- Which root sets reproduce 7,854 / 18,802 / 1,498 / 161.
- Whether the `fo4-data` runner has any gate env var set.
- The exact `fo4_tests.exe` location (derived from CMake, not built here).
- The gtest summary and `GTEST_SKIP` output format for the vendored gtest version.
- Whether the Radicle CK test reproduces 158/161.
- Where the F4SE source trees live on the data machine.
