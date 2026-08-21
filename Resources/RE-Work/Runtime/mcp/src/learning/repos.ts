import type Database from "better-sqlite3";
import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { readdirSync, statSync } from "node:fs";
import { basename, join, resolve } from "node:path";
import type { Clock } from "../clock.js";
import type { LearningConfig } from "../config.js";

export interface DiscoveredRepo {
  path: string;
  repoFingerprint: string | null;
  lastDiscoveredAt: string;
  lastScannedAt: string | null;
  lastScanError: string | null;
}

interface Row {
  path: string;
  repo_fingerprint: string | null;
  last_discovered_at: string;
  last_scanned_at: string | null;
  last_scan_error: string | null;
}

function toRepo(row: Row): DiscoveredRepo {
  return {
    path: row.path,
    repoFingerprint: row.repo_fingerprint,
    lastDiscoveredAt: row.last_discovered_at,
    lastScannedAt: row.last_scanned_at,
    lastScanError: row.last_scan_error,
  };
}

// Directories that never contain a consumer repo worth scanning, and are expensive to walk.
const SKIP_DIRS = new Set([
  ".git",
  "node_modules",
  "build",
  "dist",
  "out",
  "target",
  ".cache",
  "vendor",
  "extern",
]);

export function fingerprintRepo(repoPath: string): string | null {
  const git = (args: string[]): string =>
    execFileSync("git", args, {
      cwd: repoPath,
      encoding: "utf8",
      stdio: ["ignore", "pipe", "pipe"],
      maxBuffer: 16 * 1024 * 1024,
    });

  try {
    // HEAD alone would miss uncommitted edits, which are exactly the ones being iterated on.
    const head = git(["rev-parse", "HEAD"]).trim();
    const status = git(["status", "--porcelain=v1", "--untracked-files=all"]);
    return createHash("sha256").update(`${head}\n${status}`).digest("hex");
  } catch {
    return null;
  }
}

export function findGitRepos(roots: string[], maxDepth: number): string[] {
  const found: string[] = [];

  const walk = (dir: string, depth: number): void => {
    if (depth > maxDepth) return;
    let entries: string[];
    try {
      entries = readdirSync(dir);
    } catch {
      return;
    }

    if (entries.includes(".git")) {
      try {
        if (statSync(join(dir, ".git")).isDirectory()) {
          found.push(dir);
          // Stop here. Repos nested inside a repo are out of scope for this phase.
          return;
        }
      } catch {
        // fall through and keep walking
      }
    }

    for (const entry of entries) {
      if (SKIP_DIRS.has(entry) || entry.startsWith(".")) continue;
      const child = join(dir, entry);
      try {
        if (statSync(child).isDirectory()) walk(child, depth + 1);
      } catch {
        // unreadable or a broken symlink
      }
    }
  };

  for (const root of roots) walk(resolve(root), 0);
  return found;
}

export function listDiscoveredRepos(db: Database.Database): DiscoveredRepo[] {
  return (db.prepare("SELECT * FROM discovered_repos ORDER BY path").all() as Row[]).map(toRepo);
}

export function isDiscoveryStale(db: Database.Database, clock: Clock, ttlMs: number): boolean {
  const row = db
    .prepare("SELECT MAX(last_discovered_at) AS newest FROM discovered_repos")
    .get() as { newest: string | null };
  if (!row.newest) return true;
  return Date.parse(clock.now()) - Date.parse(row.newest) >= ttlMs;
}

export function discoverRepos(
  db: Database.Database,
  clock: Clock,
  config: LearningConfig,
  opts: { force?: boolean } = {}
): { discovered: number; skipped: boolean } {
  if (!opts.force && !isDiscoveryStale(db, clock, config.repoDiscoveryTtlMs)) {
    const n = (db.prepare("SELECT COUNT(*) AS n FROM discovered_repos").get() as { n: number }).n;
    return { discovered: n, skipped: true };
  }

  const paths = findGitRepos(config.workspaceRoots, config.discoveryMaxDepth);
  const now = clock.now();

  // Only discovery columns are touched; scan state survives so a rediscovery never forces a rescan.
  const upsert = db.prepare(
    `INSERT INTO discovered_repos (path, repo_fingerprint, last_discovered_at)
     VALUES (?, ?, ?)
     ON CONFLICT(path) DO UPDATE SET
       repo_fingerprint = excluded.repo_fingerprint,
       last_discovered_at = excluded.last_discovered_at`
  );

  db.transaction(() => {
    for (const path of paths) upsert.run(path, fingerprintRepo(path), now);
  })();

  return { discovered: paths.length, skipped: false };
}

export function markRepoScanned(
  db: Database.Database,
  clock: Clock,
  repoPath: string,
  fingerprint: string | null,
  error: string | null
): void {
  db.prepare(
    `UPDATE discovered_repos
        SET last_scanned_at = ?, repo_fingerprint = ?, last_scan_error = ?
      WHERE path = ?`
  ).run(clock.now(), fingerprint, error, repoPath);
}

export function resolveRepoArgument(db: Database.Database, repoArg: string): DiscoveredRepo {
  const repos = listDiscoveredRepos(db);
  const wanted = resolve(repoArg);

  const byPath = repos.find((r) => resolve(r.path) === wanted);
  if (byPath) return byPath;

  const byName = repos.filter((r) => basename(r.path) === repoArg);
  if (byName.length === 1) return byName[0];
  if (byName.length > 1) {
    throw new Error(
      `"${repoArg}" matches ${byName.length} repos: ${byName.map((r) => r.path).join(", ")}. ` +
        `Pass a full path.`
    );
  }

  throw new Error(
    `No discovered repo matches "${repoArg}". Known repos: ` +
      (repos.length ? repos.map((r) => basename(r.path)).join(", ") : "(none discovered yet)")
  );
}
