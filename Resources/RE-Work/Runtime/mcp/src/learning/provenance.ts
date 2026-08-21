import type Database from "better-sqlite3";
import type { Clock } from "../clock.js";

export type ProvenanceVerdict = "og_native" | "ae_native" | "mixed" | "insufficient";

export interface Provenance {
  lowIdCount: number;
  aeHitRate: number | null;
  baseline: number | null;
  verdict: ProvenanceVerdict;
}

// A file's REL::ID literals were authored against exactly one Address Library, but the syntax does
// not record which, and a bare REL::ID(x) hands the same x to all three runtimes. Guessing wrong is
// how 72k references resolved "successfully" against the wrong build.
//
// The asymmetry that makes this decidable: the OG library is contiguous over 0..max, so every id in
// that range resolves against OG whether or not it means anything there — an OG hit is worth zero
// bits. The AE library is sparse over the same range, so an OG-authored id lands in it only by
// coincidence, at the rate of AE's density there. An AE-authored id lands every time.
//
// Ids above the OG maximum are excluded: they cannot be OG-authored, so they cannot discriminate.
const MIN_SAMPLE = 30;
// An og_native call needs the observed rate to be within noise of pure coincidence. Three times the
// coincidence rate is the cutoff; between that and a half is reported as mixed rather than guessed.
const OG_NATIVE_MULTIPLE = 3;
const AE_NATIVE_RATE = 0.5;

export interface IdSpace {
  ogMaxId: number;
  baseline: number;
  hasAe: boolean;
}

// Derived from the index rather than hardcoded so a re-import with different coverage stays honest.
export function measureIdSpace(
  staticDb: Database.Database,
  ogVersion: string,
  aeVersion: string
): IdSpace {
  const og = staticDb
    .prepare("SELECT MAX(id) AS maxId FROM address_library WHERE version = ?")
    .get(ogVersion) as { maxId: number | null };
  const ogMaxId = og.maxId ?? 0;

  const ae = staticDb
    .prepare("SELECT COUNT(*) AS n FROM address_library WHERE version = ? AND id <= ?")
    .get(aeVersion, ogMaxId) as { n: number };

  return {
    ogMaxId,
    baseline: ogMaxId > 0 ? ae.n / (ogMaxId + 1) : 0,
    hasAe: ae.n > 0,
  };
}

export function classifyIds(
  staticDb: Database.Database,
  ids: number[],
  space: IdSpace,
  aeVersion: string,
  ogVersion?: string
): Provenance {
  const usable = ids.filter((id) => Number.isFinite(id) && id >= 0);
  const low = usable.filter((id) => id <= space.ogMaxId);

  if (!space.hasAe || usable.length < MIN_SAMPLE) {
    return {
      lowIdCount: low.length,
      aeHitRate: null,
      baseline: space.baseline,
      verdict: "insufficient",
    };
  }

  const inAe = staticDb.prepare(
    "SELECT 1 FROM address_library WHERE version = ? AND id = ? LIMIT 1"
  );

  if (low.length >= MIN_SAMPLE) {
    let hits = 0;
    for (const id of low) {
      if (inAe.get(aeVersion, id)) hits++;
    }
    const rate = hits / low.length;

    let verdict: ProvenanceVerdict;
    if (rate >= AE_NATIVE_RATE) verdict = "ae_native";
    else if (rate <= space.baseline * OG_NATIVE_MULTIPLE) verdict = "og_native";
    else verdict = "mixed";

    return { lowIdCount: low.length, aeHitRate: rate, baseline: space.baseline, verdict };
  }

  // Too few ambiguous ids to run the density test — which happens when a file's ids sit entirely
  // above the OG maximum. That is itself the answer: an id no OG library could contain, that the
  // AE library does contain, was written for AE. Fall back to resolution asymmetry.
  if (!ogVersion) {
    return { lowIdCount: low.length, aeHitRate: null, baseline: space.baseline, verdict: "insufficient" };
  }
  const inOg = staticDb.prepare(
    "SELECT 1 FROM address_library WHERE version = ? AND id = ? LIMIT 1"
  );
  let ae = 0;
  let og = 0;
  for (const id of usable) {
    if (inAe.get(aeVersion, id)) ae++;
    if (inOg.get(ogVersion, id)) og++;
  }
  const aeRate = ae / usable.length;
  const ogRate = og / usable.length;

  let verdict: ProvenanceVerdict;
  if (aeRate >= AE_NATIVE_RATE && ogRate < AE_NATIVE_RATE) verdict = "ae_native";
  else if (ogRate >= AE_NATIVE_RATE && aeRate < AE_NATIVE_RATE) verdict = "og_native";
  else verdict = "mixed";

  return { lowIdCount: low.length, aeHitRate: aeRate, baseline: space.baseline, verdict };
}

export function recordProvenance(
  db: Database.Database,
  clock: Clock,
  repoPath: string,
  filePath: string,
  provenance: Provenance
): void {
  db.prepare(
    `INSERT INTO file_provenance
       (repo_path, file_path, low_id_count, ae_hit_rate, baseline, verdict, computed_at)
     VALUES (?, ?, ?, ?, ?, ?, ?)
     ON CONFLICT(repo_path, file_path) DO UPDATE SET
       low_id_count = excluded.low_id_count,
       ae_hit_rate  = excluded.ae_hit_rate,
       baseline     = excluded.baseline,
       verdict      = excluded.verdict,
       computed_at  = excluded.computed_at`
  ).run(
    repoPath,
    filePath,
    provenance.lowIdCount,
    provenance.aeHitRate,
    provenance.baseline,
    provenance.verdict,
    clock.now()
  );
}

export function getProvenance(
  db: Database.Database,
  repoPath: string,
  filePath: string
): Provenance | null {
  const row = db
    .prepare(
      "SELECT low_id_count, ae_hit_rate, baseline, verdict FROM file_provenance WHERE repo_path = ? AND file_path = ?"
    )
    .get(repoPath, filePath) as
    | { low_id_count: number; ae_hit_rate: number | null; baseline: number | null; verdict: ProvenanceVerdict }
    | undefined;
  if (!row) return null;
  return {
    lowIdCount: row.low_id_count,
    aeHitRate: row.ae_hit_rate,
    baseline: row.baseline,
    verdict: row.verdict,
  };
}

export function clearRepoProvenance(db: Database.Database, repoPath: string): void {
  db.prepare("DELETE FROM file_provenance WHERE repo_path = ?").run(repoPath);
}
