# BGS — code notes

## `src/Archive/ScanReport.cs` — headless-scan JSON contract (issue #192)

`ScanReport` / `ScanFinding` / `ScanContext` are the machine-readable result of the headless scan
modes, emitted as one JSON document under `--json`.

- **Why it lives in `BehaviourGraph.Core`, not the app:** the model and its JSON envelope are
  unit-testable directly from `tools/tests` (`tools/tests/ScanReportTests.cs`) without the GUI;
  `app/ModlistScan.cs` only populates and serializes it. The end-to-end CLI contract (stdout/stderr
  routing, exit codes, real finding accumulation) is covered by `tools/tests/ScanCliTests.cs`, which
  references the app project (`ValidateExecutableReferencesMatchSelfContained=false`) to drive the
  real `ModlistScan` commands.
- **`ScanReport.ExitCode` mirrors the process exit code** (0 clean, 1 defects, 2 usage/config). It is
  set by the scan method — and by `ModlistScan.Fail` for the exit-2 paths — so the JSON's `exitCode`
  and the process status can never diverge; CI can gate on either.
- **camelCase + null-omitting** (`ScanReport.Options`): `--scan-archives` (no profile/dataFolder) and
  the modlist modes share one schema; `ScanContext.Profile`/`DataFolder` and `ScanFinding.Source`
  drop out of the JSON when null.
- **`schemaVersion`** (`ScanReport.CurrentSchemaVersion`, `[JsonPropertyOrder(-1)]` so it leads the
  document) lets CI consumers detect envelope changes. Bump it on any breaking schema change.
- Finding `kind` values: `missing-animations`, `missing-clip-animations`, `unreadable-character`,
  `unreadable-behaviour`, `corrupt`, `unreadable`, `skip`, `archive-warning`, plus `usage`/`config`
  for the terminal exit-2 outcomes below.
- **`summary` field set per mode** (the stable CI contract, asserted in `tools/tests/ScanCliTests.cs`):
  - `scan-archives`: `archives`, `scanned`, `corrupt`
  - `scan-modlist`: `checked`, `broken`, `missing`, `unreadable`
  - `scan-clips`: `checked`, `broken`, `missingClips`, `unreadable`
  - a terminal usage/config failure emits an **empty** `summary` with `exitCode: 2`.
- **`--json` always yields a report** (`ModlistScan.Fail`): every terminal outcome — a missing CLI
  argument, a bad `--scan-archives` directory, or an `Mo2ScanData.TryOpen` failure — emits a valid
  `ScanReport` with `exitCode: 2` (finding kind `usage` or `config`) to stdout, never a bare stderr
  line, so redirected stdout is always parseable JSON.

## `app/ModlistScan.cs` — `--json` wiring

- Under `--json`, progress/layout lines go to **stderr** (`ModlistScan.Info`) and per-defect human
  lines are suppressed, so **stdout carries only the JSON document** — safe to redirect in CI
  (`bgs --scan-clips <mo2> --json > report.json`).
- Findings accumulate during the existing, unchanged scan loops (`ScanModlist`, `ScanClips`, `Run`);
  the JSON `details` array carries the *full* defect list while human output still truncates
  (40 animations / 20 clips) for readability.
- **`skip` is warning-level and does NOT affect the exit code** (decision for `Run`/`scan-archives`).
  A `.ba2` that cannot be opened is surfaced as a `skip` finding but not counted toward `corrupt`,
  because it is most often a benign non-behaviour archive (e.g. a DX10 texture archive); the finding's
  `details` carry the reason. `exitCode` reflects only defects found in *inspected* content
  (`corrupt`). A consumer that must treat "could not inspect an archive" as a failure asserts the JSON
  has no `skip` finding rather than relying on the process status — see the exit-0 assertion in
  `tools/tests/ScanCliTests.cs`.
- `missing-clip-animations` from `ModlistScan.ScanClips` is the `BoundAnimationData::Create`
  load-crash class: a clip whose animation resolves nowhere across the merged load order. The
  resolution rule these scans depend on — clip references are authoring-time `.hkt` tails that
  resolve by stripping the authoring root and swapping the extension to `.hkx` — is verified FO4
  reverse-engineering of the animation load path; `GameData.Normalize` and `ProjectChain.ResolvePath`
  implement the extension-agnostic match that the scan's existence checks rely on.
