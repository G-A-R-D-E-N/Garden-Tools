# Organized corpus indexing, CSV audit, and PDB evidence — design

**Date:** 2026-08-13
**Status:** approved in brainstorming, ready for implementation planning
**Builds on:** `2026-08-13-re-learning-tools-design.md` (revision 3) and its Phase 1 implementation,
which is complete and on `feat/learning-tools-and-daemon`.

## Problem

Three gaps, all in the same area — the `Organized/` corpus and the 1.10.155 PDB are underused.

1. **19 of 24 `Organized/*.csv` files are not indexed.** `build-index.ts` ingests only
   `applied_misname_corrections`, `commonlib_vtable_slots`, `crossref_163_disagreements_vs_155`,
   `misnames_to_revert`, and `vtable_audit_report`. The other ~10,100 rows of audit trail —
   `confirmed_misnames_to_apply` (150), `crossref_review_candidates` (313),
   `applied_safe_gaps` (2,178), `vtable_audit_fixes` (364), and 15 more — are invisible to every
   tool.

2. **Nothing validates the CSV corpus.** These files record claims ("address X was renamed
   old→new"). Nothing checks the claims are still true, that plan files agree with their
   corresponding record files, or that addresses still resolve. The project has already been bitten
   by this once: 13 corrections were applied and later reverted after a cross-check.

3. **The 1.10.155 private Microsoft PDB is excluded.** `paths.ts` indexes only the 1.10.163 dump,
   commenting that the 155 dump's "name cross-check value is already in `verdict_155`". That covers
   the 40,073 crossref rows only. The dump holds **411,713 functions** with full Microsoft
   signatures — calling convention, real parameter names, FPO flags — an independent ground truth
   for names that neither CommonLibF4 RTTI nor the community labels provide.

## Verified facts

Measured this session, not assumed.

| Fact | Value | Method |
|---|---|---|
| PDB dump line grammar | **100%** of 411,713 lines parse | streaming regex over the real file |
| Name extraction from PDB signatures | **~85%** | improved extractor; misses are template/lambda internals |
| Distinct names in PDB | ~215,000–247,000 | set of extracted names |
| Distinct 1.10.163 symbol names | 347,519 (13,725 placeholder-ish) | live `re-index.sqlite` |
| **1.10.163 names with an exact match in the PDB** | **133,636 (38.5%)** | set intersection, measured with a degraded 72.8% extractor, so this is a floor |
| Total rows across all 24 `Organized/*.csv` | ~13,000 | `wc -l` per file |

The PDB dump grammar differs from the 163 dump and is genuinely harder:

```
  func [0x00020900+ 0 - 0x00020908- 1 | sizeof=  8] (FPO) hknpBodyId& __cdecl hknpBodyId::operator=(hknpBodyId& __that)
```

Address range with pad offsets, `sizeof`, optional `(FPO)`, then a full C++ signature. The
qualified name must be extracted from the signature, which requires handling MSVC special forms
(`` `scalar deleting destructor' ``, `` `dynamic initializer for 'X'' ``), `operator` overloads, and
nested templates. The ~15% that resist extraction are STL/template internals that `CLAUDE.md`
already documents as un-nameable.

## Design principles

- **The 155 PDB is name-keyed evidence, never an address source.** Its RVAs are wrong for the
  1.10.163 hook target. Confusing the two is what `CLAUDE.md` calls the single most likely mistake
  in this folder, so the schema must make it structurally impossible rather than merely discouraged.
- **Never overwrite what `organize_functions.py` owns.** All generated output goes to a new
  subfolder.
- **The audit reads from disk, so it is never stale.** ~13,000 rows is cheap.
- **Writes are explicit.** No tool writes a file as a side effect of being read.
- **A learned finding is still evidence, not authority.** Unchanged from the Phase 1 design.

## 1. Indexing the full `Organized/` CSV corpus

Nineteen new tables would be schema explosion for data whose main use is being audited and
cross-referenced. Most of the files share a handful of shapes:

| Shape | Files |
|---|---|
| `address,old_name,new_name,kind,note` | `applied_hknpShape`, `applied_misname_corrections`, `applied_reassignments`, `applied_safe_gaps`, `applied_std_func` |
| `address,existing_name,decoded_name` | `crossref_review_candidates`, `crossref_safe_naming_gaps`, `hknpShape_authoritative`, `reassign_apply`, `safe_gaps_apply`, `safe_gaps_class_reassigned_deferred`, `std_func_sweep` |
| `address,revert_to_name` | `hknpShape_rename`, `inconclusive_resolved_rename`, `misnames_to_revert_simple` |
| `address,name` | `all_inconclusive_resolved`, `final_2_held` |
| specialty | `commonlib_vtable_slots`, `vtable_audit_report`, `vtable_audit_fixes`, `confirmed_misnames_to_apply`, `decompile_targets`, `misnames_to_revert`, `crossref_163_disagreements_vs_155` |

So: **one generic table** capturing the common columns, with the original row preserved verbatim.

```sql
CREATE TABLE organized_rows (
  source     TEXT NOT NULL,   -- CSV basename without extension
  row_no     INTEGER NOT NULL,-- 1-based data row, for citing a location
  address    TEXT,            -- original address text as written
  rva        INTEGER,         -- normalized, NULL when unparseable or absent
  name_a     TEXT,            -- old_name / existing_name / wrongly_renamed_to / class
  name_b     TEXT,            -- new_name / decoded_name / revert_to / claimed_real_name
  kind       TEXT,            -- kind / method / verdict, where present
  note       TEXT,            -- note / evidence, where present
  extra      TEXT,            -- JSON of every column not mapped above; nothing is lost
  PRIMARY KEY (source, row_no)
);

CREATE INDEX idx_organized_rows_rva    ON organized_rows(rva);
CREATE INDEX idx_organized_rows_source ON organized_rows(source);
CREATE INDEX idx_organized_rows_name_a ON organized_rows(name_a);
CREATE INDEX idx_organized_rows_name_b ON organized_rows(name_b);

CREATE TABLE organized_sources (
  source      TEXT PRIMARY KEY,
  columns     TEXT NOT NULL,  -- JSON array of the real header, so the mapping stays auditable
  row_count   INTEGER NOT NULL,
  shape       TEXT NOT NULL   -- which mapping rule was applied
);
```

`name_a`/`name_b` are deliberately generic. A per-file column mapping table (`organized_sources`)
records the real header so the normalization is never lossy or mysterious.

The five already-indexed CSVs keep their existing dedicated tables — those have consumers
(`check_name_confidence`, `get_vtable_slots`) and changing them is out of scope. They are *also*
loaded into `organized_rows` so corpus-wide queries see everything. Duplication is acceptable: the
static index is rebuildable and this costs a few MB.

New tool: **`search_organized(query, source?, limit?)`** — find rows by name substring or address
across the whole corpus, returning `source:row_no` citations.

## 2. PDB as independent evidence

```sql
CREATE TABLE pdb_symbols (
  name                TEXT NOT NULL,
  signature           TEXT NOT NULL,
  calling_convention  TEXT,
  sizeof              INTEGER,
  is_fpo              INTEGER NOT NULL DEFAULT 0,
  pdb_rva             INTEGER,       -- 1.10.155 address. NEVER used for hooks. See below.
  version             TEXT NOT NULL DEFAULT '1.10.155'
);

CREATE INDEX idx_pdb_symbols_name ON pdb_symbols(name);
```

**Isolation rules, which are the whole point of a separate table:**

- No index on `pdb_rva`, so address lookups against it are not merely discouraged but slow and
  unnatural.
- `lookup_by_address` and `lookup_by_name` do not read this table at all.
- `version` is written by the loader as a constant. It is never taken from a caller argument.
- `pdb_rva` is retained (a future 155↔163 mapping may want it) but is omitted from every default
  tool response.

New tool: **`pdb_signature(name)`** — Microsoft's real signature, calling convention, and sizeof for
a name, with an explicit note that the address space is 1.10.155.

`check_name_confidence` gains a `pdbCorroboration` field: `confirmed` when the name exists in the
PDB, `absent` when it does not, `unavailable` when the PDB is not indexed. A name the private PDB
never heard of is a meaningful signal, and this is the tool people already ask "can I trust this".

## 3. CSV corpus audit

New tool: **`audit_csv_corpus({ source?, write?, limit? })`**. Reads the CSVs from disk so results
are never stale. Four finding types:

### `claim_drift` (highest value)

For each `applied_*.csv` row asserting address X was renamed `old_name` → `new_name`, check what
`symbols` actually holds at that RVA:

- index shows `new_name` → consistent, no finding;
- index shows `old_name` → **drift**: the recorded rename never landed;
- index shows a third name → **drift**: something else was applied;
- no symbol at that RVA → reported as `unresolved_address` instead.

This is the check that catches "we wrote down a fix that didn't happen", the failure mode behind the
13 reverted corrections.

### `structural`

Address parses as hex; required columns present for the file's shape; no duplicate `address` within
a file; row count matches `organized_sources`.

### `pair_disagreement`

Four plan/record pairs must agree on address set and target name:

| Plan | Record | Rows |
|---|---|---|
| `safe_gaps_apply` | `applied_safe_gaps` | 2,178 / 2,178 |
| `reassign_apply` | `applied_reassignments` | 199 / 199 |
| `std_func_sweep` | `applied_std_func` | 423 / 423 |
| `misnames_to_revert` | `misnames_to_revert_simple` | 13 / 13 |

Equal row counts are not proof of agreement; the addresses and targets are compared.

### `pdb_uncorroborated`

For rows asserting a *confirmed real name* — `confirmed_misnames_to_apply.claimed_real_name`,
`applied_*.new_name`, `misnames_to_revert.revert_to_commonlib_confirmed_name` — check whether that
name exists in `pdb_symbols`. Absent is a **warning, not an error**: only 38.5% of 163 names match,
and 1.10.155 genuinely predates 1.10.163, so a missing name may be a real new function. Severity is
`info` unless the name is also absent from `symbols`.

### Severity

`error` — structural failure or a claim contradicted by the index.
`warning` — pair disagreement, unresolved address.
`info` — PDB uncorroborated.

## 4. Generated output in `Organized/`

New folder `Organized/generated/`, written only when a tool is called with `write: true`. Nothing
`organize_functions.py` owns is ever touched. Each file carries a header comment naming the
generator, the source data, and the UTC timestamp.

| File | Content |
|---|---|
| `generated/csv_audit_report.csv` | one row per finding: `source, row_no, address, finding_type, severity, detail` |
| `generated/csv_audit_report.md` | human-readable summary: counts by type and severity, then the errors in full |
| `generated/learned_findings.csv` | export of current `re-learning.sqlite` findings — `address, rva, proposed_name, confidence, note, origin, model, created_at, static_name, contradiction` |
| `generated/pdb_crosscheck_163_vs_155.csv` | the ~133k exact name matches: `name, rva_163, signature_163, signature_155, calling_convention, sizeof_155, sizeof_163` |

`Organized/generated/` is added to the workspace `.gitignore` alongside the rest of `Organized/`.

The learned-findings export is the seed of a future promotion workflow (learned → reviewed →
static), but **this design does not build promotion**. It only makes machine findings reviewable in
the same format as human ones.

## Tool surface summary

| Tool | Status |
|---|---|
| `search_organized(query, source?, limit?)` | new |
| `pdb_signature(name)` | new |
| `audit_csv_corpus({ source?, write?, limit? })` | new |
| `export_learnings({ write? })` | new |
| `check_name_confidence` | extended with `pdbCorroboration` |
| `build-index` | extended with `organized_rows`, `organized_sources`, `pdb_symbols` |

The eight existing tools and the four Phase 1 learning tools are otherwise unchanged.

## Error handling

- PDB dump file missing → `pdb_symbols` stays empty; `pdb_signature` reports unavailable;
  `check_name_confidence` returns `pdbCorroboration: "unavailable"`; the audit skips
  `pdb_uncorroborated`. Everything else works.
- A CSV missing or malformed → that source is reported as a `structural` error and the audit
  continues with the rest. One bad file never aborts the corpus audit.
- A CSV gains or loses a column → the shape mapping falls back to putting unmapped columns in
  `extra`, and records the real header in `organized_sources`. Never a hard failure.
- `Organized/generated/` not writable → the tool returns findings normally and reports that the
  write failed. Analysis is not lost because the disk is read-only.
- Learning DB unavailable → `export_learnings` fails clearly; every other tool is unaffected.

## Testing

Mirrors the Phase 1 approach: fixture CSVs and fixture in-memory SQLite, no Ghidra or Ollama.

**Parsing**
- PDB line grammar parses the real forms, including `(FPO)`, absent `(FPO)`, and the pad-offset
  address range.
- Name extraction handles MSVC backtick forms, `operator` overloads, destructors, and nested
  templates; an unextractable line is skipped rather than stored with a wrong name.
- Each CSV shape maps to `name_a`/`name_b` correctly, and unmapped columns land in `extra` as JSON.

**Isolation (the load-bearing tests)**
- `lookup_by_address` never returns a `pdb_symbols` row.
- A 1.10.155 `pdb_rva` cannot be reached through any default tool response.
- `pdb_symbols.version` is always `1.10.155` regardless of caller input.

**Audit**
- A row whose claimed `new_name` matches the index produces no finding.
- A row whose address still holds `old_name` produces `claim_drift`.
- An address absent from `symbols` produces `unresolved_address`, not `claim_drift`.
- A duplicate address within one file produces `structural`.
- A pair whose row counts match but whose addresses differ produces `pair_disagreement`.
- A claimed name absent from the PDB produces `info`, not `error`.
- A missing PDB skips `pdb_uncorroborated` without failing the run.
- A malformed CSV does not abort the audit of the other 23.

**Writes**
- No file is written unless `write: true`.
- Writing touches only `Organized/generated/`; a test asserts no file outside it is modified.
- An unwritable output directory still returns findings.

## Acceptance criteria

1. All 24 `Organized/*.csv` are queryable through `search_organized`, with `source:row_no`
   citations, and no original column is lost.
2. `pdb_symbols` holds the parsed PDB dump, and no tool can return a 1.10.155 address as though it
   were a 1.10.163 one.
3. `check_name_confidence` reports PDB corroboration and degrades cleanly when the PDB is absent.
4. `audit_csv_corpus` reports all four finding types with severities, and one bad file does not
   abort it.
5. Running the audit against the real corpus produces a report a human can act on, and any
   `claim_drift` errors it finds are real.
6. Generated files appear only under `Organized/generated/`, only on `write: true`, and nothing
   `organize_functions.py` owns is modified.
7. Rebuilding `re-index.sqlite` still cannot touch `re-learning.sqlite`.

## Out of scope

- **Promotion of learned findings into the static corpus.** The export makes it reviewable; a
  human-approved promotion workflow is a separate design.
- **A 1.10.155 → 1.10.163 address mapping.** `pdb_rva` is retained so this stays possible, but
  building it is not part of this work.
- **Re-parsing the raw `.pdb` binaries.** `Fallout4.pdb` (452 MB) and `Fallout4Beta.pdb` (670 MB)
  stay untouched; the existing `llvm-pdbutil` text dump is the source.
- **Changing the five dedicated CSV tables** or the tools that consume them.
- **Auto-fixing anything the audit finds.** It reports; a human decides.
