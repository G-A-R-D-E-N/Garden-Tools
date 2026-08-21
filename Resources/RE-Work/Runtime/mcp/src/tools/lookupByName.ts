import { formatVa, getDb } from "../db.js";

export interface NameLookupResult {
  version: string;
  rva: string;
  va: string;
  name: string;
  signature: string;
  sizeof: number | null;
}

const MAX_RESULTS = 50;

export function lookupByName(name: string, version = "1.10.163"): NameLookupResult[] {
  const db = getDb();
  const rows = db
    .prepare(
      "SELECT rva, name, signature, sizeof FROM symbols WHERE version = ? AND name LIKE ? LIMIT ?"
    )
    .all(version, `%${name}%`, MAX_RESULTS) as {
    rva: number;
    name: string;
    signature: string;
    sizeof: number | null;
  }[];

  return rows.map((row) => ({
    version,
    rva: "0x" + row.rva.toString(16),
    va: formatVa(row.rva),
    name: row.name,
    signature: row.signature,
    sizeof: row.sizeof,
  }));
}
