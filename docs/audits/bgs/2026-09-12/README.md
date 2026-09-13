# BGS audit evidence, 12 September 2026

This record preserves the [12 findings](report.md) from revision `d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958`. It does not assert that all findings remain open on current main. Source links in the report are pinned to that revision; the report was recorded from a separate branch based on `8d4c6685aa76a65e2a8e9ed408472769dc6aa3d9`.

## Evidence

The retained rerun records the audited source commit `d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958`, original evidence commit `9e4c749d94c69344974d15bc50e7a31e46a9e0d1` (not the current audit-record HEAD), and observed .NET SDK `8.0.424` (MSBuild `17.11.48+02bf66295`, Windows `win-x64`).

- [Original reproduction output](evidence/results.txt): findings 1–5, including both clipboard behaviors.
- [Unreadable character scan](evidence/scan-unreadable.txt): finding 6, exit code 0 despite an unreadable candidate.
- [Overridden behavior scan](evidence/scan-overrides.txt): finding 7, duplicate path counted twice and lower-priority copy reported.
- [Portable rerun output](evidence/rerun.txt): all seven reproduced findings using the supplied harness against the audited revision.

Log cache, project, and temporary-directory paths have been replaced with `<nuget-cache>`, `<audit-repro>`, and `<synthetic-inputs>` as applicable. Diagnostic text, values, and outcomes are unchanged. No game assets or generated binary files are included.

The original baseline was a successful Windows Release build with five warnings, 395 passing xUnit tests, and 188 passing headless UI checks. Those are recorded observations, not newly claimed CI results. The raw original baseline logs were not retained. The report identifies the skipped sample-driven coverage and limits of headless focus behavior.

## Reproduce the historical failures

Use the pinned .NET SDK 8.0.424. Create a separate checkout of the audited revision, or use an existing unchanged checkout of it. From a repository checkout:

```powershell
git worktree add --detach ../bgs-audit-source d2ae05ba1959bf7d8fa2d640ad07cab3ea6c1958
$source = (Resolve-Path ../bgs-audit-source).Path
git -C $source rev-parse HEAD
dotnet run --project docs/audits/bgs/2026-09-12/repro/Audit.csproj "-p:AuditSourceRoot=$source"
```

The harness rejects an `AuditSourceRoot` whose checked-out SHA differs from the audited revision or whose working tree has tracked or untracked changes. The project references existing BGS test fixtures and headless controls from `AuditSourceRoot`; the evidence-recording branch's production BGS files are not its test target. The command restores/builds the referenced projects and uses their existing package versions.

A successful run means the historical bugs **were reproduced**, not that BGS passed acceptance. Checks deliberately assert the observed failing behavior. Do not add this harness to normal CI as a passing regression suite; invert the relevant assertions when implementing fixes. It relies on private fixture names at the pinned revision and is not a general-purpose test tool.

The harness creates a fresh temporary directory, prints its location, and retains synthetic HKX files and preferences for inspection. It does not open a real mod manager profile or game installation. The skeleton test is bounded to 100,000 entries; it does not deliberately exhaust memory. The scan fixtures exercise BGS's ordering and path-selection logic, not a complete emulation of a live mod manager.

Findings 8 and 11 are source-traced rather than interactive reproductions. Findings 9, 10, and 12 are workflow/build/inventory findings. Current-main triage and native Windows/Linux acceptance remain separate work.
