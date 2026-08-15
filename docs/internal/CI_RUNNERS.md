# CI runners: the on-demand self-hosted fleet

How `branch-validation.yml` actually gets executed, where the runners live, why they are
stopped most of the time, and what to do when CI queues forever.

For the still-unregistered Windows leg see [`WINDOWS_CI_RUNNER.md`](WINDOWS_CI_RUNNER.md).

## Where the runners live

Everything runs on one Debian VPS, reachable as the `gitlab` alias in `~/.ssh/config`
(`git.nomadicinteractive.dev:63985`, user `nomad`, hostname `v2202608364060495190`).
That box also serves a public GitLab, so it is not a disposable CI box -- treat changes
there with care.

Six runners under `/opt/actions-runner-*`, each a systemd unit named
`actions.runner.<ORG>[-<REPO>].<runner-name>.service` running as user `github-actions`.
Two of them serve this repo:

| Unit (suffix after `actions.runner.`) | Scope | GitHub target |
| --- | --- | --- |
| `PRISMA-USER-INTERFACE-FRAMEWORK-PluginEditTool.debian-wine-msvc-plugin-edit-tool-2` | repo | `PRISMA-USER-INTERFACE-FRAMEWORK/PluginEditTool` |
| `PRISMA-USER-INTERFACE-FRAMEWORK.debian-wine-msvc` | org | `PRISMA-USER-INTERFACE-FRAMEWORK` |
| `Fallout-Anomaly.debian-wine-msvc` | org | `Fallout-Anomaly` |
| `G-A-R-D-E-N.debian-wine-msvc` | org | `G-A-R-D-E-N` |
| `G-A-R-D-E-N-CO-BETA.debian-wine-msvc-2` | repo | `G-A-R-D-E-N/CO-BETA` |
| `G-A-R-D-E-N-FO4-IDE.debian-wine-msvc-fo4-ide-2` | repo | `G-A-R-D-E-N/FO4-IDE` |

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
`/etc/gh-runner/webhook.secret` and ignores anything whose action is not `queued`.

**Wake, fallback.** `gh-runner-sweep.timer` every 30 minutes marks every runner pending,
giving each a turn. This exists so a dropped webhook delivery cannot strand a job
indefinitely.

**Serialize.** `MAX_CONCURRENT = 1`. `_drain()` refuses to start a runner while another
is active and logs `slot full (1 active), <unit> stays queued`.

**Stop.** `gh-runner-idle-stop.timer` every 60 seconds stops a runner that has shown no
live `Runner.Worker` across consecutive ticks -- after 5 minutes normally, or 2 minutes
when another runner is pending -- then drains the queue so the next one starts.

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
