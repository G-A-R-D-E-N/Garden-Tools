# CI runners: the on-demand self-hosted fleet

How `branch-validation.yml` actually gets executed, where the runners live, why they are
stopped most of the time, and what to do when CI queues forever.

For the still-unregistered Windows leg see [`WINDOWS_CI_RUNNER.md`](WINDOWS_CI_RUNNER.md).

> **Security -- absolute rule: no self-hosted runner or runner group may be reachable by a
> PUBLIC repo.** Anyone can fork a public repo and open a PR; a self-hosted job would then
> build/test that untrusted fork code on this box -- which also hosts a production GitLab
> and has passwordless sudo (GitHub's own docs: self-hosted runners "should almost never be
> used for public repositories"). This is an infrastructure rule, enforced at registration,
> not a workflow convention:
>
> 1. **Remove any repo-level self-hosted runner registered on a public repo**, and
> 2. **Exclude every public repo from the org runner groups** that expose this fleet
>    (`PRISMA-USER-INTERFACE-FRAMEWORK`, `G-A-R-D-E-N`, `Fallout-Anomaly`). An org runner
>    is reachable by any repo the runner group allows; "public repos = no access" must be
>    set on the group.
>
> The public mirror `G-A-R-D-E-N/FO4IDE` runs its CI on **GitHub-hosted** runners
> (`ubuntu-latest` / `windows-latest`), free and unlimited on public repos. The guard
> workflow `.github/workflows/no-self-hosted-guard.yml` on that repo is **defense-in-depth
> only** -- it fails CI if a workflow reintroduces a `self-hosted` label, but it CANNOT stop
> a self-hosted job from *executing first* if a runner is actually reachable, so it is not a
> substitute for removing the registration / scoping the runner group.
>
> **Verification status (2026-08-16): CLOSED across all three orgs.** Audited via the API.
> The `Default` runner group (id 1) of **every** org that shares this fleet was
> `visibility=all`, so each org's self-hosted runner was reachable by that org's PUBLIC
> repos -- the real hole, below the workflow-level fix:
> - `G-A-R-D-E-N`: public `BGS`, `FO4IDE`, `.github`
> - `PRISMA-USER-INTERFACE-FRAMEWORK`: public `PrismaVRFork`, `Prisma2.0`, `PRISMADESIGNER`
> - `Fallout-Anomaly`: public `roadmap`, `changelog`, `fallenworld-website`
>
> Remediated: **all three** `Default` groups are now `visibility=selected`, scoped to that
> org's PRIVATE repos only; no public repo is selected in any of them (re-verified: 0 public
> repos in any group). No repo-level self-hosted runner is registered on any public repo
> (checked `FO4IDE`, `BGS`, `.github` -- all empty). The stale hyphenated
> `G-A-R-D-E-N/FO4-IDE` runner has since been removed from the host (no unit/dir/systemd record)
> and its two mappings dropped from `ghrunnerd.py`. Commands used
> (per org, group id 1; keep-list = that org's private repo ids):
>
> ```bash
> gh api -X PATCH orgs/G-A-R-D-E-N/actions/runner-groups/1 -f visibility=selected
> echo '{"selected_repository_ids":[<8 private repo ids>]}' | \
>   gh api -X PUT orgs/G-A-R-D-E-N/actions/runner-groups/1/repositories --input -
> ```
>
> Re-audit if a new public repo is added to the org: it will NOT auto-join the group now
> (that is the point), but confirm no one flips the group back to `all`. See the memory note
> `project-public-repo-github-hosted-only`.

## Where the runners live

Everything runs on one Debian VPS, reachable as the `gitlab` alias in `~/.ssh/config`
(`git.nomadicinteractive.dev:63985`, user `nomad`, hostname `v2202608364060495190`).
That box also serves a public GitLab, so it is not a disposable CI box -- treat changes
there with care.

Five active runners under `/opt/actions-runner-*`, each a systemd unit named
`actions.runner.<ORG>[-<REPO>].<runner-name>.service` running as user `github-actions`.
Two of them serve this repo. (A sixth unit, `G-A-R-D-E-N-FO4-IDE.debian-wine-msvc-fo4-ide-2`,
was removed 2026-08-16: it was bound to `G-A-R-D-E-N/FO4-IDE`, which does not exist -- the repo
was renamed to the PUBLIC `FO4IDE`, orphaning it. Its mappings are gone from `ghrunnerd.py`.)

| Unit (suffix after `actions.runner.`) | Scope | GitHub target |
| --- | --- | --- |
| `PRISMA-USER-INTERFACE-FRAMEWORK-PluginEditTool.debian-wine-msvc-plugin-edit-tool-2` | repo | `PRISMA-USER-INTERFACE-FRAMEWORK/PluginEditTool` |
| `PRISMA-USER-INTERFACE-FRAMEWORK.debian-wine-msvc` | org | `PRISMA-USER-INTERFACE-FRAMEWORK` |
| `Fallout-Anomaly.debian-wine-msvc` | org | `Fallout-Anomaly` |
| `G-A-R-D-E-N.debian-wine-msvc` | org | `G-A-R-D-E-N` |
| `G-A-R-D-E-N-CO-BETA.debian-wine-msvc-2` | repo | `G-A-R-D-E-N/CO-BETA` |

The runner labels are `self-hosted, Linux, X64, wine, msvc`. `portable-core-and-web`
asks for `[self-hosted, Linux, X64]` and `windows-compile` for the full label set, so
either PRISMA runner can serve either job. Despite the `wine` and `msvc` labels, the
`windows-compile` job is a pure managed cross-compile (`-p:EnableWindowsTargeting=true`)
and needs neither Wine nor MSVC.

## Why the runners are stopped most of the time

The host is 16 GB / 8 CPU and originally had **no swap**. Observed peaks are 8.1 GB and
9.6 GB for a single job. Two concurrent builds would have exceeded physical memory with
nothing to absorb the overflow, so the fleet is serialized to one active runner.

Serialization lives in the waker, not in runner job hooks. A hook-based mutex
(`ACTIONS_RUNNER_HOOK_JOB_STARTED`) would block a job that had already started, so the
waiting job would burn its own 40-minute workflow timeout sitting in the queue. Leaving
the runner stopped instead means the job simply stays queued on GitHub's side, costing
nothing, until a slot frees.

## The supervisor

`/usr/local/lib/gh-runner/ghrunnerd.py`, vendored here as
[`tools/ci/ghrunnerd.py`](../../tools/ci/ghrunnerd.py). Subcommands:
`serve`, `drain`, `idle-stop`, `sweep`, `status`, `wake <unit>`.

State lives in `/var/lib/gh-runner` (`pending/` markers, `lock`, `idle-since.*`).

**Wake, primary.** Three org-level webhooks on the `workflow_job` event POST to
`https://git.nomadicinteractive.dev/gh-runner-hook`. One hook per org
(`PRISMA-USER-INTERFACE-FRAMEWORK`, `G-A-R-D-E-N`, `Fallout-Anomaly`) covers all six
runners. nginx terminates TLS and proxies to `127.0.0.1:9494` through an **exact-match**
location in `/etc/nginx/sites-enabled/git`; exact match outranks the prefix `location /`,
so it cannot shadow a GitLab route. The receiver verifies `X-Hub-Signature-256` against
`/etc/gh-runner/webhook.secret` and ignores anything whose action is not `queued`. An
empty or whitespace-only secret file is refused at startup rather than tolerated: it
would authenticate every caller against a publicly known empty HMAC key.

**Label filtering.** A wake only happens when the job's `workflow_job.labels` are ones
this fleet can actually serve -- `serves_labels()` requires `self-hosted` and rejects
anything outside `self-hosted, linux, x64, wine, msvc`. Without this, every queued job
in these three orgs woke a Linux runner, including `ubuntu-latest` and `windows-latest`
jobs and the gated `[self-hosted, Windows, X64]` leg. Since there is only one slot, each
of those consumed it and then sat idle for minutes while a runner that could serve its
job waited. A job whose labels are rejected is not stranded -- it is simply left to the
sweep, which is the right trade for a job this fleet cannot run anyway.

The corollary: if a repo in one of these orgs adopts a self-hosted label outside that
set, its jobs stop being woken promptly and fall back to a wait of up to 30 minutes.
Add the label to `RUNNER_LABELS` when that happens.

**Wake, fallback.** `gh-runner-sweep.timer` every 30 minutes marks every runner pending,
giving each a turn. This exists so a dropped webhook delivery cannot strand a job
indefinitely.

**Serialize.** `MAX_CONCURRENT = 1`. `_drain()` refuses to start a runner while another
is active and logs `slot full (1 active), <unit> stays queued`.

**Stop.** `gh-runner-idle-stop.timer` every 60 seconds stops a runner that has shown no
live `Runner.Worker` across consecutive ticks -- after 5 minutes normally, or 2 minutes
when another runner is pending -- then drains the queue so the next one starts.

While a job is actually running the idle markers are cleared, *unless* another unit is
pending. That exception matters: a runner with a continuous backlog claims its next job
within the same tick, so `job_running()` never goes false, the markers were wiped every
time, and the busy listener could never age into a stop. Other repositories starved
behind it indefinitely. Retaining the markers under contention means the marker is
already past threshold the moment the last job ends, so the slot rotates immediately
instead of after another full idle wait. Stopping is still re-checked under the lock, so
a runner that has just claimed work is never stopped out from under it.

### No credential on the host

The supervisor never calls the GitHub API, so **no GitHub token is stored on that box**.
The webhook secret only authenticates inbound calls. Keep it that way: the same host
serves public GitLab, and a PAT there would be a standing liability. Wake is push-based;
stopping is a purely local decision.

## Containment

All six units are in `actions-runners.slice`:

```
MemoryHigh=9G
MemoryMax=11G
MemorySwapMax=6G
CPUWeight=50
IOWeight=50
```

An 8 GB `/swapfile` was added (persisted in `/etc/fstab`, `vm.swappiness=10`) as an
overflow cushion rather than a performance tier.

## Operating it

```bash
sudo /usr/local/lib/gh-runner/ghrunnerd.py status
```

```bash
sudo journalctl -u gh-runner-webhook.service -f -o cat
```

Force a runner up immediately, bypassing the wait for a webhook:

```bash
sudo /usr/local/lib/gh-runner/ghrunnerd.py wake actions.runner.PRISMA-USER-INTERFACE-FRAMEWORK-PluginEditTool.debian-wine-msvc-plugin-edit-tool-2.service
```

Suspend on-demand behaviour and pin a runner up (for debugging a flaky job):

```bash
sudo systemctl stop gh-runner-idle-stop.timer && sudo systemctl start actions.runner.PRISMA-USER-INTERFACE-FRAMEWORK-PluginEditTool.debian-wine-msvc-plugin-edit-tool-2.service
```

Re-enable it afterwards with `sudo systemctl start gh-runner-idle-stop.timer`.

The runner units are deliberately `disable`d, so nothing starts at boot; the webhook and
the sweep bring them up. Do not `systemctl enable` them again unless you are reverting
the whole design.

## When CI queues forever

The failure that motivated all of this: a transient GitHub broker drop
(`SocketException (125)`, `BrokerMessageListener: Get next message has been cancelled`)
makes a runner cancel its in-flight job and exit with status **0**. The stock unit files
carried no `Restart=` directive, so systemd treated the clean exit as intended and left
the runner dead. Every subsequent run queued until someone noticed. All six units now
carry `Restart=always`, which does not fight `systemctl stop` and therefore coexists
with idle-stop.

Diagnosis order:

1. `gh api /repos/<owner>/<repo>/actions/runners` -- is the runner `online`?
2. `sudo /usr/local/lib/gh-runner/ghrunnerd.py status` -- is something stuck `pending`?
3. `sudo journalctl -u gh-runner-webhook.service` -- did the wake arrive?
4. Hook deliveries: `gh api /orgs/<org>/hooks/<id>/deliveries` -- did GitHub get a 200?
5. `/opt/actions-runner-*/_diag/Runner_*.log` -- the runner's own log. Note these are
   **not** in journalctl.

### Outage corpses are not source failures

After a runner dies, jobs killed mid-flight stay terminally failed even once runners
return; only still-queued jobs resume. A red run in that window is very likely an
artifact, not a real failure. The tell is `##[error]The runner has received a shutdown
signal` in the job log, plus a `completed_at` matching the runner's death. Confirm by
comparing job timestamps, then clear it:

```bash
gh run rerun <run-id> --failed
```

This bit real work on 2026-08-15: PR #168 looked like a Papyrus implementation failure
and was nothing of the sort.

## Real-data tests (FO4_DATA_DIR)

`native-ci.yml` runs the C++ tests that need a Fallout 4 `Data` folder when one is configured. Two repo variables feed them:

- `FO4_DATA_DIR_LINUX`, read by the `Test` step of `Native (Linux, Debug)`. It is unset today.
- `FO4_DATA_DIR_WINDOWS`, read by the `Test` step of `Native tests (Windows, MSVC)`. It points at the Steam install's `Fallout 4\Data` folder on the Windows runner. The `Test (Debug)` step clears it, so the Debug pass runs without game data: with data it exceeds the job's 90-minute limit.

The folder must hold the seven official masters (`Fallout4.esm`, `DLCRobot.esm`, `DLCworkshop01.esm`, `DLCworkshop02.esm`, `DLCworkshop03.esm`, `DLCCoast.esm`, `DLCNukaWorld.esm`) and at least one `Fallout4 - *.ba2` in its root. Each job's `Check FO4 data folder` step enforces this. Unset means the real-data tests skip. A set but bad value makes that step exit 1, which turns the required native-ci contexts red on every PR.

Data adds test time. Windows Release `Test` took about 437 s with data and about 30 s without (run 764).

This is separate from `FO4RE_TEST_DATA`, which [`WINDOWS_CI_RUNNER.md`](WINDOWS_CI_RUNNER.md) documents for the managed `FO4RecordEditor.Tests` fixture tests. Do not confuse the two variables.

Ask whoever sets `FO4_DATA_DIR_WINDOWS` to record the game build for that folder (the `Fallout4.exe` version). Some real-data asserts fail on a different build: `Radicle/tests/test_writeback_real.cpp:81`, `Radicle/tests/test_check_errors.cpp:195-197` and `Radicle/tests/test_weather_concept.cpp:607-611`. A game update can therefore turn CI red.

`FO4_DATA_DIR_LINUX` stays unset until a Linux Debug run with data has been timed against that job's 60-minute limit.

## Gotchas

- `swapon` and `mkswap` are not on the non-login `PATH`; use `/sbin/...` explicitly.
  `sudo` finds them via `secure_path`.
- Never leave an nginx backup inside `sites-enabled/` -- the directory is globbed, so the
  backup loads as a second config and `nginx -t` fails with `duplicate upstream`. Backups
  belong in `/etc/nginx/backups/`.
- `pgrep -f Runner.Worker` inside a script matches the script's own command line. Use a
  bracket pattern (`[R]unner\.Worker`).
- GitHub withholds job logs until the whole run finishes, so a mid-run failing job shows
  nothing until you cancel or let the run complete.
- `/opt/actions-runner-bgs` exists with no corresponding systemd unit -- an orphan from an
  earlier setup, not part of this fleet.
- `windows-tests` stays `skipped` until repo variable `RUN_WINDOWS_TESTS` is `true` and a
  `[self-hosted, Windows, X64]` runner exists. Never make it a required check while it is
  gated; a skipped required check blocks every PR forever.
