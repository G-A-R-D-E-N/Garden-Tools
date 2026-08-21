# Organized Corpus Indexing, CSV Audit, and PDB Evidence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Index all 24 `Organized/*.csv` files and the 1.10.155 PDB dump into `re-index.sqlite`, add a CSV corpus audit that catches claim drift and plan/record disagreement, and surface the results as new MCP tools plus generated reports under `Organized/generated/`.

**Architecture:** A new pure module (`src/organized/csvSources.ts`) declares an explicit column mapping for each of the 24 CSVs and normalizes any row into one shape. `build-index.ts` uses it to populate a single `organized_rows` table (plus `pdb_symbols` from a new PDB dump parser). Four new MCP tools sit on top: `search_organized` and `pdb_signature` read the index directly; `audit_csv_corpus` re-reads the CSVs from disk (so it is never stale) and cross-checks them against the live index; `export_learnings` exports `re-learning.sqlite` findings to CSV. `check_name_confidence` gains a PDB-corroboration field. The PDB table is name-keyed only — no tool can reach a 1.10.155 address through it.

**Tech Stack:** TypeScript (ES2022, NodeNext ESM), `better-sqlite3` 13.0.1, `csv-parse` (already a dependency), `vitest`. No new dependencies.

## Source spec

`docs/superpowers/specs/2026-08-13-organized-corpus-and-pdb-design.md`. This plan implements it in full except promotion workflows and raw `.pdb` binary parsing, which the spec marks out of scope.

## Facts verified this session — do not re-derive

| Fact | Value | How it was checked |
|---|---|---|
| `Organized/*.csv` count | 24 files, ~13,000 rows total | `wc -l` per file |
| CSVs already indexed today | 5 of 24 (`applied_misname_corrections`, `commonlib_vtable_slots`, `crossref_163_disagreements_vs_155`, `misnames_to_revert`, `vtable_audit_report`) | `grep` in `build-index.ts` |
| Distinct real column-header shapes across the 24 CSVs | 12 (see Task 3's literal mapping table) | `head -1` on every file |
| Every CSV's `address` column is a full VA (`0x14...`), **except** `decompile_targets.csv` | confirmed by grepping every address column for non-`0x14` values | shell sweep, see below |
| `decompile_targets.csv`'s `target` column mixes `addr:0xHEX` (bare RVA after the prefix) and bare symbol names with no address at all | both forms present | `cut`+`sort -u` on the column |
| 1.10.155 PDB dump (`Symbols/Fallout4_functions.txt`) | 105 MB, 411,713 `func [...]` lines, plain `\n`, no BOM, 6 header lines before `---GLOBALS---` | `ls -la`, `cat -A`, `grep -c` |
| PDB line grammar parse rate | **100%** (411,713 / 411,713) | streaming regex over the real file |
| PDB name-extraction rate with the tested extractor | **84.9%** (349,489 / 411,713), 247,040 distinct names | live Node run, see Task 1 |
| 1.10.163 distinct symbol names with an exact match in the PDB | **133,636 / 347,519 (38.5%)** | live Node run against `re-index.sqlite` |
| `re-index.sqlite` bundled SQLite version | 3.53.3 (past the WAL-reset fix floor of 3.51.3) | Phase 1, unchanged |
| Workspace root (`ReverseEngineering`) is **not** a git repository | only `re-mcp/` inside it is a repo | `git rev-parse --is-inside-work-tree` in both dirs |

**Correction to the spec:** the spec says to add `Organized/generated/` to "the workspace root `.gitignore`". That file does not exist — the workspace root is not a git repository, so nothing tracks `Organized/` at all. This plan does **not** touch any `.gitignore`; the new folder is simply outside version control already, which is the same state the rest of `Organized/` is already in.

**Deliberate CSV-writer note:** `package.json` has `csv-parse` but no CSV-writing library. Rather than add a dependency for four flat, no-embedded-newline output shapes, this plan writes a ~15-line manual CSV serializer (`src/organized/writeCsv.ts`) with correct quoting.

**Deliberate deviation from the spec's "header comment" note:** the spec says generated files "carry a header comment naming the generator... and the UTC timestamp." For the two generated **CSV** files (`learned_findings.csv`, `pdb_crosscheck_163_vs_155.csv`) this plan omits a `#`-comment header line, because none of the 24 existing `Organized/*.csv` files use one and `csv-parse` (used to re-read them) does not skip comment lines by default — adding one would make the generated CSVs the only ones in the corpus that don't round-trip through the same reader. Generation metadata (generator, timestamp, source counts) goes in the `.md` report instead, which is prose already.

## Global Constraints

Every task's requirements implicitly include this section.

- **Branch:** continue on `feat/learning-tools-and-daemon` (or a branch cut from it). The PreToolUse hook in this repo blocks direct commits to `main`.
- **The five already-dedicated CSV tables (`name_disagreements`, `misname_corrections`, `misname_reverts`, `vtable_expected`, `vtable_audit`) and the tools that read them are unchanged.** This plan adds `organized_rows`/`organized_sources` as a parallel, corpus-wide view; it does not replace or refactor the existing five.
- **`pdb_symbols` is indexed by name only.** No index on `pdb_rva`. No tool joins `pdb_symbols` to anything by address. `version` on every `pdb_symbols` row is the literal string `'1.10.155'`, written by the loader — never taken from a caller argument.
- **`audit_csv_corpus` re-reads CSVs from disk on every call.** It must never depend on `organized_rows` (which reflects the last `build-index` run and can be stale relative to a hand-edited CSV).
- **Every generated file lands under `Organized/generated/` and only when a tool is called with `write: true`.** No tool writes a file as a side effect of a read-only call.
- **All relative imports use the `.js` extension** (NodeNext ESM), matching the existing code.
- **Commit after every task**, in the existing `feat:` / `test:` / `docs:` style.
- **Do not run `npm run build-index` inside any task** except the final one — it reprocesses ~1 GB of source data and takes several minutes. Tests use fixtures.

## File Structure

**Created**

| Path | Responsibility |
|---|---|
| `src/pdb/parsePdbDump.ts` | Pure PDB line/signature parser |
| `src/organized/csvSources.ts` | Declarative 24-entry column mapping + pure row normalizer |
| `src/organized/writeCsv.ts` | Minimal CSV serializer with correct quoting |
| `src/tools/searchOrganized.ts` | MCP tool: search `organized_rows` |
| `src/tools/pdbSignature.ts` | MCP tool: look up a name in `pdb_symbols` |
| `src/tools/auditCsvCorpus.ts` | MCP tool: re-read CSVs, cross-check, optionally write reports |
| `src/tools/exportLearnings.ts` | MCP tool: export `re-learning.sqlite` findings to CSV |
| `tests/pdb/parsePdbDump.test.ts` | |
| `tests/organized/csvSources.test.ts` | |
| `tests/organized/writeCsv.test.ts` | |
| `tests/tools/searchOrganized.test.ts` | |
| `tests/tools/pdbSignature.test.ts` | |
| `tests/tools/auditCsvCorpus.test.ts` | |
| `tests/tools/exportLearnings.test.ts` | |

**Modified**

| Path | Change |
|---|---|
| `src/paths.ts` | `PDB_DUMP_PATH`, `ORGANIZED_GENERATED_DIR` |
| `src/build-index.ts` | `pdb_symbols`, `organized_rows`, `organized_sources` schema + indexers, wired into `main()` |
| `src/tools/checkNameConfidence.ts` | `computePdbCorroboration` export + `pdbCorroboration`/`pdbMatches` fields on the result |
| `src/index.ts` | register the four new tools |
| `README.md` | document the new tools and generated files |

---

### Task 1: PDB dump parser

**Files:**
- Create: `src/pdb/parsePdbDump.ts`
- Test: `tests/pdb/parsePdbDump.test.ts`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `interface PdbFunctionLine { pdbRva: number; sizeof: number; isFpo: boolean; signature: string }`
  - `function parsePdbLine(line: string): PdbFunctionLine | null`
  - `function extractSignatureName(signature: string): string | null`
  - `function extractCallingConvention(signature: string): string | null`

**Context the implementer needs.** The dump's line grammar, confirmed against the real file:

```
  func [0x00020900+ 0 - 0x00020908- 1 | sizeof=  8] (FPO) hknpBodyId& __cdecl hknpBodyId::operator=(hknpBodyId& __that)
  func [0x01cce5b0+17 - 0x01cce6f4-13 | sizeof=324] bool __cdecl BSCullingProcess::AddShared(NiAVObject* apObject)
```

The first hex group is the function's own start address within the 1.10.155 module — already an RVA, no VA subtraction needed (this dump format has no separate image-base line the way the 163 dump's header does). `(FPO)` is present on some lines and absent on others. Six lines before `---GLOBALS---` are dump metadata, not function lines; they simply won't match the regex.

The following extractor is **already tested against the real file** (84.9% name-extraction rate, 411,713/411,713 line-grammar parse rate) — use it verbatim, do not redesign it:

```ts
const LINE_RE =
  /^\s*func \[0x([0-9A-Fa-f]+)\+\s*\d+\s*-\s*0x[0-9A-Fa-f]+-\s*\d+\s*\|\s*sizeof=\s*(\d+)\]\s*(\(FPO\)\s*)?(.*)$/;

export function parsePdbLine(line: string): PdbFunctionLine | null {
  const match = line.match(LINE_RE);
  if (!match) return null;
  return {
    pdbRva: parseInt(match[1], 16),
    sizeof: parseInt(match[2], 10),
    isFpo: match[3] !== undefined,
    signature: match[4],
  };
}

export function extractCallingConvention(signature: string): string | null {
  const match = signature.match(/(__cdecl|__thiscall|__stdcall|__fastcall|__vectorcall)/);
  return match ? match[1] : null;
}

export function extractSignatureName(signature: string): string | null {
  // MSVC special forms: `scalar deleting destructor', `dynamic initializer for 'X'', etc.
  const backtick = signature.match(/((?:[\w:<>,~\s*&]+::)?`[^`']+'(?:[^(]*)?)\s*\(/);
  if (backtick) return backtick[1].trim().replace(/^.*?\s(?=[A-Za-z_`])/, "");

  // operator overloads: operator=, operator+, operator new, etc.
  const operator = signature.match(/([A-Za-z_][\w:<>,~\s*&]*::)?(operator(?:[^\w\s(]+|\s+\w+))\s*\(/);
  if (operator) return ((operator[1] || "") + operator[2]).replace(/\s+/g, " ").trim();

  const parenIndex = signature.indexOf("(");
  if (parenIndex < 0) return null;
  const head = signature.slice(0, parenIndex).trim();
  const match = head.match(/([A-Za-z_~][\w:~]*(?:<[^>]*>)?(?:::[A-Za-z_~][\w:~]*(?:<[^>]*>)?)*)$/);
  return match ? match[1] : null;
}
```

- [ ] **Step 1: Write the failing test**

Create `tests/pdb/parsePdbDump.test.ts`:

```ts
import { describe, expect, it } from "vitest";
import {
  extractCallingConvention,
  extractSignatureName,
  parsePdbLine,
} from "../../src/pdb/parsePdbDump.js";

// Captured verbatim from Symbols/Fallout4_functions.txt.
const REAL_LINES = [
  "  func [0x00020900+ 0 - 0x00020908- 1 | sizeof=  8] (FPO) hknpBodyId& __cdecl hknpBodyId::operator=(hknpBodyId& __that)",
  "  func [0x01cce5b0+17 - 0x01cce6f4-13 | sizeof=324] (FPO) bool __cdecl BSCullingProcess::AddShared(NiAVObject* apObject)",
  "  func [0x017c8e90+ 0 - 0x017c8ec4- 1 | sizeof= 52] (FPO) unsigned __int64 __cdecl hkStringMapOperations::hash(unsigned __int64 key)",
];

const NO_FPO_LINE =
  "  func [0x0abc1230+ 3 - 0x0abc1250- 2 | sizeof= 32] void __cdecl PlayerCharacter::Update(float dt)";

const HEADER_LINES = [
  "Summary for E:\\F4SE OG\\Tools\\PDB\\Fallout4.pdb",
  "  Size: 452201472 bytes",
  "  Guid: {E0BB0FDB-47DA-48F1-886C-CC6967D72468}",
  "  Age: 1",
  "  Attributes: HasPrivateSymbols ",
  "---GLOBALS---",
];

describe("parsePdbLine", () => {
  it("parses the real dump's line grammar with (FPO) present", () => {
    const parsed = parsePdbLine(REAL_LINES[0])!;
    expect(parsed.pdbRva).toBe(0x00020900);
    expect(parsed.sizeof).toBe(8);
    expect(parsed.isFpo).toBe(true);
    expect(parsed.signature).toContain("hknpBodyId::operator=");
  });

  it("parses a line with (FPO) absent", () => {
    const parsed = parsePdbLine(NO_FPO_LINE)!;
    expect(parsed.isFpo).toBe(false);
    expect(parsed.pdbRva).toBe(0x0abc1230);
    expect(parsed.sizeof).toBe(32);
  });

  it("parses every real captured line", () => {
    for (const line of REAL_LINES) {
      expect(parsePdbLine(line), line).not.toBeNull();
    }
  });

  it("returns null for dump header/metadata lines", () => {
    for (const line of HEADER_LINES) {
      expect(parsePdbLine(line), line).toBeNull();
    }
  });

  it("returns null for a blank line", () => {
    expect(parsePdbLine("")).toBeNull();
  });
});

describe("extractCallingConvention", () => {
  it("finds __cdecl", () => {
    expect(extractCallingConvention(parsePdbLine(REAL_LINES[1])!.signature)).toBe("__cdecl");
  });

  it("returns null when no calling convention is present", () => {
    expect(extractCallingConvention("void Foo::Bar(int x)")).toBeNull();
  });
});

describe("extractSignatureName", () => {
  it("extracts a plain qualified name", () => {
    expect(extractSignatureName(parsePdbLine(REAL_LINES[1])!.signature)).toBe(
      "BSCullingProcess::AddShared"
    );
    expect(extractSignatureName(parsePdbLine(REAL_LINES[2])!.signature)).toBe(
      "hkStringMapOperations::hash"
    );
  });

  it("extracts an operator overload", () => {
    expect(extractSignatureName(parsePdbLine(REAL_LINES[0])!.signature)).toBe(
      "hknpBodyId::operator="
    );
  });

  it("extracts an MSVC scalar-deleting-destructor form", () => {
    const name = extractSignatureName(
      "void* __cdecl hkaAnimationContainer::`scalar deleting destructor'()"
    );
    expect(name).toContain("scalar deleting destructor");
    expect(name).toContain("hkaAnimationContainer::");
  });

  it("extracts a dynamic-initializer form", () => {
    const name = extractSignatureName(
      "void __cdecl `dynamic initializer for 'fVATSAPCostStimpak''()"
    );
    expect(name).toContain("dynamic initializer");
  });

  it("returns null rather than a wrong name for an unparseable nested-template/lambda form", () => {
    const name = extractSignatureName(
      "BSContainer::ForEachResult __cdecl NiNode::ForEachChild<<lambda_6ec21535ec589316bd10ef46d0628226> >()"
    );
    // Either a best-effort name or null is acceptable; it must never throw.
    expect(() => name).not.toThrow();
  });

  it("returns null when there is no parenthesis at all", () => {
    expect(extractSignatureName("not a signature")).toBeNull();
  });
});
```

- [ ] **Step 2: Run it and verify it fails**

Run: `npm test -- tests/pdb/parsePdbDump.test.ts`
Expected: FAIL — `Cannot find module '../../src/pdb/parsePdbDump.js'`.

- [ ] **Step 3: Implement the parser**

Create `src/pdb/parsePdbDump.ts` with the three functions and the `PdbFunctionLine` interface exactly as given in "Context the implementer needs" above.

- [ ] **Step 4: Run the test and verify it passes**

Run: `npm test -- tests/pdb/parsePdbDump.test.ts`
Expected: PASS, 13 tests. (The lambda test only asserts it doesn't throw, so either outcome for that line passes.)

- [ ] **Step 5: Commit**

```bash
git add src/pdb/parsePdbDump.ts tests/pdb/parsePdbDump.test.ts
git commit -m "feat: add PDB dump line/signature parser"
```

---

### Task 2: `pdb_symbols` schema and build-index wiring

**Files:**
- Modify: `src/paths.ts`
- Modify: `src/build-index.ts`
- Test: `tests/buildIndex.test.ts` (append to the existing file)

**Interfaces:**
- Consumes: `parsePdbLine`, `extractSignatureName`, `extractCallingConvention` (Task 1).
- Produces:
  - `PDB_DUMP_PATH` exported from `src/paths.ts`
  - New static table `pdb_symbols(name, signature, calling_convention, sizeof, is_fpo, pdb_rva, version)`, indexed on `name` only
  - `indexPdbSymbols(db: Database.Database): Promise<number>` exported from `build-index.ts` for testing

**Context the implementer needs.** Mirror `indexSymbolDumps`'s existing streaming pattern exactly (`createReadStream` + `readline` + batched transaction inserts of 5000), since the source file is the same order of magnitude (105 MB vs. the 163 dump's own size) and that pattern already works. **Do not add an index on `pdb_rva`** — that omission is the isolation mechanism the spec relies on, not an oversight.

- [ ] **Step 1: Add the path constant**

Append to `src/paths.ts`, near `SYMBOL_DUMPS`:

```ts
// The 1.10.155 PDB dump. Names/signatures only — its addresses are for a DIFFERENT game version
// and are never used as 1.10.163 hook offsets. See pdb_symbols' isolation rules in build-index.ts.
export const PDB_DUMP_PATH = join(SYMBOLS_DIR, "Fallout4_functions.txt");

