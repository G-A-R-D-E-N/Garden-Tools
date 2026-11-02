# Developer workflow: build, test, CI and review

Draft for new developers. Source: a read-only checkout of `nomad/dev`. Anything not found in the repo is marked "unverified". This page does not say whether any CI job passes today.

## Repo layout (top level)

- `Radicle/` is the native C++23 Fallout 4 plugin SDK (namespace `fo4`). It has `include/`, `src/`, `tests/`, `tools/`, `schema/`, `testdata/`, `bindings/` (C# interop), `docs/` (`Radicle/README.md`).
- `PluginEditTool/` is FO4IDE, the plugin and record editor. It has `native/` (C++ host), `web/` (React UI), the C# projects `FO4RecordEditor*`, and `Mutagen/` (`PluginEditTool/README.md`).
- The C# editor code is "retained only while native parity and regression coverage are completed" (`PluginEditTool/README.md`).
- `BGS/` is a third tool with its own CI (`.github/workflows/bgs-ci.yml`). It is not covered by this page.
- `docs/` holds `code-notes/`, `audits/`, `evidence/`, `RELEASES.md`, and the real-data runbooks `docs/papyrus-real-data-gates.md` and `docs/swf-real-data-gates.md`.
- `.github/workflows/` holds the CI workflows and `.github/scripts/` holds helper scripts.
- `ci/runners/` documents the self-hosted runners (`ci/runners/README.md`).
- `cmake/xwin-clang-cl.cmake` is a toolchain file (name suggests Windows cross-compile with clang-cl and xwin; the cross jobs in `native-ci.yml` use it, details unverified).
- `build/evidence/` holds one tracked file, `native-backend-progress.json`. It is not a build output directory (what writes it is unverified).
- `README.md` lists the tools; `AGENTS.md` holds the permanent testing rules.

## Building

### Radicle on its own

From `Radicle/README.md`, run inside `Radicle/`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Needs C++23, CMake 3.25+ and zlib. Radicle's CMake options include `RADICLE_BUILD_TESTS`, `RADICLE_BUILD_TOOLS`, `RADICLE_WARNINGS_AS_ERRORS`, `FO4_ENABLE_ASAN`, `FO4_ENABLE_UBSAN` (`Radicle/CMakeLists.txt`). Its CMake also runs schema checks (`radicle_schema_check` and similar) that fail if committed generated `.inc` files are out of date.

### PluginEditTool native (builds Radicle too)

`PluginEditTool/native` builds Radicle as a subproject, so one build covers both (comment in `.github/workflows/native-ci.yml`). The CI Linux configure line is:

```bash
cmake -S PluginEditTool/native -B build/native -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DFO4IDE_BUILD_TESTS=ON -DRADICLE_BUILD_TESTS=ON \
  -DRADICLE_BUILD_TOOLS=ON -DRADICLE_CSHARP_TESTS=ON -DRADICLE_WARNINGS_AS_ERRORS=ON
cmake --build build/native
```

The documented release build on Windows (`PluginEditTool/README.md`):

```powershell
cmake -S native -B native/build/release -G "Visual Studio 17 2022" -A x64 `
  -DFO4IDE_BUILD_TESTS=OFF
cmake --build native/build/release --config Release --target fo4ide
```

Windows prerequisites: Visual Studio 2022 (Desktop C++), CMake 3.25+, Python 3, Node.js/npm, Git, and internet for pinned FetchContent dependencies. `PluginEditTool/publish.bat [deploy-folder]` builds the UI and native host; `PluginEditTool/package.ps1` makes a release zip. Neither calls `dotnet` (`PluginEditTool/README.md`).

Linux: `PluginEditTool/packaging/linux/build-deb.sh 1.0.0` builds a `.deb`. It needs a C++23 toolchain, CMake/Ninja, Python 3, Node/npm, `dpkg-deb`, `fakeroot` (`PluginEditTool/README.md`).

### PluginEditTool web

Run in `PluginEditTool/web` (scripts from `PluginEditTool/web/package.json`; CI uses `npm ci` first):

| Command | What it does |
|---|---|
| `npm run dev` | `vite` dev server |
| `npm run lint` | `eslint .` |
| `npm run build` | `tsc -b && vite build` |
| `npm test` | vitest, one run |
| `npm run test:watch` | vitest watch mode |

### PluginEditTool managed (C#)

CI restores with `--locked-mode`, then builds and tests in Release. From `.github/workflows/plugin-editor-ci.yml`, run in `PluginEditTool`:

```bash
dotnet restore FO4RecordEditor.Core.Tests/FO4RecordEditor.Core.Tests.csproj --locked-mode
dotnet build   FO4RecordEditor.Core.Tests/FO4RecordEditor.Core.Tests.csproj --configuration Release --no-restore
dotnet test    FO4RecordEditor.Core.Tests/FO4RecordEditor.Core.Tests.csproj --configuration Release --no-build
```

The same steps run for `FO4RecordEditor.Tests/FO4RecordEditor.Tests.csproj` on Windows.

## Test suites and where tests go

From `AGENTS.md`:

| Area | Suite |
|---|---|
| Radicle | `Radicle/tests` (one gtest binary, `fo4_tests`) |
| Radicle real-data checks | `*_real.cpp` (runs only on the Fallout 4 data runner) |
| PluginEditTool native | `PluginEditTool/native/tests` |
| PluginEditTool managed | `PluginEditTool/FO4RecordEditor.Tests` and `PluginEditTool/FO4RecordEditor.Core.Tests` |
| BGS | `BGS/tools/tests` |
| PET web | colocated `*.test.ts(x)` files |

Testing rules (`AGENTS.md`, "permanent, for every agent and every change"):

1. Prefer end-to-end and integration tests with observable behavior (real FO4 plugin data, Papyrus, save/load, streaming, asset pipelines, reference artifacts).
2. Write the test from the failure mode before you implement the fix.
3. No new test file per bug; extend an existing suite when it fits.
4. No trivial getter, setter, constructor or mock-only tests. Do not duplicate behavior that is already well covered.
5. Every new test must say which realistic regression it detects.
6. End-to-end tests should produce deterministic evidence (files, hashes, logs, manifests) where practical.
7. Keep focused unit tests for boundaries, parsing, serialization, arithmetic and failure handling.
8. Do not build test infrastructure bigger than the feature it tests.

Radicle's test principles (`Radicle/docs/TEST_STRATEGY.md`): tests prove stored plugin reads, edits and serialization; Mutagen/FO4IDE results are oracles; unknown bytes must survive; FormKey tests catch master-index corruption. Acceptance for editable surfaces is parse, expose, edit, serialize, reparse. Sanitizers: `FO4_ENABLE_ASAN`, `FO4_ENABLE_UBSAN`.

## Real-data tests

- Some tests read an installed Fallout 4 `Data` folder through `FO4_DATA_DIR`. When it is unset they skip (`Radicle/docs/TEST_STRATEGY.md`). A skip is not a pass.
- Opt-in corpus variables named there: `FO4_PEX_CORPUS`, `FO4_PEX_ARCHIVES`. Papyrus gates use `FO4_PSC_CORPUS`, `FO4RE_PSC_CORPUS`, `FO4RE_PSC_ROOTS`, `FO4RE_PEX_CORPUS` (`docs/papyrus-real-data-gates.md`).
- Real-data tests record the game build (`game_build`). Exact-count assertions are pinned to AE 1.11.240.0. On OG and NG they record values and skip only the exact equality (`Radicle/docs/TEST_STRATEGY.md`).
- In CI, `REAL_DATA_TEST_REGEX` in `.github/workflows/native-ci.yml` picks which gtest names are "real-data". The normal Windows job excludes them (`ctest -E`). The `fo4-data` job runs them. The Linux job checks the regex still covers game-data tests (`.github/scripts/check_real_data_regex.py`).
- Per-gate run lines, env vars and pass rules: `docs/papyrus-real-data-gates.md` and `docs/swf-real-data-gates.md`. Those runbooks run `fo4_tests` from the `Radicle` directory.
- The data runner is a self-hosted runner with the label `fo4-data`. Its `FO4_DATA_DIR` is set in the runner config, and only the real-data job runs on it (`ci/runners/README.md`).

## CI workflows

All run on self-hosted runners; hosted runners are avoided on purpose (`.github/workflows/native-ci.yml` header). Runner details: `ci/runners/README.md`.

### `native-ci` (`.github/workflows/native-ci.yml`)

Triggers: push to `main`, pull requests, manual dispatch. Docs, BGS, PET web, C# and Mutagen paths are ignored. PRs run through the `pull_request` trigger only.

| Job name | What it does |
|---|---|
| Native (Linux, Debug) | Configure, build, check FO4 data folder, check the real-data regex, run the corpus-script unit tests, ctest (excluding `radicle_csharp_bindings`), then managed tests with the network off |
| Radicle (Windows cross, clang-cl/xwin, build-only) | Cross-build Radicle for Windows; not on PRs |
| PluginEditTool native (Windows cross, clang-cl/xwin) | Cross-build `FO4RecordEditor.exe`; not on PRs |
| Native tests (Windows, MSVC) | Release build and ctest excluding real-data tests; the Debug pass runs only off PRs |
| Native real-data tests (Windows, MSVC, Fallout 4 data) | Runs only on the `fo4-data` host; selects and runs the real-data tests when a Data folder is present |

### `plugin-editor-ci` (`.github/workflows/plugin-editor-ci.yml`)

Managed C# tests. Triggers on changes to `PluginEditTool/FO4RecordEditor*/**`, `Mutagen/**` and a few docs. Jobs: "Managed tests (Windows)" (both test projects) and "Core managed tests (Linux)" (Core tests only).

### `pet-web-ci` (`.github/workflows/pet-web-ci.yml`)

Job "Web lint, build and tests" in `PluginEditTool/web` with Node 22: `npm ci`, `npm run lint`, `npm run build`, `npm test`. Triggers on `PluginEditTool/web/**`.

### `bgs-ci` (`.github/workflows/bgs-ci.yml`)

Job "BGS build and test (matrix rid)". Triggers on `BGS/**` and `Radicle/**`, so a Radicle change also runs it. It builds the native Radicle library, then builds BGS .NET projects and runs the xUnit suite and UI smoke harnesses.

### `release` (`.github/workflows/release.yml`)

Manual dispatch only. Jobs: "FO4IDE package (Windows)" and "Radicle source snapshot (Linux)".

## CI guard (from `README.md`)

`native-ci` shares one self-hosted host. A guard, `ci-guard.mjs`, checks pushes and workflow dispatches in this repo:

| Rule | Refused when | Override |
|---|---|---|
| No manual runs on PR branches | A dispatch targets a branch with an open PR | `CI_DISPATCH_OK=1` with `CI_DISPATCH_REASON="..."` |
| Batch pushes | The branch has an open PR and its newest `native-ci` run is queued or running | `CI_PUSH_NOW=1` |
| Build and test locally first | A push changes C++ or CMake under `PluginEditTool/native` or `Radicle` and no passing local build is recorded for the commit | `CI_LOCAL_TEST_OK=1` |

To record a local pass, commit, then run `node ~/.codex/hooks/ci-guard.mjs --test build/radicle build/fo4ide`. It runs `cmake --build` and `ctest` on each directory and writes HEAD to `.git/ci-guard-tested`. The guard reads `FORGEJO_TOKEN` and allows the action with a warning if the token is missing or the API is slow (3 seconds).

## Code conventions

- Keep source files under the 750-line limit. `docs/code-notes/GARDEN-TOOLS.md` calls it "the 750-line source limit" and records files that were split to stay under it (for example `RadiclePluginBackend.cpp`, `form_links.cpp`). When a file nears the limit, split it by responsibility.
- Longer explanations, reasoning and bug history go in `docs/code-notes/<project>.md` (this repo has `docs/code-notes/GARDEN-TOOLS.md` and `Radicle/docs/code-notes/Radicle.md`), anchored to the code as `<file>:<symbol>`, not in source comments.
- Radicle lists its translation units in `Radicle/CMakeLists.txt`; add new source files there.

## Branch and PR flow

The repo documents only these points:

- PRs run CI through the `pull_request` trigger; push-to-branch runs are intentionally omitted, and `main` pushes run `native-ci` (`.github/workflows/native-ci.yml` header).
- Do not trigger manual runs on a PR branch and batch your pushes (CI guard above).
- Releases are manual dispatches of `release.yml`; see `docs/RELEASES.md`.

Branch naming, review and merge rules are not written down in the repo. Ask a maintainer.
