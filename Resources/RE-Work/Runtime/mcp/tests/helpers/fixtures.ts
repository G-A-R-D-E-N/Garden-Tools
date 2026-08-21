import Database from "better-sqlite3";
import { execFileSync } from "node:child_process";
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { openLearningDb } from "../../src/learning/db.js";

export interface TempDb {
  db: Database.Database;
  path: string;
  dir: string;
  cleanup(): void;
}

export function tempLearningDb(): TempDb {
  const dir = mkdtempSync(join(tmpdir(), "re-learning-"));
  const path = join(dir, "re-learning.sqlite");
  const db = openLearningDb({ path });
  return {
    db,
    path,
    dir,
    cleanup() {
      try {
        db.close();
      } catch {
        // already closed by the test
      }
      rmSync(dir, { recursive: true, force: true });
    },
  };
}

// The static index as build-index.ts shapes it. Tests that exercise name resolution need the whole
// ladder present — a missing og_ae_map is not "no bridge", it is a broken index, and the audit
// should not paper over that difference.
export function tempStaticDb(seed = ""): Database.Database {
  const db = new Database(":memory:");
  db.exec(`
    CREATE TABLE symbols (version TEXT, rva INTEGER, name TEXT, signature TEXT, sizeof INTEGER);
    CREATE TABLE address_library (version TEXT, id INTEGER, rva INTEGER);
    CREATE TABLE og_ae_map (name TEXT, og_rva INTEGER, og_rel_id INTEGER, ae_rva INTEGER, ae_rel_id INTEGER);
    CREATE TABLE pdb_symbols (name TEXT, signature TEXT, calling_convention TEXT, sizeof INTEGER,
                              is_fpo INTEGER NOT NULL DEFAULT 0, pdb_rva INTEGER,
                              version TEXT NOT NULL DEFAULT '1.10.155');
    CREATE TABLE subsystem_index (version TEXT, category TEXT, rva INTEGER, name TEXT);
    CREATE TABLE name_disagreements (address TEXT, rva INTEGER, existing_name TEXT, decoded_name TEXT, note TEXT, verdict_155 TEXT);
    CREATE TABLE misname_corrections (address TEXT, rva INTEGER, old_name TEXT, new_name TEXT, method TEXT, note TEXT);
    CREATE TABLE misname_reverts (address TEXT, rva INTEGER, wrongly_renamed_to TEXT, revert_to TEXT);
  `);
  if (seed) db.exec(seed);
  return db;
}

// A synthetic pair of Address Libraries with the density asymmetry the provenance test relies on:
// OG contiguous over its whole id range, AE sparse over that range plus its own high ids.
export function seedIdSpaces(
  db: Database.Database,
  opts: {
    ogMax?: number;
    aeStride?: number;
    ogVersion?: string;
    ngVersion?: string;
    aeVersion?: string;
  } = {}
): void {
  const ogMax = opts.ogMax ?? 2000;
  const aeStride = opts.aeStride ?? 20;
  const ogVersion = opts.ogVersion ?? "1.10.163";
  const aeVersion = opts.aeVersion ?? "1.11.221";
  const ngVersion = opts.ngVersion ?? "1.10.984";
  const insert = db.prepare("INSERT INTO address_library (version, id, rva) VALUES (?, ?, ?)");
  db.transaction(() => {
    for (let id = 0; id <= ogMax; id++) insert.run(ogVersion, id, 0x1000 + id * 0x10);
    // NG and AE draw from the same post-1.10.163 id space, at their own layouts.
    for (const [version, base] of [
      [ngVersion, 0x800000],
      [aeVersion, 0xa00000],
    ] as const) {
      for (let id = 0; id <= ogMax; id += aeStride) insert.run(version, id, base + id * 0x10);
      for (let id = 100000; id < 100000 + 500; id++) insert.run(version, id, base + id * 0x4);
    }
  })();
}

// The rva seedIdSpaces assigns, so a test can point og_ae_map at the address that will actually be
// resolved instead of a hand-computed one that drifts.
export function seededRva(version: string, id: number): number {
  if (version === "1.10.163") return 0x1000 + id * 0x10;
  const base = version === "1.10.984" ? 0x800000 : 0xa00000;
  return id >= 100000 ? base + id * 0x4 : base + id * 0x10;
}

export function tempGitRepo(files: Record<string, string> = {}): {
  path: string;
  cleanup(): void;
  write(relPath: string, content: string): void;
  commitAll(message: string): void;
} {
  const dir = mkdtempSync(join(tmpdir(), "re-repo-"));
  const git = (...args: string[]) =>
    execFileSync("git", args, { cwd: dir, stdio: "pipe", encoding: "utf8" });

  git("init", "-q", "-b", "main");
  git("config", "user.email", "test@example.com");
  git("config", "user.name", "Test");

  const write = (relPath: string, content: string) => {
    const full = join(dir, relPath);
    mkdirSync(join(full, ".."), { recursive: true });
    writeFileSync(full, content);
  };

  for (const [relPath, content] of Object.entries(files)) write(relPath, content);
  write(".keep", "");
  git("add", "-A");
  git("commit", "-q", "-m", "initial");

  return {
    path: dir,
    write,
    commitAll(message: string) {
      git("add", "-A");
      git("commit", "-q", "-m", message);
    },
    cleanup: () => rmSync(dir, { recursive: true, force: true }),
  };
}
