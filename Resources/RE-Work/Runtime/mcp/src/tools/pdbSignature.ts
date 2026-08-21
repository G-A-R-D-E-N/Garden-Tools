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

  // Deliberately selects only the name-keyed columns — the PDB address column must never
  // leave this table.
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
