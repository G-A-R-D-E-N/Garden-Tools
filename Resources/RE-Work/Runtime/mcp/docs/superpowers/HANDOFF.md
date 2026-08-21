# f4-re-mcp — Session Handoff

**Written:** 2026-08-13
**Repo:** `/run/media/user/Games-Storage/Projects/Tools & Reverse Engineering/ReverseEngineering/re-mcp`
**Branch:** `feat/learning-tools-and-daemon` (HEAD = `76ada31`)
**Working tree:** clean. **Tests:** 177 passing, 16 files.

`re-mcp` is its own git repo. The parent `ReverseEngineering/` directory is **not** a git repo —
don't go looking for a workspace-root `.gitignore`, there isn't one.

⚠️ **A PreToolUse hook blocks commits to `main`.** Stay on `feat/learning-tools-and-daemon` or a
branch cut from it.

---

## What this project is

A local MCP server exposing verified Fallout 4 engine reverse-engineering facts from a ~1 GB SQLite
index (`re-index.sqlite`), plus a newer *learning* layer that decompiles single functions through
Ghidra headless and extracts names with a local Ollama model, storing them durably in a **separate**
database (`re-learning.sqlite`).

**Tool count today: 12 registered** — 8 original + 4 learning tools.

---

## Status: three workstreams

### 1. Learning tools Phase 1 — ✅ COMPLETE AND VERIFIED LIVE

Spec: `docs/superpowers/specs/2026-08-13-re-learning-tools-design.md` (revision 3)
Plan: `docs/superpowers/plans/2026-08-13-re-learning-tools-phase1.md` (14 tasks, all done)

Commits `5687d54` → `3eddde5`. Delivered:

- `re-learning.sqlite` — durable, WAL, migration-driven, **never** touched by `build-index.ts`
- `analyzeTarget()` shared pipeline (Ghidra + Ollama + atomic supersession)
- 4 MCP tools: `learn_from_target`, `audit_hardcoded_addresses`, `suggest_hooks`, `recent_learnings`
- `subsystem_index` added to the static build (365,059 rows, 22 categories)

**Verified against real runtimes, not mocks:**

| Check | Result |
|---|---|
| `learn_from_target("0x749ea0")` | real Ghidra + Ollama, **7.3 s** |
| Dedup on repeat call | `analysisRun: false`, Ollama skipped |
| `audit_hardcoded_addresses()` | **61,892 refs** across 10 repos, 53,723 resolved, ~25 s cold |
| `suggest_hooks` repo scoping | Prisma 4 hooked / HouseRules 0 — contamination fix works on real data |
| Full `build-index` rebuild | `re-learning.sqlite` survived untouched ✅ |

Three defects were found **only by running it** (mocked tests were green while nothing worked):
Ghidra rejects `&` in paths (workspace is under `Tools & Reverse Engineering`); headless wraps
script output as `INFO  DecompileAt.java> … (GhidraScript)`; dot-directories were double-counting
`.claude/worktrees` copies (25,622 phantom refs).

### 2. Learning tools Phase 2 — ❌ NOT STARTED (no plan written)

Spec items 9–13 of the revision-3 spec: persistent `analysis_queue`, `daemon_runs`,
`f4-re-daemon --once`, systemd user timer, `daemon_status` tool. The interfaces it will consume
already exist as real code: `analyzeTarget`, `AnalyzeError.kind`/`.retryable`, `createDefaultDeps`,
`MIGRATIONS` (Phase 2 appends migration version 2).

### 3. Organized corpus + CSV audit + PDB — 🟡 IN PROGRESS, Task 1 of 10

Spec: `docs/superpowers/specs/2026-08-13-organized-corpus-and-pdb-design.md`
Plan: `docs/superpowers/plans/2026-08-13-organized-corpus-and-pdb-plan.md` (10 tasks, ~3,200 lines)

Goal: index all 24 `Organized/*.csv` (only 5 are indexed today), index the 1.10.155 Microsoft PDB
dump as name-keyed evidence, audit the CSV corpus for drift, and write reports to
`Organized/generated/`.

**Execution state — this is where you pick up.**

