# RE learning tools + unattended analysis on f4-re-mcp — optimized design

**Date:** 2026-08-13
**Status:** revision 3, reviewed and verified this session — see "Review notes" below. Handed off
to a separate session for implementation planning; the phasing question at the bottom is
unresolved and should be settled with the user before writing an implementation plan.
**Supersedes:** revision 2

## Review notes (verified this session, before handoff)

Three load-bearing claims in this revision were checked against the actual `re-mcp` source and
live search rather than accepted on the document's own confidence:

- **DB split is fixing a real defect in revision 2, not a style preference.** `build-index.ts`'s
  `main()` runs `if (existsSync(DB_PATH)) unlinkSync(DB_PATH)` before recreating the schema from
  scratch. Revision 2 assumed additive tables were safe in the same file because
  `build-index.ts` "doesn't touch them" — false; `unlinkSync` deletes the whole file regardless
  of what's in it. Any future rebuild would have silently destroyed every learned finding.
  Confirmed by direct source read, not inferred.
- **`suggest_hooks` requiring an explicit `repo` argument fixes a real cross-repo contamination
  bug.** Revision 2's version diffed "what's already hooked" across *all* discovered repos
  combined — a function hooked by `BehaviourGraphStudio` would incorrectly stop being suggested
  for Prisma. Confirmed against revision 2's own committed text.