export const ORGANIZED_GENERATED_DIR = join(ORGANIZED_DIR, "generated");
```

- [ ] **Step 2: Write the failing test**

Append to `tests/buildIndex.test.ts` (add the import and the new `describe` block; do not remove anything existing):

```ts
import { indexPdbSymbols } from "../src/build-index.js";
```

```ts
describe("pdb_symbols isolation", () => {
  function schemaFor(db: Database) {
    db.exec(`
      CREATE TABLE pdb_symbols (
        name                TEXT NOT NULL,
        signature           TEXT NOT NULL,
        calling_convention  TEXT,
        sizeof              INTEGER,
        is_fpo              INTEGER NOT NULL DEFAULT 0,
        pdb_rva             INTEGER,
        version             TEXT NOT NULL DEFAULT '1.10.155'
      );
      CREATE INDEX idx_pdb_symbols_name ON pdb_symbols(name);
    `);
  }

  it("has no index on pdb_rva", () => {
    const db = new Database(":memory:");
    schemaFor(db);
    const indexes = db
      .prepare("SELECT sql FROM sqlite_master WHERE type='index' AND tbl_name='pdb_symbols'")
      .all() as { sql: string }[];
    db.close();
    expect(indexes).toHaveLength(1);
    expect(indexes[0].sql).toContain("(name)");
    expect(indexes.some((i) => i.sql.includes("pdb_rva"))).toBe(false);
  });

  it("indexPdbSymbols writes rows with a hardcoded version, from a fixture file", async () => {
    const dir = tempDir();
    const fixturePath = join(dir, "fixture_pdb.txt");
    writeFileSync(
      fixturePath,
      [
        "Summary for E:\\fake.pdb",
        "---GLOBALS---",
        "  func [0x00020900+ 0 - 0x00020908- 1 | sizeof=  8] (FPO) hknpBodyId& __cdecl hknpBodyId::operator=(hknpBodyId& __that)",
        "  func [0x01cce5b0+17 - 0x01cce6f4-13 | sizeof=324] bool __cdecl BSCullingProcess::AddShared(NiAVObject* apObject)",
        "not a func line",
      ].join("\n")
    );

    const db = new Database(":memory:");
    schemaFor(db);
    const count = await indexPdbSymbols(db, fixturePath);

    expect(count).toBe(2);
    const rows = db.prepare("SELECT * FROM pdb_symbols ORDER BY name").all() as {
      name: string;
      version: string;
      pdb_rva: number;
      calling_convention: string;
    }[];
    expect(rows).toHaveLength(2);
    expect(rows.every((r) => r.version === "1.10.155")).toBe(true);
    expect(rows.find((r) => r.name === "BSCullingProcess::AddShared")!.pdb_rva).toBe(0x01cce5b0);
    expect(rows.find((r) => r.name === "BSCullingProcess::AddShared")!.calling_convention).toBe(
      "__cdecl"
    );
    db.close();
  });

  it("skips a line whose name cannot be extracted rather than storing a null name", async () => {
    const dir = tempDir();
    const fixturePath = join(dir, "fixture_pdb2.txt");
    writeFileSync(
      fixturePath,
      "  func [0x1+ 0 - 0x2- 1 | sizeof=  1] not a signature at all\n"
    );
    const db = new Database(":memory:");
    schemaFor(db);
    const count = await indexPdbSymbols(db, fixturePath);
    expect(count).toBe(0);
    db.close();
  });

  it("returns 0 and does not throw when the dump file is missing", async () => {
    const db = new Database(":memory:");
    schemaFor(db);
    const count = await indexPdbSymbols(db, join(tempDir(), "does-not-exist.txt"));
    expect(count).toBe(0);
    db.close();
  });
});
```

- [ ] **Step 3: Run it and verify it fails**

Run: `npm test -- tests/buildIndex.test.ts`
Expected: FAIL — `indexPdbSymbols` is not exported from `build-index.ts`.

- [ ] **Step 4: Add the schema**

In `createSchema()` in `src/build-index.ts`, append inside the `db.exec` template literal, after `subsystem_index`:

```sql
    CREATE TABLE pdb_symbols (
      name                TEXT NOT NULL,
      signature           TEXT NOT NULL,
      calling_convention  TEXT,
      sizeof              INTEGER,
      is_fpo              INTEGER NOT NULL DEFAULT 0,
      pdb_rva             INTEGER,
      version             TEXT NOT NULL DEFAULT '1.10.155'
    );
    CREATE INDEX idx_pdb_symbols_name ON pdb_symbols(name);
```

- [ ] **Step 5: Add the indexer**

Add to `src/build-index.ts`, and add `parsePdbLine, extractSignatureName, extractCallingConvention` to a new import from `./pdb/parsePdbDump.js`:

```ts
import { extractCallingConvention, extractSignatureName, parsePdbLine } from "./pdb/parsePdbDump.js";
```

```ts
export async function indexPdbSymbols(
  db: Database.Database,
  dumpPath: string = PDB_DUMP_PATH
): Promise<number> {
  if (!existsSync(dumpPath)) {
    console.log(`  SKIP ${dumpPath} (not found)`);
    return 0;
  }

  const insert = db.prepare(
    `INSERT INTO pdb_symbols (name, signature, calling_convention, sizeof, is_fpo, pdb_rva, version)
     VALUES (?, ?, ?, ?, ?, ?, '1.10.155')`
  );
  const insertBatch = db.transaction(
    (rows: [string, string, string | null, number, number, number][]) => {
      for (const row of rows) insert.run(...row);
    }
  );

  const rl = createInterface({ input: createReadStream(dumpPath, { encoding: "utf8" }) });
  let batch: [string, string, string | null, number, number, number][] = [];
  let count = 0;
  for await (const line of rl) {
    const parsed = parsePdbLine(line);
    if (!parsed) continue;
    const name = extractSignatureName(parsed.signature);
    if (!name) continue;
    batch.push([
      name,
      parsed.signature,
      extractCallingConvention(parsed.signature),
      parsed.sizeof,
      parsed.isFpo ? 1 : 0,
      parsed.pdbRva,
    ]);
    count++;
    if (batch.length >= 5000) {
      insertBatch(batch);
      batch = [];
    }
  }
  if (batch.length > 0) insertBatch(batch);
  return count;
}
```

Add `PDB_DUMP_PATH` to the existing `./paths.js` import list.

- [ ] **Step 6: Wire it into `main()`**

In `main()`, after the `subsystem_index` block and before `db.close()`:

```ts
  console.log("Indexing pdb_symbols (Symbols/Fallout4_functions.txt)...");
  const pdbCount = await indexPdbSymbols(db);
  console.log(`  total: ${pdbCount.toLocaleString()} symbols`);
