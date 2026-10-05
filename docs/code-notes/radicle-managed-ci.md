# Managed binding CI

## .github/workflows/native-ci.yml:native-linux, native-windows-tests

The native test jobs explicitly install .NET 8 and configure `RADICLE_CSHARP_TESTS=ON`. Their existing required CTest steps execute `Radicle/CMakeLists.txt:radicle_csharp_bindings`, which passes the built native library, layout probe and ABI rejection stubs to the managed suite. Native cross-build jobs cannot execute those binaries and retain tests disabled.

`Radicle/bindings/csharp/Radicle.Interop.Tests/Radicle.Interop.Tests.csproj` pins its test packages. `dotnet test` restores them through the runner's configured package sources/cache. SDK setup or package restore failure fails the job; no soft-fail fallback substitutes generation checks for managed execution. This does not claim offline or pre-seeded runner acceptance. #384 remains open until the current head executes the managed suite on both runners; a restore failure must be resolved with an evidenced package source/cache.

The public setup action's [documented .NET 8 input](https://github.com/actions/setup-dotnet) is used. CI evidence is recorded against the actual PR head; successful manual managed runs do not close the CI gap.

`.github/workflows/native-ci.yml:native-windows-tests` sets the documented `DOTNET_INSTALL_DIR` to the job workspace's `build/dotnet`. Run 574 at `77052bcb1b2939d6a341830f9fc32d158593a3fd` failed SDK setup because the runner cannot install under `C:\Program Files\dotnet`. Its Linux job executed `radicle_csharp_bindings` successfully. Windows acceptance still requires the new head's CI result.
