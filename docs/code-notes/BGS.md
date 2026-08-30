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

### `BGS/app/ModlistScan.cs:ModlistScan.ScanClips`

- `missing-clip-animations` from `ModlistScan.ScanClips` is a static unresolved-reference finding:
  a clip whose animation resolves nowhere across the merged load order. It is not a reproduction of
  the Fallout 4 animation-DB or binding failure path. Runtime evidence recorded in issue #207 shows
  guarded lookup, update gating, default-animation fallback, and validation diagnostics for missing
  or unready bindings in the examined path. The scan's path rule remains the authoring-time `.hkt`
  tail mapped to the corresponding `.hkx` in the merged view; `GameData.Normalize` and
  `ProjectChain.ResolvePath` implement that existence check.

## Headless scan progress + performance (issue #193)

### `src/Archive/GameData.cs` — `ResolveSource` (existence without decompression)

`ContainsAnimation` / `ResolveAnimation` used to route through `ReadAnimation`, which reads and
**decompresses** the target file's bytes just to report where it resolves. In the scan hot loop that
meant every per-clip / per-animation existence check (`ModlistScan.ScanClips.Resolves` →
`Mo2ScanData.ExistsDataRelative` → `GameData.ContainsAnimation`) decompressed the matched animation —
the dominant cost on large load orders. `GameData.ResolveSource` now determines the source
(`"loose"` / archive file name / `null`) using only the already-cached indices (`ModLooseIndex`,
`CaseInsensitivePath`, and `FindEntry` over the cached `Names`/`Entries`) with **no byte read**.
`ReadAnimation` still reads bytes and is used where the caller needs the file contents (reading a
character/behaviour file to parse it).

Policy: `ResolveSource` / `ContainsAnimation` validate **virtual-path resolution only** — whether the
declared animation resolves to a loose file or an archive entry. Whether that entry's payload can
actually be read/decompressed (corruption, unsupported compression) is a **separate validation
concern**, deliberately out of scope here — do not "fix" this back into decompression. The runtime
RE pass did not establish a corresponding engine fault branch for an animation class unsupported by
BGS's decoder; see issue #207 and the rejected hypotheses in #208/#209.

### `src/Archive/GameData.cs` — `TryArchiveEntries` (archive index parsed once per run)

`Mo2ScanData.ModHkxPaths` used to `Ba2.Open()` each mod archive itself (parsing its index/name table),
enumerate, and dispose it — then `GameData` parsed the *same* archive a second time for existence/read
resolution. `GameData.TryArchiveEntries` exposes the shared, cached `Open` (`GameData._opened`) with the
same DX10-texture / unreadable-GNRL handling, and `ModHkxPaths` now consumes entries through it. So each
archive's index is parsed **once per run** and reused by both discovery and resolution — the actual
#193 "parsed once" goal, not just relying on a cache that discovery bypassed. Regression:
`tools/tests/ScanCliTests.cs` (`ArchiveIsParsedOnceAcrossDiscoveryAndResolution`) counts
`Ba2.OpenCountForTest` and asserts a single parse across `ModHkxPaths` + `ExistsDataRelative`.

### `app/ModlistScan.cs` — progress to stderr

- `ModlistScan.Progress` writes to **stderr in every mode** (progress is transient diagnostic; stdout
  stays a single clean JSON document under `--json`, and the report under human mode).
- `ModlistScan.StepDue` throttles per-file progress to ~20 updates regardless of load-order size.
- `Mo2ScanData.ModHkxPaths` takes an optional `onArchive` callback so the archive-indexing phase
  (which opens every enabled mod's `.ba2`) reports progress too.
- Progress strings use ASCII `...`, not `…` (U+2026), to avoid mojibake under the Windows console
  codepage.

## Packfile pointer-layout conversion (issue #188)

### `BGS/src/Hkx/PackfileConverter.cs:PackfileConverter.ConvertTo`

The conversion matrix is deliberately limited to the canonical little-endian layouts reproduced
by `LayoutWalker`:

| Source pointer/layout | Target pointer/layout | Status | Basis or refusal |
| --- | --- | --- | --- |
| 8-byte / `[8,1,0,1]` | 4-byte / `[4,1,0,1]` | Supported | Existing FO4 packfiles and the shipped class table; tested against the checked-in vanilla fixtures. |
| 4-byte / `[4,1,0,1]` | 8-byte / `[8,1,0,1]` | Supported | A 4-byte result produced from the FO4 fixtures is read back and widened successfully. |
| 8-byte / `[8,1,0,1]` | 8-byte / `[8,1,0,1]` | Supported | Header restamp/no-op after signature validation. |
| 4-byte / `[4,1,0,1]` | 4-byte / `[4,1,0,1]` | Supported | Header restamp/no-op for a canonical 4-byte image. |
| Any other pointer width | Any target | Unsupported | `TryConvertTo` reports that only 4-byte and 8-byte pointers are representable. |
| Canonical source | Canonical target with an unsafe section, fixup, signature, value, or placement | Unsupported | The conversion remains fail-closed; the refusal names the first exact condition. |
| Any newer or non-canonical Havok layout | Any target | Unsupported | No real fixture or verified layout rule justifies inferring the conversion. `#115` and `#116` remain out of scope. |

`PackfileImage.Read` rejects non-canonical source rules before conversion. `ConvertTo` is the
tool-facing throwing boundary: it returns `true` for a proven conversion and otherwise throws
`InvalidDataException` naming the source layout, target layout, and refusal. `TryConvertTo` keeps
the boolean fail-closed API and exposes the precise refusal reason for callers that need to handle
it without throwing. Neither API commits a partially converted image; bytes and fixups are
committed only after every safety and placement check succeeds.

### `BGS/tools/tests/PackfileConverterTests.cs:PackfileConverterTests`

Focused tests cover both canonical directions, the source/target wording for an unsupported width,
and the exact section-level refusal. The broader FO4 graph, array, variant, relative-array, fixup,
overflow, and real-file coverage remains in `BGS/tools/symrm/Tests.cs`.