| Task | State |
|---|---|
| 1. PDB dump parser | ✅ COMPLETE. Implemented (`794e178`) + fix round 1 (`76ada31`) + **re-review DONE + fix round 2 committed**. 191 tests passing. |
| 2–10 | Not started. Briefs exist for 2 and 3. |

---

## ✅ IMMEDIATE NEXT ACTION — RESOLVED (2026-08-13)

**Task 1's re-review was dispatched; fix round 1 was found incomplete; fix round 2 is implemented
and verified but NOT committed.** Full verdict + measurements + rulings are in the ledger
(`.superpowers/sdd/2026-08-13-organized-corpus-and-pdb-plan/progress.md` → "RE-REVIEW 76ada31").

TL;DR (identity-checked against the real 411,713-line dump, not just counted):

- 76ada31's counts were real (100% parse, 83.86% extraction, 23,560 qualified operators) but its
  "~1,747 operators lost qualification" claim was an undercount — the 794e178 code produced
  **20,120 bare operator names**, and 76ada31 still left **18,272** (`operator()` on lambdas/
  functors → literal `"operator"`, a Task 2 collision bomb), plus 1,639 conversion operators
  collapsing to their conversion type (`"__ptr64"`), 4,484 lambda ops missing the leading `<`,
  and qualifier chains broken across nested templates (`std::_Func_impl<...>::_Get` → `_Get`).
