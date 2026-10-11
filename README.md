# GARDEN-TOOLS

Native tools used across GARDEN projects.

| Tool | What it is |
|------|------------|
| [Radicle](Radicle/) | Fallout 4 plugin SDK. FormKey identity, lossless plugin IO, master remap. |
| [PluginEditTool](PluginEditTool/) | FO4 record / plugin editor (native host, Mutagen core, web UI). |
| [Resources/RE-Work](Resources/RE-Work/) | Creation Kit and Fallout 4 runtime reverse-engineering notes. |

Radicle roadmap: [milestones](https://github.com/G-A-R-D-E-N/GARDEN-TOOLS/milestones) and [Radicle/ROADMAP.md](Radicle/ROADMAP.md).

## CI guard

`native-ci.yml` runs on one shared self-hosted host, so a wasted run delays everyone's queue. A guard (`ci-guard.mjs` in the shared agent hooks) checks every `git push` through the global pre-push hook, and every agent push or workflow dispatch before it runs. It only acts on this repository.

| Rule | Refused when | Override |
|------|--------------|----------|
| 1. No manual runs on PR branches | A workflow dispatch targets a branch with an open pull request. The PR already runs CI for that commit. | `CI_DISPATCH_OK=1` together with `CI_DISPATCH_REASON="..."` |
| 2. Batch pushes | The branch has an open PR and its newest `native-ci` run is still queued or running. Each push cancels that run partway through. Finish the batch, then push once. | `CI_PUSH_NOW=1` (e.g. to fix a build break you just caused) |
| 3. Build and test locally first | The push changes C++ or CMake files under `PluginEditTool/native` or `Radicle` and no passing local build is recorded for the pushed commit. Docs-only and workflow-only pushes skip this. | `CI_LOCAL_TEST_OK=1` |

To satisfy rule 3, commit your changes, then build, test and record the result in one step:

```sh
node ~/.codex/hooks/ci-guard.mjs --test build/radicle build/fo4ide
```

It runs `cmake --build` and `ctest` on each build directory and, if all pass, writes the HEAD commit to `.git/ci-guard-tested`.

The guard reads a Forgejo API token from `FORGEJO_TOKEN`. If the token is missing or the API does not answer within 3 seconds, it allows the action and prints a one-line warning; it never blocks work because it is offline. Set an override for a single command, e.g. `CI_PUSH_NOW=1 git push`.
