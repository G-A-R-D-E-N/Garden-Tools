import type Database from "better-sqlite3";
import { readFileSync, readdirSync, statSync } from "node:fs";
import { join, relative, sep } from "node:path";
import type { Clock } from "../clock.js";
import { RUNTIME_ORDER, type LearningConfig, type Runtime } from "../config.js";
import { fingerprintRepo, markRepoScanned } from "./repos.js";
import {
  parseReferences,
  scalarIdsIn,
  type ParsedReference,
  type ReferenceForm,
  type ReferenceKind,
  type SlotMethod,
} from "./grammar.js";
import {
  classifyIds,
  clearRepoProvenance,
  measureIdSpace,
  recordProvenance,
  type IdSpace,
} from "./provenance.js";

export { parseReferences, scalarIdsIn } from "./grammar.js";
export type { ParsedReference, ReferenceForm, ReferenceKind, SlotMethod } from "./grammar.js";

export interface StoredSlot {
  runtime: Runtime;
  version: string;
  method: SlotMethod;
  rawValue: string;
  resolvedRva: number | null;
  unresolvedReason: string | null;
}

export interface StoredReference {
  id: number;
  repoPath: string;
  filePath: string;
  lineNumber: number;
  kind: ReferenceKind;
  form: ReferenceForm;
  rawValue: string;
  slots: StoredSlot[];
  lastSeenAt: string;
}

const SOURCE_EXTENSIONS = [".cpp", ".cc", ".cxx", ".h", ".hpp", ".inl"];
const SKIP_DIRS = new Set([".git", "node_modules", "build", "dist", "out", "vendor", "extern"]);
const MAX_FILE_BYTES = 2 * 1024 * 1024;

export function sourceFilesIn(repoPath: string): string[] {
  const files: string[] = [];

  const walk = (dir: string): void => {
    let entries: string[];
    try {
      entries = readdirSync(dir);
    } catch {
      return;
    }
    for (const entry of entries) {
      if (SKIP_DIRS.has(entry)) continue;
      const full = join(dir, entry);
      let stat;
      // Dot-directories hold copies of the same source, not new source: .claude/worktrees,
      // .release-build-*, .idea caches. Counting them double-reports every reference. Repo
      // discovery already skips them for the same reason.
      if (entry.startsWith(".")) continue;
      try {
        stat = statSync(full);
      } catch {
        continue;
      }
      if (stat.isDirectory()) {
        walk(full);
      } else if (
        SOURCE_EXTENSIONS.some((ext) => entry.endsWith(ext)) &&
        stat.size <= MAX_FILE_BYTES
      ) {
        files.push(relative(repoPath, full).split(sep).join("/"));
      }
    }
  };

  walk(repoPath);
  return files;
}

export function resolveSlot(
  staticDb: Database.Database,
  method: SlotMethod,
  rawValue: string,
  version: string
): { rva: number | null; reason: string | null } {
  if (method === "offset") {
    const rva = parseInt(rawValue.replace(/^0x/i, ""), 16);
    if (Number.isNaN(rva)) return { rva: null, reason: "Offset literal is not hex." };
    return { rva, reason: null };
  }

  const id = parseInt(rawValue, 10);
  if (Number.isNaN(id)) return { rva: null, reason: "ID literal is not a decimal integer." };

  const row = staticDb
    .prepare("SELECT rva FROM address_library WHERE version = ? AND id = ? LIMIT 1")
    .get(version, id) as { rva: number } | undefined;
  if (!row) {
    return {
      rva: null,
      reason: `No Address Library entry for id ${id} in ${version}.`,
    };
  }
  return { rva: row.rva, reason: null };
}

function slotsFor(
  staticDb: Database.Database,
  config: LearningConfig,
  ref: ParsedReference
): StoredSlot[] {
  return ref.slots.map((slot) => {
    const version = config.runtimeVersions[slot.runtime];
    if (slot.declaredUnavailable) {
      return {
        runtime: slot.runtime,
        version,
        method: slot.method,
        rawValue: slot.rawValue,
        resolvedRva: null,
        unresolvedReason: "Declared unavailable on this runtime (0 sentinel).",
      };
    }
    const { rva, reason } = resolveSlot(staticDb, slot.method, slot.rawValue, version);
    return {
      runtime: slot.runtime,
      version,
      method: slot.method,
      rawValue: slot.rawValue,
      resolvedRva: rva,
      unresolvedReason: reason,
    };
  });
}

