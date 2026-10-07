# Managed binding CI

## Radicle/bindings/csharp/tools/run_managed_tests.py:main

The clean restore rehearsal copies both managed projects without bin/obj, assigns empty package, HTTP and plugin caches, and clears all package sources and fallback folders except the repository package feed. The SDK is pinned in `Radicle/bindings/csharp/global.json`. Linux CI runs the rehearsal in a fresh user/network namespace with only loopback; an external connection must fail with ENETUNREACH before restore and the complete managed suite run. Missing packages or failed tests fail the native Linux job. This first rehearsal is expected to reproduce the missing offline feed; no offline acceptance is claimed until its packages are supplied and the new head passes.

`.github/workflows/native-ci.yml:native-linux` and `native-windows-tests` publish the two exact contexts selected by the repository's new main protection rule. Direct main pushes are disabled and the rule is enforced for repository admins. The reopened saved form shows both contexts as Matched and admin enforcement checked; `build/evidence/pr426-protection-saved.txt` records that UI evidence because the API response omits the admin-enforcement field. Before this change, the repository reported zero branch protections. A live failing-check merge-gate proof remains pending.

## .github/workflows/native-ci.yml:native-linux, native-windows-tests

The native test jobs explicitly install .NET 8 and configure `RADICLE_CSHARP_TESTS=ON`. Their existing required CTest steps execute `Radicle/CMakeLists.txt:radicle_csharp_bindings`, which passes the built native library, layout probe and ABI rejection stubs to the managed suite. Native cross-build jobs cannot execute those binaries and retain tests disabled.

`Radicle/bindings/csharp/Radicle.Interop.Tests/Radicle.Interop.Tests.csproj` pins its test packages. `dotnet test` restores them through the runner's configured package sources/cache. SDK setup or package restore failure fails the job; no soft-fail fallback substitutes generation checks for managed execution. This does not claim offline or pre-seeded runner acceptance. #384 remains open until the current head executes the managed suite on both runners; a restore failure must be resolved with an evidenced package source/cache.

The public setup action's [documented .NET 8 input](https://github.com/actions/setup-dotnet) is used. CI evidence is recorded against the actual PR head; successful manual managed runs do not close the CI gap.

`.github/workflows/native-ci.yml:native-windows-tests` sets the documented `DOTNET_INSTALL_DIR` to the job workspace's `build/dotnet`. Run 574 at `77052bcb1b2939d6a341830f9fc32d158593a3fd` failed SDK setup because the runner cannot install under `C:\Program Files\dotnet`. Its Linux job executed `radicle_csharp_bindings` successfully. Windows acceptance still requires the new head's CI result.

`Radicle/bindings/csharp/Radicle.Interop.Tests/QuestRuntimeTests.cs`, `PexModelTests.cs:ReadsFromAFile` and `SeqModelTests.cs:ReadsFromAFile` use `Fixtures.cs:TempDirectory` for owned unique fixture paths. Concurrent quest suites at `cbe898279607d5e3efe2afcc53da778ec52b8042` reproduced file-sharing and directory-deletion failures with the previous fixed paths; proof logs are in that worktree's ignored `build/parallel-red-detail-{a,b}.log`. These are fixture isolation failures, not plugin mutation failures.

`.github/workflows/native-ci.yml:native-windows-tests` shuts down the job's .NET build servers before workspace cleanup. Both native jobs at `561888c78e57d081e9fe8e722f1eb6be618046f7` passed managed CTest in run 576, but Windows cleanup reported access denied deleting the workspace SDK executable. [The documented shutdown command](https://learn.microsoft.com/en-us/dotnet/core/tools/dotnet-build-server) stops persistent build servers; clean teardown still requires current-head CI verification.
