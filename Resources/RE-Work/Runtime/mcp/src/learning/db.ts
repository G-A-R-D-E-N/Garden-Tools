import Database from "better-sqlite3";
import { LEARNING_DB_PATH } from "../paths.js";
import { MIGRATIONS } from "./migrations.js";

// SQLite 3.7.0–3.51.2 carry a WAL-reset data race between a checkpoint and a concurrent write
// transaction (fixed in 3.51.3). This design keeps multiple connections open on one WAL file,
// which is exactly the shape that triggers it.
export const WAL_SAFE_MIN_VERSION = "3.51.3";

export class WalUnsafeError extends Error {
  constructor(found: string) {
    super(
      `SQLite ${found} is affected by the WAL-reset bug; ${WAL_SAFE_MIN_VERSION} or newer is ` +
        `required for learning writes. The read-only static index tools are unaffected.`
    );
    this.name = "WalUnsafeError";
  }
}

export function compareSqliteVersions(a: string, b: string): number {
  const pa = a.split(".").map(Number);
  const pb = b.split(".").map(Number);
  for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
    const diff = (pa[i] ?? 0) - (pb[i] ?? 0);
    if (diff !== 0) return diff;
  }
  return 0;
}

export function assertWalSafe(version: string): void {
  if (compareSqliteVersions(version, WAL_SAFE_MIN_VERSION) < 0) {
    throw new WalUnsafeError(version);
  }
}

export function runMigrations(db: Database.Database): number {
  let current = db.pragma("user_version", { simple: true }) as number;
  for (const migration of MIGRATIONS) {
    if (migration.version <= current) continue;
    db.transaction(() => {
      db.exec(migration.sql);
      db.pragma(`user_version = ${migration.version}`);
    })();
    current = migration.version;
  }
  return current;
}

export function openLearningDb(
  options: { path?: string; readonly?: boolean } = {}
): Database.Database {
  const path = options.path ?? LEARNING_DB_PATH;
  const db = new Database(path, { readonly: options.readonly ?? false });

  // Gate on the version the Node binding actually links, not the system sqlite3 CLI.
  const version = (db.prepare("SELECT sqlite_version() AS v").get() as { v: string }).v;
  try {
    assertWalSafe(version);
  } catch (err) {
    db.close();
    throw err;
  }

  if (!options.readonly) {
    db.pragma("journal_mode = WAL");
    db.pragma("foreign_keys = ON");
    db.pragma("busy_timeout = 5000");
    runMigrations(db);
  }
  return db;
}

export function sqliteVersionOf(db: Database.Database): string {
  return (db.prepare("SELECT sqlite_version() AS v").get() as { v: string }).v;
}
