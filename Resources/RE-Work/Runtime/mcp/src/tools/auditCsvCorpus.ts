import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import type Database from "better-sqlite3";
import { systemClock, type Clock } from "../clock.js";
import { getDb } from "../db.js";
import {
  CSV_SOURCES,
  parseOrganizedCsv,
  readOrganizedCsv,
  sourceNameFor,
  type ParsedOrganizedRow,
} from "../organized/csvSources.js";
import { writeCsv } from "../organized/writeCsv.js";
import { ORGANIZED_DIR, ORGANIZED_GENERATED_DIR } from "../paths.js";

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
// The other 18 sources are plans/candidates/held lists; a plan differing from what was applied
// is not drift, and CLAUDE.md documents real partial application ("3 held", "121 skipped").
export const CLAIM_DRIFT_SOURCES = [
  "applied_hknpShape",
  "applied_misname_corrections",
  "applied_reassignments",
  "applied_safe_gaps",
  "applied_std_func",
  "misnames_to_revert",
];

// A bare qualified name never contains a space or a paren. Real corpus data carries full C++
// signatures in name columns (e.g. misnames_to_revert's
// "public virtual void __thiscall ImageSpaceEffect::Render(...) __ptr64 ") — comparing that
// against symbols.name would never match and would manufacture noise, so both claim_drift
// (here) and pdb_uncorroborated (Task 8) skip those rows.
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

// A documented reversion record: the revert-side CSVs (hknpShape_rename.csv,
// misnames_to_revert_simple.csv) map an address to the name it was deliberately reverted to. When
// the live index shows exactly that reverted name, the drift is explained and on record — it gets
// reported as info (still visible in the report, with the citation) rather than an error.
export interface RevertRecord {
  name: string;
  file: string;
  rowNo: number;
}

export function checkClaimDrift(
  staticDb: Database.Database,
  source: string,
  rows: ParsedOrganizedRow[],
  version: string,
  reverts?: Map<number, RevertRecord>
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
      // A reversion explains the drift only when it points at exactly what the live index shows.
      // If the revert record names something else, that is a genuine three-way disagreement.
      const revert = reverts?.get(row.rva);
      const documented = revert !== undefined && revert.name === symbol.name;
      findings.push({
        source,
        rowNo: row.rowNo,
        address: row.address,
        findingType: "claim_drift",
        severity: documented ? "info" : "error",
        detail: documented
          ? `${source} claims this address is "${row.nameB}", but the live index shows "${symbol.name}" — ` +
            `documented reversion in ${revert.file} row ${revert.rowNo}, so this drift is expected.`
          : `${source} claims this address is "${row.nameB}", but the live index shows "${symbol.name}".`,
      });
    }
  }

  return findings;
}

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
      // Real corpus data carries full C++ signatures in some name columns (safe_gaps_apply's
      // decoded_name is 100% signature text, e.g. "private static unsigned int
      // NiRefObject::ms_uiObjects") while its applied-record counterpart holds the bare name.
      // Comparing those would manufacture noise on every row, so a name comparison is only
      // meaningful when both sides are bare names (same rule as claim_drift/pdb_uncorroborated).
      if (!rowA.nameB || !rowB.nameB) continue;
      if (looksLikeSignatureText(rowA.nameB) || looksLikeSignatureText(rowB.nameB)) continue;
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

export interface AuditCsvDeps {
  staticDb: Database.Database;
  organizedDir: string;
  generatedDir: string;
  // Timestamps only from src/clock.ts — injected so tests can pin time.
  clock?: Clock;
}

export function createAuditCsvDeps(overrides: Partial<AuditCsvDeps> = {}): AuditCsvDeps {
  return {
    staticDb: overrides.staticDb ?? getDb(),
    organizedDir: overrides.organizedDir ?? ORGANIZED_DIR,
    generatedDir: overrides.generatedDir ?? ORGANIZED_GENERATED_DIR,
    clock: overrides.clock ?? systemClock,
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

// The revert-side CSVs that document deliberate reversions, address -> the name it was reverted to.
// Their nameB column is the revert target (hknpShape_rename.csv's revert_to_name,
// misnames_to_revert_simple.csv's revert_to_name). A claim that drifts toward exactly this name is
// explained; anything else is not.
const REVERT_SOURCES = ["hknpShape_rename.csv", "misnames_to_revert_simple.csv"];

export function loadRevertRecords(organizedDir: string): Map<number, RevertRecord> {
  const reverts = new Map<number, RevertRecord>();
  for (const mapping of CSV_SOURCES) {
    if (!REVERT_SOURCES.includes(mapping.file)) continue;
    const path = join(organizedDir, mapping.file);
    if (!existsSync(path)) continue;
    try {
      const { rows: raw } = readOrganizedCsv(path);
      for (const row of parseOrganizedCsv(mapping, raw)) {
        if (row.rva === null || !row.nameB) continue;
        reverts.set(row.rva, { name: row.nameB, file: mapping.file, rowNo: row.rowNo });
      }
    } catch {
      // Missing/unparseable revert files surface as structural errors from the main loop; a
      // missing revert record just means the drift stays an error (conservative).
    }
  }
  return reverts;
}

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
  const reverts = loadRevertRecords(d.organizedDir);

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
    findings.push(...checkClaimDrift(d.staticDb, source, rows, VERSION, reverts));
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
    `Generated by audit_csv_corpus at ${(d.clock ?? systemClock).now()}.`,
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
