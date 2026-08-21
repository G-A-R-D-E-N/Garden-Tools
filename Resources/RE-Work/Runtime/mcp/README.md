# f4-re-mcp

Local MCP server exposing verified Fallout 4 engine reverse-engineering facts from a pre-built
SQLite index of everything in `..\` (Address Library `.bin`s, the OG symbol dump, the OG/AE
cross-mapping, and the audit-trail CSVs of confirmed/reverted name corrections and vtable
verdicts). Entirely local - no network access, no data leaves this machine.

The point is to stop guessing symbol names, offsets, and OG/AE addresses from memory - the exact
failure mode that's caused real bugs before (a wrong RTTI name trusted, a hook landing in
`.rdata` instead of `.text`, OG vs AE address-space mixups).

## Setup

```
npm install
npm run build-index
```

`build-index` parses the source data into `re-index.sqlite` (gitignored, ~1.2GB, takes a few
minutes now that it also indexes the 1.10.155 PDB dump and the 24 `..\Organized` CSVs). Re-run it
whenever the source data under `..\Bins`, `..\Organized`, `..\Symbols`, or
`..\IDA_Functions_OG_AE.csv` changes.

Then build and add it as an MCP server (already done in this workspace's `E:\F4SE OG\.mcp.json`
as `f4-re-mcp`):

```
npm run build
```

## Tools

- **`lookup_by_address`** - resolve a hex address (VA or RVA, with/without `0x`) to its symbol
  name, signature, and Address Library id, for a given runtime version (default `1.10.163`).
- **`lookup_by_name`** - find symbols by name (substring match).
- **`check_name_confidence`** - the main "can I trust this name" tool. Cross-references confirmed
  corrections, reverted corrections, and unresolved disagreements, and returns one clear verdict.
- **`get_vtable_slots`** - CommonLibF4's expected vtable layout for a class, joined with the live
  Ghidra audit verdict per slot where available.
- **`map_og_ae`** - map a function between Old-Gen and AE address spaces, by name or address.
- **`search_findings`** - keyword search over the RE findings docs (struct offsets, bug
  write-ups) that only exist as prose, not structured data.
- **`havok_class`** / **`search_havok_source`** - read a Havok 2014.1.1 SDK class declaration, or
  keyword-search that source. Reference only; Havok's wording never goes into a repository.
- **`search_organized`** - substring or address search over all 24 `..\Organized\*.csv` audit files
  (52,488 rows), each result citing its `source:row_no`. See "CSV corpus and PDB evidence".
- **`pdb_signature`** - name lookup in the 1.10.155 Microsoft PDB dump; returns the real signature
  and calling convention. Name-only - never an address source. See "CSV corpus and PDB evidence".
- **`audit_csv_corpus`** - re-reads all 24 CSVs from disk and cross-checks their claims against the
  live index (claim drift, unresolved addresses, plan-vs-applied pairs, PDB corroboration).
- **`export_learnings`** - exports current learned findings to `..\Organized\generated\learned_findings.csv`.

## Learning tools

These four tools *write*. They decompile a single function through Ghidra headless and extract a
proposed name with a local Ollama model, storing the result durably.

- **`learn_from_target`** - analyze one target (symbol name or hex address) and store a learned
  finding. `learn_from_target({ target: "0x749ea0" })` takes about 7 seconds end to end. Calling it
  again on unchanged code is deduplicated and skips the model entirely.
- **`audit_hardcoded_addresses`** - resolve every `REL::Offset` / `REL::ID` / `REL::VariantID` in
  the workspace's consumer repos, **once per runtime slot**, each against its own Address Library.
  Read `groups` first; it is the report. `references` is capped detail. See "Runtime is a
  dimension" below.
- **`suggest_hooks`** - for one subsystem *and one repo*, the engine functions that repo does not
  yet reference. `repo` is required: a function hooked in Prisma must not vanish from
  BehaviourGraphStudio's suggestions.
- **`recent_learnings`** - what has been learned lately. `origin: "daemon"` isolates unattended
  findings; `current_only: false` shows superseded history.

### Runtime is a dimension, not a setting

A `REL::ID` does not name one address. CommonLibF4 stores `COMMONLIB_RUNTIMECOUNT` (3) ids per
literal, indexed by `REX::FModule::Runtime` — `kOG`, `kNG`, `kAE` — and looks up whichever slot
matches the running game. `REL::ID(x)` puts the same `x` in all three; `REL::VariantID{ og, ae }`
fills them properly, padding the tail with the last value. `0` means "absent on this runtime".

The audit resolves every slot against its own build: `kOG` 1.10.163, `kNG` 1.10.984, `kAE`
1.11.221. Names still come from 1.10.163 — it is the only build with curated names — so an AE
address is named by bridging through `og_ae_map`, never by reading the OG symbol table at the same
numeric address.

**Why a single default version was worse than wrong.** The OG Address Library is contiguous over
ids 0..1,583,368. Every id in that range resolves against it, whether or not it means anything
there. So an AE-authored id did not fail — it returned a confident, unrelated name. The AE library
covers only 5.6% of that same range, which is what makes provenance decidable:

| file's low ids appear in the AE library | reading |
|---|---|
| ~5.6% (the coincidence rate) | authored against OG |
| ~100% | authored against AE |

`file_provenance` records that measurement per file. When a file's ids sit entirely above the OG
maximum the density test has nothing to measure, and resolution asymmetry stands in for it.

Verdicts: `single_id_all_runtimes` is the one that used to pass silently — one literal handed to
every runtime when the file's own id population says it was written for one. `slot_unresolved`,
`slot_name_mismatch`, `unbridged` (kAE resolved but `og_ae_map` cannot name it),
`declared_unavailable`, `unresolvable_symbol`, `stale_scan`, `ok`.

### Two databases, on purpose

| File | Lifecycle | Owner |
|---|---|---|
| `re-index.sqlite` | **Rebuildable.** `build-index` deletes and recreates it. | `build-index.ts` |
| `re-learning.sqlite` | **Durable.** Migration-driven, never deleted. | the learning subsystem |

They are separate files because `build-index.ts` starts with `unlinkSync(DB_PATH)`. Learned
findings stored in that file would be destroyed by any future rebuild. No learning code writes to
`re-index.sqlite`, and the read-only tools keep working when `re-learning.sqlite` is missing.

A learned finding is **evidence, never authority**. It never edits a static symbol or name. When it
disagrees with an audited static name, the tools return a `contradiction` and a loud `warning`, and
the audited name wins.

### Prerequisites

- Ghidra 12.1 at `../Ghidra/ghidra_12.1_PUBLIC` with Java 21.
- Ollama serving `qwen2.5-coder:7b` on `localhost:11434`.
- SQLite **3.51.3 or newer** in the `better-sqlite3` binding. Older releases carry the WAL-reset
  data race, and startup refuses learning writes on them. `better-sqlite3` 13.0.1 bundles 3.53.3,
  so this passes today.

### Configuration

All optional; sensible defaults come from `src/config.ts`.

| Variable | Default |
|---|---|
| `RE_MCP_LEARNING_DB_PATH` | `./re-learning.sqlite` |
| `RE_MCP_OLLAMA_URL` | `http://localhost:11434` |
| `RE_MCP_OLLAMA_MODEL` | `qwen2.5-coder:7b` |
| `RE_MCP_WORKSPACE_ROOTS` | the workspace root two levels above `..` (colon-separated) |
| `RE_MCP_GHIDRA_TIMEOUT_MS` | `360000` |
| `RE_MCP_OLLAMA_TIMEOUT_MS` | `120000` |
| `RE_MCP_MAX_DECOMPILE_BYTES` | `64000` |
| `RE_MCP_MAX_MODEL_RESPONSE_BYTES` | `16000` |
| `RE_MCP_DISCOVERY_MAX_DEPTH` | `4` |
| `RE_MCP_DISCOVERY_TTL_MS` | `86400000` (24 h) |

