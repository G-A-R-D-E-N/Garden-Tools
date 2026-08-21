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