- Fix round 2 rewrote the matcher: depth-aware `::` splitter (template-aware), paren-depth-0 CC
  strip (fixes fn-ptr-param cuts), `operator()`/allocator/conversion handling, anchored backtick
  forms, quote-aware segments (`` `anonymous namespace' ``).
- **Final: 97.47% extraction (401,299/411,713), 43,042/43,042 source-qualified operators keep
  qualification, every identity flag 0, remaining 2.53% nulls all pathological** (sampled).
  191 tests pass (14 new regression tests), tsc clean.
- Ruling on 76ada31's "null for unqualified operators": global `operator new`/`delete` (10 lines)
  are now extracted — real non-colliding names; bare symbol/conversion ops stay null (cannot be
  global in C++).

Changes are **committed**. The review gate earned its keep: green tests + healthy-looking
aggregates hid ~24,500 wrong-name lines across two fix rounds.

Original review artifacts (superseded):
- Findings + fix diff: `.superpowers/sdd/2026-08-13-organized-corpus-and-pdb-plan/review-794e178..76ada31.diff`
- Implementer report (fix report appended under `# Fix Round 1 Report`): `.superpowers/sdd/2026-08-13-organized-corpus-and-pdb-plan/task-1-report.md`
- Brief: `.superpowers/sdd/2026-08-13-organized-corpus-and-pdb-plan/task-1-brief.md`

---

## 🔴 KNOWN PLAN DEFECT — read before running Task 1 or trusting the plan's code blocks

**The plan's Task 1 pasted an `extractSignatureName` regex and said "use it verbatim, do not
redesign it". That regex does not work.** With `\s` in the qualifier character class it greedily
swallows the return type and calling convention, so
`hknpBodyId& __cdecl hknpBodyId::operator=(...)` yields
`"hknpBodyId& __cdecl hknpBodyId::operator="`. It fails 2 of the plan's own Task 1 test cases.

Root cause of the bad plan text: when the plan was written, the 84.9% extraction rate was measured
against the real file — but only the **count** of extracted names was checked, never their
**identity**. The aggregate rate looked right while the names were wrong.

**Controller ruling already applied:** the "use verbatim" instruction is void. The surviving
requirement is the plan's stated invariant — *`extractSignatureName` must return `null` rather than
a wrong name when it cannot confidently extract*. This was not escalated as a "which governs"
question because the plan is internally contradictory there (both the plan's regex and the first fix
were wrong in opposite directions), so there was no coherent alternative to choose.

**Implication for whoever continues:** the plan's other pasted code blocks are similarly
*unverified against real data* except where the plan explicitly cites a live measurement. Treat
Task 3's 24-entry CSV mapping table as the next highest-risk item — its headers and shapes WERE
verified against the real files this session, but the same "measured the count, not the identity"
failure mode could hide there.

---

## Pre-flight resolution to carry into Task 8

Task 8's `writeAuditReports` code block calls `new Date().toISOString()` directly. That collides
with the Phase 1 global constraint *"all timestamps come from the shared Clock helper; never call
`new Date()` outside `src/clock.ts`"*. **Use `systemClock.now()` from `src/clock.ts` instead.**
Already recorded in the ledger; not yet applied because Task 8 hasn't run.

---

## File locations

### Plans, specs, ledger

```
re-mcp/docs/superpowers/
├── specs/2026-08-13-re-learning-tools-design.md            ← Phase 1+2 spec (rev 3)
├── specs/2026-08-13-organized-corpus-and-pdb-design.md     ← corpus/PDB spec
├── plans/2026-08-13-re-learning-tools-phase1.md            ← DONE
├── plans/2026-08-13-organized-corpus-and-pdb-plan.md       ← IN PROGRESS (task 1/10)
└── HANDOFF.md                                             ← this file

re-mcp/.superpowers/sdd/2026-08-13-organized-corpus-and-pdb-plan/   ← git-ignored scratch
├── progress.md                  ← THE LEDGER. Read first; it survives compaction.
├── task-1-brief.md  task-2-brief.md  task-3-brief.md
├── task-1-report.md             ← incl. "# Fix Round 1 Report"
├── review-46c1d7d..794e178.diff ← task 1 initial review package
└── review-794e178..76ada31.diff ← task 1 fix diff (re-review pending)
```

Regenerate briefs/packages with the superpowers scripts at
`/home/ciusr/.claude/plugins/cache/claude-plugins-official/superpowers/6.2.0/skills/subagent-driven-development/scripts/`
(`task-brief PLAN N`, `review-package PLAN BASE HEAD`, `sdd-workspace PLAN`).

### Source

```
re-mcp/src/
├── index.ts                     ← MCP server, 12 tools registered
├── build-index.ts               ← builds re-index.sqlite; UNLINKS it first (that's why DBs are split)
├── db.ts                        ← getDb(), parseAddressToRva(), formatVa()
├── paths.ts                     ← all path constants
├── clock.ts  config.ts
├── pdb/parsePdbDump.ts          ← NEW (task 1)
├── adapters/ghidra.ts           ← analyzeHeadless, timeout, tree-kill, & -path symlink workaround
├── adapters/ollama.ts           ← JSON-schema structured extraction
├── learning/{db,migrations,findings,repos,references,analyze}.ts
└── tools/*.ts                   ← 12 tools
```

### Data (all outside the git repo, in `ReverseEngineering/`)

| Path | What |
|---|---|
| `../Organized/*.csv` | 24 audit-trail CSVs, ~13,000 rows. **5 indexed, 19 not.** |
| `../Organized/1.10.163/*.txt` | 22 subsystem category files → `subsystem_index` |
| `../Symbols/Fallout4_functions.txt` | **1.10.155 PDB dump, 105 MB, 411,713 functions** ← task 1–2 input |
| `../Symbols/Fallout4.pdb` | raw 452 MB PDB (not parsed; out of scope) |
| `../Projects/fwgh_proj_1.10.163_unpacked/` | the OG Ghidra project (`Fallout4_163_unpacked`) |
| `../Ghidra/ghidra_12.1_PUBLIC/support/analyzeHeadless` | native Linux, Java 21 |
| `../Scripts/DecompileAt.java` | the decompile script the adapter drives |
| `re-mcp/re-index.sqlite` | ~1 GB, rebuildable, gitignored |
| `re-mcp/re-learning.sqlite` | durable learned findings, gitignored |

---

## Environment facts (verified live — do not re-derive)

- **SQLite bundled in `better-sqlite3` 13.0.1 = 3.53.3**, past the 3.51.3 WAL-reset fix floor. The
  WAL safety gate passes; it's a runtime assertion, not a blocker.
- **Ollama 0.32.6** at `localhost:11434`, `qwen2.5-coder:7b` present. JSON-schema `format` works;
  a structured extraction returns in ~5 s.
- **Java 21**, Ghidra 12.1 headless native Linux.
- ⚠️ **`npm install` requires `--no-bin-links`** — this is an **NTFS volume with no symlink
  support**. Consequently `npx vitest` DOES NOT WORK. Test command is:
  ```bash
  npm test -- <path>
  ```
  which invokes `node ./node_modules/vitest/vitest.mjs run`.
- ⚠️ **Ghidra rejects `&` in path elements** and this workspace lives under
  `Tools & Reverse Engineering`. `src/adapters/ghidra.ts` routes project/script paths through a
  stable symlink in the temp dir. That's why a symlink appears in `/tmp`.
- Ghidra headless wraps every script `println` as `INFO  <script>> <line> (GhidraScript)`. The
  parser must unwrap it.
- ESM/NodeNext: **all relative imports carry a `.js` extension**, even for `.ts` files.

### Measured numbers worth not re-deriving

| Measurement | Value |
|---|---|
| PDB dump line-grammar parse rate | 100% (411,713 / 411,713) |
| PDB name-extraction rate | **97.47%** (401,299 / 411,713) since fix round 2; the ~2.5% misses are compiler thunks / fn-ptr-in-template / template operators — null by design |
| Source-qualified operator lines keeping qualification | 43,042 / 43,042 (plus 10 global `operator new`/`delete` extracted bare — legit) |
| 1.10.163 distinct symbol names | 347,519 |
| …with an exact name match in the PDB | **133,636 (38.5%)** ← the corroboration base |
| Consumer refs across the workspace | 61,892 in 10 repos |
| `Organized/` CSV rows | ~13,000 across 24 files |

---

## Design invariants that must not be broken

1. **Two databases, and they never mix.** `build-index.ts` starts with
   `unlinkSync(DB_PATH)` — anything durable stored beside it would be destroyed on every rebuild.
   No learning code writes to `re-index.sqlite`.
2. **The 1.10.155 PDB is name-keyed evidence, NEVER an address source.** Its RVAs are for a
   different game version. `pdb_symbols` gets **no index on `pdb_rva`**, and no tool joins it by
   address. CLAUDE.md calls mixing OG/AE/155 address spaces the single most likely mistake in this
   workspace.
3. **A learned finding is evidence, never authority.** It never edits a static symbol. On
   disagreement the tools return a `contradiction` + loud `warning`, and an audited static name wins.
4. **`check_name_confidence` verdict precedence is revert → correction → disagreement.** A reverted
   correction means the ORIGINAL name is confirmed correct. Getting this backwards inverts the
   verdict for 13 real addresses. (I had it backwards in the plan; the code is correct.)
5. **Generated output only ever lands in `Organized/generated/`, only on explicit `write: true`.**
   Never overwrite anything `organize_functions.py` owns.
6. **Child processes: argv arrays only.** Never a shell string containing a path or user input.
   Decompiler text and model output are untrusted data.

---

## Deliberate scope decisions already made (don't re-litigate)

- `claim_drift` runs against only **6** sources (the `applied_*` files + `misnames_to_revert`) — the
  other 18 are plans/candidates where differing from the record is legitimate, not an error.
- `pdb_uncorroborated` runs against **8** sources and is always `info` severity — 1.10.155 predates
  1.10.163, so a missing name doesn't prove wrongness.
- `vtable_symbol` references (`RE::VTABLE::Actor[5]`) are recorded **unresolved** by design; they're
  CommonLibF4 header symbols this index can't map to an RVA. (The spec called this kind
  `vtable_rva`; reality has no raw vtable RVAs, so it was renamed.)
- Repo discovery finds **git repos only**. `Prisma/PirsmaFix/` has real `REL::Offset` refs but no
  `.git` — reachable via the explicit `paths` argument.
- `HouseRules` reports ~12,000 refs because it vendors CommonLibF4 at `lib/commonlibf4`. Those are
  genuinely in the repo; filtering vendored deps was left as a judgment call for the user.

---

## Suggested order from here

1. ✅ Re-review Task 1's fix — **DONE** (2026-08-13); fix round 2 committed, see ledger.
2. Tasks 2–10 of the corpus/PDB plan. Briefs exist for 2 and 3; generate 4–10 with `task-brief`.
3. Final whole-branch review, then `superpowers:finishing-a-development-branch`.
4. Phase 2 of the learning tools (daemon) — needs a plan written first.

Update `.superpowers/sdd/.../progress.md` after every task. It is the recovery map; after a
compaction, trust it and `git log` over recollection.