export function replaceRepoReferences(
  db: Database.Database,
  clock: Clock,
  repoPath: string,
  scanFingerprint: string,
  refs: Omit<StoredReference, "id" | "repoPath" | "lastSeenAt">[]
): number {
  const now = clock.now();
  const run = db.transaction(() => {
    db.prepare(
      "DELETE FROM reference_slots WHERE reference_id IN (SELECT id FROM consumer_references WHERE repo_path = ?)"
    ).run(repoPath);
    db.prepare("DELETE FROM consumer_references WHERE repo_path = ?").run(repoPath);

    const insertRef = db.prepare(
      `INSERT OR IGNORE INTO consumer_references
         (repo_path, file_path, line_number, reference_kind, raw_value, form,
          resolved_version, resolved_rva, scan_fingerprint, last_seen_at)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`
    );
    const insertSlot = db.prepare(
      `INSERT OR REPLACE INTO reference_slots
         (reference_id, runtime, version, method, raw_value, resolved_rva, unresolved_reason)
       VALUES (?, ?, ?, ?, ?, ?, ?)`
    );

    for (const ref of refs) {
      // The legacy pair is kept in step with the OG slot so old readers see something coherent
      // rather than a stale value; reference_slots is what the audit actually reads.
      const og = ref.slots.find((s) => s.runtime === "kOG") ?? null;
      const info = insertRef.run(
        repoPath,
        ref.filePath,
        ref.lineNumber,
        ref.kind,
        ref.rawValue,
        ref.form,
        og?.resolvedRva != null ? og.version : null,
        og?.resolvedRva ?? null,
        scanFingerprint,
        now
      );

      const id =
        info.changes > 0
          ? Number(info.lastInsertRowid)
          : ((
              db
                .prepare(
                  `SELECT id FROM consumer_references
                    WHERE repo_path = ? AND file_path = ? AND line_number = ?
                      AND reference_kind = ? AND raw_value = ?`
                )
                .get(repoPath, ref.filePath, ref.lineNumber, ref.kind, ref.rawValue) as
                | { id: number }
                | undefined
            )?.id ?? 0);
      if (!id) continue;

      for (const slot of ref.slots) {
        insertSlot.run(
          id,
          slot.runtime,
          slot.version,
          slot.method,
          slot.rawValue,
          slot.resolvedRva,
          slot.unresolvedReason
        );
      }
    }
  });
  run.immediate();
  return refs.length;
}

