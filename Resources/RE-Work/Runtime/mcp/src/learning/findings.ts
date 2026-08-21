import type Database from "better-sqlite3";
import type { Clock } from "../clock.js";

export type Confidence = "high" | "medium" | "low";
export type Origin = "interactive" | "daemon";

export interface LearnedFinding {
  id: number;
  version: string;
  rva: number;
  proposedName: string | null;
  confidence: Confidence;
  note: string | null;
  source: string;
  origin: Origin;
  rawDecompileHash: string;
  model: string;
  promptVersion: string;
  extractorVersion: string;
  ghidraVersion: string | null;
  projectFingerprint: string | null;
  supersedesId: number | null;
  isCurrent: 0 | 1;
  createdAt: string;
}

export interface ExtractionIdentity {
  version: string;
  rva: number;
  rawDecompileHash: string;
  model: string;
  promptVersion: string;
  extractorVersion: string;
  projectFingerprint: string | null;
}

export interface RecordFindingInput extends ExtractionIdentity {
  proposedName: string | null;
  confidence: Confidence;
  note: string | null;
  origin: Origin;
  ghidraVersion: string | null;
  force?: boolean;
}

interface Row {
  id: number;
  version: string;
  rva: number;
  proposed_name: string | null;
  confidence: Confidence;
  note: string | null;
  source: string;
  origin: Origin;
  raw_decompile_hash: string;
  model: string;
  prompt_version: string;
  extractor_version: string;
  ghidra_version: string | null;
  project_fingerprint: string | null;
  supersedes_id: number | null;
  is_current: 0 | 1;
  created_at: string;
}

function toFinding(row: Row): LearnedFinding {
  return {
    id: row.id,
    version: row.version,
    rva: row.rva,
    proposedName: row.proposed_name,
    confidence: row.confidence,
    note: row.note,
    source: row.source,
    origin: row.origin,
    rawDecompileHash: row.raw_decompile_hash,
    model: row.model,
    promptVersion: row.prompt_version,
    extractorVersion: row.extractor_version,
    ghidraVersion: row.ghidra_version,
    projectFingerprint: row.project_fingerprint,
    supersedesId: row.supersedes_id,
    isCurrent: row.is_current,
    createdAt: row.created_at,
  };
}

function identityOf(row: LearnedFinding): ExtractionIdentity {
  return {
    version: row.version,
    rva: row.rva,
    rawDecompileHash: row.rawDecompileHash,
    model: row.model,
    promptVersion: row.promptVersion,
    extractorVersion: row.extractorVersion,
    projectFingerprint: row.projectFingerprint,
  };
}

export function sameExtractionIdentity(a: ExtractionIdentity, b: ExtractionIdentity): boolean {
  return (
    a.version === b.version &&
    a.rva === b.rva &&
    a.rawDecompileHash === b.rawDecompileHash &&
    a.model === b.model &&
    a.promptVersion === b.promptVersion &&
    a.extractorVersion === b.extractorVersion &&
    a.projectFingerprint === b.projectFingerprint
  );
}

const SELECT_CURRENT =
  "SELECT * FROM learned_findings WHERE version = ? AND rva = ? AND is_current = 1";

export function getCurrentFinding(
  db: Database.Database,
  version: string,
  rva: number
): LearnedFinding | null {
  const row = db.prepare(SELECT_CURRENT).get(version, rva) as Row | undefined;
  return row ? toFinding(row) : null;
}

export function getFindingHistory(
  db: Database.Database,
  version: string,
  rva: number
): LearnedFinding[] {
  const rows = db
    .prepare(
      "SELECT * FROM learned_findings WHERE version = ? AND rva = ? ORDER BY created_at DESC, id DESC"
    )
    .all(version, rva) as Row[];
  return rows.map(toFinding);
}

export function recordFinding(
  db: Database.Database,
  clock: Clock,
  input: RecordFindingInput
): { finding: LearnedFinding; deduplicated: boolean } {
  // BEGIN IMMEDIATE: a write is certain unless we bail on dedup, so take the write lock up front
  // rather than risking an upgrade failure partway through.
  const run = db.transaction((): { finding: LearnedFinding; deduplicated: boolean } => {
    const existingRow = db.prepare(SELECT_CURRENT).get(input.version, input.rva) as Row | undefined;
    const existing = existingRow ? toFinding(existingRow) : null;

    if (existing && !input.force && sameExtractionIdentity(identityOf(existing), input)) {
      return { finding: existing, deduplicated: true };
    }

    if (existing) {
      db.prepare("UPDATE learned_findings SET is_current = 0 WHERE id = ?").run(existing.id);
    }

    const info = db
      .prepare(
        `INSERT INTO learned_findings
           (version, rva, proposed_name, confidence, note, origin, raw_decompile_hash,
            model, prompt_version, extractor_version, ghidra_version, project_fingerprint,
            supersedes_id, is_current, created_at)
         VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 1, ?)`
      )
      .run(
        input.version,
        input.rva,
        input.proposedName,
        input.confidence,
        input.note,
        input.origin,
        input.rawDecompileHash,
        input.model,
        input.promptVersion,
        input.extractorVersion,
        input.ghidraVersion,
        input.projectFingerprint,
        existing?.id ?? null,
        clock.now()
      );

    const inserted = db
      .prepare("SELECT * FROM learned_findings WHERE id = ?")
      .get(Number(info.lastInsertRowid)) as Row;
    return { finding: toFinding(inserted), deduplicated: false };
  });

  return run.immediate();
}

export function listRecentFindings(
  db: Database.Database,
  opts: { limit: number; since?: string; origin?: Origin | "all"; currentOnly: boolean }
): LearnedFinding[] {
  const where: string[] = [];
  const params: (string | number)[] = [];

  if (opts.currentOnly) where.push("is_current = 1");
  if (opts.since) {
    where.push("created_at >= ?");
    params.push(opts.since);
  }
  if (opts.origin && opts.origin !== "all") {
    where.push("origin = ?");
    params.push(opts.origin);
  }

  const sql =
    "SELECT * FROM learned_findings" +
    (where.length ? ` WHERE ${where.join(" AND ")}` : "") +
    " ORDER BY created_at DESC, id DESC LIMIT ?";
  params.push(opts.limit);

  return (db.prepare(sql).all(...params) as Row[]).map(toFinding);
}