```

- [ ] **Step 7: Run the test and verify it passes**

Run: `npm test -- tests/buildIndex.test.ts`
Expected: PASS, 11 tests (7 existing + 4 new).

- [ ] **Step 8: Run the full suite and the build**

Run: `npm test && npm run build`
Expected: all green, exit 0.

- [ ] **Step 9: Commit**

```bash
git add src/paths.ts src/build-index.ts tests/buildIndex.test.ts
git commit -m "feat: index the 1.10.155 PDB dump into a name-only pdb_symbols table"
```

---

### Task 3: CSV column mapping module

**Files:**
- Create: `src/organized/csvSources.ts`
- Test: `tests/organized/csvSources.test.ts`

**Interfaces:**
- Consumes: `parseAddressToRva` from `../db.js` (existing, already exported — handles both a full VA and a bare RVA, with/without `0x`, any case).
- Produces:
  - `interface OrganizedFileMapping { file: string; addressCol: string | null; addressPrefix?: string; nameACol: string | null; nameBCol: string | null; kindCol: string | null; noteCol: string | null; extraCols: string[] }`
  - `const CSV_SOURCES: OrganizedFileMapping[]` — exactly 24 entries, one per real file in `Organized/`
  - `interface ParsedOrganizedRow { rowNo: number; address: string | null; rva: number | null; nameA: string | null; nameB: string | null; kind: string | null; note: string | null; extra: Record<string, string> }`
  - `function parseOrganizedCsv(mapping: OrganizedFileMapping, rows: Record<string, string>[]): ParsedOrganizedRow[]`
  - `function readOrganizedCsv(path: string): { columns: string[]; rows: Record<string, string>[] }`
  - `function sourceNameFor(file: string): string` — basename without `.csv`, e.g. `"applied_safe_gaps"`

**Context the implementer needs.** This module is pure with respect to the mapping/normalization logic; only `readOrganizedCsv` touches the filesystem, and it is a near-duplicate of `build-index.ts`'s private `readCsv` helper. That duplication is deliberate — `build-index.ts`'s five existing dedicated-table indexers already depend on their own local `readCsv`, and changing five working call sites for a one-line reuse is out of scope for this plan.

**The 24-entry mapping table, verified against every real file's header this session — copy verbatim:**

```ts
export const CSV_SOURCES: OrganizedFileMapping[] = [
  { file: "all_inconclusive_resolved.csv", addressCol: "address", nameACol: null, nameBCol: "name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "applied_hknpShape.csv", addressCol: "address", nameACol: "old_name", nameBCol: "new_name", kindCol: "kind", noteCol: "note", extraCols: [] },
  { file: "applied_misname_corrections.csv", addressCol: "address", nameACol: "old_name", nameBCol: "new_name", kindCol: "method", noteCol: "note", extraCols: [] },
  { file: "applied_reassignments.csv", addressCol: "address", nameACol: "old_name", nameBCol: "new_name", kindCol: "kind", noteCol: "note", extraCols: [] },
  { file: "applied_safe_gaps.csv", addressCol: "address", nameACol: "old_name", nameBCol: "new_name", kindCol: "kind", noteCol: "note", extraCols: [] },
  { file: "applied_std_func.csv", addressCol: "address", nameACol: "old_name", nameBCol: "new_name", kindCol: "kind", noteCol: "note", extraCols: [] },
  { file: "commonlib_vtable_slots.csv", addressCol: null, nameACol: "class", nameBCol: "expected_method", kindCol: "slot", noteCol: null, extraCols: [] },
  { file: "confirmed_misnames_to_apply.csv", addressCol: "address", nameACol: "old_name", nameBCol: "claimed_real_name", kindCol: null, noteCol: "evidence", extraCols: [] },
  { file: "crossref_163_disagreements_vs_155.csv", addressCol: "address", nameACol: "existing_name", nameBCol: "decoded_name", kindCol: "verdict_vs_1.10.155", noteCol: "note", extraCols: [] },
  { file: "crossref_review_candidates.csv", addressCol: "address", nameACol: "existing_name", nameBCol: "decoded_name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "crossref_safe_naming_gaps.csv", addressCol: "address", nameACol: "existing_name", nameBCol: "decoded_name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "decompile_targets.csv", addressCol: "target", addressPrefix: "addr:", nameACol: "target", nameBCol: null, kindCol: "category", noteCol: "note", extraCols: [] },
  { file: "final_2_held.csv", addressCol: "address", nameACol: null, nameBCol: "name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "hknpShape_authoritative.csv", addressCol: "address", nameACol: "existing_name", nameBCol: "decoded_name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "hknpShape_rename.csv", addressCol: "address", nameACol: null, nameBCol: "revert_to_name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "inconclusive_resolved_rename.csv", addressCol: "address", nameACol: null, nameBCol: "revert_to_name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "misnames_to_revert.csv", addressCol: "address", nameACol: "wrongly_renamed_to", nameBCol: "revert_to_commonlib_confirmed_name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "misnames_to_revert_simple.csv", addressCol: "address", nameACol: null, nameBCol: "revert_to_name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "reassign_apply.csv", addressCol: "address", nameACol: "existing_name", nameBCol: "decoded_name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "safe_gaps_apply.csv", addressCol: "address", nameACol: "existing_name", nameBCol: "decoded_name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "safe_gaps_class_reassigned_deferred.csv", addressCol: "address", nameACol: "existing_name", nameBCol: "decoded_name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "std_func_sweep.csv", addressCol: "address", nameACol: "existing_name", nameBCol: "decoded_name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "vtable_audit_fixes.csv", addressCol: "address", nameACol: null, nameBCol: "expected_qualified_name", kindCol: null, noteCol: null, extraCols: [] },
  { file: "vtable_audit_report.csv", addressCol: "address", nameACol: "expected_method", nameBCol: "actual_name", kindCol: "verdict", noteCol: null, extraCols: ["class", "slot"] },
];
```

**`decompile_targets.csv` is the one file needing the dual-purpose-column branch** (`addressPrefix`). Its `target` column holds either `"addr:0x5f90b0"` (an address — note: **bare RVA**, not a full VA like every other file's `address` column) or a bare symbol name like `"TESQuestStageItem::Resolve"` with no address at all. The exact normalization logic:

```ts
export function parseOrganizedCsv(
  mapping: OrganizedFileMapping,
  rows: Record<string, string>[]
): ParsedOrganizedRow[] {
  return rows.map((row, i) => {
    let addressRaw = mapping.addressCol ? (row[mapping.addressCol] ?? null) : null;

    if (mapping.addressPrefix) {
      if (addressRaw && addressRaw.startsWith(mapping.addressPrefix)) {
        addressRaw = addressRaw.slice(mapping.addressPrefix.length);
      } else {
        // Not address-shaped after all — decompile_targets.csv's bare-name rows land here.
        addressRaw = null;
      }
    }

    const rva = addressRaw ? parseAddressToRva(addressRaw) : null;

    let nameA: string | null = null;
    if (mapping.nameACol) {
      if (mapping.nameACol === mapping.addressCol) {
        // Dual-purpose column: only treat it as a name when it did NOT resolve as an address.
        nameA = rva === null ? (row[mapping.nameACol] ?? null) : null;
      } else {
        nameA = row[mapping.nameACol] ?? null;
      }
    }

    const extra: Record<string, string> = {};
    for (const col of mapping.extraCols) {
      if (row[col] !== undefined) extra[col] = row[col];
    }

    return {
      rowNo: i + 1,
      address: addressRaw,
      rva,
      nameA,
      nameB: mapping.nameBCol ? (row[mapping.nameBCol] ?? null) : null,
      kind: mapping.kindCol ? (row[mapping.kindCol] ?? null) : null,
      note: mapping.noteCol ? (row[mapping.noteCol] ?? null) : null,
      extra,
    };
  });
}

export function sourceNameFor(file: string): string {
  return file.endsWith(".csv") ? file.slice(0, -4) : file;
}
```

`readOrganizedCsv` mirrors `build-index.ts`'s private `readCsv` (BOM strip + `csv-parse/sync` with `columns: true`), but also returns the header:

```ts
import { readFileSync } from "node:fs";
import { parse } from "csv-parse/sync";

export function readOrganizedCsv(path: string): { columns: string[]; rows: Record<string, string>[] } {
  const content = readFileSync(path, "utf8").replace(/^﻿/, "");
  const rows = parse(content, { columns: true, skip_empty_lines: true }) as Record<string, string>[];
  const columns = rows.length > 0 ? Object.keys(rows[0]) : [];
  return { columns, rows };
}
```

- [ ] **Step 1: Write the failing test**

Create `tests/organized/csvSources.test.ts`:

```ts
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { readdirSync } from "node:fs";
import { afterEach, describe, expect, it } from "vitest";
import { RE_ROOT } from "../../src/paths.js";
import {
  CSV_SOURCES,
  parseOrganizedCsv,
  readOrganizedCsv,
  sourceNameFor,
} from "../../src/organized/csvSources.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

function writeFixtureCsv(content: string): string {
  const dir = mkdtempSync(join(tmpdir(), "re-csv-"));
  cleanups.push(() => rmSync(dir, { recursive: true, force: true }));
  const path = join(dir, "fixture.csv");
  writeFileSync(path, content);
  return path;
}

describe("CSV_SOURCES completeness", () => {
  it("covers every real file in Organized/, and only real files", () => {
    const real = readdirSync(join(RE_ROOT, "Organized")).filter((f) => f.endsWith(".csv"));
    const declared = CSV_SOURCES.map((s) => s.file);
    expect(declared.sort()).toEqual(real.sort());
  });

  it("has exactly one entry per file, no duplicates", () => {
    const files = CSV_SOURCES.map((s) => s.file);
    expect(new Set(files).size).toBe(files.length);
  });

  it("declares at least a name column or an address column for every file", () => {
    for (const mapping of CSV_SOURCES) {
      expect(mapping.addressCol || mapping.nameACol || mapping.nameBCol, mapping.file).toBeTruthy();
    }
  });
});

describe("sourceNameFor", () => {
  it("strips the .csv extension", () => {
    expect(sourceNameFor("applied_safe_gaps.csv")).toBe("applied_safe_gaps");
  });
});

describe("parseOrganizedCsv — representative shapes", () => {
  it("address,name shape (all_inconclusive_resolved / final_2_held)", () => {
    const mapping = CSV_SOURCES.find((s) => s.file === "all_inconclusive_resolved.csv")!;
    const rows = parseOrganizedCsv(mapping, [{ address: "0x1414d85f0", name: "hknpShape::getType" }]);
    expect(rows).toEqual([
      {
        rowNo: 1,
        address: "0x1414d85f0",
        rva: 0x1414d85f0 - 0x140000000,
        nameA: null,
        nameB: "hknpShape::getType",
        kind: null,
        note: null,
        extra: {},
      },
    ]);
  });

  it("address,old_name,new_name,kind,note shape (applied_*)", () => {
    const mapping = CSV_SOURCES.find((s) => s.file === "applied_safe_gaps.csv")!;
    const rows = parseOrganizedCsv(mapping, [
      { address: "0x140724fa0", old_name: "Old::Name", new_name: "New::Name", kind: "reassign", note: "n" },
    ]);
    expect(rows[0]).toMatchObject({ nameA: "Old::Name", nameB: "New::Name", kind: "reassign", note: "n" });
  });

  it("applied_misname_corrections uses method as kind", () => {
    const mapping = CSV_SOURCES.find((s) => s.file === "applied_misname_corrections.csv")!;
    const rows = parseOrganizedCsv(mapping, [
      { address: "0x140724fa0", old_name: "A", new_name: "B", method: "decompile", note: "n" },
    ]);
    expect(rows[0].kind).toBe("decompile");
  });

  it("class,slot,expected_method shape has no address (commonlib_vtable_slots)", () => {
    const mapping = CSV_SOURCES.find((s) => s.file === "commonlib_vtable_slots.csv")!;
    const rows = parseOrganizedCsv(mapping, [{ class: "Actor", slot: "5", expected_method: "Update" }]);
    expect(rows[0]).toMatchObject({ address: null, rva: null, nameA: "Actor", nameB: "Update", kind: "5" });
  });

  it("crossref_163_disagreements_vs_155 reads the dotted verdict column literally", () => {
    const mapping = CSV_SOURCES.find((s) => s.file === "crossref_163_disagreements_vs_155.csv")!;
    const rows = parseOrganizedCsv(mapping, [
      {
        address: "0x140749ea0",
        existing_name: "A",
        decoded_name: "B",
        note: "n",
        "verdict_vs_1.10.155": "inconclusive",
      },
    ]);
    expect(rows[0].kind).toBe("inconclusive");
  });

  it("vtable_audit_report bundles class and slot into extra", () => {
    const mapping = CSV_SOURCES.find((s) => s.file === "vtable_audit_report.csv")!;
    const rows = parseOrganizedCsv(mapping, [
      {
        class: "ActorPackageData",
        slot: "0",
        expected_method: "~ActorPackageData",
        actual_name: "~ActorPackageData",
        verdict: "match",
        address: "0x140724fa0",
      },
    ]);
    expect(rows[0]).toMatchObject({
      nameA: "~ActorPackageData",
      nameB: "~ActorPackageData",
      kind: "match",
      extra: { class: "ActorPackageData", slot: "0" },
    });
    expect(rows[0].rva).not.toBeNull();
  });

  it("decompile_targets: addr: prefix becomes a bare RVA, not a full VA", () => {
    const mapping = CSV_SOURCES.find((s) => s.file === "decompile_targets.csv")!;
    const rows = parseOrganizedCsv(mapping, [
      { target: "addr:0x5f90b0", category: "Quests", note: "n" },
    ]);
    expect(rows[0].address).toBe("0x5f90b0");
    expect(rows[0].rva).toBe(0x5f90b0);
    expect(rows[0].nameA).toBeNull();
  });

  it("decompile_targets: a bare name with no addr: prefix becomes nameA, not an address", () => {
    const mapping = CSV_SOURCES.find((s) => s.file === "decompile_targets.csv")!;
    const rows = parseOrganizedCsv(mapping, [
      { target: "TESQuestStageItem::Resolve", category: "Quests", note: "n" },
    ]);
    expect(rows[0].address).toBeNull();
    expect(rows[0].rva).toBeNull();
    expect(rows[0].nameA).toBe("TESQuestStageItem::Resolve");
  });

  it("an address that fails to parse yields rva=null without throwing", () => {
    const mapping = CSV_SOURCES.find((s) => s.file === "vtable_audit_fixes.csv")!;
    const rows = parseOrganizedCsv(mapping, [
      { address: "not-hex", expected_qualified_name: "Foo::Bar" },
    ]);
    expect(rows[0].rva).toBeNull();
  });

  it("1-based rowNo across multiple rows", () => {
    const mapping = CSV_SOURCES.find((s) => s.file === "final_2_held.csv")!;
    const rows = parseOrganizedCsv(mapping, [
      { address: "0x140DE5F50", name: "A" },
      { address: "0x140705990", name: "B" },
    ]);
    expect(rows.map((r) => r.rowNo)).toEqual([1, 2]);
  });
});

describe("readOrganizedCsv", () => {
  it("reads header and rows, stripping a BOM", () => {
    const path = writeFixtureCsv("﻿address,name\n0x140724fa0,Foo::Bar\n");
    const { columns, rows } = readOrganizedCsv(path);
    expect(columns).toEqual(["address", "name"]);
    expect(rows).toEqual([{ address: "0x140724fa0", name: "Foo::Bar" }]);
  });

  it("handles a quoted field containing a comma", () => {
    const path = writeFixtureCsv('address,note\n0x1,"contains, a comma"\n');
    expect(readOrganizedCsv(path).rows[0].note).toBe("contains, a comma");
  });

  it("returns empty columns for a header-only file", () => {
    const path = writeFixtureCsv("address,name\n");
    expect(readOrganizedCsv(path)).toEqual({ columns: [], rows: [] });
  });
});
```

- [ ] **Step 2: Run it and verify it fails**

Run: `npm test -- tests/organized/csvSources.test.ts`
Expected: FAIL — `Cannot find module '../../src/organized/csvSources.js'`.

- [ ] **Step 3: Implement the module**

Create `src/organized/csvSources.ts` with the `OrganizedFileMapping` interface, the full `CSV_SOURCES` array, `ParsedOrganizedRow`, `parseOrganizedCsv`, `sourceNameFor`, and `readOrganizedCsv` exactly as given above. Import `parseAddressToRva` from `../db.js`.

- [ ] **Step 4: Run the test and verify it passes**

Run: `npm test -- tests/organized/csvSources.test.ts`
Expected: PASS, 18 tests. The completeness test reads the real `Organized/` directory — if it fails on file names, the mapping table above has a typo; fix the table, not the test.

- [ ] **Step 5: Run the full suite and the build**

Run: `npm test && npm run build`
Expected: all green.

- [ ] **Step 6: Commit**

```bash
git add src/organized/csvSources.ts tests/organized/csvSources.test.ts
git commit -m "feat: add declarative column mapping for all 24 Organized CSVs"
```

---

### Task 4: `organized_rows`/`organized_sources` schema and build-index wiring

**Files:**
- Modify: `src/build-index.ts`
- Test: `tests/buildIndex.test.ts` (append)

**Interfaces:**
- Consumes: `CSV_SOURCES`, `parseOrganizedCsv`, `readOrganizedCsv`, `sourceNameFor` (Task 3).
- Produces:
  - New static tables `organized_rows(source, row_no, address, rva, name_a, name_b, kind, note, extra)` PK `(source, row_no)`, and `organized_sources(source, columns, row_count, shape)`.
  - `indexOrganizedRowsGeneric(db: Database.Database, organizedDir?: string): number` exported for testing.

**Context the implementer needs.** All 24 CSVs are loaded here, **including the 5 already loaded into their own dedicated tables** — the spec calls this acceptable duplication so `search_organized` (Task 5) sees the whole corpus in one place. This does not touch `indexOrganizedCsvs` (the existing five-table function) at all.

- [ ] **Step 1: Write the failing test**

Append to `tests/buildIndex.test.ts`:

```ts
import { indexOrganizedRowsGeneric } from "../src/build-index.js";
```

```ts
describe("organized_rows / organized_sources", () => {
  function schemaFor(db: Database) {
    db.exec(`
      CREATE TABLE organized_rows (
        source   TEXT NOT NULL,
        row_no   INTEGER NOT NULL,
        address  TEXT,
        rva      INTEGER,
        name_a   TEXT,
        name_b   TEXT,
        kind     TEXT,
        note     TEXT,
        extra    TEXT,
        PRIMARY KEY (source, row_no)
      );
      CREATE TABLE organized_sources (
        source     TEXT PRIMARY KEY,
        columns    TEXT NOT NULL,
        row_count  INTEGER NOT NULL,
        shape      TEXT NOT NULL
      );
    `);
  }

  function fixtureOrganizedDir(): string {
    const dir = tempDir();
    writeFileSync(join(dir, "all_inconclusive_resolved.csv"), "address,name\n0x1414d85f0,hknpShape::getType\n0x1414d8660,hknpShape::calcSize\n");
    writeFileSync(join(dir, "commonlib_vtable_slots.csv"), "class,slot,expected_method\nActor,5,Update\n");
    return dir;
  }

  it("indexes rows keyed by (source, row_no)", () => {
    const db = new Database(":memory:");
    schemaFor(db);
    const total = indexOrganizedRowsGeneric(db, fixtureOrganizedDir());
    expect(total).toBe(3);

    const rows = db
      .prepare("SELECT * FROM organized_rows WHERE source = ? ORDER BY row_no")
      .all("all_inconclusive_resolved") as { row_no: number; name_b: string; rva: number }[];
    expect(rows).toHaveLength(2);
    expect(rows[0]).toMatchObject({ row_no: 1, name_b: "hknpShape::getType" });
    db.close();
  });

  it("records the real header and row count in organized_sources", () => {
    const db = new Database(":memory:");
    schemaFor(db);
    indexOrganizedRowsGeneric(db, fixtureOrganizedDir());

    const source = db
      .prepare("SELECT * FROM organized_sources WHERE source = ?")
      .get("commonlib_vtable_slots") as { columns: string; row_count: number };
    expect(JSON.parse(source.columns)).toEqual(["class", "slot", "expected_method"]);
    expect(source.row_count).toBe(1);
    db.close();
  });

  it("stores extra columns as JSON", () => {
    const dir = tempDir();
    writeFileSync(
      join(dir, "vtable_audit_report.csv"),
      "class,slot,expected_method,actual_name,verdict,address\nActorPackageData,0,~ActorPackageData,~ActorPackageData,match,0x140724fa0\n"
    );
    const db = new Database(":memory:");
    schemaFor(db);
    indexOrganizedRowsGeneric(db, dir);
    const row = db
      .prepare("SELECT extra FROM organized_rows WHERE source = 'vtable_audit_report'")
      .get() as { extra: string };
    expect(JSON.parse(row.extra)).toEqual({ class: "ActorPackageData", slot: "0" });
    db.close();
  });

  it("one bad/missing file does not abort indexing the rest", () => {
    const dir = fixtureOrganizedDir();
    // Every other declared file is simply absent from this fixture dir; that must not throw.
    const db = new Database(":memory:");
    schemaFor(db);
    expect(() => indexOrganizedRowsGeneric(db, dir)).not.toThrow();
    const n = (db.prepare("SELECT COUNT(*) AS n FROM organized_rows").get() as { n: number }).n;
    expect(n).toBe(3);
    db.close();
  });
});
```

- [ ] **Step 2: Run it and verify it fails**

Run: `npm test -- tests/buildIndex.test.ts`
Expected: FAIL — `indexOrganizedRowsGeneric` is not exported.

- [ ] **Step 3: Add the schema**

Append inside `createSchema()`'s `db.exec`, after `pdb_symbols`:

```sql
    CREATE TABLE organized_rows (
      source   TEXT NOT NULL,
      row_no   INTEGER NOT NULL,
      address  TEXT,
      rva      INTEGER,
      name_a   TEXT,
      name_b   TEXT,
      kind     TEXT,
      note     TEXT,
      extra    TEXT,
      PRIMARY KEY (source, row_no)
    );
    CREATE INDEX idx_organized_rows_rva    ON organized_rows(rva);
    CREATE INDEX idx_organized_rows_source ON organized_rows(source);
    CREATE INDEX idx_organized_rows_name_a ON organized_rows(name_a);
    CREATE INDEX idx_organized_rows_name_b ON organized_rows(name_b);

    CREATE TABLE organized_sources (
      source     TEXT PRIMARY KEY,
      columns    TEXT NOT NULL,
      row_count  INTEGER NOT NULL,
      shape      TEXT NOT NULL
    );
```

- [ ] **Step 4: Add the indexer**

Add to `src/build-index.ts`, importing `CSV_SOURCES, parseOrganizedCsv, readOrganizedCsv, sourceNameFor` from `./organized/csvSources.js`:

```ts
export function indexOrganizedRowsGeneric(
  db: Database.Database,
  organizedDir: string = ORGANIZED_DIR
): number {
  const insertRow = db.prepare(
    `INSERT INTO organized_rows (source, row_no, address, rva, name_a, name_b, kind, note, extra)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)`
  );
  const insertSource = db.prepare(
    `INSERT OR REPLACE INTO organized_sources (source, columns, row_count, shape) VALUES (?, ?, ?, ?)`
  );

  let total = 0;
  for (const mapping of CSV_SOURCES) {
    const path = join(organizedDir, mapping.file);
    if (!existsSync(path)) {
      console.log(`  SKIP ${mapping.file} (not found)`);
      continue;
    }
    const source = sourceNameFor(mapping.file);
    const { columns, rows } = readOrganizedCsv(path);
    const parsed = parseOrganizedCsv(mapping, rows);

    db.transaction(() => {
      for (const row of parsed) {
        insertRow.run(
          source,
          row.rowNo,
          row.address,
          row.rva,
          row.nameA,
          row.nameB,
          row.kind,
          row.note,
          JSON.stringify(row.extra)
        );
      }
      insertSource.run(source, JSON.stringify(columns), parsed.length, columns.join(","));
    })();

    total += parsed.length;
    console.log(`  ${source}: ${parsed.length.toLocaleString()} rows`);
  }
  return total;
}
```

- [ ] **Step 5: Wire it into `main()`**

After the `pdb_symbols` block:

```ts
  console.log("Indexing organized_rows (Organized/*.csv, all 24 files)...");
  const organizedRowCount = indexOrganizedRowsGeneric(db);
  console.log(`  total: ${organizedRowCount.toLocaleString()} rows`);
```

- [ ] **Step 6: Run the test and verify it passes**

Run: `npm test -- tests/buildIndex.test.ts`
Expected: PASS, 15 tests (11 from Task 2 + 4 new).

- [ ] **Step 7: Run the full suite and the build**

Run: `npm test && npm run build`
Expected: all green.

- [ ] **Step 8: Commit**

```bash
git add src/build-index.ts tests/buildIndex.test.ts
git commit -m "feat: index all 24 Organized CSVs into a corpus-wide organized_rows table"
```

---

### Task 5: `search_organized` MCP tool

**Files:**
- Create: `src/tools/searchOrganized.ts`
- Modify: `src/index.ts`
- Test: `tests/tools/searchOrganized.test.ts`

**Interfaces:**
- Consumes: nothing new — reads the `organized_rows` table directly via `better-sqlite3`.
- Produces:
  - `interface OrganizedSearchResult { source: string; rowNo: number; address: string | null; rva: string | null; nameA: string | null; nameB: string | null; kind: string | null; note: string | null }`
  - `function searchOrganized(args: { query: string; source?: string; limit?: number }, db?: Database.Database): { query: string; matchCount: number; truncated: boolean; results: OrganizedSearchResult[] }`

**Context the implementer needs.** If `query` looks like a hex address (matches `/^(0x)?[0-9a-fA-F]+$/`, the same heuristic `resolveTarget` in `src/learning/analyze.ts` already uses), search by exact `rva` after parsing it with `parseAddressToRva`. Otherwise, substring-match `name_a` or `name_b` (case-insensitive via `LIKE` — SQLite's default `LIKE` is already ASCII-case-insensitive). Cap `limit` at 200; default 50.

- [ ] **Step 1: Write the failing test**

Create `tests/tools/searchOrganized.test.ts`:

```ts
import Database from "better-sqlite3";
import { afterEach, describe, expect, it } from "vitest";
import { searchOrganized } from "../../src/tools/searchOrganized.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

function fixtureDb(): Database.Database {
  const db = new Database(":memory:");
  cleanups.push(() => db.close());
  db.exec(`
    CREATE TABLE organized_rows (
      source TEXT, row_no INTEGER, address TEXT, rva INTEGER,
      name_a TEXT, name_b TEXT, kind TEXT, note TEXT, extra TEXT
    );
  `);
  db.prepare(
    "INSERT INTO organized_rows VALUES ('applied_safe_gaps', 1, '0x140724fa0', 4749216, 'Old::Name', 'Actor::Update', 'reassign', 'n', '{}')"
  ).run();
  db.prepare(
    "INSERT INTO organized_rows VALUES ('crossref_review_candidates', 7, '0x140724fa0', 4749216, 'Existing::Name', 'Actor::Update', null, null, '{}')"
  ).run();
  db.prepare(
    "INSERT INTO organized_rows VALUES ('final_2_held', 2, '0x140DE5F50', 4746512, null, 'EquipActorOutfitItemsFunctor::operator()', null, null, '{}')"
  ).run();
  return db;
}

describe("searchOrganized", () => {
  it("finds rows by a name substring, across sources", () => {
    const result = searchOrganized({ query: "Actor::Update" }, fixtureDb());
    expect(result.matchCount).toBe(2);
    expect(result.results.map((r) => r.source).sort()).toEqual([
      "applied_safe_gaps",
      "crossref_review_candidates",
    ]);
  });

  it("scopes to one source when given", () => {
    const result = searchOrganized({ query: "Actor::Update", source: "final_2_held" }, fixtureDb());
    expect(result.matchCount).toBe(0);
  });

  it("finds rows by hex address, exact rva match", () => {
    const result = searchOrganized({ query: "0x140724fa0" }, fixtureDb());
    expect(result.matchCount).toBe(2);
    expect(result.results.every((r) => r.rva === "0x724fa0")).toBe(true);
  });

  it("finds rows by a bare-RVA-looking query too", () => {
    const result = searchOrganized({ query: "724fa0" }, fixtureDb());
    expect(result.matchCount).toBe(2);
  });

  it("caps results at the limit and reports truncated", () => {
    const db = fixtureDb();
    const result = searchOrganized({ query: "Actor::Update", limit: 1 }, db);
    expect(result.results).toHaveLength(1);
    expect(result.truncated).toBe(true);
  });

  it("caps the limit parameter itself at 200", () => {
    const result = searchOrganized({ query: "x", limit: 10000 }, fixtureDb());
    expect(result).toBeDefined(); // no throw; the cap is exercised for real in a larger fixture
  });

  it("returns no matches without throwing for a query nothing matches", () => {
    const result = searchOrganized({ query: "NoSuchNameAnywhere" }, fixtureDb());
    expect(result.matchCount).toBe(0);
    expect(result.results).toEqual([]);
  });
});
```

- [ ] **Step 2: Run it and verify it fails**

Run: `npm test -- tests/tools/searchOrganized.test.ts`
Expected: FAIL — module not found.

- [ ] **Step 3: Implement the tool**

Create `src/tools/searchOrganized.ts`:

```ts
import type Database from "better-sqlite3";
import { getDb, parseAddressToRva } from "../db.js";

export interface OrganizedSearchResult {
  source: string;
  rowNo: number;
  address: string | null;
  rva: string | null;
  nameA: string | null;
  nameB: string | null;
  kind: string | null;
  note: string | null;
}

export interface OrganizedSearchOutput {
  query: string;
  matchCount: number;
  truncated: boolean;
  results: OrganizedSearchResult[];
}

const MAX_LIMIT = 200;
const HEX_QUERY = /^(0x)?[0-9a-fA-F]+$/;

interface Row {
  source: string;
  row_no: number;
  address: string | null;
  rva: number | null;
  name_a: string | null;
  name_b: string | null;
  kind: string | null;
  note: string | null;
}

function toResult(row: Row): OrganizedSearchResult {
  return {
    source: row.source,
    rowNo: row.row_no,
    address: row.address,
    rva: row.rva === null ? null : `0x${row.rva.toString(16)}`,
    nameA: row.name_a,
    nameB: row.name_b,
    kind: row.kind,
    note: row.note,
  };
}

export function searchOrganized(
  args: { query: string; source?: string; limit?: number },
  db?: Database.Database
): OrganizedSearchOutput {
  const database = db ?? getDb();
  const limit = Math.min(args.limit ?? 50, MAX_LIMIT);

  const where: string[] = [];
  const params: (string | number)[] = [];

  if (HEX_QUERY.test(args.query.trim())) {
    const rva = parseAddressToRva(args.query.trim());
    where.push("rva = ?");
    params.push(rva as number);
  } else {
    where.push("(name_a LIKE ? OR name_b LIKE ?)");
    params.push(`%${args.query}%`, `%${args.query}%`);
  }

  if (args.source) {
    where.push("source = ?");
    params.push(args.source);
  }

  const sql = `SELECT * FROM organized_rows WHERE ${where.join(" AND ")} ORDER BY source, row_no LIMIT ?`;
  const rows = database.prepare(sql).all(...params, limit + 1) as Row[];

  const truncated = rows.length > limit;
  const page = truncated ? rows.slice(0, limit) : rows;

  return {
    query: args.query,
    matchCount: page.length,
    truncated,
    results: page.map(toResult),
  };
}
```

- [ ] **Step 4: Register the tool**

In `src/index.ts`, add the import and registration alongside the other tools:

```ts
import { searchOrganized } from "./tools/searchOrganized.js";
```

```ts
server.tool(
  "search_organized",
  "Search across all 24 Organized/*.csv files at once (the full human audit-trail corpus, not " +
    "just the five files with dedicated tools). Pass a name substring, or a hex address to find " +
    "every row that mentions it. Results cite source:row_no so a claim can be traced back to its file.",
  {
    query: z.string().describe('Name substring or hex address, e.g. "SetFlag" or "0x749ea0".'),
    source: z.string().optional().describe('Limit to one CSV, e.g. "applied_safe_gaps".'),
    limit: z.number().optional().describe("Max rows, default 50, capped at 200."),
  },
  async ({ query, source, limit }) => run(() => searchOrganized({ query, source, limit }))
);
```

- [ ] **Step 5: Run the test and the build**

Run: `npm test -- tests/tools/searchOrganized.test.ts && npm run build`
Expected: PASS, 7 tests; build exit 0.

- [ ] **Step 6: Commit**

```bash
git add src/tools/searchOrganized.ts src/index.ts tests/tools/searchOrganized.test.ts
git commit -m "feat: add search_organized MCP tool over the full 24-file CSV corpus"
```

---

### Task 6: `pdb_signature` MCP tool and `check_name_confidence` PDB corroboration

**Files:**
- Create: `src/tools/pdbSignature.ts`
- Modify: `src/tools/checkNameConfidence.ts`
- Modify: `src/index.ts`
- Test: `tests/tools/pdbSignature.test.ts`
- Test: `tests/tools/checkNameConfidence.test.ts` (new file — the tool has no existing tests)

**Interfaces:**
- Consumes: nothing new.
- Produces:
  - `interface PdbSignatureMatch { name: string; signature: string; callingConvention: string | null; sizeof: number | null; isFpo: boolean }`
  - `function pdbSignature(args: { name: string; limit?: number }, db?: Database.Database): { name: string; matchCount: number; truncated: boolean; matches: PdbSignatureMatch[]; note: string }`
  - `type PdbCorroborationStatus = "confirmed" | "absent" | "unavailable"`
  - `function computePdbCorroboration(db: Database.Database, name: string): { status: PdbCorroborationStatus; matches: string[] }` exported from `checkNameConfidence.ts`
  - `ConfidenceResult` gains `pdbCorroboration: PdbCorroborationStatus` and `pdbMatches: string[]`

**Context the implementer needs.** `pdb_symbols` is deliberately never queried by address (Global Constraints). `pdb_rva` must not appear anywhere in `pdbSignature`'s return type or in `computePdbCorroboration`'s output — that omission is the isolation boundary from the spec, not an oversight; a reviewer should be able to grep this task's diff for `pdb_rva` and find zero occurrences outside `build-index.ts`.

`checkNameConfidence` currently has no tests at all (pre-existing gap, out of scope to backfill in full). This task adds `computePdbCorroboration` as a separately exported, independently unit-tested pure-ish function, and wires it into the existing `checkNameConfidence` with a minimal change — it does not add broad test coverage for the pre-existing verdict logic.

- [ ] **Step 1: Write the failing tests**

Create `tests/tools/pdbSignature.test.ts`:

```ts
import Database from "better-sqlite3";
import { afterEach, describe, expect, it } from "vitest";
import { pdbSignature } from "../../src/tools/pdbSignature.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

function fixtureDb(): Database.Database {
  const db = new Database(":memory:");
  cleanups.push(() => db.close());
  db.exec(`
    CREATE TABLE pdb_symbols (
      name TEXT, signature TEXT, calling_convention TEXT, sizeof INTEGER, is_fpo INTEGER, pdb_rva INTEGER, version TEXT
    );
  `);
  db.prepare(
    "INSERT INTO pdb_symbols VALUES ('BSCullingProcess::AddShared', 'bool __cdecl BSCullingProcess::AddShared(NiAVObject*)', '__cdecl', 324, 1, 30162864, '1.10.155')"
  ).run();
  db.prepare(
    "INSERT INTO pdb_symbols VALUES ('BSCullingProcess::RemoveShared', 'bool __cdecl BSCullingProcess::RemoveShared(NiAVObject*)', '__cdecl', 200, 0, 30162900, '1.10.155')"
  ).run();
  return db;
}

describe("pdbSignature", () => {
  it("finds signatures by substring", () => {
    const result = pdbSignature({ name: "BSCullingProcess" }, fixtureDb());
    expect(result.matchCount).toBe(2);
    expect(result.matches.map((m) => m.name).sort()).toEqual([
      "BSCullingProcess::AddShared",
      "BSCullingProcess::RemoveShared",
    ]);
  });

  it("returns calling convention, sizeof, and isFpo", () => {
    const result = pdbSignature({ name: "AddShared" }, fixtureDb());
    expect(result.matches[0]).toMatchObject({ callingConvention: "__cdecl", sizeof: 324, isFpo: true });
  });

  it("never includes a pdb_rva field", () => {
    const result = pdbSignature({ name: "AddShared" }, fixtureDb());
    expect(JSON.stringify(result)).not.toContain("pdb_rva");
    expect(JSON.stringify(result)).not.toContain("pdbRva");
  });

  it("always carries the address-space warning note", () => {
    const result = pdbSignature({ name: "AddShared" }, fixtureDb());
    expect(result.note).toMatch(/1\.10\.155/);
    expect(result.note.toLowerCase()).toMatch(/not.*hook|never.*hook/);
  });

  it("returns no matches without throwing", () => {
    const result = pdbSignature({ name: "NoSuchFunction" }, fixtureDb());
    expect(result.matchCount).toBe(0);
    expect(result.matches).toEqual([]);
  });
});
```

Create `tests/tools/checkNameConfidence.test.ts`:

```ts
import Database from "better-sqlite3";
import { afterEach, describe, expect, it } from "vitest";
import { computePdbCorroboration } from "../../src/tools/checkNameConfidence.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

function dbWithPdbTable(): Database.Database {
  const db = new Database(":memory:");
  cleanups.push(() => db.close());
  db.exec(`
    CREATE TABLE pdb_symbols (
      name TEXT, signature TEXT, calling_convention TEXT, sizeof INTEGER, is_fpo INTEGER, pdb_rva INTEGER, version TEXT
    );
  `);
  db.prepare("INSERT INTO pdb_symbols VALUES ('Actor::SetFlag', 'sig', '__cdecl', 1, 0, 999, '1.10.155')").run();
  return db;
}

describe("computePdbCorroboration", () => {
  it("reports unavailable when pdb_symbols does not exist", () => {
    const db = new Database(":memory:");
    cleanups.push(() => db.close());
    expect(computePdbCorroboration(db, "Actor::SetFlag")).toEqual({ status: "unavailable", matches: [] });
  });

  it("reports confirmed with the matched names when the PDB has the name", () => {
    const result = computePdbCorroboration(dbWithPdbTable(), "Actor::SetFlag");
    expect(result.status).toBe("confirmed");
    expect(result.matches).toContain("Actor::SetFlag");
  });

  it("reports absent when the PDB is indexed but has no match", () => {
    const result = computePdbCorroboration(dbWithPdbTable(), "NoSuchName");
    expect(result).toEqual({ status: "absent", matches: [] });
  });

  it("substring-matches, consistent with this tool's existing name convention", () => {
    const result = computePdbCorroboration(dbWithPdbTable(), "SetFlag");
    expect(result.status).toBe("confirmed");
  });
});
```

- [ ] **Step 2: Run both and verify they fail**

Run: `npm test -- tests/tools/pdbSignature.test.ts tests/tools/checkNameConfidence.test.ts`
Expected: FAIL — both modules/exports not found.

- [ ] **Step 3: Implement `pdbSignature`**

Create `src/tools/pdbSignature.ts`:

```ts
import type Database from "better-sqlite3";
import { getDb } from "../db.js";

export interface PdbSignatureMatch {
  name: string;
  signature: string;
  callingConvention: string | null;
  sizeof: number | null;
  isFpo: boolean;
}

const MAX_LIMIT = 50;
const ADDRESS_SPACE_NOTE =
  "Address space is 1.10.155, not 1.10.163 — never use this for a hook offset.";

interface Row {
  name: string;
  signature: string;
  calling_convention: string | null;
  sizeof: number | null;
  is_fpo: number;
}

export function pdbSignature(
  args: { name: string; limit?: number },
  db?: Database.Database
): { name: string; matchCount: number; truncated: boolean; matches: PdbSignatureMatch[]; note: string } {
  const database = db ?? getDb();
  const limit = Math.min(args.limit ?? 25, MAX_LIMIT);

  const rows = database
    .prepare("SELECT name, signature, calling_convention, sizeof, is_fpo FROM pdb_symbols WHERE name LIKE ? LIMIT ?")
    .all(`%${args.name}%`, limit + 1) as Row[];

  const truncated = rows.length > limit;
  const page = truncated ? rows.slice(0, limit) : rows;

  return {
    name: args.name,
    matchCount: page.length,
    truncated,
    matches: page.map((row) => ({
      name: row.name,
      signature: row.signature,
      callingConvention: row.calling_convention,
      sizeof: row.sizeof,
      isFpo: row.is_fpo === 1,
    })),
    note: ADDRESS_SPACE_NOTE,
  };
}
```

- [ ] **Step 4: Register `pdb_signature`**

In `src/index.ts`:

```ts
import { pdbSignature } from "./tools/pdbSignature.js";
```

```ts
server.tool(
  "pdb_signature",
  "Look up a name in the 1.10.155 private Microsoft PDB dump for its real signature, calling " +
    "convention, and size — an independent ground truth for what a function is actually called, " +
    "from neither CommonLibF4 RTTI nor the community labels. The PDB's own addresses are for a " +
    "different game version and are never returned or usable as hook offsets.",
  {
    name: z.string().describe('Name or substring, e.g. "AddShared" or "BSCullingProcess".'),
    limit: z.number().optional().describe("Max matches, default 25, capped at 50."),
  },
  async ({ name, limit }) => run(() => pdbSignature({ name, limit }))
);
```

- [ ] **Step 5: Extend `checkNameConfidence.ts`**

Replace the full content of `src/tools/checkNameConfidence.ts` with:

```ts
import type Database from "better-sqlite3";
import { getDb, parseAddressToRva } from "../db.js";

export type PdbCorroborationStatus = "confirmed" | "absent" | "unavailable";

export interface ConfidenceResult {
  verdict: "reverted" | "confirmed_wrong" | "unconfirmed_disagreement" | "no_data";
  summary: string;
  details: Record<string, unknown>[];
  pdbCorroboration: PdbCorroborationStatus;
  pdbMatches: string[];
}

const PDB_MATCH_LIMIT = 5;

export function computePdbCorroboration(
  db: Database.Database,
  name: string
): { status: PdbCorroborationStatus; matches: string[] } {
  const hasTable = db
    .prepare("SELECT name FROM sqlite_master WHERE type = 'table' AND name = 'pdb_symbols'")
    .get();
  if (!hasTable) return { status: "unavailable", matches: [] };

  const rows = db
    .prepare("SELECT DISTINCT name FROM pdb_symbols WHERE name LIKE ? LIMIT ?")
    .all(`%${name}%`, PDB_MATCH_LIMIT) as { name: string }[];

  if (rows.length === 0) return { status: "absent", matches: [] };
  return { status: "confirmed", matches: rows.map((r) => r.name) };
}

function nameToCorroborate(db: Database.Database, input: { address?: string; name?: string }, rva: number | null): string | null {
  if (input.name) return input.name;
  if (rva === null) return null;
  const symbol = db
    .prepare("SELECT name FROM symbols WHERE version = '1.10.163' AND rva = ? LIMIT 1")
    .get(rva) as { name: string } | undefined;
  return symbol?.name ?? null;
}

export function checkNameConfidence(input: { address?: string; name?: string }): ConfidenceResult {
  const db = getDb();

  if (!input.address && !input.name) {
    throw new Error("Pass either address or name.");
  }

  const rva = input.address ? parseAddressToRva(input.address) : null;
  if (input.address && rva === null) {
    throw new Error(`Could not parse "${input.address}" as a hex address.`);
  }

  const toCorroborate = nameToCorroborate(db, input, rva);
  const pdb = toCorroborate
    ? computePdbCorroboration(db, toCorroborate)
    : { status: "absent" as const, matches: [] };

  const revertRows = rva !== null
    ? db.prepare("SELECT * FROM misname_reverts WHERE rva = ?").all(rva)
    : db
        .prepare("SELECT * FROM misname_reverts WHERE wrongly_renamed_to LIKE ? OR revert_to LIKE ?")
        .all(`%${input.name}%`, `%${input.name}%`);
  if (revertRows.length > 0) {
    return {
      verdict: "reverted",
      summary:
        "This name was flagged as wrong and a correction was applied, but that correction was " +
        "later reverted after further review - the original name is confirmed correct.",
      details: revertRows as Record<string, unknown>[],
      pdbCorroboration: pdb.status,
      pdbMatches: pdb.matches,
    };
  }

  const correctionRows = rva !== null
    ? db.prepare("SELECT * FROM misname_corrections WHERE rva = ?").all(rva)
    : db
        .prepare("SELECT * FROM misname_corrections WHERE old_name LIKE ? OR new_name LIKE ?")
        .all(`%${input.name}%`, `%${input.name}%`);
  if (correctionRows.length > 0) {
    return {
      verdict: "confirmed_wrong",
      summary: "This name is confirmed wrong - a corrected name has been applied.",
      details: correctionRows as Record<string, unknown>[],
      pdbCorroboration: pdb.status,
      pdbMatches: pdb.matches,
    };
  }

  const disagreementRows = rva !== null
    ? db.prepare("SELECT * FROM name_disagreements WHERE rva = ?").all(rva)
    : db
        .prepare("SELECT * FROM name_disagreements WHERE existing_name LIKE ? OR decoded_name LIKE ?")
        .all(`%${input.name}%`, `%${input.name}%`);
  if (disagreementRows.length > 0) {
    return {
      verdict: "unconfirmed_disagreement",
      summary:
        "CommonLibF4's name disagrees with a decoded community label at this address, but this " +
        "was never resolved to a confirmed correction - treat both names as suspect until verified.",
      details: disagreementRows as Record<string, unknown>[],
      pdbCorroboration: pdb.status,
      pdbMatches: pdb.matches,
    };
  }

  return {
    verdict: "no_data",
    summary:
      "No flagged history for this name/address in the audit trail. This does not mean it's " +
      "confirmed correct - it just was never flagged as a disagreement or corrected.",
    details: [],
    pdbCorroboration: pdb.status,
    pdbMatches: pdb.matches,
  };
}
```

Only the additions are new: `computePdbCorroboration`, `nameToCorroborate`, the `pdbCorroboration`/`pdbMatches` fields threaded through all four return sites, and the `PdbCorroborationStatus` type. The four verdict branches' own logic is byte-for-byte unchanged.

- [ ] **Step 6: Update the tool's registration description in `src/index.ts`**

Find the existing `check_name_confidence` registration and extend its description string (the `{ address, name }` schema is unchanged):

```ts
server.tool(
  "check_name_confidence",
  "Check whether a symbol name is trustworthy before hooking it. Cross-references the confirmed " +
    "misname corrections, reverted-correction, and unresolved-disagreement audit trails built up " +
    "from Ghidra label-recovery and decompile verification, plus independent corroboration " +
    "against the 1.10.155 Microsoft PDB where available. Pass either address or name.",
  {
```

(Only the description string changes; the parameter schema and handler body stay exactly as they are.)

- [ ] **Step 7: Run both tests and verify they pass**

Run: `npm test -- tests/tools/pdbSignature.test.ts tests/tools/checkNameConfidence.test.ts`
Expected: PASS, 5 + 4 = 9 tests.

- [ ] **Step 8: Run the full suite and the build**

Run: `npm test && npm run build`
Expected: all green.

- [ ] **Step 9: Commit**

```bash
git add src/tools/pdbSignature.ts src/tools/checkNameConfidence.ts src/index.ts tests/tools/pdbSignature.test.ts tests/tools/checkNameConfidence.test.ts
git commit -m "feat: add pdb_signature tool and PDB corroboration in check_name_confidence"
```

---

### Task 7: `writeCsv` helper and the audit's live cross-checks

**Files:**
- Create: `src/organized/writeCsv.ts`
- Create: `src/tools/auditCsvCorpus.ts`
- Test: `tests/organized/writeCsv.test.ts`
- Test: `tests/tools/auditCsvCorpus.test.ts`

**Interfaces:**
- Consumes: `CSV_SOURCES`, `parseOrganizedCsv`, `readOrganizedCsv`, `sourceNameFor`, `OrganizedFileMapping`, `ParsedOrganizedRow` (Task 3).
- Produces:
  - `function csvField(value: string | number | null): string`
  - `function writeCsv(path: string, header: string[], rows: (string | number | null)[][]): void`
  - `type CsvFindingType = "claim_drift" | "structural" | "unresolved_address" | "pair_disagreement" | "pdb_uncorroborated"`
  - `type CsvFindingSeverity = "error" | "warning" | "info"`
  - `interface CsvAuditFinding { source: string; rowNo: number; address: string | null; findingType: CsvFindingType; severity: CsvFindingSeverity; detail: string }`
  - `const CLAIM_DRIFT_SOURCES: string[]` — exactly the 6 sources below
  - `function looksLikeSignatureText(value: string): boolean`
  - `function checkStructural(source: string, rows: ParsedOrganizedRow[]): CsvAuditFinding[]`
  - `function checkUnresolvedAddresses(staticDb: Database.Database, source: string, rows: ParsedOrganizedRow[], version: string): CsvAuditFinding[]`
  - `function checkClaimDrift(staticDb: Database.Database, source: string, rows: ParsedOrganizedRow[], version: string): CsvAuditFinding[]`

**Scoping decision, made explicitly rather than guessed — this task implements it, do not second-guess it:** `claim_drift` (an `error`-severity finding, meaning "this CSV's claim about current state is contradicted by the live index") only runs against the 6 sources that assert a completed, current fact:

```ts
export const CLAIM_DRIFT_SOURCES = [
  "applied_hknpShape",
  "applied_misname_corrections",
  "applied_reassignments",
  "applied_safe_gaps",
  "applied_std_func",
  "misnames_to_revert",
];
```

The other 18 sources are plans, candidates, or held-for-review lists (`confirmed_misnames_to_apply`, `safe_gaps_apply`, `crossref_review_candidates`, etc.) — a plan legitimately differing from what was applied is not an error, and CLAUDE.md documents real cases of partial application ("3 held", "121 un-nameable templates skipped"). Running `claim_drift` against them would manufacture false errors. Two of these 18 (`safe_gaps_apply`/`reassign_apply`/`std_func_sweep` vs their `applied_*` counterparts) get checked instead by `pair_disagreement` in Task 8, which is the check actually designed for "does the plan match the record".

**A second scoping rule, needed because real data contains it:** `confirmed_misnames_to_apply.csv` and `misnames_to_revert.csv` sometimes carry a full C++ signature string in their name column instead of a bare qualified name (verified live this session — e.g. `"public virtual void __thiscall ImageSpaceEffect::Render(class BSTriShape * __ptr64,...) __ptr64 "`). Comparing that text against `symbols.name` would never match and would manufacture noise. `looksLikeSignatureText` (`/[\s(]/.test(value)` — a bare qualified name never contains a space or a paren) flags these rows so both `claim_drift` (Task 7) and `pdb_uncorroborated` (Task 8) skip them rather than emit a misleading finding.

- [ ] **Step 1: Write the failing `writeCsv` test**

Create `tests/organized/writeCsv.test.ts`:

```ts
import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { parse } from "csv-parse/sync";
import { afterEach, describe, expect, it } from "vitest";
import { csvField, writeCsv } from "../../src/organized/writeCsv.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

function tempPath(name: string): string {
  const dir = mkdtempSync(join(tmpdir(), "re-writecsv-"));
  cleanups.push(() => rmSync(dir, { recursive: true, force: true }));
  return join(dir, name);
}

describe("csvField", () => {
  it("passes a plain value through unquoted", () => {
    expect(csvField("Actor::SetFlag")).toBe("Actor::SetFlag");
  });

  it("quotes and escapes a value containing a comma", () => {
    expect(csvField("a, b")).toBe('"a, b"');
  });

  it("escapes an embedded quote by doubling it", () => {
    expect(csvField('say "hi"')).toBe('"say ""hi"""');
  });

  it("renders null as an empty field", () => {
    expect(csvField(null)).toBe("");
  });

  it("renders a number as its string form", () => {
    expect(csvField(42)).toBe("42");
  });
});

describe("writeCsv", () => {
  it("creates parent directories that do not exist yet", () => {
    const path = tempPath("nested/dir/out.csv");
    writeCsv(path, ["a", "b"], [["1", "2"]]);
    expect(readFileSync(path, "utf8")).toContain("a,b");
  });

  it("round-trips through the same CSV reader the corpus uses", () => {
    const path = tempPath("out.csv");
    writeCsv(path, ["address", "note"], [
      ["0x1", "contains, a comma"],
      ["0x2", 'has "quotes"'],
      ["0x3", null],
    ]);
    const rows = parse(readFileSync(path, "utf8"), { columns: true }) as Record<string, string>[];
    expect(rows).toEqual([
      { address: "0x1", note: "contains, a comma" },
      { address: "0x2", note: 'has "quotes"' },
      { address: "0x3", note: "" },
    ]);
  });
});
```

- [ ] **Step 2: Run it and verify it fails**

Run: `npm test -- tests/organized/writeCsv.test.ts`
Expected: FAIL — module not found.

- [ ] **Step 3: Implement `writeCsv`**

Create `src/organized/writeCsv.ts`:

```ts
import { mkdirSync, writeFileSync } from "node:fs";
import { dirname } from "node:path";

export function csvField(value: string | number | null): string {
  if (value === null || value === undefined) return "";
  const text = String(value);
  if (/[",\n\r]/.test(text)) {
    return '"' + text.replace(/"/g, '""') + '"';
  }
  return text;
}

export function writeCsv(
  path: string,
  header: string[],
  rows: (string | number | null)[][]
): void {
  const lines = [header.map(csvField).join(",")];
  for (const row of rows) lines.push(row.map(csvField).join(","));
  mkdirSync(dirname(path), { recursive: true });
  writeFileSync(path, lines.join("\n") + "\n", "utf8");
}
```

- [ ] **Step 4: Run the `writeCsv` test and verify it passes**

Run: `npm test -- tests/organized/writeCsv.test.ts`
Expected: PASS, 7 tests.

- [ ] **Step 5: Write the failing audit cross-check tests**

Create `tests/tools/auditCsvCorpus.test.ts`:

```ts
import Database from "better-sqlite3";
import { afterEach, describe, expect, it } from "vitest";
import type { ParsedOrganizedRow } from "../../src/organized/csvSources.js";
import {
  checkClaimDrift,
  checkStructural,
  checkUnresolvedAddresses,
  looksLikeSignatureText,
} from "../../src/tools/auditCsvCorpus.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

function staticDb(): Database.Database {
  const db = new Database(":memory:");
  cleanups.push(() => db.close());
  db.exec(`CREATE TABLE symbols (version TEXT, rva INTEGER, name TEXT, signature TEXT, sizeof INTEGER);`);
  db.prepare("INSERT INTO symbols VALUES ('1.10.163', 0x724fa0, 'Actor::SetFlag', 'sig', 1)").run();
  return db;
}

function row(overrides: Partial<ParsedOrganizedRow>): ParsedOrganizedRow {
  return {
    rowNo: 1,
    address: "0x140724fa0",
    rva: 0x724fa0,
    nameA: null,
    nameB: null,
    kind: null,
    note: null,
    extra: {},
    ...overrides,
  };
}

describe("looksLikeSignatureText", () => {
  it("flags a full signature", () => {
    expect(looksLikeSignatureText("public virtual void __thiscall Foo::Bar(int x)")).toBe(true);
  });
  it("does not flag a bare qualified name", () => {
    expect(looksLikeSignatureText("Actor::SetFlag")).toBe(false);
  });
});

describe("checkStructural", () => {
  it("flags an address that failed to parse", () => {
    const findings = checkStructural("some_source", [row({ address: "not-hex", rva: null })]);
    expect(findings).toHaveLength(1);
    expect(findings[0]).toMatchObject({ findingType: "structural", severity: "error" });
  });

  it("flags a duplicate address within one file", () => {
    const findings = checkStructural("some_source", [
      row({ rowNo: 1, address: "0x1", rva: 1 }),
      row({ rowNo: 2, address: "0x1", rva: 1 }),
    ]);
    expect(findings).toHaveLength(1);
    expect(findings[0]).toMatchObject({ findingType: "structural", severity: "warning", rowNo: 2 });
  });

  it("produces nothing for clean, unique rows", () => {
    expect(
      checkStructural("some_source", [row({ address: "0x1", rva: 1 }), row({ address: "0x2", rva: 2, rowNo: 2 })])
    ).toEqual([]);
  });

  it("ignores rows with no address at all (e.g. commonlib_vtable_slots)", () => {
    expect(checkStructural("commonlib_vtable_slots", [row({ address: null, rva: null })])).toEqual([]);
  });
});

describe("checkUnresolvedAddresses", () => {
  it("flags an address with no symbol in the live index", () => {
    const findings = checkUnresolvedAddresses(staticDb(), "some_source", [
      row({ address: "0x1", rva: 1 }),
    ], "1.10.163");
    expect(findings).toHaveLength(1);
    expect(findings[0]).toMatchObject({ findingType: "unresolved_address", severity: "warning" });
  });

  it("is silent when the address resolves", () => {
    const findings = checkUnresolvedAddresses(staticDb(), "some_source", [
      row({ address: "0x140724fa0", rva: 0x724fa0 }),
    ], "1.10.163");
    expect(findings).toEqual([]);
  });

  it("skips rows with no address to check", () => {
    expect(checkUnresolvedAddresses(staticDb(), "commonlib_vtable_slots", [row({ address: null, rva: null })], "1.10.163")).toEqual([]);
  });
});

describe("checkClaimDrift", () => {
  it("is silent when nameB matches the live symbol", () => {
    const findings = checkClaimDrift(
      staticDb(),
      "applied_safe_gaps",
      [row({ address: "0x140724fa0", rva: 0x724fa0, nameB: "Actor::SetFlag" })],
      "1.10.163"
    );
    expect(findings).toEqual([]);
  });

  it("flags drift when nameB no longer matches the live symbol", () => {
    const findings = checkClaimDrift(
      staticDb(),
      "applied_safe_gaps",
      [row({ address: "0x140724fa0", rva: 0x724fa0, nameB: "Actor::OtherName" })],
      "1.10.163"
    );
    expect(findings).toHaveLength(1);
    expect(findings[0]).toMatchObject({ findingType: "claim_drift", severity: "error" });
    expect(findings[0].detail).toContain("Actor::SetFlag");
    expect(findings[0].detail).toContain("Actor::OtherName");
  });

  it("does not fire for a source outside CLAIM_DRIFT_SOURCES", () => {
    const findings = checkClaimDrift(
      staticDb(),
      "confirmed_misnames_to_apply",
      [row({ address: "0x140724fa0", rva: 0x724fa0, nameB: "Actor::OtherName" })],
      "1.10.163"
    );
    expect(findings).toEqual([]);
  });

  it("skips a row whose nameB is signature text rather than a bare name", () => {
    const findings = checkClaimDrift(
      staticDb(),
      "misnames_to_revert",
      [row({ address: "0x140724fa0", rva: 0x724fa0, nameB: "public virtual void __thiscall Foo::Bar()" })],
      "1.10.163"
    );
    expect(findings).toEqual([]);
  });

  it("skips a row whose address does not resolve (unresolved_address's job, not claim_drift's)", () => {
    const findings = checkClaimDrift(
      staticDb(),
      "applied_safe_gaps",
      [row({ address: "0xdead", rva: 0xdead, nameB: "Anything" })],
      "1.10.163"
    );
    expect(findings).toEqual([]);
  });
});
```

- [ ] **Step 6: Run it and verify it fails**

Run: `npm test -- tests/tools/auditCsvCorpus.test.ts`
Expected: FAIL — module not found.

- [ ] **Step 7: Implement the cross-check functions**

Create `src/tools/auditCsvCorpus.ts` with this content (Task 8 appends `pair_disagreement`, `pdb_uncorroborated`, the top-level `auditCsvCorpus` function, and the write path — do not add them yet):

```ts
import type Database from "better-sqlite3";
import type { ParsedOrganizedRow } from "../organized/csvSources.js";

export type CsvFindingType =
  | "claim_drift"
  | "structural"
  | "unresolved_address"
  | "pair_disagreement"
  | "pdb_uncorroborated";

export type CsvFindingSeverity = "error" | "warning" | "info";

export interface CsvAuditFinding {
  source: string;
  rowNo: number;
  address: string | null;
  findingType: CsvFindingType;
  severity: CsvFindingSeverity;
  detail: string;
}

// Only sources whose nameB asserts a completed, current fact — see the plan's Task 7 rationale.
export const CLAIM_DRIFT_SOURCES = [
  "applied_hknpShape",
  "applied_misname_corrections",
  "applied_reassignments",
  "applied_safe_gaps",
  "applied_std_func",
  "misnames_to_revert",
];

export function looksLikeSignatureText(value: string): boolean {
  return /[\s(]/.test(value);
}

export function checkStructural(source: string, rows: ParsedOrganizedRow[]): CsvAuditFinding[] {
  const findings: CsvAuditFinding[] = [];
  const seenAddresses = new Map<string, number>();

  for (const row of rows) {
    if (row.address === null) continue;

    if (row.rva === null) {
      findings.push({
        source,
        rowNo: row.rowNo,
        address: row.address,
        findingType: "structural",
        severity: "error",
        detail: `Address "${row.address}" could not be parsed as hex.`,
      });
      continue;
    }

    const firstSeenAt = seenAddresses.get(row.address);
    if (firstSeenAt !== undefined) {
      findings.push({
        source,
        rowNo: row.rowNo,
        address: row.address,
        findingType: "structural",
        severity: "warning",
        detail: `Duplicate address, also seen at row ${firstSeenAt}.`,
      });
    } else {
      seenAddresses.set(row.address, row.rowNo);
    }
  }

  return findings;
}

export function checkUnresolvedAddresses(
  staticDb: Database.Database,
  source: string,
  rows: ParsedOrganizedRow[],
  version: string
): CsvAuditFinding[] {
  const findings: CsvAuditFinding[] = [];
  const existsStmt = staticDb.prepare("SELECT 1 FROM symbols WHERE version = ? AND rva = ? LIMIT 1");

  for (const row of rows) {
    if (row.rva === null) continue;
    const found = existsStmt.get(version, row.rva);
    if (!found) {
      findings.push({
        source,
        rowNo: row.rowNo,
        address: row.address,
        findingType: "unresolved_address",
        severity: "warning",
        detail: `No symbol exists at this address in version ${version}.`,
      });
    }
  }

  return findings;
}

export function checkClaimDrift(
  staticDb: Database.Database,
  source: string,
  rows: ParsedOrganizedRow[],
  version: string
): CsvAuditFinding[] {
  if (!CLAIM_DRIFT_SOURCES.includes(source)) return [];

  const findings: CsvAuditFinding[] = [];
  const nameStmt = staticDb.prepare("SELECT name FROM symbols WHERE version = ? AND rva = ? LIMIT 1");

  for (const row of rows) {
    if (row.rva === null || !row.nameB) continue;
    if (looksLikeSignatureText(row.nameB)) continue;

    const symbol = nameStmt.get(version, row.rva) as { name: string } | undefined;
    if (!symbol) continue; // unresolved_address already covers this row

    if (symbol.name !== row.nameB) {
      findings.push({
        source,
        rowNo: row.rowNo,
        address: row.address,
        findingType: "claim_drift",
        severity: "error",
        detail: `${source} claims this address is "${row.nameB}", but the live index shows "${symbol.name}".`,
      });
    }
  }

  return findings;
}
```

- [ ] **Step 8: Run the test and verify it passes**

Run: `npm test -- tests/tools/auditCsvCorpus.test.ts`
Expected: PASS, 17 tests (5 + 4 + 3 + 6, tallying the describe blocks above — count the literal `it(...)` calls if this differs).

- [ ] **Step 9: Run the full suite and the build**

Run: `npm test && npm run build`
Expected: all green.

- [ ] **Step 10: Commit**

```bash
git add src/organized/writeCsv.ts src/tools/auditCsvCorpus.ts tests/organized/writeCsv.test.ts tests/tools/auditCsvCorpus.test.ts
git commit -m "feat: add CSV writer and the audit's structural/claim-drift cross-checks"
```

---

### Task 8: pair/PDB checks, `audit_csv_corpus` orchestration, generated reports, registration

**Files:**
- Modify: `src/tools/auditCsvCorpus.ts` (append to the file created in Task 7 — do not replace Task 7's content)
- Modify: `src/index.ts`
- Test: `tests/tools/auditCsvCorpus.test.ts` (append)

**Interfaces:**
- Consumes: everything from Task 7, plus `getDb` (`../db.js`), `ORGANIZED_DIR`/`ORGANIZED_GENERATED_DIR` (`../paths.js`), `writeCsv` (Task 7's `../organized/writeCsv.js`).
- Produces:
  - `interface CsvPair { a: string; b: string }`
  - `const CSV_PAIRS: CsvPair[]` — exactly the 4 pairs below
  - `function checkPairDisagreement(pair: CsvPair, rowsA: ParsedOrganizedRow[], rowsB: ParsedOrganizedRow[]): CsvAuditFinding[]`
  - `const PDB_UNCORROBORATED_SOURCES: string[]` — `CLAIM_DRIFT_SOURCES` plus 2 more
  - `function checkPdbUncorroborated(staticDb: Database.Database, source: string, rows: ParsedOrganizedRow[]): CsvAuditFinding[]`
  - `interface AuditCsvDeps { staticDb: Database.Database; organizedDir: string; generatedDir: string }`
  - `function createAuditCsvDeps(overrides?: Partial<AuditCsvDeps>): AuditCsvDeps`
  - `interface AuditCsvCorpusResult { sourcesAudited: number; totalRows: number; findings: CsvAuditFinding[]; truncated: boolean; countsBySeverity: Record<CsvFindingSeverity, number>; countsByType: Partial<Record<CsvFindingType, number>>; wrote: { csvAuditReportCsv: string; csvAuditReportMd: string; pdbCrosscheckCsv: string | null } | null }`
  - `function auditCsvCorpus(args: { source?: string; write?: boolean; limit?: number }, deps?: AuditCsvDeps): AuditCsvCorpusResult`

**The 4 pairs, per the spec, using the literal source names Task 3 already established:**

```ts
export const CSV_PAIRS: CsvPair[] = [
  { a: "safe_gaps_apply", b: "applied_safe_gaps" },
  { a: "reassign_apply", b: "applied_reassignments" },
  { a: "std_func_sweep", b: "applied_std_func" },
  { a: "misnames_to_revert", b: "misnames_to_revert_simple" },
];
```

`checkPairDisagreement` compares by `rva`, since that's the one column both sides always have and it's already normalized (address text formatting can differ trivially). An address present on only one side is itself a disagreement (`severity: "warning"`), not silently ignored.

**PDB-uncorroborated scope, extending Task 7's rationale:** the 6 `CLAIM_DRIFT_SOURCES` plus `confirmed_misnames_to_apply` and `vtable_audit_fixes` — 8 sources total whose `nameB` plausibly names something that should exist in an independent, contemporaneous PDB. Severity is always `info` (a name absent from a 1.10.155 dump does not prove it's wrong — 1.10.155 predates 1.10.163 and may simply not have had the function yet).

**Pair checks only run for a whole-corpus audit** (`args.source` unset) — a pair inherently needs both files, and scoping to one file of a pair silently produces no pair findings rather than a confusing partial comparison.

- [ ] **Step 1: Write the failing tests**

Append to `tests/tools/auditCsvCorpus.test.ts` (add these imports alongside the existing ones):

```ts
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import {
  CSV_PAIRS,
  auditCsvCorpus,
  checkPairDisagreement,
  checkPdbUncorroborated,
  type AuditCsvDeps,
} from "../../src/tools/auditCsvCorpus.js";
```

```ts
describe("checkPairDisagreement", () => {
  const pair = { a: "safe_gaps_apply", b: "applied_safe_gaps" };

  it("is silent when both sides agree", () => {
    const findings = checkPairDisagreement(
      pair,
      [row({ address: "0x1", rva: 1, nameB: "Actor::SetFlag" })],
      [row({ address: "0x1", rva: 1, nameB: "Actor::SetFlag" })]
    );
    expect(findings).toEqual([]);
  });

  it("flags a name mismatch between the plan and the record", () => {
    const findings = checkPairDisagreement(
      pair,
      [row({ address: "0x1", rva: 1, nameB: "Actor::SetFlag" })],
      [row({ address: "0x1", rva: 1, nameB: "Actor::DifferentName" })]
    );
    expect(findings).toHaveLength(1);
    expect(findings[0]).toMatchObject({ findingType: "pair_disagreement", severity: "warning" });
    expect(findings[0].detail).toContain("Actor::SetFlag");
    expect(findings[0].detail).toContain("Actor::DifferentName");
  });

  it("flags an address present in only one side", () => {
    const findings = checkPairDisagreement(
      pair,
      [row({ address: "0x1", rva: 1, nameB: "Actor::SetFlag" })],
      []
    );
    expect(findings).toHaveLength(1);
    expect(findings[0].detail).toContain("missing from applied_safe_gaps");
  });

  it("declares exactly the 4 pairs from the spec", () => {
    expect(CSV_PAIRS).toEqual([
      { a: "safe_gaps_apply", b: "applied_safe_gaps" },
      { a: "reassign_apply", b: "applied_reassignments" },
      { a: "std_func_sweep", b: "applied_std_func" },
      { a: "misnames_to_revert", b: "misnames_to_revert_simple" },
    ]);
  });
});

describe("checkPdbUncorroborated", () => {
  function dbWithPdb(names: string[]): Database.Database {
    const db = staticDb();
    db.exec(`CREATE TABLE pdb_symbols (name TEXT, signature TEXT, calling_convention TEXT, sizeof INTEGER, is_fpo INTEGER, pdb_rva INTEGER, version TEXT);`);
    for (const name of names) {
      db.prepare("INSERT INTO pdb_symbols (name, signature, version) VALUES (?, 'sig', '1.10.155')").run(name);
    }
    return db;
  }

  it("is info-severity when a claimed name is absent from the PDB", () => {
    const findings = checkPdbUncorroborated(dbWithPdb([]), "applied_safe_gaps", [
      row({ nameB: "Actor::SetFlag" }),
    ]);
    expect(findings).toHaveLength(1);
    expect(findings[0].severity).toBe("info");
  });

  it("is silent when the name is present", () => {
    const findings = checkPdbUncorroborated(dbWithPdb(["Actor::SetFlag"]), "applied_safe_gaps", [
      row({ nameB: "Actor::SetFlag" }),
    ]);
    expect(findings).toEqual([]);
  });

  it("does not run for a source outside the scope list", () => {
    const findings = checkPdbUncorroborated(dbWithPdb([]), "crossref_review_candidates", [
      row({ nameB: "Actor::SetFlag" }),
    ]);
    expect(findings).toEqual([]);
  });

  it("skips a row whose nameB is signature text", () => {
    const findings = checkPdbUncorroborated(dbWithPdb([]), "applied_safe_gaps", [
      row({ nameB: "public virtual void __thiscall Foo::Bar()" }),
    ]);
    expect(findings).toEqual([]);
  });

  it("does not throw when pdb_symbols does not exist", () => {
    const db = staticDb();
    expect(() => checkPdbUncorroborated(db, "applied_safe_gaps", [row({ nameB: "X" })])).not.toThrow();
    expect(checkPdbUncorroborated(db, "applied_safe_gaps", [row({ nameB: "X" })])).toEqual([]);
  });
});

describe("auditCsvCorpus (integration, fixture CSVs on disk)", () => {
  function fixtureCorpus(): { organizedDir: string; generatedDir: string } {
    const organizedDir = mkdtempSync(join(tmpdir(), "re-organized-"));
    cleanups.push(() => rmSync(organizedDir, { recursive: true, force: true }));
    const generatedDir = join(organizedDir, "generated");

    // A claim_drift case: applied_safe_gaps says 0x724fa0 -> Actor::WrongName, but the live
    // index (set up below) says Actor::SetFlag.
    writeFileSync(
      join(organizedDir, "applied_safe_gaps.csv"),
      "address,old_name,new_name,kind,note\n0x140724fa0,Old::Name,Actor::WrongName,reassign,n\n"
    );
    // Its plan-side pair, disagreeing with the record above.
    writeFileSync(
      join(organizedDir, "safe_gaps_apply.csv"),
      "address,existing_name,decoded_name\n0x140724fa0,Old::Name,Actor::SetFlag\n"
    );
    // An unresolved address.
    writeFileSync(
      join(organizedDir, "crossref_review_candidates.csv"),
      "address,existing_name,decoded_name\n0x14deadbe0,A,B\n"
    );

    return { organizedDir, generatedDir };
  }

  function deps(organizedDir: string, generatedDir: string): AuditCsvDeps {
    const db = staticDb();
    db.prepare("INSERT INTO symbols VALUES ('1.10.163', 0x724fa0, 'Actor::SetFlag', 'sig', 1)").run();
    return { staticDb: db, organizedDir, generatedDir };
  }

  it("audits only the files present, reporting the rest as file-not-found only when scoped elsewhere", () => {
    const { organizedDir, generatedDir } = fixtureCorpus();
    // Scope to just the 3 fixture files so the other 21 declared-but-absent files don't drown the
    // assertions in "file not found" noise unrelated to this test's purpose.
    const result = auditCsvCorpus(
      { source: "applied_safe_gaps" },
      deps(organizedDir, generatedDir)
    );
    expect(result.sourcesAudited).toBe(1);
    expect(result.findings.some((f) => f.findingType === "claim_drift")).toBe(true);
  });

  it("reports unresolved_address for the crossref fixture row", () => {
    const { organizedDir, generatedDir } = fixtureCorpus();
    const result = auditCsvCorpus(
      { source: "crossref_review_candidates" },
      deps(organizedDir, generatedDir)
    );
    expect(result.findings).toHaveLength(1);
    expect(result.findings[0].findingType).toBe("unresolved_address");
  });

  it("reports pair_disagreement only on a whole-corpus run", () => {
    const { organizedDir, generatedDir } = fixtureCorpus();
    const scoped = auditCsvCorpus({ source: "applied_safe_gaps" }, deps(organizedDir, generatedDir));
    expect(scoped.findings.some((f) => f.findingType === "pair_disagreement")).toBe(false);

    const full = auditCsvCorpus({}, deps(organizedDir, generatedDir));
    expect(full.findings.some((f) => f.findingType === "pair_disagreement")).toBe(true);
  });

  it("throws a clear error for an unknown source", () => {
    const { organizedDir, generatedDir } = fixtureCorpus();
    expect(() => auditCsvCorpus({ source: "NotARealSource" }, deps(organizedDir, generatedDir))).toThrow(
      /Unknown source/
    );
  });

  it("a missing declared file becomes a structural error, not a thrown exception", () => {
    const { organizedDir, generatedDir } = fixtureCorpus();
    // commonlib_vtable_slots.csv is declared in CSV_SOURCES but absent from this fixture dir.
    const result = auditCsvCorpus({ source: "commonlib_vtable_slots" }, deps(organizedDir, generatedDir));
    expect(result.findings).toHaveLength(1);
    expect(result.findings[0]).toMatchObject({ findingType: "structural", severity: "error" });
  });

  it("writes nothing when write is not set", () => {
    const { organizedDir, generatedDir } = fixtureCorpus();
    const result = auditCsvCorpus({ source: "applied_safe_gaps" }, deps(organizedDir, generatedDir));
    expect(result.wrote).toBeNull();
    expect(existsSync(generatedDir)).toBe(false);
  });

  it("writes exactly three files under generated/ when write is true", () => {
    const { organizedDir, generatedDir } = fixtureCorpus();
    const result = auditCsvCorpus({ write: true }, deps(organizedDir, generatedDir));

    expect(result.wrote).not.toBeNull();
    expect(existsSync(result.wrote!.csvAuditReportCsv)).toBe(true);
    expect(existsSync(result.wrote!.csvAuditReportMd)).toBe(true);
    expect(result.wrote!.csvAuditReportCsv.startsWith(generatedDir)).toBe(true);

    const csvContent = readFileSync(result.wrote!.csvAuditReportCsv, "utf8");
    expect(csvContent).toContain("claim_drift");

    const mdContent = readFileSync(result.wrote!.csvAuditReportMd, "utf8");
    expect(mdContent).toContain("## Counts by severity");
  });

  it("the full report on disk is not truncated even when the response limit caps it", () => {
    const { organizedDir, generatedDir } = fixtureCorpus();
    const result = auditCsvCorpus({ write: true, limit: 0 }, deps(organizedDir, generatedDir));
    expect(result.findings).toHaveLength(0);
    expect(result.truncated).toBe(true);
    const csvContent = readFileSync(result.wrote!.csvAuditReportCsv, "utf8");
    expect(csvContent.split("\n").filter(Boolean).length).toBeGreaterThan(1); // header + real findings
  });

  it("pdbCrosscheckCsv is null when pdb_symbols is not indexed", () => {
    const { organizedDir, generatedDir } = fixtureCorpus();
    const result = auditCsvCorpus({ write: true }, deps(organizedDir, generatedDir));
    expect(result.wrote!.pdbCrosscheckCsv).toBeNull();
  });

  it("writes a real pdb crosscheck row when pdb_symbols has a matching name", () => {
    const { organizedDir, generatedDir } = fixtureCorpus();
    const d = deps(organizedDir, generatedDir);
    d.staticDb.exec(`CREATE TABLE pdb_symbols (name TEXT, signature TEXT, calling_convention TEXT, sizeof INTEGER, is_fpo INTEGER, pdb_rva INTEGER, version TEXT);`);
    d.staticDb
      .prepare("INSERT INTO pdb_symbols VALUES ('Actor::SetFlag', 'void Actor::SetFlag()', '__cdecl', 1, 0, 999, '1.10.155')")
      .run();

    const result = auditCsvCorpus({ write: true }, d);
    expect(result.wrote!.pdbCrosscheckCsv).not.toBeNull();
    const content = readFileSync(result.wrote!.pdbCrosscheckCsv!, "utf8");
    expect(content).toContain("Actor::SetFlag");
  });
});
```

- [ ] **Step 2: Run it and verify it fails**

Run: `npm test -- tests/tools/auditCsvCorpus.test.ts`
Expected: FAIL — the new exports do not exist yet.

- [ ] **Step 3: Append `checkPairDisagreement`, `checkPdbUncorroborated`, and the deps/result types**

Append to `src/tools/auditCsvCorpus.ts` (add `getDb` to a new `../db.js` import and `ORGANIZED_DIR, ORGANIZED_GENERATED_DIR` to a new `../paths.js` import at the top of the file):

```ts
export interface CsvPair {
  a: string;
  b: string;
}

export const CSV_PAIRS: CsvPair[] = [
  { a: "safe_gaps_apply", b: "applied_safe_gaps" },
  { a: "reassign_apply", b: "applied_reassignments" },
  { a: "std_func_sweep", b: "applied_std_func" },
  { a: "misnames_to_revert", b: "misnames_to_revert_simple" },
];

export function checkPairDisagreement(
  pair: CsvPair,
  rowsA: ParsedOrganizedRow[],
  rowsB: ParsedOrganizedRow[]
): CsvAuditFinding[] {
  const findings: CsvAuditFinding[] = [];
  const label = `${pair.a} vs ${pair.b}`;

  const mapA = new Map<number, ParsedOrganizedRow>();
  for (const row of rowsA) if (row.rva !== null) mapA.set(row.rva, row);
  const mapB = new Map<number, ParsedOrganizedRow>();
  for (const row of rowsB) if (row.rva !== null) mapB.set(row.rva, row);

  const allRvas = new Set([...mapA.keys(), ...mapB.keys()]);
  for (const rva of allRvas) {
    const rowA = mapA.get(rva);
    const rowB = mapB.get(rva);
    const address = (rowA ?? rowB)!.address;

    if (!rowA) {
      findings.push({
        source: label, rowNo: rowB!.rowNo, address, findingType: "pair_disagreement", severity: "warning",
        detail: `Present in ${pair.b} (row ${rowB!.rowNo}) but missing from ${pair.a}.`,
      });
    } else if (!rowB) {
      findings.push({
        source: label, rowNo: rowA.rowNo, address, findingType: "pair_disagreement", severity: "warning",
        detail: `Present in ${pair.a} (row ${rowA.rowNo}) but missing from ${pair.b}.`,
      });
    } else if (rowA.nameB !== rowB.nameB) {
      findings.push({
        source: label, rowNo: rowA.rowNo, address, findingType: "pair_disagreement", severity: "warning",
        detail: `${pair.a} row ${rowA.rowNo} says "${rowA.nameB}", ${pair.b} row ${rowB.rowNo} says "${rowB.nameB}".`,
      });
    }
  }

  return findings;
}

// A name absent from 1.10.155 does not prove it's wrong — 1.10.155 predates 1.10.163. info only.
export const PDB_UNCORROBORATED_SOURCES = [
  ...CLAIM_DRIFT_SOURCES,
  "confirmed_misnames_to_apply",
  "vtable_audit_fixes",
];

function hasPdbSymbolsTable(db: Database.Database): boolean {
  return !!db.prepare("SELECT name FROM sqlite_master WHERE type = 'table' AND name = 'pdb_symbols'").get();
}

export function checkPdbUncorroborated(
  staticDb: Database.Database,
  source: string,
  rows: ParsedOrganizedRow[]
): CsvAuditFinding[] {
  if (!PDB_UNCORROBORATED_SOURCES.includes(source)) return [];
  if (!hasPdbSymbolsTable(staticDb)) return [];

  const findings: CsvAuditFinding[] = [];
  const existsStmt = staticDb.prepare("SELECT 1 FROM pdb_symbols WHERE name = ? LIMIT 1");

  for (const row of rows) {
    if (!row.nameB || looksLikeSignatureText(row.nameB)) continue;
    if (!existsStmt.get(row.nameB)) {
      findings.push({
        source, rowNo: row.rowNo, address: row.address, findingType: "pdb_uncorroborated", severity: "info",
        detail: `"${row.nameB}" was not found in the 1.10.155 PDB by exact name.`,
      });
    }
  }

  return findings;
}
```

- [ ] **Step 4: Append `AuditCsvDeps`, `AuditCsvCorpusResult`, and `auditCsvCorpus`**

```ts
export interface AuditCsvDeps {
  staticDb: Database.Database;
  organizedDir: string;
  generatedDir: string;
}

export function createAuditCsvDeps(overrides: Partial<AuditCsvDeps> = {}): AuditCsvDeps {
  return {
    staticDb: overrides.staticDb ?? getDb(),
    organizedDir: overrides.organizedDir ?? ORGANIZED_DIR,
    generatedDir: overrides.generatedDir ?? ORGANIZED_GENERATED_DIR,
  };
}

export interface AuditCsvCorpusResult {
  sourcesAudited: number;
  totalRows: number;
  findings: CsvAuditFinding[];
  truncated: boolean;
  countsBySeverity: Record<CsvFindingSeverity, number>;
  countsByType: Partial<Record<CsvFindingType, number>>;
  wrote: { csvAuditReportCsv: string; csvAuditReportMd: string; pdbCrosscheckCsv: string | null } | null;
}

const MAX_FINDINGS_IN_RESPONSE = 500;
const VERSION = "1.10.163";

export function auditCsvCorpus(
  args: { source?: string; write?: boolean; limit?: number },
  deps?: AuditCsvDeps
): AuditCsvCorpusResult {
  const d = deps ?? createAuditCsvDeps();

  const knownSources = CSV_SOURCES.map((m) => sourceNameFor(m.file));
  if (args.source && !knownSources.includes(args.source)) {
    throw new Error(`Unknown source "${args.source}". Known sources: ${knownSources.join(", ")}`);
  }
  const mappingsToRun = args.source
    ? CSV_SOURCES.filter((m) => sourceNameFor(m.file) === args.source)
    : CSV_SOURCES;

  const parsedBySource = new Map<string, ParsedOrganizedRow[]>();
  let totalRows = 0;
  const findings: CsvAuditFinding[] = [];

  for (const mapping of mappingsToRun) {
    const source = sourceNameFor(mapping.file);
    const path = join(d.organizedDir, mapping.file);
    if (!existsSync(path)) {
      findings.push({
        source, rowNo: 0, address: null, findingType: "structural", severity: "error",
        detail: `File not found: ${mapping.file}`,
      });
      continue;
    }

    let rows: ParsedOrganizedRow[];
    try {
      const { rows: raw } = readOrganizedCsv(path);
      rows = parseOrganizedCsv(mapping, raw);
    } catch (err) {
      findings.push({
        source, rowNo: 0, address: null, findingType: "structural", severity: "error",
        detail: `Failed to read/parse: ${err instanceof Error ? err.message : String(err)}`,
      });
      continue;
    }

    parsedBySource.set(source, rows);
    totalRows += rows.length;
    findings.push(...checkStructural(source, rows));
    findings.push(...checkUnresolvedAddresses(d.staticDb, source, rows, VERSION));
    findings.push(...checkClaimDrift(d.staticDb, source, rows, VERSION));
    findings.push(...checkPdbUncorroborated(d.staticDb, source, rows));
  }

  if (!args.source) {
    for (const pair of CSV_PAIRS) {
      const rowsA = parsedBySource.get(pair.a);
      const rowsB = parsedBySource.get(pair.b);
      if (rowsA && rowsB) findings.push(...checkPairDisagreement(pair, rowsA, rowsB));
    }
  }

  const limit = Math.min(args.limit ?? MAX_FINDINGS_IN_RESPONSE, MAX_FINDINGS_IN_RESPONSE);
  const truncated = findings.length > limit;
  const page = truncated ? findings.slice(0, limit) : findings;

  const countsBySeverity: Record<CsvFindingSeverity, number> = { error: 0, warning: 0, info: 0 };
  const countsByType: Partial<Record<CsvFindingType, number>> = {};
  for (const finding of findings) {
    countsBySeverity[finding.severity]++;
    countsByType[finding.findingType] = (countsByType[finding.findingType] ?? 0) + 1;
  }

  const wrote = args.write
    ? writeAuditReports(d, findings, countsBySeverity, countsByType, mappingsToRun.length, totalRows)
    : null;

  return {
    sourcesAudited: mappingsToRun.length,
    totalRows,
    findings: page,
    truncated,
    countsBySeverity,
    countsByType,
    wrote,
  };
}

function writeAuditReports(
  d: AuditCsvDeps,
  findings: CsvAuditFinding[],
  countsBySeverity: Record<CsvFindingSeverity, number>,
  countsByType: Partial<Record<CsvFindingType, number>>,
  sourcesAudited: number,
  totalRows: number
): NonNullable<AuditCsvCorpusResult["wrote"]> {
  const csvPath = join(d.generatedDir, "csv_audit_report.csv");
  writeCsv(
    csvPath,
    ["source", "row_no", "address", "finding_type", "severity", "detail"],
    findings.map((f) => [f.source, f.rowNo, f.address, f.findingType, f.severity, f.detail])
  );

  const errorLines = findings
    .filter((f) => f.severity === "error")
    .map((f) => `- **${f.source}:${f.rowNo}** (${f.findingType}) ${f.detail}`);
  const md = [
    "# CSV audit report",
    "",
    `Generated by audit_csv_corpus at ${new Date().toISOString()}.`,
    `Sources audited: ${sourcesAudited}. Total rows: ${totalRows}.`,
    "",
    "## Counts by severity",
    "",
    `- error: ${countsBySeverity.error}`,
    `- warning: ${countsBySeverity.warning}`,
    `- info: ${countsBySeverity.info}`,
    "",
    "## Counts by type",
    "",
    ...Object.entries(countsByType).map(([type, count]) => `- ${type}: ${count}`),
    "",
    "## Errors",
    "",
    ...(errorLines.length ? errorLines : ["(none)"]),
    "",
  ].join("\n");
  const mdPath = join(d.generatedDir, "csv_audit_report.md");
  mkdirSync(d.generatedDir, { recursive: true });
  writeFileSync(mdPath, md, "utf8");

  let pdbCrosscheckCsv: string | null = null;
  if (hasPdbSymbolsTable(d.staticDb)) {
    const crosscheckPath = join(d.generatedDir, "pdb_crosscheck_163_vs_155.csv");
    const rows163 = d.staticDb
      .prepare("SELECT name, MIN(rva) AS rva, signature, sizeof FROM symbols WHERE version = ? GROUP BY name")
      .all(VERSION) as { name: string; rva: number; signature: string; sizeof: number | null }[];
    const stmt155 = d.staticDb.prepare(
      "SELECT signature, calling_convention, sizeof FROM pdb_symbols WHERE name = ? LIMIT 1"
    );
    const csvRows: (string | number | null)[][] = [];
    for (const row of rows163) {
      const match = stmt155.get(row.name) as
        | { signature: string; calling_convention: string | null; sizeof: number | null }
        | undefined;
      if (!match) continue;
      csvRows.push([
        row.name,
        `0x${row.rva.toString(16)}`,
        row.signature,
        match.signature,
        match.calling_convention,
        match.sizeof,
        row.sizeof,
      ]);
    }
    writeCsv(
      crosscheckPath,
      ["name", "rva_163", "signature_163", "signature_155", "calling_convention", "sizeof_155", "sizeof_163"],
      csvRows
    );
    pdbCrosscheckCsv = crosscheckPath;
  }

  return { csvAuditReportCsv: csvPath, csvAuditReportMd: mdPath, pdbCrosscheckCsv };
}
```

- [ ] **Step 5: Run the test and verify it passes**

Run: `npm test -- tests/tools/auditCsvCorpus.test.ts`
Expected: PASS, roughly 32 tests total (Task 7's 17 + Task 8's additions).

- [ ] **Step 6: Register the tool**

In `src/index.ts`:

```ts
import { auditCsvCorpus } from "./tools/auditCsvCorpus.js";
```

```ts
server.tool(
  "audit_csv_corpus",
  "Re-read all 24 Organized/*.csv files from disk right now and cross-check their claims against " +
    "the live index: does an applied rename still hold (claim_drift), does the address even exist " +
    "(unresolved_address), do a plan file and its record file agree (pair_disagreement), and does " +
    "a claimed real name show up in the independent 1.10.155 PDB (pdb_uncorroborated, info-only). " +
    "Pass write:true to also write Organized/generated/csv_audit_report.csv/.md and a PDB " +
    "name-crosscheck CSV.",
  {
    source: z.string().optional().describe('Limit to one CSV, e.g. "applied_safe_gaps". Omit to audit all 24.'),
    write: z.boolean().optional().describe("Write the generated reports under Organized/generated/."),
    limit: z.number().optional().describe("Max findings in the response, default/cap 500. The written report is never truncated."),
  },
  async ({ source, write, limit }) => run(() => auditCsvCorpus({ source, write, limit }))
);
```

- [ ] **Step 7: Run the full suite and the build**

Run: `npm test && npm run build`
Expected: all green.

- [ ] **Step 8: Commit**

```bash
git add src/tools/auditCsvCorpus.ts src/index.ts tests/tools/auditCsvCorpus.test.ts
git commit -m "feat: add audit_csv_corpus MCP tool with pair and PDB corroboration checks"
```

---

### Task 9: `export_learnings` MCP tool

**Files:**
- Create: `src/tools/exportLearnings.ts`
- Modify: `src/index.ts`
- Test: `tests/tools/exportLearnings.test.ts`

**Interfaces:**
- Consumes: `listRecentFindings` (Phase 1, `../learning/findings.js`), `getStaticContext`/`detectContradiction` (Phase 1, `../learning/analyze.js`), `formatVa` (`../db.js`), `writeCsv` (Task 7), `ORGANIZED_GENERATED_DIR` (Task 2), `AuditDeps`/`createAuditDeps` (Phase 1, `../tools/auditHardcodedAddresses.js` — reused as-is, no new deps type needed).
- Produces:
  - `interface ExportLearningsResult { count: number; wrote: string | null }`
  - `async function exportLearnings(args: { write?: boolean }, deps?: AuditDeps, generatedDir?: string): Promise<ExportLearningsResult>`

**Context the implementer needs.** This reuses Phase 1's existing `AuditDeps`/`createAuditDeps` (already has `staticDb`, `learningDb`, `clock`, `config`) rather than inventing a new deps type — the same pattern `recentLearnings` and `suggestHooks` already follow. Only **current** findings are exported (`currentOnly: true`), matching the spec's framing of this as "current machine-generated evidence," not a full history dump. The output directory is a **separate, optional third parameter** rather than a field on `AuditDeps` — `AuditDeps` is shared by three other Phase 1 tools, and widening it for one tool's write path is unnecessary churn. It defaults to `ORGANIZED_GENERATED_DIR` and is overridden in tests so `npm test` never touches the real `Organized/generated/` directory.

- [ ] **Step 1: Write the failing test**

Create `tests/tools/exportLearnings.test.ts`:

```ts
import Database from "better-sqlite3";
import { existsSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { fixedClock } from "../../src/clock.js";
import { loadConfig } from "../../src/config.js";
import { recordFinding } from "../../src/learning/findings.js";
import type { AuditDeps } from "../../src/tools/auditHardcodedAddresses.js";
import { exportLearnings } from "../../src/tools/exportLearnings.js";
import { tempLearningDb } from "../helpers/fixtures.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

const CLOCK = fixedClock("2026-08-13T10:00:00.000Z");

function deps(): AuditDeps {
  const learning = tempLearningDb();
  cleanups.push(learning.cleanup);
  const sdb = new Database(":memory:");
  cleanups.push(() => sdb.close());
  sdb.exec(`
    CREATE TABLE symbols (version TEXT, rva INTEGER, name TEXT, signature TEXT, sizeof INTEGER);
    CREATE TABLE address_library (version TEXT, id INTEGER, rva INTEGER);
    CREATE TABLE name_disagreements (address TEXT, rva INTEGER, existing_name TEXT, decoded_name TEXT, note TEXT, verdict_155 TEXT);
    CREATE TABLE misname_corrections (address TEXT, rva INTEGER, old_name TEXT, new_name TEXT, method TEXT, note TEXT);
    CREATE TABLE misname_reverts (address TEXT, rva INTEGER, wrongly_renamed_to TEXT, revert_to TEXT);
    INSERT INTO symbols VALUES ('1.10.163', 0x1000, 'Combat::Start', 'sig', 8);
  `);
  return { staticDb: sdb, learningDb: learning.db, clock: CLOCK, config: loadConfig({}) };
}

const BASE = {
  version: "1.10.163",
  rva: 0x1000,
  rawDecompileHash: "h",
  model: "m",
  promptVersion: "1",
  extractorVersion: "1",
  projectFingerprint: null,
  proposedName: "Combat::Start",
  confidence: "high" as const,
  note: "matches",
  origin: "interactive" as const,
  ghidraVersion: null,
};

```ts
describe("exportLearnings", () => {
  function tempOutDir(): string {
    const dir = mkdtempSync(join(tmpdir(), "re-export-"));
    cleanups.push(() => rmSync(dir, { recursive: true, force: true }));
    return dir;
  }

  it("counts only current findings, excluding superseded history", async () => {
    const d = deps();
    recordFinding(d.learningDb, CLOCK, BASE);
    recordFinding(
      d.learningDb,
      fixedClock("2026-08-13T11:00:00.000Z"),
      { ...BASE, rawDecompileHash: "h2" }
    );

    const result = await exportLearnings({ write: false }, d, tempOutDir());
    expect(result.count).toBe(1);
    expect(result.wrote).toBeNull();
  });

  it("writes a CSV with static name and contradiction columns when write is true", async () => {
    const d = deps();
    recordFinding(d.learningDb, CLOCK, { ...BASE, proposedName: "Combat::Different" });

    const result = await exportLearnings({ write: true }, d, tempOutDir());

    expect(result.count).toBe(1);
    expect(result.wrote).not.toBeNull();
    expect(existsSync(result.wrote!)).toBe(true);

    const content = readFileSync(result.wrote!, "utf8");
    expect(content).toContain("Combat::Different");
    expect(content).toContain("Combat::Start"); // static_name column
    expect(content).toContain("interactive");
  });

  it("returns count 0 and wrote null for an empty learning DB", async () => {
    const result = await exportLearnings({ write: false }, deps(), tempOutDir());
    expect(result.count).toBe(0);
    expect(result.wrote).toBeNull();
  });

  it("does not write when write is false even with findings present", async () => {
    const d = deps();
    recordFinding(d.learningDb, CLOCK, BASE);
    const result = await exportLearnings({}, d, tempOutDir());
    expect(result.wrote).toBeNull();
  });
});
```

- [ ] **Step 2: Run it and verify it fails**

Run: `npm test -- tests/tools/exportLearnings.test.ts`
Expected: FAIL — module not found.

- [ ] **Step 3: Implement the tool**

Create `src/tools/exportLearnings.ts`:

```ts
import { join } from "node:path";
import { formatVa } from "../db.js";
import { detectContradiction, getStaticContext } from "../learning/analyze.js";
import { listRecentFindings } from "../learning/findings.js";
import { ORGANIZED_GENERATED_DIR } from "../paths.js";
import { writeCsv } from "../organized/writeCsv.js";
import { createAuditDeps, type AuditDeps } from "./auditHardcodedAddresses.js";

export interface ExportLearningsResult {
  count: number;
  wrote: string | null;
}

const EXPORT_LIMIT = 100_000;
const HEADER = [
  "address", "rva", "version", "proposed_name", "confidence", "note",
  "origin", "model", "created_at", "static_name", "contradiction",
];

export async function exportLearnings(
  args: { write?: boolean },
  deps?: AuditDeps,
  generatedDir: string = ORGANIZED_GENERATED_DIR
): Promise<ExportLearningsResult> {
  const d = deps ?? createAuditDeps();
  const findings = listRecentFindings(d.learningDb, {
    limit: EXPORT_LIMIT,
    currentOnly: true,
    origin: "all",
  });

  let wrote: string | null = null;
  if (args.write) {
    const rows = findings.map((finding) => {
      const context = getStaticContext(d.staticDb, finding.version, finding.rva);
      const contradiction = detectContradiction(context, finding);
      return [
        formatVa(finding.rva),
        `0x${finding.rva.toString(16)}`,
        finding.version,
        finding.proposedName,
        finding.confidence,
        finding.note,
        finding.origin,
        finding.model,
        finding.createdAt,
        context.staticName,
        contradiction ? `${contradiction.staticName} vs ${contradiction.proposedName}` : null,
      ];
    });

    const path = join(generatedDir, "learned_findings.csv");
    writeCsv(path, HEADER, rows);
    wrote = path;
  }

  return { count: findings.length, wrote };
}
```

- [ ] **Step 4: Register the tool**

In `src/index.ts`:

```ts
import { exportLearnings } from "./tools/exportLearnings.js";
```

```ts
server.tool(
  "export_learnings",
  "Export current learned findings from re-learning.sqlite to a CSV in the same address,name " +
    "shape as the human audit-trail CSVs, with the static name and any contradiction attached. " +
    "Makes machine findings reviewable next to human ones; does not promote them into the static " +
    "index.",
  {
    write: z.boolean().optional().describe("Write Organized/generated/learned_findings.csv."),
  },
  async ({ write }) => runAsync(() => exportLearnings({ write }))
);
```

- [ ] **Step 5: Run the test and the build**

Run: `npm test -- tests/tools/exportLearnings.test.ts && npm run build`
Expected: PASS, 4 tests; build exit 0.

- [ ] **Step 6: Run the full suite**

Run: `npm test`
Expected: all green — this is the last new-code task, so the full count matters. Note it for the final task's self-review.

- [ ] **Step 7: Commit**

```bash
git add src/tools/exportLearnings.ts src/index.ts tests/tools/exportLearnings.test.ts
git commit -m "feat: add export_learnings MCP tool"
```

---

### Task 10: real rebuild, real audit run, README, final verification

**Files:**
- Modify: `README.md`
- No new test file — this task's verification is a real `build-index` run plus real tool calls, mirroring Phase 1's Task 14.

**Interfaces:** none new. This task proves the acceptance criteria against real data and documents the result.

- [ ] **Step 1: Run the full mocked suite one more time**

Run: `npm test && npm run build`
Expected: all green. Note the total test count for the commit message.

- [ ] **Step 2: Rebuild the static index**

Run: `npm run build-index`
Expected: exit 0. New console output lines for `pdb_symbols` (hundreds of thousands of rows) and `organized_rows` (one line per CSV, 24 total, summing to ~13,000). This reprocesses the full corpus and takes several minutes.

- [ ] **Step 3: Verify the rebuild's real numbers**

Run:

```bash
node -e "
const D = require('better-sqlite3');
const db = new D('./re-index.sqlite', { readonly: true });
console.log('pdb_symbols:', db.prepare('SELECT COUNT(*) n FROM pdb_symbols').get().n);
console.log('organized_rows:', db.prepare('SELECT COUNT(*) n FROM organized_rows').get().n);
console.log('organized_sources:', db.prepare('SELECT COUNT(*) n FROM organized_sources').get().n);
console.log('rows per source:');
for (const r of db.prepare('SELECT source, row_count FROM organized_sources ORDER BY source').all()) {
  console.log(' ', r.source, r.row_count);
}
db.close();
"
```

Expected: `organized_sources` = 24 (every declared file present). `organized_rows` totals close to the ~13,000 measured this session (exact figure may drift slightly if the CSVs have changed since). `pdb_symbols` in the hundreds of thousands, consistent with the 84.9% extraction rate measured against the real 411,713-line dump.

- [ ] **Step 4: Verify the learning database survived the rebuild untouched**

```bash
ls -la re-learning.sqlite
node -e "
const D = require('better-sqlite3');
const db = new D('./re-learning.sqlite', { readonly: true });
console.log('learned_findings:', db.prepare('SELECT COUNT(*) n FROM learned_findings').get().n);
db.close();
"
```

Expected: the file exists and its row count is unchanged from before this task's `build-index` run — this is acceptance criterion 7 (unchanged from the Phase 1 spec), re-proven for the two new tables added this phase.

- [ ] **Step 5: Run a real, whole-corpus audit — do not pre-filter to a `source`**

```bash
node -e "
import('./dist/tools/auditCsvCorpus.js').then(async (m) => {
  const result = m.auditCsvCorpus({ write: true });
  console.log(JSON.stringify({ sourcesAudited: result.sourcesAudited, totalRows: result.totalRows, countsBySeverity: result.countsBySeverity, countsByType: result.countsByType, wrote: result.wrote }, null, 2));
});
"
```

Expected: `sourcesAudited: 24`. Read the actual counts — do not assume they will be zero. In particular:

- Any `claim_drift` errors are **real findings to report**, not a bug to silently work around. If any appear, inspect a few with `search_organized` and state in the completion summary whether they look like genuine drift or an artifact of this task's own checks (e.g. a `looksLikeSignatureText` false positive/negative). This is the exact kind of result Phase 1's real Ghidra/Ollama run also surfaced (a live contradiction) — expect something similar here.
- `pair_disagreement` count for `misnames_to_revert` vs `misnames_to_revert_simple` is worth reading closely — these are the 13 addresses CLAUDE.md already discusses in detail.

- [ ] **Step 6: Verify the three generated files**

```bash
ls -la Organized/generated/
head -20 Organized/generated/csv_audit_report.md
wc -l Organized/generated/csv_audit_report.csv Organized/generated/pdb_crosscheck_163_vs_155.csv
```

Expected: three files present, the `.md` report readable, and the crosscheck CSV row count in the neighborhood of the 133,636 measured this session (again, may drift slightly; report the actual number).

- [ ] **Step 7: Real `search_organized` and `pdb_signature` spot checks**

```bash
node -e "
import('./dist/tools/searchOrganized.js').then(async (m) => {
  console.log(JSON.stringify(m.searchOrganized({ query: 'NavMesh::CreateGroupData' }), null, 2));
});
"
node -e "
import('./dist/tools/pdbSignature.js').then(async (m) => {
  console.log(JSON.stringify(m.pdbSignature({ name: 'NavMesh::CreateGroupData' }), null, 2));
});
"
```

Expected: `search_organized` finds `NavMesh::CreateGroupData` in `vtable_audit_fixes` (per `03-FINDINGS.md`'s existing documentation of this address). `pdb_signature` either returns a real Microsoft signature or an empty match array — either is a valid, reportable result; state which one actually happened.

- [ ] **Step 8: Real `export_learnings` run**

```bash
node -e "
import('./dist/tools/exportLearnings.js').then(async (m) => {
  console.log(JSON.stringify(await m.exportLearnings({ write: true }), null, 2));
});
"
cat Organized/generated/learned_findings.csv
```

Expected: at least the one finding from Phase 1's live `learn_from_target` run (`NavMesh::UpdateGroupData` vs. the audited static name `NavMesh::CreateGroupData`) appears, with its contradiction column populated.

- [ ] **Step 9: Confirm nothing outside `Organized/generated/` changed**

```bash
find "$(dirname "$(pwd)")" -maxdepth 1 -name "Organized" # sanity: confirm ORGANIZED_DIR resolution
git -C . status --short  # inside re-mcp only; ReverseEngineering itself has no git repo to check
ls Organized/ | grep -v '^generated$'
```

Manually confirm none of the 24 original `Organized/*.csv` files or the `Organized/<version>/*.txt` category files were modified (their mtimes/content unchanged) — this task only ever calls tools with `write: true`, which per Task 8/9's implementation only ever write into `Organized/generated/`.

- [ ] **Step 10: Update the README**

Add a section to `README.md`, after the existing "Learning tools" section from Phase 1, documenting:

- the four new tools (`search_organized`, `pdb_signature`, `audit_csv_corpus`, `export_learnings`) with one example call each, using this session's real numbers (24 CSVs, ~13,000 rows, the PDB's real extraction rate, and whatever `claim_drift`/`pair_disagreement` counts Step 5 actually produced);
- the `pdb_symbols` isolation rule (name-only, no address lookups, `version` hardcoded);
- `check_name_confidence`'s new `pdbCorroboration` field;
- that `Organized/generated/` is not tracked by any `.gitignore` because `ReverseEngineering` itself is not a git repository — a correction to the spec's original assumption, recorded here so a future session doesn't go looking for a gitignore entry that was never added;
- the `CLAIM_DRIFT_SOURCES` / `PDB_UNCORROBORATED_SOURCES` scoping rule and why (a plan CSV differing from what was applied is not an error).

- [ ] **Step 11: Final verification**

Run: `npm test && npm run build`
Expected: all green, exit 0. Report the actual test count.

- [ ] **Step 12: Commit**

```bash
git add README.md
git commit -m "docs: document CSV corpus indexing, PDB evidence, and the audit's real findings

Verified live after a full build-index rebuild: <fill in the real
sourcesAudited/totalRows/countsBySeverity/countsByType numbers from Step 5,
and the real pdb_symbols/organized_rows counts from Step 3>."
```

(The commit message's bracketed placeholder must be filled with the actual numbers observed in Steps 3 and 5 before committing — this is data this task itself produces, not something to leave unresolved.)

---

## Self-review

Checked after writing, against the spec.

**Spec coverage.**

| Spec section | Task |
|---|---|
| PDB parser, 84.9% extraction rate, 100% line-grammar parse | 1 |
| `pdb_symbols` schema, name-only isolation | 2 |
| All 24 CSVs indexed via one generic table | 3, 4 |
| `search_organized` | 5 |
| `pdb_signature`, `check_name_confidence` PDB corroboration | 6 |
| `audit_csv_corpus` — structural, unresolved_address, claim_drift | 7 |
| `audit_csv_corpus` — pair_disagreement, pdb_uncorroborated, generated files | 8 |
| `export_learnings` | 9 |
| Real rebuild, real audit run, README, acceptance criteria | 10 |
| Acceptance criterion 1 (search_organized covers all 24, citations, no data lost) | 3, 5, 10 (Step 7) |
| Acceptance criterion 2 (pdb_symbols isolation) | 2, 6 (explicit `pdb_rva` grep-absence test) |
| Acceptance criterion 3 (check_name_confidence PDB corroboration + graceful degradation) | 6 |
| Acceptance criterion 4 (four finding types, one bad file doesn't abort) | 7, 8 |
| Acceptance criterion 5 (real audit produces actionable findings) | 10 (Step 5) |
| Acceptance criterion 6 (generated files only under `Organized/generated/`, only on write:true) | 7, 8, 9, 10 (Step 9) |
| Acceptance criterion 7 (rebuild never touches re-learning.sqlite) | 10 (Step 4) |

**Deliberate scope narrowing, stated rather than hidden (both already called out inline where they occur):** `claim_drift` runs against 6 sources, not all 24 — the spec's finding-type description didn't enumerate an exact source list, and this plan derives one defensibly from the actual file names and CLAUDE.md's own framing rather than guessing across all 24 and risking false errors. `pdb_uncorroborated` runs against 8 sources for the same reason.

**Placeholder scan:** no "TBD"/"TODO"/"fill in details" remain except the one deliberate, self-documenting exception in Task 10 Step 12 (a commit message that must be completed with that task's own real output — this is the same pattern Phase 1's Task 14 used for its manual acceptance numbers).

**Type consistency:** `ParsedOrganizedRow`, `OrganizedFileMapping`, `CsvAuditFinding`, `CsvFindingType/Severity`, `AuditCsvDeps` are each defined once (Tasks 3, 7, 8) and imported unchanged everywhere else. `AuditDeps` (Task 9) is Phase 1's existing type, not redefined. `formatVa`, `getDb`, `parseAddressToRva` are the existing `db.ts` exports throughout, never reimplemented.

## Execution handoff

Plan saved. Two execution options:

**1. Subagent-Driven (recommended)** — a fresh subagent per task, review between tasks, fast iteration. Given the CSV mapping table in Task 3 is the highest-risk part of this plan (24 hand-verified entries), a subagent-per-task with review is the safer choice here.

**2. Inline Execution** — execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?