### Known limits

- **`RE::VTABLE::Class[n]` never resolves to an RVA.** It is a CommonLibF4 header symbol, not an
  address this index can map. Those references are reported unresolved with the class and index
  intact rather than coerced into a number.
- **Discovery finds git repos only.** `Prisma/PirsmaFix/` has real `REL::Offset` references but no
  `.git`, so it is invisible to the no-argument audit. Pass it via `paths` explicitly.
- **Vendored dependencies count as that repo's references.** `HouseRules` vendors CommonLibF4 under
  `lib/commonlibf4`, which is why it reports ~12,000 references almost identical to CommonLibF4's
  own. They are genuinely in the repo; whether they are interesting is a judgement call.
- **Ghidra rejects `&` in paths.** This workspace lives under `Tools & Reverse Engineering`, so the
  adapter routes project and script paths through a stable symlink in the temp dir. Nothing to
  configure, but that is why a symlink appears there.
- **`npm install` needs `--no-bin-links` on this NTFS volume**, which has no symlink support. The
  `test` scripts therefore invoke `vitest.mjs` by path rather than through `npx`.

### Tests

```
npm test
```

282 tests, no Ghidra or Ollama required — both are injected adapters and are mocked. Real-runtime
checks are manual; see `docs/superpowers/plans/`.

