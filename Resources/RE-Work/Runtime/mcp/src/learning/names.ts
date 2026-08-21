import type Database from "better-sqlite3";

export type NameSource = "symbols" | "og_ae_map" | null;

export interface ResolvedName {
  name: string | null;
  source: NameSource;
  // The 1.10.163 address this name was read from. For an AE slot this is the bridged OG address,
  // which is what makes the curated name applicable at all; it is never a hook address for AE.
  ogRva: number | null;
  // og_ae_map's own (IDA-derived) name. Kept alongside the curated one because the two disagree
  // often enough to matter: the map carries full signatures, the curated set carries the audit
  // trail. Neither is a strict improvement on the other.
  bridgeName: string | null;
  // The real Microsoft private-PDB signature for this name, when the 1.10.155 dump has it. An
  // independent authority — names/signatures only. Its addresses are for another build entirely.
  pdbSignature: string | null;
  subsystem: string | null;
}

// subsystem_index's primary key is (version, category, rva), which SQLite treats as a covering
// index for a `category` lookup and then scans the whole version prefix — measured at ~10 ms a
// call against 365k rows, which dominated the audit. idx_subsystem_index_rva is (version, rva) and
// is the right one; naming it explicitly is the only way to stop the planner choosing otherwise.
const subsystemCache = new WeakMap<Database.Database, Database.Statement>();
function subsystemStmt(staticDb: Database.Database): Database.Statement {
  let stmt = subsystemCache.get(staticDb);
  if (!stmt) {
    try {
      stmt = staticDb.prepare(
        "SELECT category FROM subsystem_index INDEXED BY idx_subsystem_index_rva WHERE version = ? AND rva = ? LIMIT 1"
      );
    } catch {
      // An index built before that name existed, or a test fixture without it.
      stmt = staticDb.prepare(
        "SELECT category FROM subsystem_index WHERE version = ? AND rva = ? LIMIT 1"
      );
    }
    subsystemCache.set(staticDb, stmt);
  }
  return stmt;
}

const EMPTY: ResolvedName = {
  name: null,
  source: null,
  ogRva: null,
  bridgeName: null,
  pdbSignature: null,
  subsystem: null,
};

function decorate(
  staticDb: Database.Database,
  ogVersion: string,
  partial: Omit<ResolvedName, "pdbSignature" | "subsystem">
): ResolvedName {
  let pdbSignature: string | null = null;
  if (partial.name) {
    const row = staticDb
      .prepare("SELECT signature FROM pdb_symbols WHERE name = ? LIMIT 1")
      .get(partial.name) as { signature: string } | undefined;
    pdbSignature = row?.signature ?? null;
  }

  let subsystem: string | null = null;
  if (partial.ogRva !== null) {
    const row = subsystemStmt(staticDb).get(ogVersion, partial.ogRva) as
      | { category: string }
      | undefined;
    subsystem = row?.category ?? null;
  }

  return { ...partial, pdbSignature, subsystem };
}

// Names an address in `version`. Curated names exist only for the OG build, so an address in any
// other build is named by bridging back through og_ae_map — never by reading the OG symbol table at
// the same numeric address, which is the mistake this whole change exists to stop.
export function resolveName(
  staticDb: Database.Database,
  version: string,
  rva: number,
  ogVersion: string,
  aeVersion: string
): ResolvedName {
  if (version === ogVersion) {
    const row = staticDb
      .prepare("SELECT name FROM symbols WHERE version = ? AND rva = ? LIMIT 1")
      .get(version, rva) as { name: string } | undefined;
    if (!row) return decorate(staticDb, ogVersion, { ...EMPTY, ogRva: rva });
    return decorate(staticDb, ogVersion, {
      name: row.name,
      source: "symbols",
      ogRva: rva,
      bridgeName: null,
    });
  }

  // Only the AE column of the bridge is populated, so an NG address has no name path today. Saying
  // so is the honest answer; guessing from the OG table at the same RVA is not.
  if (version !== aeVersion) return EMPTY;

  // Keyed by Address Library id, never by og_ae_map.ae_rva. That column holds 1.11.191 addresses,
  // not AE ones — measured 2026-08-14, all 3000 sampled ae_rva values resolve in the 191 Address
  // Library and equal what their own ae_rel_id resolves to there, while agreeing with 1.11.221
  // only 58.7% of the time. Matching on it therefore names a neighbouring function whenever the
  // two builds moved that code, which reads as a confident, plausible, wrong answer (id
  // 554863/2230378 came back GetDesiredAimHeading instead of GetAimVector; NavMesh::CreateGroupData
  // is 0x77f7a0 in 191 and 0x77f7d0 in 221). og_rva is sound and still used; only the AE address
  // column is untrustworthy.
  const aeId = staticDb
    .prepare("SELECT id FROM address_library WHERE version = ? AND rva = ? LIMIT 1")
    .get(aeVersion, rva) as { id: number } | undefined;
  if (!aeId) return EMPTY;

  const bridge = staticDb
    .prepare("SELECT name, og_rva FROM og_ae_map WHERE ae_rel_id = ? LIMIT 1")
    .get(aeId.id) as { name: string; og_rva: number | null } | undefined;
  if (!bridge) return EMPTY;

  const curated =
    bridge.og_rva !== null
      ? (staticDb
          .prepare("SELECT name FROM symbols WHERE version = ? AND rva = ? LIMIT 1")
          .get(ogVersion, bridge.og_rva) as { name: string } | undefined)
      : undefined;

  return decorate(staticDb, ogVersion, {
    name: curated?.name ?? bridge.name,
    source: curated ? "symbols" : "og_ae_map",
    ogRva: bridge.og_rva,
    bridgeName: bridge.name,
  });
}

// The AE Address Library id paired with an OG id. This is the value a bare REL::ID(ogId) should
// have been given for its AE slot, so it is both the proof and the fix.
//
// Everything here works in id space. og_ae_map's ae_rva column holds 1.11.191 addresses and must
// never be matched on; ae_rel_id is exact.
export function bridgeOgIdToAeId(staticDb: Database.Database, ogRelId: number): number | null {
  const row = staticDb
    .prepare("SELECT ae_rel_id FROM og_ae_map WHERE og_rel_id = ? AND ae_rel_id IS NOT NULL LIMIT 1")
    .get(ogRelId) as { ae_rel_id: number } | undefined;
  return row?.ae_rel_id ?? null;
}

export function bridgeAeIdToOgId(staticDb: Database.Database, aeRelId: number): number | null {
  const row = staticDb
    .prepare("SELECT og_rel_id FROM og_ae_map WHERE ae_rel_id = ? AND og_rel_id IS NOT NULL LIMIT 1")
    .get(aeRelId) as { og_rel_id: number } | undefined;
  return row?.og_rel_id ?? null;
}
