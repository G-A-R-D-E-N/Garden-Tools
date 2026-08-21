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

// pdb_symbols is name-keyed evidence only (1.10.155) — this never touches the PDB address
// column. A missing table is "unavailable" (the index predates Task 2), an indexed-but-miss
// is "absent".
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