export function scanRepo(
  db: Database.Database,
  staticDb: Database.Database,
  clock: Clock,
  config: LearningConfig,
  repoPath: string,
  opts: { force?: boolean } = {}
): { scanned: boolean; references: number; error: string | null } {
  const fingerprint = fingerprintRepo(repoPath);

  if (fingerprint === null) {
    const error = `Could not fingerprint ${repoPath} — missing, unreadable, or not a git repo.`;
    markRepoScanned(db, clock, repoPath, null, error);
    // The previous cache is deliberately left in place; one bad cycle must not erase known work.
    return { scanned: false, references: 0, error };
  }

  const previous = db
    .prepare(
      "SELECT repo_fingerprint, last_scanned_at, last_scan_error FROM discovered_repos WHERE path = ?"
    )
    .get(repoPath) as
    | {
        repo_fingerprint: string | null;
        last_scanned_at: string | null;
        last_scan_error: string | null;
      }
    | undefined;

  const unchanged =
    previous?.last_scanned_at &&
    previous.repo_fingerprint === fingerprint &&
    !previous.last_scan_error;
  if (unchanged && !opts.force) {
    const n = (
      db
        .prepare("SELECT COUNT(*) AS n FROM consumer_references WHERE repo_path = ?")
        .get(repoPath) as { n: number }
    ).n;
    return { scanned: false, references: n, error: null };
  }

  let space: IdSpace;
  try {
    space = measureIdSpace(staticDb, config.runtimeVersions.kOG, config.runtimeVersions.kAE);
  } catch {
    space = { ogMaxId: 0, baseline: 0, hasAe: false };
  }

  try {
    const collected: Omit<StoredReference, "id" | "repoPath" | "lastSeenAt">[] = [];
    const provenanceByFile: { filePath: string; ids: number[] }[] = [];

    for (const filePath of sourceFilesIn(repoPath)) {
      let text: string;
      try {
        text = readFileSync(join(repoPath, filePath), "utf8");
      } catch {
        continue;
      }
      const parsed = parseReferences(text);
      if (parsed.length === 0) continue;

      provenanceByFile.push({ filePath, ids: scalarIdsIn(parsed) });

      for (const ref of parsed) {
        collected.push({
          filePath,
          lineNumber: ref.lineNumber,
          kind: ref.kind,
          form: ref.form,
          rawValue: ref.rawValue,
          slots: slotsFor(staticDb, config, ref),
        });
      }
    }

    replaceRepoReferences(db, clock, repoPath, fingerprint, collected);

    clearRepoProvenance(db, repoPath);
    for (const { filePath, ids } of provenanceByFile) {
      recordProvenance(
        db,
        clock,
        repoPath,
        filePath,
        classifyIds(staticDb, ids, space, config.runtimeVersions.kAE, config.runtimeVersions.kOG)
      );
    }

    markRepoScanned(db, clock, repoPath, fingerprint, null);
    return { scanned: true, references: collected.length, error: null };
  } catch (err) {
    const error = err instanceof Error ? err.message : String(err);
    markRepoScanned(db, clock, repoPath, previous?.repo_fingerprint ?? null, error);
    return { scanned: false, references: 0, error };
  }
}

export function listReferences(
  db: Database.Database,
  opts: { repoPath?: string; version?: string } = {}
): StoredReference[] {
  const where: string[] = [];
  const params: (string | number)[] = [];
  if (opts.repoPath) {
    where.push("r.repo_path = ?");
    params.push(opts.repoPath);
  }
  if (opts.version) {
    where.push("EXISTS (SELECT 1 FROM reference_slots s WHERE s.reference_id = r.id AND s.version = ?)");
    params.push(opts.version);
  }

  const rows = db
    .prepare(
      "SELECT r.* FROM consumer_references r" +
        (where.length ? ` WHERE ${where.join(" AND ")}` : "") +
        " ORDER BY r.repo_path, r.file_path, r.line_number"
    )
    .all(...params) as {
    id: number;
    repo_path: string;
    file_path: string;
    line_number: number;
    reference_kind: ReferenceKind;
    raw_value: string;
    form: ReferenceForm;
    last_seen_at: string;
  }[];

  // Ordered by the runtime enum, not by whatever SQLite returns: slot order is positional in
  // CommonLibF4, so a caller reading slots[1] must get kNG.
  const slotStmt = db.prepare(
    `SELECT runtime, version, method, raw_value, resolved_rva, unresolved_reason
       FROM reference_slots WHERE reference_id = ?
      ORDER BY CASE runtime ${RUNTIME_ORDER.map((r, i) => `WHEN '${r}' THEN ${i}`).join(" ")} END`
  );

  return rows.map((row) => ({
    id: row.id,
    repoPath: row.repo_path,
    filePath: row.file_path,
    lineNumber: row.line_number,
    kind: row.reference_kind,
    form: row.form,
    rawValue: row.raw_value,
    lastSeenAt: row.last_seen_at,
    slots: (
      slotStmt.all(row.id) as {
        runtime: Runtime;
        version: string;
        method: SlotMethod;
        raw_value: string;
        resolved_rva: number | null;
        unresolved_reason: string | null;
      }[]
    ).map((s) => ({
      runtime: s.runtime,
      version: s.version,
      method: s.method,
      rawValue: s.raw_value,
      resolvedRva: s.resolved_rva,
      unresolvedReason: s.unresolved_reason,
    })),
  }));
}