- **The SQLite WAL-reset bug is real, not an invented justification.** Verified via web search:
  a genuine data race between a checkpoint and a concurrent write transaction, present in SQLite
  3.7.0–3.51.2, discovered by Tailscale after a six-month investigation, fixed in 3.51.3. It
  requires WAL mode with multiple open connections on one file to trigger — exactly this design's
  shape. `better-sqlite3` inherits whatever SQLite version it bundles.
  Sources: [Tailscale](https://tailscale.com/blog/sqlite-wal-reset-bug),
  [Antithesis](https://antithesis.com/blog/2026/wal-reset-bug/),
  [The Register](https://www.theregister.com/databases/2026/08/12/deeply-buried-16-year-old-sqlite-bug-caused-last-years-tailscale-outages/5287004),
  [AlternativeTo (3.53 release notes)](https://alternativeto.net/news/2026/4/sqlite-3-53-fixes-wal-reset-bug-adds-qrf-library-new-sql-features-improved-cli-and-more/).

**Open question left for the next session to resolve with the user, not decided here:** this
revision is considerably larger than the approved revision 2 (two SQLite files with migrations,
extraction-identity versioning, fingerprinted repo caching, a persistent fair-scheduled queue
with retry/backoff, a systemd timer+oneshot rewrite of the daemon, a status tool, an expanded
test matrix). Whether to build it as one implementation plan or phase it (e.g. DB split + shared
pipeline + interactive tools, then persistent queue + daemon + systemd, then
observability/hardening) was raised but not answered before handoff.

## Problem

Fallout 4 engine addresses/RVAs are hardcoded across multiple projects in the workspace, not only Prisma. Any repo under `Projects/` that hooks engine internals can accumulate stale, weakly verified, or duplicated reverse-engineering knowledge.

Today, address verification and deeper analysis require manually running Ghidra headless scripts and hand-maintaining exported files. The existing `f4-re-mcp` server exposes read-only tools over the static SQLite index at `re-mcp/re-index.sqlite`, which is rebuilt from exported source data. It has no durable learned-data store, no write path for learned findings, and no unattended analysis worker.

The required behavior is:

1. Learned findings persist independently of destructive static-index rebuilds.
2. A local Ollama model extracts structured findings from scoped Ghidra decompilation output.
3. Analysis runs unattended without requiring a Claude Code session to remain connected.
4. Address consumers are discovered across the workspace rather than maintained in a hardcoded project list.
5. Interactive MCP tools and unattended analysis use the same analysis pipeline and produce auditable results.

Not wanted: a Matrix-style web UI, auth layer, or HTTP API. MCP tools, SQLite, and systemd/journal logs remain the operational surface.

## Design principles

- **Static truth and learned state have different lifecycles.** Never store durable learnings in a database that is intentionally rebuildable.
- **One analysis implementation.** Interactive and unattended callers use the same target resolution, Ghidra, extraction, validation, and persistence code.
- **LLM output is evidence, not authority.** A learned finding may contradict the static index, but never silently overwrites static data.
- **Incremental work over repeated workspace sweeps.** Cache discovered repos and parsed engine references; do not grep every repo every daemon cycle.
- **Persistent scheduling state.** Failures, retries, and pending work survive process restarts.
- **Bounded resource use.** One Ghidra analysis at a time by default, hard per-stage timeouts, capped cycles, retry backoff.
- **Verify the SQLite runtime before enabling WAL concurrency.** The implementation must record/check `sqlite_version()` during startup and reject known-vulnerable WAL versions (SQLite < 3.51.3 — see "Review notes") unless a fixed backport is explicitly documented.

## Architecture

Three logical components, two SQLite files:

1. **`re-index.sqlite` — static/rebuildable**
   - Existing `f4-re-mcp` read-only index.
   - Continues to be owned by `build-index.ts`.
   - Adds `subsystem_index`, because that table is derived from static exported files and can be regenerated safely.
   - Existing six tools keep their current read-only behavior.

2. **`re-learning.sqlite` — durable/writeable**
   - New persistent store owned by the learning subsystem.
   - WAL mode.
   - Contains learned findings, repo discovery state, parsed consumer references, queue state, and daemon-run metadata.
   - Schema is migration-driven and is never deleted by `build-index.ts`.

3. **Processes**
   - `f4-re-mcp`: interactive MCP server. Opens `re-index.sqlite` read-only as today. Opens `re-learning.sqlite` lazily only for learning/audit tools.
   - `f4-re-daemon --once`: standalone worker that performs one bounded unattended cycle and exits.
   - `f4-re-daemon.timer`: systemd user timer triggers the worker approximately every 30 minutes. This replaces an always-sleeping process and makes scheduling/restarts systemd's responsibility.

`loginctl enable-linger <user>` remains an installation-time opt-in if unattended work must continue after logout.

### SQLite runtime gate

Because this design relies on concurrent WAL access, startup/acceptance must verify the SQLite library actually used by the Node binding, not merely the system `sqlite3` CLI. Record `SELECT sqlite_version()` in diagnostics. Do not enable concurrent WAL operation on a runtime affected by the WAL-reset bug (fixed in SQLite 3.51.3 — confirmed real, see "Review notes" above); require a fixed SQLite release/backport before deployment.

## Why the databases are split

`re-index.sqlite` is explicitly rebuildable from static exports. Learned findings are explicitly required to accumulate and preserve history. Keeping both lifecycles in one file creates an avoidable data-loss boundary.

The optimized design therefore treats:

- `re-index.sqlite` as generated state;
- `re-learning.sqlite` as durable state.

Joins across them are performed in application code or through a read-only `ATTACH` where the SQLite binding supports it safely. No learning code writes to `re-index.sqlite`.

## Static schema addition

`subsystem_index` remains generated by `build-index.ts` from `Organized/<version>/<Category>.txt`.

```sql
CREATE TABLE subsystem_index (
  version  TEXT NOT NULL,
  category TEXT NOT NULL,
  rva      INTEGER NOT NULL,
  name     TEXT NOT NULL,
  PRIMARY KEY (version, category, rva)
);

CREATE INDEX idx_subsystem_index_category
  ON subsystem_index(version, category);
```

The primary key prevents duplicate rows across repeated builds.

## Durable learning schema

All timestamps are UTC ISO-8601 values generated by the application through one shared clock helper. Tests inject that clock.

```sql
CREATE TABLE learned_findings (
  id                    INTEGER PRIMARY KEY AUTOINCREMENT,
  version               TEXT NOT NULL,
  rva                   INTEGER NOT NULL,
  proposed_name         TEXT,
  confidence            TEXT NOT NULL CHECK (confidence IN ('high', 'medium', 'low')),
  note                  TEXT,
  source                TEXT NOT NULL DEFAULT 'headless+ollama',
  origin                TEXT NOT NULL CHECK (origin IN ('interactive', 'daemon')),
  raw_decompile_hash    TEXT NOT NULL,
  model                 TEXT NOT NULL,
  prompt_version        TEXT NOT NULL,
  extractor_version     TEXT NOT NULL,
  ghidra_version        TEXT,
  project_fingerprint   TEXT,
  supersedes_id         INTEGER REFERENCES learned_findings(id),
  is_current            INTEGER NOT NULL DEFAULT 1 CHECK (is_current IN (0, 1)),
  created_at            TEXT NOT NULL
);

CREATE UNIQUE INDEX idx_learned_findings_one_current
  ON learned_findings(version, rva)
  WHERE is_current = 1;

CREATE INDEX idx_learned_findings_recent
  ON learned_findings(created_at DESC);

CREATE INDEX idx_learned_findings_target_history
  ON learned_findings(version, rva, created_at DESC);
```

`supersedes_id` points backward to the previous row. This avoids having to write a forward reference to a row that does not exist yet.

A replacement write is:

1. `BEGIN IMMEDIATE`.
2. Re-read the current row for `(version, rva)`.
3. If the exact extraction identity is unchanged, return it and perform no write.
4. Mark the old current row `is_current = 0`.
5. Insert the new current row with `supersedes_id = old.id`.
7. Commit.

Any failure rolls back both changes. The partial unique index enforces one current finding per target.

### Extraction identity

The dedup key is not only the raw decompile hash. Re-extraction should be allowed when the model, prompt, extractor, or project changes materially.

Equivalent extraction identity:

```text
(version, rva, raw_decompile_hash, model, prompt_version,
 extractor_version, project_fingerprint)
```

## Repo discovery and cached consumer references

### `discovered_repos`

```sql
CREATE TABLE discovered_repos (
  path                TEXT PRIMARY KEY,
  repo_fingerprint    TEXT,
  last_discovered_at  TEXT NOT NULL,
  last_scanned_at     TEXT,
  last_scan_error     TEXT
);
```

A repo fingerprint should include at least the current Git `HEAD` plus working-tree state so uncommitted changes are not invisible. A daily forced rediscovery remains the safety net for newly added or removed repos.

### `consumer_references`

Do not re-grep every repo every 30-minute cycle. Persist normalized address consumers after a repo scan.

```sql
CREATE TABLE consumer_references (
  id                INTEGER PRIMARY KEY AUTOINCREMENT,
  repo_path         TEXT NOT NULL,
  file_path         TEXT NOT NULL,
  line_number       INTEGER NOT NULL,
  reference_kind    TEXT NOT NULL,
  raw_value         TEXT NOT NULL,
  resolved_version  TEXT,
  resolved_rva      INTEGER,
  scan_fingerprint  TEXT NOT NULL,
  last_seen_at      TEXT NOT NULL,
  UNIQUE(repo_path, file_path, line_number, reference_kind, raw_value)
);

CREATE INDEX idx_consumer_references_rva
  ON consumer_references(resolved_version, resolved_rva);
```

Supported `reference_kind` values initially include:

- `rel_offset`
- `rel_id`
- `vtable_rva`

Keep the syntactic form instead of pretending every source token is already an RVA. Normalize to `resolved_rva` only when the existing address-resolution logic can do so for the requested runtime version.

### Discovery/scan policy

- Discover git repos under configured workspace roots every 24 hours, on empty cache, or when explicitly forced.
- Scan a repo only when its fingerprint changed, its previous scan failed, or `force_rescan` is requested.
- Replace that repo's cached `consumer_references` transactionally after a successful scan.
- On scan failure, retain the previous successful cache and record the error. Do not erase known references because one scan failed.
- Use argv-based process spawning (`spawn`/`execFile`), never shell-concatenated commands containing paths or user input.

## Persistent unattended queue

Rebuilding a transient priority list every cycle causes starvation and makes retries forget their history. Persist work instead.

```sql
CREATE TABLE analysis_queue (
  id               INTEGER PRIMARY KEY AUTOINCREMENT,
  version          TEXT NOT NULL,
  rva              INTEGER NOT NULL,
  reason           TEXT NOT NULL,
  priority         INTEGER NOT NULL,
  status           TEXT NOT NULL CHECK (status IN ('pending','running','retry','done','suppressed')),
  attempts         INTEGER NOT NULL DEFAULT 0,
  next_attempt_at  TEXT,
  last_error       TEXT,
  first_seen_at    TEXT NOT NULL,
  updated_at       TEXT NOT NULL
);

CREATE UNIQUE INDEX idx_analysis_queue_active_target
  ON analysis_queue(version, rva)
  WHERE status IN ('pending', 'running', 'retry');

CREATE INDEX idx_analysis_queue_ready
  ON analysis_queue(status, next_attempt_at, priority, first_seen_at);
```

### Queue sources

New candidates are upserted from:

1. unresolved `name_disagreements` without a current learned finding;
2. cached hardcoded consumer references without a current learned finding;
3. confirmed `subsystem_index` functions without a current learned finding.

The daemon has an explicit configured version set, initially only the supported/default `1.10.163` OG project unless additional project mappings are configured. It does not silently attempt every indexed runtime version.

### Fair scheduling

Avoid permanent starvation of subsystem work. For an eight-item default cycle, use a weighted target such as:

- 3 disagreement items;
- 3 hardcoded-reference items;
- 2 subsystem-gap items.

Unused slots spill into the other classes. Age increases effective priority so an old low-priority candidate eventually runs.

### Retry policy

Classify failures:

- **project locked / temporarily unavailable**: retry next cycle without counting as a hard failure;
- **Ollama unavailable / timeout**: exponential backoff;
- **Ghidra transient failure / timeout**: exponential backoff;
- **target cannot resolve**: bounded retries, then `suppressed` with error;
- **invalid LLM schema repeatedly**: bounded retries, then `suppressed`.

A suppressed item remains inspectable and can be manually requeued.

## Shared analysis pipeline

Implement one internal function used by both MCP and daemon callers:

```text
analyzeTarget({ target, version, origin, force })
```

### Pipeline

1. Validate/normalize version and target.
2. Resolve target to `(version, RVA, Ghidra project/program)`.
3. Check for an existing current finding and determine whether analysis can be skipped.
4. Perform an advisory project-lock preflight if a reliable lock signal is available.
5. Invoke `analyzeHeadless` scoped to the single target, using read-only/decompile-only behavior where supported.
6. Treat Ghidra's own inability to acquire/open the project as authoritative. A preflight file check is never the only lock guarantee because it has a race window.
8. Enforce a hard Ghidra timeout and kill the child process tree on expiry.
9. Capture only the bounded signature/body payload needed for extraction.
10. Hash normalized raw output.
11. If extraction identity is unchanged, return the current finding without calling Ollama.
12. Send the bounded decompile plus existing static/learned context to Ollama.
13. Treat decompiler text as untrusted model input/data, not instructions.
13. Require strict structured output for `{ proposed_name, confidence, note }`; reject extra/invalid shapes.
14. Persist using the atomic supersession transaction.
15. Return the finding plus static-index comparison metadata.

### Resource limits

Defaults should be configurable, with conservative initial values:

- one active Ghidra analysis per worker;
- hard timeout per Ghidra invocation;
- hard timeout per Ollama request;
- maximum items per cycle;
- maximum total cycle duration;
- maximum decompile bytes sent to the model;
- maximum model response bytes.

The daemon exits cleanly when its cycle budget is exhausted; pending work remains queued.

## MCP tools

### `learn_from_target(target, version?, force?)`

Runs the shared analysis pipeline immediately with `origin = 'interactive'`.

Default version remains `1.10.163` OG unless configured otherwise.

Returns:

- resolved target/version/RVA;
- whether analysis was run or deduplicated;
- learned finding;
- static-index confidence/name context;
- contradiction flag when learned and static evidence disagree.

### `audit_hardcoded_addresses(paths?, force_rescan?, version?)`

With no paths:

- ensure repo discovery cache exists;
- refresh stale discovery if necessary;
- incrementally refresh changed repo scans;
- audit cached `consumer_references` rather than grepping the entire workspace on every call.

With explicit paths, validate each path and scan/audit only that scope.

For every resolved reference return:

- repo-relative `file:line`;
- source reference kind/value;
- resolved version/RVA where possible;
- static-index verdict;
- current learned finding if present;
- contradiction state.

Unresolved `REL::ID`/pattern references are reported as unresolved rather than coerced into an RVA.

### `suggest_hooks(subsystem, repo, version?)`

`repo` is required because hook coverage is project-specific.

1. Resolve the requested repo from discovered repos or an explicit validated path.
2. Load the subsystem candidates for `version` from `subsystem_index`.
3. Compare only that repo's cached engine references against the subsystem candidates.
4. Return missing candidates with static confidence/name metadata and any current learned finding.

A separate explicit `scope = 'workspace'` option may be added later if a workspace-global view is useful, but global diffing is not the default.

### `recent_learnings(limit?, since?, origin?, current_only?)`

Defaults:

- `limit = 50`;
- `current_only = true`.

Supports `origin = 'daemon' | 'interactive' | 'all'` so a session can specifically ask what unattended analysis found.

Returns creation time, origin, target, finding, supersession metadata, and contradiction state.

If the user wants *activity* rather than current truth, `current_only = false` includes rows that were later superseded.

### `daemon_status()`

Add a small read-only operational tool rather than forcing users to inspect systemd first.

Returns:

- last daemon run start/end/result;
- queue counts by status/reason;
- oldest pending item age;
- last repo discovery/scan time;
- recent suppressed/error count;
- Ollama/Ghidra availability from the most recent cycle, not a live expensive probe by default.

`journalctl` remains the detailed log surface.

## Daemon execution

Use a systemd user **timer + oneshot service** rather than a permanent process that wakes and sleeps.

Example shape:

```ini
# ~/.config/systemd/user/f4-re-daemon.service
[Unit]
Description=Fallout 4 RE learning cycle

[Service]
Type=oneshot
WorkingDirectory=/path/to/re-mcp
ExecStart=/usr/bin/node /path/to/re-mcp/dist/f4-re-daemon.js --once
```

```ini
# ~/.config/systemd/user/f4-re-daemon.timer
[Unit]
Description=Run Fallout 4 RE learning cycle periodically

[Timer]
OnBootSec=5m
OnUnitActiveSec=30m
Persistent=true
RandomizedDelaySec=2m

[Install]
WantedBy=timers.target
```

The exact Node path/install path should be generated from the real environment rather than hardcoded in source control.

### Cycle

One invocation performs:

1. Open/migrate `re-learning.sqlite`.
2. Refresh repo discovery if stale or missing.
3. Incrementally rescan changed consumer repos.
4. Upsert queue candidates from disagreements, consumer references, and subsystem gaps.
5. Claim a fair bounded batch transactionally.
6. Analyze items serially by default through the shared pipeline.
7. Mark each item done/retry/suppressed independently.
8. Record run metrics/state.
9. Exit.

A failure on one target never aborts unrelated queued items unless the global dependency itself is down and continuing would only generate identical failures. For example, if Ollama is confirmed unavailable, end the cycle early after recording a global dependency failure instead of retrying it once per queued item.

## Operational state

```sql
CREATE TABLE daemon_runs (
  id              INTEGER PRIMARY KEY AUTOINCREMENT,
  started_at      TEXT NOT NULL,
  finished_at     TEXT,
  result          TEXT,
  items_attempted INTEGER NOT NULL DEFAULT 0,
  items_succeeded INTEGER NOT NULL DEFAULT 0,
  items_retried   INTEGER NOT NULL DEFAULT 0,
  items_suppressed INTEGER NOT NULL DEFAULT 0,
  error_summary   TEXT
);
```

This gives `daemon_status()` stable data without parsing journal text.

## Static vs learned precedence

Define this explicitly so tools do not drift:

- Static index data remains the canonical published/indexed RE dataset.
- Learned findings are current machine-generated evidence.
- A learned finding never silently edits or promotes a static symbol/name/confidence entry.
- If a learned finding contradicts a high-confidence static entry, tools flag the conflict prominently.
- Promotion from learned evidence into the static exported source remains a separate explicit/manual workflow unless intentionally designed later.

## Error handling

- Project appears locked: skip/refuse as appropriate, but treat the actual Ghidra open/lock result as authoritative.
- `analyzeHeadless` no-match/error: no finding write; classify for retry/suppression.
- Ghidra timeout: terminate child process tree; no finding write.
- Ollama unreachable/timeout: no finding write; retry with backoff. If globally unavailable, stop wasting the rest of the cycle.
- Invalid model output: no finding write; preserve validation error for diagnostics.
- SQLite busy: both writer connections use a bounded busy timeout; write transactions use `BEGIN IMMEDIATE` where a write is known to be required.
- SQLite runtime version fails the WAL safety gate: refuse learning/daemon writes with a clear diagnostic; legacy static read-only MCP tools remain available.
- Repo discovery/scan failure: retain last known-good cached references and record the failure.
- Static index unavailable: learning DB remains intact; tools that require static context fail clearly rather than mutating learned data.
- Learning DB unavailable/corrupt: existing six read-only MCP tools continue to work because their static connection is independent.

## Security and isolation

The system is local and trusted, but still avoid unnecessary command-execution hazards:

- never interpolate target/path/model strings into shell command text;
- use argv-based child-process APIs;
- validate numeric address inputs and supported versions;
- canonicalize explicit repo paths before scanning;
- keep workspace roots configurable and explicit;
- bound stdout/stderr capture from child processes;
- bound decompile/model payload sizes;
- treat decompiled strings/comments/symbol text as untrusted data for the LLM;
- do not let model output select commands, paths, Ghidra scripts, or SQL.

## Testing

Add a minimal test harness around fixture SQLite files, fixture repos, and mocked process/model adapters.

### Database/invariant tests

- static rebuild never touches `re-learning.sqlite`;
- schema migrations are idempotent;
- startup detects and rejects a SQLite runtime that fails the WAL safety version gate;
- exactly one current learned finding per `(version, rva)`;
- supersession history is preserved;
- failed replacement transaction leaves previous row current;
- extraction identity deduplicates exact repeats but allows re-extraction after model/prompt/project changes.

### Discovery/reference tests

- multiple consumer repos are discovered;
- unchanged repo fingerprint skips re-scan;
- working-tree change invalidates the repo scan cache;
- failed scan retains last known-good references;
- removed references disappear only after a successful replacement scan;
- `REL::Offset`, `REL::ID`, and vtable patterns remain distinguishable.

### Tool tests

- `audit_hardcoded_addresses` returns static/learned conflicts correctly;
- `suggest_hooks` is repo-specific and is not contaminated by hooks in another repo;
- `recent_learnings(origin='daemon')` excludes interactive findings;
- `current_only=false` exposes superseded history;
- stale/empty discovery cache refreshes on-demand.

### Queue/daemon tests

- candidate sources upsert without duplicate active work;
- weighted scheduling prevents subsystem starvation;
- batch and cycle limits are enforced;
- lock skip is retried without destructive side effects;
- transient failure backoff works;
- repeated permanent failure becomes suppressed;
- one failed target does not abort unrelated items;
- global Ollama outage short-circuits the remainder of the cycle;
- interrupted `running` work is safely reclaimable on the next cycle.

### Process-boundary tests

- Ghidra and Ollama timeouts kill/cancel correctly;
- malformed/oversized model output never writes;
- command arguments are passed without shell interpolation;
- read-only legacy MCP tools still work when `re-learning.sqlite` is unavailable.

Real Ghidra and live Ollama calls remain integration/manual acceptance tests rather than CI requirements.

## Acceptance criteria

The design is complete when all of the following are true:

1. Rebuilding `re-index.sqlite` cannot delete learned history.
2. A Claude Code session can call the four core learning/audit tools without changing the behavior of the six existing tools.
3. `suggest_hooks` reports missing hooks for one specified repo, not merely workspace-global usage.
4. The unattended system continues across client disconnects and, with linger enabled, user logout.
5. The unattended worker does not repeatedly rescan unchanged repos or retry the same failed item every 30 minutes without backoff.
6. A Ghidra/Ollama/DB failure cannot produce a partial supersession write.
7. The user can distinguish daemon-generated findings from interactive findings.
8. Queue state and recent daemon health are inspectable without reading raw journal logs.
9. Static and learned disagreements are surfaced but never silently reconciled.
10. All child-process and model inputs are bounded and validated.

## Alternatives considered

### Same SQLite file for static index and learned state

Rejected. Static index rebuild semantics and durable learning semantics conflict. The split databases make the ownership boundary explicit and eliminate the largest data-loss risk in revision 2.

### Permanent sleeping daemon process

Rejected in favor of a systemd timer + oneshot worker. A timer provides the same unattended behavior with simpler crash handling, no custom sleep loop, clearer per-run observability, and no duplicate overlapping cycle when the service is already active.

### Re-grep every repo every daemon cycle

Rejected. Persist parsed consumer references and invalidate them only when a repo fingerprint changes or a scan is forced.

### Transient queue rebuilt every cycle

Rejected. Persistent queue state is necessary for retry backoff, fairness, restart recovery, and useful status reporting.

### Workspace-global `suggest_hooks`

Rejected as the default because hook coverage is project-specific. A hook present in Prisma must not make it disappear from suggestions for BehaviourGraphStudio.

### Learned finding automatically edits static index

Rejected. Machine-extracted evidence remains separate until an explicit promotion workflow exists.

## Implementation order

1. Verify the Node SQLite binding/runtime version and establish the WAL safety gate.
2. Split durable state into `re-learning.sqlite`; add schema migrations and invariants.
3. Add `subsystem_index` uniqueness to the static build.
4. Extract shared target-resolution/Ghidra/Ollama/persistence pipeline.
5. Add repo discovery plus cached `consumer_references`.
6. Implement `learn_from_target` and `audit_hardcoded_addresses` on the shared layer.
7. Fix `suggest_hooks` to require repo scope.
8. Add `recent_learnings` with origin/history filtering.
9. Add persistent `analysis_queue` and `daemon_runs`.
10. Implement `f4-re-daemon --once`.
11. Add systemd user service/timer and optional linger setup instructions.
12. Add `daemon_status`.
13. Run mocked test suite, then manual Ghidra/Ollama acceptance against a disposable target before enabling the timer.

## Revision history

- **Revision 1:** on-demand-only design, Prisma-scoped audit, no unattended worker.
- **Revision 2:** generalized consumer discovery, added background daemon, queue concept, and recent learnings.
- **Revision 3 (this optimization):** split rebuildable and durable databases; added DB invariants, extraction provenance, cached consumer references, persistent/fair queueing, retry/backoff, project-scoped hook suggestions, daemon-vs-interactive provenance, systemd timer/oneshot execution, bounded resource controls, explicit static-vs-learned precedence, and operational status data. Reviewed and independently verified (DB-split rationale, suggest_hooks contamination fix, WAL-reset bug claim) before handoff to a separate implementation-planning session.
