# PRISMA light runner

The PRISMA organization currently has one general self-hosted Linux runner serving the whole organization. Windows/F4SE jobs add `wine,msvc`, but cheap Node, .NET, and static jobs also compete for the same listener.

`prisma-light-1` is a second, deliberately restricted runner for those cheap jobs.

## Isolation model

The runner is organization-scoped so approved PRISMA repositories can use it, but it is registered with:

```text
--labels prisma-light --no-default-labels
```

It therefore has only the `prisma-light` routing label. It cannot satisfy jobs that ask for `self-hosted`, `Linux`, `X64`, `wine`, `msvc`, or any combination of those labels.

Workflows must opt in explicitly:

```yaml
runs-on: prisma-light
```

The service runs as its own non-root Unix account, is not allowed in privileged groups, and receives a per-service memory ceiling. It remains inside the host's `actions-runners.slice` aggregate boundary.

## Scheduler relationship

The light runner is intentionally always on and is not added to `ghrunnerd.py`'s managed `UNITS` map.

Deploy the managed-worker filtering change before relying on concurrent light/heavy jobs. The scheduler must only count `Runner.Worker` processes belonging to runners it manages; an active `prisma-light-1` worker must not occupy the heavy on-demand slot.

## Bootstrap

Use the tracked Actions runner archive version and digest from `tools/ci/prisma-runners.json`.

Mint a fresh PRISMA organization registration token, then run:

```bash
gh api -X POST orgs/PRISMA-USER-INTERFACE-FRAMEWORK/actions/runners/registration-token --jq .token \
  | bash tools/ci/install-prisma-runner.sh \
      prisma-light-1 \
      /path/to/actions-runner-linux-x64-2.336.0.tar.gz
```

The installer reads the token from standard input, creates the dedicated service account and install directory, registers the runner without default labels, installs the systemd service, applies the memory/security drop-in, and enables the service.

## Acceptance

Before merging workflows that use `runs-on: prisma-light`, verify all of the following:

```bash
systemctl is-active actions.runner.PRISMA-USER-INTERFACE-FRAMEWORK.prisma-light-1.service
systemctl show actions.runner.PRISMA-USER-INTERFACE-FRAMEWORK.prisma-light-1.service \
  -p User -p Slice -p MemoryHigh -p MemoryMax -p MemorySwapMax

gh api orgs/PRISMA-USER-INTERFACE-FRAMEWORK/actions/runners \
  --jq '.runners[] | select(.name == "prisma-light-1") | {name,status,busy,labels:[.labels[].name]}'
```

Expected remote labels are exactly `prisma-light`. If default labels such as `self-hosted`, `Linux`, or `X64` appear, remove and re-register the runner before routing jobs to it.

Then merge only cheap, non-Docker, non-Wine jobs that target `runs-on: prisma-light`. Keep F4SE/Wine jobs on the heavy runner and keep secret-bearing sync workloads in their dedicated trust boundary.
