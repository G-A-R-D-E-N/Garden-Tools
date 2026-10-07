# Radicle managed test package feed

This directory contains the complete 14-package dependency closure for the pinned managed tests. CI restores from these original `.nupkg` files using `../Radicle.Interop.Tests/NuGet.Config`, with no external package source or fallback directory. `../global.json` pins the SDK; the test project commits `packages.lock.json` and uses locked restore. The shipped library has no packages, lock settings or inherited feed config, so downstream consumers can select their own runtime identifiers and package sources.

`manifest.json` records each original NuGet URL, package version, byte length, SHA-256 and package-declared license. All archives were downloaded from those URLs and matched the previously resolved package bytes. Original package contents, signatures and license files remain intact.

`../tools/run_managed_tests.py:main` verifies the feed inventory and hashes before restoring copied projects with empty package/HTTP/plugin caches. Linux CI additionally runs the same complete suite in a loopback-only network namespace and requires an external connection to fail with ENETUNREACH. SDK/action installation and native dependency acquisition occur before this proof; an entirely disconnected runner bootstrap is not claimed.

When updating dependencies, obtain the original archives for the entire new closure, update the manifest and exact PackageReference versions, then regenerate the test lock using `dotnet restore Radicle.Interop.Tests --configfile Radicle.Interop.Tests/NuGet.Config --use-lock-file --force-evaluate -p:RestoreLockedMode=false` from the parent directory. Run the clean helper and both native CI jobs before accepting the new closure. Runtime CI never downloads missing packages or ignores failed restore/tests.
