import type Database from "better-sqlite3";
import { RUNTIME_VERSIONS } from "../config.js";
import { formatVa, getDb, parseAddressToRva } from "../db.js";

export interface OgAeMapResult {
  name: string;
  ogRva: string | null;
  ogVa: string | null;
  ogRelId: number | null;
  // Resolved from ae_rel_id through the AE Address Library, never read from og_ae_map.ae_rva —
  // see the note on AE_ADDRESSES_ARE_IDS below.
  aeRva: string | null;
  aeVa: string | null;
  aeRelId: number | null;
}

interface MapRow {
  name: string;
  og_rva: number | null;
  og_rel_id: number | null;
  ae_rva: number | null;
  ae_rel_id: number | null;
}

const OG_VERSION = RUNTIME_VERSIONS.kOG;
const AE_VERSION = RUNTIME_VERSIONS.kAE;

// og_ae_map.ae_rva is a 1.11.191 address, not an AE (1.11.221) one — the column was materialized
// against that build. Measured 2026-08-14 over 3000 rows: every ae_rva resolves in the 191 Address
// Library and equals what its own ae_rel_id resolves to there, while it agrees with 1.11.221 only
// 58.7% of the time, wherever the two builds happen not to have moved that function. The partial
// agreement is the danger — right often enough to look trustworthy, and the rest are confident
// wrong answers rather than misses. NavMesh::CreateGroupData is the worked case: 0x77f7a0 in 191,
// 0x77f7d0 in 221, and 0x77f7a0 in 221 is a different function entirely.
//
// ae_rel_id is exact, because Address Library ids are stable across the 1.11.x builds, so every AE
// address here is resolved in id space and the raw column is never queried or returned.
const MAX_RESULTS = 25;

// A leading-wildcard LIKE cannot use a b-tree index, so a name search was a full scan of og_ae_map
// — 267k rows, ~40 ms, the slowest path in this tool by two orders of magnitude. og_ae_map_fts is a
// trigram FTS5 mirror, which SQLite *can* use for an infix LIKE. Prepared once per database and
// cached, because the fallback below is decided at prepare time.
const nameSearchCache = new WeakMap<Database.Database, Database.Statement>();

export function nameSearchStmt(staticDb: Database.Database): Database.Statement {
  let stmt = nameSearchCache.get(staticDb);
  if (!stmt) {
    try {
      stmt = staticDb.prepare(
        `SELECT m.* FROM og_ae_map_fts f
           JOIN og_ae_map m ON m.rowid = f.rowid
          WHERE f.name LIKE ?
          LIMIT ?`
      );
    } catch {
      // An index built before the FTS mirror existed. Same results, just slowly.
      stmt = staticDb.prepare("SELECT * FROM og_ae_map WHERE name LIKE ? LIMIT ?");
    }
    nameSearchCache.set(staticDb, stmt);
  }
  return stmt;
}

function idAt(staticDb: Database.Database, version: string, rva: number): number | null {
  const row = staticDb
    .prepare("SELECT id FROM address_library WHERE version = ? AND rva = ? LIMIT 1")
    .get(version, rva) as { id: number } | undefined;
  return row?.id ?? null;
}

function formatRow(staticDb: Database.Database, row: MapRow): OgAeMapResult {
  let aeRva: number | null = null;
  if (row.ae_rel_id !== null) {
    const hit = staticDb
      .prepare("SELECT rva FROM address_library WHERE version = ? AND id = ? LIMIT 1")
      .get(AE_VERSION, row.ae_rel_id) as { rva: number } | undefined;
    aeRva = hit?.rva ?? null;
  }

  return {
    name: row.name,
    ogRva: row.og_rva !== null ? "0x" + row.og_rva.toString(16) : null,
    ogVa: row.og_rva !== null ? formatVa(row.og_rva) : null,
    ogRelId: row.og_rel_id,
    aeRva: aeRva !== null ? "0x" + aeRva.toString(16) : null,
    aeVa: aeRva !== null ? formatVa(aeRva) : null,
    aeRelId: row.ae_rel_id,
  };
}

export function mapOgAe(
  nameOrAddress: string,
  staticDb: Database.Database = getDb()
): OgAeMapResult[] {
  const rva = parseAddressToRva(nameOrAddress);

  if (rva !== null && /^(0x)?[0-9a-f]+$/i.test(nameOrAddress.trim())) {
    // An address could be from either build, and we cannot tell which by looking at it. Try both
    // id spaces. A null id compares as NULL in SQL, so it simply never matches.
    const rows = staticDb
      .prepare(
        `SELECT * FROM og_ae_map
          WHERE og_rva = ? OR og_rel_id = ? OR ae_rel_id = ?
          LIMIT ?`
      )
      .all(
        rva,
        idAt(staticDb, OG_VERSION, rva),
        idAt(staticDb, AE_VERSION, rva),
        MAX_RESULTS
      ) as MapRow[];
    if (rows.length > 0) return rows.map((row) => formatRow(staticDb, row));
  }

  const rows = nameSearchStmt(staticDb).all(`%${nameOrAddress}%`, MAX_RESULTS) as MapRow[];
  return rows.map((row) => formatRow(staticDb, row));
}
