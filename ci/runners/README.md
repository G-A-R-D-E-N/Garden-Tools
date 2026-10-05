# Self-hosted runners and the 2026-10-04 CI audit

All Forgejo Actions runners for GARDEN-TOOLS, GardenEngine and Garden_Engine_Source
run on one 12-thread Windows host: Windows runners natively, Linux runners in the
`Ubuntu` WSL distro (user `nomad`, systemd `--user` units). Forgejo itself runs on
the VPS behind `git.nomadicinteractive.dev`.

## Scripts in this directory

Installed on the runner host at `C:\tools\forgejo-runner\`.

| Script | Called from | Why |
|---|---|---|
| `clear-stale-git-locks.ps1` | every Windows `start-runner.ps1`, and the `ForgejoRunner-clear-stale-git-locks` scheduled task every 5 min | A runner killed mid action-fetch leaves `shallow.lock` in the shared action cache (`C:\ci\act\<2hex>\<62hex>`); every later job using that action then fails before its first step. Removes locks older than 5 minutes. |
| `alias-profile-path.ps1` | every Windows `start-runner.ps1` | Rewrites PATH entries under the real profile to `C:\ci\home\...` so CI logs never show the profile folder name. |

## Runner settings (host-side, not in any repository)

| Runner | Labels | Slots | Notes |
|---|---|---|---|
| `GARDEN-Windows-...` (`C:\tools\forgejo-runner\garden`) | self-hosted, Windows, X64, windows, windows-latest | 1 | `CMAKE_BUILD_PARALLEL_LEVEL=4` |
| `GARDEN-MSVC-Windows` (`C:\tools\forgejo-runner\garden-msvc`) | self-hosted, Windows, X64, garden-msvc | 1 | `self-hosted` added so main's MSVC job can use it; `CMAKE_BUILD_PARALLEL_LEVEL=4` |
| `garden-linux-wsl`, `garden-tools-linux-wsl-dedicated` (WSL) | self-hosted, Linux, X64, garden | 2 each | `DOTNET_INSTALL_DIR=/home/nomad/.dotnet` (no sudo for `/usr/share/dotnet`), `CMAKE_BUILD_PARALLEL_LEVEL=4` |
| `gardenengine-win-runner-2` (`Gitrunners\windows-garden-engine-source-forgejo`) | self-hosted, Windows, X64, gardenengine, windows-2022, windows-latest | 2 | only `windows-2022` runner for GardenEngine |
| `gardenengine-ubuntu-runner-2`, `garden-engine-ubuntu-runner` (WSL) | self-hosted, Linux, X64, gardenengine, ubuntu-latest, ubuntu-24.04 | 1 each | the second one previously had only the ubuntu-* labels |

All Windows runners: `workdir_parent: C:\ci\act`, HOME/TEMP/APPDATA under the `C:\ci\home`
junction, no HTTP(S) proxy. Linux runners keep a per-runner `workdir_parent` under
`~/.cache/act-<runner>`.

## Problems found and how each was fixed

### GARDEN-TOOLS (`native-ci.yml`, `bgs-ci.yml`)

| Problem | Fix |
|---|---|
| ccache hit 0.5-0.9%: workflow-level `CCACHE_BASEDIR: ${{ github.workspace }}` is empty on host-mode runners, so every per-job directory hashed differently | PR 413: each ccache step writes `CCACHE_BASEDIR=$PWD`. Hits went to 93-98%, Linux job ~25 min to ~4-8 min. |
| clang-cl/xwin cross lanes compiled without a cache | PR 414 |
| Up to seven builds at once, each one compile per core, on 12 threads | PR 415 + runner env: `CMAKE_BUILD_PARALLEL_LEVEL=4`; Windows Debug pass on main only |
| 63 graph tests failed after PR 413: fixtures located via `__FILE__`, which ccache's base_dir made build-relative | PR 417: `FO4IDE_TEST_FIXTURES_DIR` from CMake. (PR 416, reverting parallel ctest, was a wrong diagnosis.) |
| Merges cancelled main's previous run; four jobs without time limits; serial ctest; MSVC Debug `/Zi` uncacheable; BGS downloaded .NET every job | PR 418: per-commit main concurrency groups, timeouts, `ctest --parallel 4`, `/Z7`, .NET in the runner tool cache |
| Main's MSVC job could only use one runner slot | runner: `self-hosted` label on `GARDEN-MSVC-Windows` |

Rule from PR 417: with ccache `base_dir`, never locate files from `__FILE__`.

### GardenEngine

| Problem | Fix |
|---|---|
| `Garden project compile` timed out at 30 min building Radicle serially (VS generator) | PR 261: `CL=/MP4`, `--parallel 4`, 45 min |
| Merges cancelled main runs (7 workflows); .NET downloaded every compile; CMake tarball every run; Linux Radicle builds uncached and one compile per core | PR 264 |
| Stock Godot, the engine release and xmake (built from source) fetched every run | PR 265: per-runner tool cache under `flock` |
| Winding engine contract ran on stock Godot 4.7.2 while the project targets 4.8 double precision | PR 266: runs on the Garden engine release |
| Engine asset name hard-coded to `godot-4.8.0-dev-double-mono-linux64.zip` | PR 267: matched by pattern |
| Vendored `Godot.NET.Sdk`/`GodotSharp` packages updated by hand and older than the engine CI uses | PR 268: hourly `engine-update` workflow opens an update PR (needs `ENGINE_UPDATE_TOKEN` secret) |
| Only one runner per platform for GardenEngine jobs | runner: second Windows slot; `gardenengine` labels on the second Linux runner |

### Garden_Engine_Source

| Problem | Fix |
|---|---|
| Every build's .NET packages versioned `4.8.0-dev-double`, so NuGet caches could not tell builds apart | PR 20: `GODOT_VERSION_STATUS=dev-double<run number>` gives `4.8.0-dev-double.<n>` |
| Release publishing used `127.0.0.1:3000`, a different Forgejo since the VPS move | PR 20: `$GITHUB_SERVER_URL` |

### Runner host

| Problem | Fix |
|---|---|
| Stale `shallow.lock` in the shared action cache failed every job using that action | `clear-stale-git-locks.ps1` at runner start and every 5 minutes |
| CI logs showed the real profile path through PATH entries | `alias-profile-path.ps1` |
| Windows runners proxied all traffic through a proxy inside WSL; jobs failed with `Proxy CONNECT aborted` whenever WSL was busy | proxy removed from the Windows runners' start scripts and job env |
| `setup-dotnet` failed on Linux runners (`mkdir: Permission denied`) | `DOTNET_INSTALL_DIR=/home/nomad/.dotnet` |

## Re-running jobs without the web UI

There is no re-run API. The web UI's re-run resets the job row (`status=5`, `task_id=0`,
`started=stopped=0`, `attempt+1`), sets the run back to waiting, and bumps
`action_tasks_version` for the repository, owner and global scopes; runners only ask for
new work when that version changes.