## CSV corpus and PDB evidence

Four tools added in the 2026-08-13 phase make the human audit-trail CSVs and the 1.10.155 PDB
symbol dump first-class queryable data. They read the same `re-index.sqlite`; only
`audit_csv_corpus` and `export_learnings` write, and only into `..\Organized\generated\` (never
over the 24 source CSVs). Real numbers from the 2026-08-14 whole-corpus verification run:

- **`search_organized`** - substring or address search over all 24 `..\Organized\*.csv` files
  (52,488 rows). Every result cites `source:row_no` so a claim traces back to its file.
  `search_organized({ query: "NavMesh::CreateGroupData" })` returns the four corpus rows at
  `0x140749ea0` (the applied correction and its plan/crossref records).
- **`pdb_signature`** - exact/substring name lookup in the 1.10.155 Microsoft PDB dump: 401,849
  names (97.6% of the 411,713-line dump; the rest are pathological compiler thunks and are
  honestly skipped, never guessed). Returns the real signature and calling convention, e.g.
  `NavMesh::CreateGroupData` -> `virtual void __cdecl NavMesh::CreateGroupData(FORM*, FORM_GROUP*)`,
  which independently corroborates the applied correction.
- **`audit_csv_corpus`** - re-reads all 24 CSVs from disk and cross-checks their claims against the
  live index. The 2026-08-14 whole-corpus run found: **32 `claim_drift` findings, all of them
  explained** (19 of `applied_hknpShape`'s 26 rows and 13 of `applied_misname_corrections`'s 150
  claim names the index no longer shows, but every one is a **documented reversion** - the live
  name equals the `revert_to_name` recorded in `hknpShape_rename.csv` / `misnames_to_revert_simple.csv`,
  so the audit reports them as info with the citation rather than as errors; the hknpShape names
  are also independently confirmed by the 1.10.155 PDB). 0 unexplained errors. **24,578 `unresolved_address`**
  warnings (nearly all from the crossref file, which lists data addresses too, while `symbols`
  holds only functions), **359 structural** warnings (legitimate repeated addresses in the vtable
  files), **1,079 `pdb_uncorroborated` + reversion infos**, and **0 `pair_disagreement`**. With `write: true`
  it writes `csv_audit_report.csv/.md` and `pdb_crosscheck_163_vs_155.csv` (150,442 corroborated
  names) under `..\Organized\generated\`. The response caps at 500 findings; the written report
  is never truncated.
- **`export_learnings`** - exports **current** learned findings from `re-learning.sqlite` to
  `..\Organized\generated\learned_findings.csv` in the same address,name shape as the human
  audit-trail CSVs, with the static name and any contradiction attached. Machine evidence made
  reviewable next to human evidence - it never promotes a finding into the index. The current
  export is the one Phase 1 live finding: `NavMesh::UpdateGroupData` vs the audited
  `NavMesh::CreateGroupData`.

### `pdb_symbols` isolation rule

The PDB dump's addresses belong to the 1.10.155 build, a *different game version* - using one as
an OG (1.10.163) hook offset would be the worst kind of wrong. So `pdb_symbols` is name-keyed
evidence only: its `version` is hardcoded `1.10.155`, there is deliberately **no index on
`pdb_rva`**, and no tool ever returns an address from it. The `pdb_rva`/`pdbRva` token appears
nowhere outside `build-index.ts` and the parser's data plumbing - never in a tool module (kept
grep-able so a future session can re-verify in one command).

### `check_name_confidence` and PDB corroboration

`check_name_confidence` gained a `pdbCorroboration` field: `confirmed` (the name at that address
or the queried name exists in the independent 1.10.155 PDB, with up to 5 matching names in
`pdbMatches`), `absent` (indexed, but no PDB match), or `unavailable` (no PDB table).
`check_name_confidence({ name: "NavMesh::CreateGroupData" })` returns
`verdict: "confirmed_wrong", pdbCorroboration: "confirmed"` - the applied correction is
independently backed by Microsoft's own symbols. When `pdb_symbols` is missing the field degrades
to `unavailable` and every other verdict stays unchanged.

### Why claim drift and PDB checks are scoped, not run against all 24 files

`claim_drift` runs only against `CLAIM_DRIFT_SOURCES` (the 6 files that record what was actually
applied) and `pdb_uncorroborated` only against `PDB_UNCORROBORATED_SOURCES` (8 files). A *plan*
CSV that differs from what was applied is not an error - plans are superseded by execution, and
some (e.g. `safe_gaps_apply`) carry full signature text in their name column while the applied
records hold bare names, which would manufacture thousands of fake "disagreements". The pair
check therefore skips name comparisons when either side is signature text (missing-side findings
still fire); this was the difference between 2,800 manufactured pair findings and the real 0.

### `Organized/generated/` is not gitignored - and that is fine

The `..\Organized\generated\` output directory is **not** covered by any `.gitignore`, because
`ReverseEngineering` itself is not a git repository (the only repo here is `re-mcp` itself). The
original plan assumed a workspace-root gitignore would cover it; that assumption was wrong and is
corrected here so a future session does not go looking for an entry that was never added. The
`re-mcp/.gitignore` ignores `*.sqlite`, which is what keeps `re-index.sqlite` and
`re-learning.sqlite` out of git.

## What this doesn't cover

The Ghidra `.rep` project databases (`..\Projects\`) aren't indexed - they're a proprietary
binary format only queryable by launching Ghidra headless, at roughly 1-2 minutes per query. Too
slow for interactive tool use. Everything already extracted from them into a CSV/text form is
indexed here; anything not yet extracted needs a real Ghidra session.

The 1.10.155 PDB symbol dump (`Symbols\Fallout4_functions.txt`) is indexed, but **name-only**:
`pdb_symbols` holds 401,849 names/signatures for corroboration (`pdb_signature`,
`check_name_confidence`'s `pdbCorroboration`), and its addresses are deliberately never queryable
because they belong to a different game build. Its earlier cross-check value lives on in
`name_disagreements.verdict_155` from the pre-computed crossref CSV.

## Installing as a connector (.mcpb)

`f4-re-mcp.mcpb` is a prebuilt drag-and-drop bundle for Claude Desktop/Code - drop it in like any
other connector. It does **not** contain the ~1GB `re-index.sqlite` (that stays local and
gitignored); on install it'll prompt for the "Path to re-index.sqlite" setting, which defaults to
this project's own `re-index.sqlite`. Build the index first (`npm run build-index`) if you haven't.

To rebuild the bundle after a code change:

```
npm run build
npm install --omit=dev
npm run pack
npm install
```

(`npm install --omit=dev` strips `typescript`/`@types/*` before packing so the bundle doesn't ship
dev tooling; the final `npm install` restores them for local development.)

## Development

```
npm run build
npx @modelcontextprotocol/inspector node dist/index.js
```
