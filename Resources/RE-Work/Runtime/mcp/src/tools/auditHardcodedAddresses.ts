import type Database from "better-sqlite3";
import { existsSync, statSync } from "node:fs";
import { resolve } from "node:path";
import { systemClock, type Clock } from "../clock.js";
import { loadConfig, RUNTIME_ORDER, type LearningConfig, type Runtime } from "../config.js";
import { formatVa, getDb } from "../db.js";
import { openLearningDb } from "../learning/db.js";
import { getCurrentFinding } from "../learning/findings.js";
import { detectContradiction, getStaticContext, type Contradiction } from "../learning/analyze.js";
import { discoverRepos, listDiscoveredRepos } from "../learning/repos.js";
import { listReferences, scanRepo } from "../learning/references.js";
import type { ReferenceForm, ReferenceKind } from "../learning/grammar.js";
import { bridgeOgIdToAeId, resolveName, type ResolvedName } from "../learning/names.js";
import { getProvenance, type Provenance } from "../learning/provenance.js";

export interface AuditDeps {
  staticDb: Database.Database;
  learningDb: Database.Database;
  clock: Clock;
  config: LearningConfig;
}

export type Verdict =
  | "ok"
  | "single_id_all_runtimes"
  | "slot_unresolved"
  | "slot_name_mismatch"
  | "unbridged"
  | "declared_unavailable"
  | "unresolvable_symbol"
  | "stale_scan";

// A generated or vendored table is upstream's to fix, and one of them can contribute tens of
// thousands of identical rows. Worth reporting, never worth putting first.
const VENDORED = /(^|\/)(lib|libs|extern|external|third_party|vendor)\//i;
const GENERATED = /\/(IDs|IDs_VTABLE|IDs_RTTI|VTABLE_IDs)\.h$/i;

export interface AuditGroup {
  repo: string;
  file: string;
  verdict: Verdict;
  count: number;
  vendored: boolean;
  generated: boolean;
  provenance: Provenance | null;
  detail: string | null;
  suggestion: string | null;
  examples: { location: string; rawValue: string }[];
}

export interface AuditedSlot {
  runtime: Runtime;
  version: string;
  method: string;
  rawValue: string;
  rva: string | null;
  va: string | null;
  name: string | null;
  nameSource: string | null;
  bridgeName: string | null;
  pdbSignature: string | null;
  subsystem: string | null;
  unresolvedReason: string | null;
}

export interface AuditedReference {
  repo: string;
  location: string;
  kind: ReferenceKind;
  form: ReferenceForm;
  rawValue: string;
  verdict: Verdict;
  detail: string | null;
  suggestion: string | null;
  provenance: Provenance | null;
  slots: AuditedSlot[];
  staticIndex: { name: string | null; confidenceVerdict: string } | null;
  learnedFinding: { proposedName: string | null; confidence: string; createdAt: string } | null;
  contradiction: Contradiction | null;
  unresolvedReason: string | null;
}

export interface AuditResult {
  nameAuthorityVersion: string;
  runtimeVersions: Record<Runtime, string>;
  scannedRepos: { repo: string; scanned: boolean; references: number; error: string | null }[];
  totals: {
    references: number;
    slots: number;
    resolvedSlots: number;
    unresolvedSlots: number;
    withLearnedFinding: number;
    contradictions: number;
    byVerdict: Record<Verdict, number>;
    firstPartyReferences: number;
  };
  // The report. One row per (file, verdict), first-party before vendored, because a generated
  // header repeating the same mistake 12,000 times is one finding, not twelve thousand.
  groups: AuditGroup[];
  references: AuditedReference[];
  truncated: { references: number } | null;
}

const DEFAULT_DETAIL_LIMIT = 100;
const EXAMPLES_PER_GROUP = 3;

const VTABLE_REASON =
  "RE::VTABLE::Class[n] is a CommonLibF4 header symbol; this index cannot map it to an RVA.";

export function createAuditDeps(overrides: Partial<AuditDeps> = {}): AuditDeps {
  return {
    staticDb: overrides.staticDb ?? getDb(),
    learningDb: overrides.learningDb ?? openLearningDb(),
    clock: overrides.clock ?? systemClock,
    config: overrides.config ?? loadConfig(),
  };
}

function emptyVerdictTally(): Record<Verdict, number> {
  return {
    ok: 0,
    single_id_all_runtimes: 0,
    slot_unresolved: 0,
    slot_name_mismatch: 0,
    unbridged: 0,
    declared_unavailable: 0,
    unresolvable_symbol: 0,
    stale_scan: 0,
  };
}

// A name comparison that ignores the decoration differences between the curated OG table and the
// bridge's IDA-derived spellings. Only a genuine disagreement should read as one.
function sameFunction(a: string | null, b: string | null): boolean {
  if (!a || !b) return true;
  const norm = (s: string) => s.replace(/\(.*$/, "").replace(/^_+/, "").toLowerCase();
  return norm(a) === norm(b);
}

function judge(
  form: ReferenceForm,
  slots: AuditedSlot[],
  provenance: Provenance | null,
  staticDb: Database.Database
): { verdict: Verdict; detail: string | null; suggestion: string | null } {
  if (form === "vtable_symbol") {
    return { verdict: "unresolvable_symbol", detail: VTABLE_REASON, suggestion: null };
  }

  // A row stored before the slot model existed. Saying so beats judging it on absent evidence,
  // which would read every one of them as "declared unavailable on all runtimes".
  if (slots.length === 0) {
    return {
      verdict: "stale_scan",
      detail: "Stored before runtime slots existed; re-run with force_rescan to judge it.",
      suggestion: null,
    };
  }

  const isScalar = form === "id_scalar" || form === "offset";

  // The same question without needing a statistical sample, so it also works on a hand-written
  // file holding three ids rather than three thousand. A scalar puts one id in every slot; if the
  // bridge says this OG id is paired with a *different* AE id, then the literal names two
  // different functions. Proof about one reference, not an inference about a file.
  if (isScalar) {
    const og = slots.find((s) => s.runtime === "kOG" && s.method === "id");
    const ae = slots.find((s) => s.runtime === "kAE" && s.method === "id");
    if (og && ae && og.rawValue === ae.rawValue) {
      const ogId = parseInt(og.rawValue, 10);
      const pairedAe = Number.isNaN(ogId) ? null : bridgeOgIdToAeId(staticDb, ogId);
      if (pairedAe !== null && pairedAe !== ogId) {
        return {
          verdict: "single_id_all_runtimes",
          detail:
            `One id for every runtime. og_ae_map pairs OG id ${ogId}` +
            `${og.name ? ` (${og.name})` : ""} with AE id ${pairedAe}, so on kAE this literal ` +
            `names a different function.`,
          suggestion: `REL::VariantID{ ${ogId}, ${pairedAe} }`,
        };
      }
    }
  }

  // The population-level form of the same question, for a generated table whose ids the bridge
  // does not carry. CommonLibF4 looks the id up in whichever library is loaded, so on the runtimes
  // it was not written for it lands somewhere unrelated rather than failing.
  if (isScalar && provenance && (provenance.verdict === "og_native" || provenance.verdict === "ae_native")) {
    const authored = provenance.verdict === "og_native" ? "kOG" : "kAE";
    const wrong = slots.filter((s) => s.runtime !== authored);
    if (wrong.length > 0) {
      const og = slots.find((s) => s.runtime === "kOG");
      let suggestion: string | null = null;
      if (authored === "kOG" && og?.method === "id") {
        const ogId = parseInt(og.rawValue, 10);
        const aeId = Number.isNaN(ogId) ? null : bridgeOgIdToAeId(staticDb, ogId);
        if (aeId !== null && aeId !== ogId) {
          suggestion = `REL::VariantID{ ${ogId}, ${aeId} }`;
        }
      }
      return {
        verdict: "single_id_all_runtimes",
        detail:
          `The literal fills all ${slots.length} runtime slots, but this file's ids measure as ` +
          `${provenance.verdict.replace("_", "-")} ` +
          `(${provenance.aeHitRate === null ? "n/a" : (100 * provenance.aeHitRate).toFixed(1) + "%"} ` +
          `AE-hit vs ${provenance.baseline === null ? "n/a" : (100 * provenance.baseline).toFixed(1) + "%"} ` +
          `by chance). On ${wrong.map((s) => s.runtime).join(", ")} it resolves to an unrelated address.`,
        suggestion,
      };
    }
  }

  if (slots.every((s) => s.unresolvedReason?.includes("0 sentinel"))) {
    return {
      verdict: "declared_unavailable",
      detail: "Every slot is the 0 sentinel — the author marked this absent on all runtimes.",
      suggestion: null,
    };
  }

  const unresolved = slots.filter((s) => s.rva === null && !s.unresolvedReason?.includes("0 sentinel"));
  if (unresolved.length > 0) {
    return {
      verdict: "slot_unresolved",
      detail: unresolved.map((s) => `${s.runtime}: ${s.unresolvedReason}`).join(" "),
      suggestion: null,
    };
  }

  const named = slots.filter((s) => s.name !== null);
  const distinct = [...new Set(named.map((s) => s.name!))];
  if (distinct.length > 1) {
    const mismatched = named.some((s) => !sameFunction(s.name, named[0].name));
    if (mismatched) {
      return {
        verdict: "slot_name_mismatch",
        detail: named.map((s) => `${s.runtime}=${s.name}`).join(" vs "),
        suggestion: null,
      };
    }
  }

  // Only kAE can be "unbridged". kOG names come from the curated table and a gap there is
  // ordinary; kNG has no name authority at all, so flagging it would fire on every reference in
  // the workspace and bury the findings that mean something.
  const blindAe = slots.filter((s) => s.runtime === "kAE" && s.rva !== null && s.name === null);
  if (blindAe.length > 0) {
    return {
      verdict: "unbridged",
      detail: `kAE resolves to ${blindAe[0].rva} but og_ae_map has no entry for it, so it cannot be named.`,
      suggestion: null,
    };
  }

  return { verdict: "ok", detail: null, suggestion: null };
}

export async function auditHardcodedAddresses(
  args: {
    paths?: string[];
    force_rescan?: boolean;
    verdicts?: Verdict[];
    include_ok?: boolean;
    detail_limit?: number;
  },
  deps?: AuditDeps
): Promise<AuditResult> {
  const d = deps ?? createAuditDeps();
  const ogVersion = d.config.runtimeVersions.kOG;
  const aeVersion = d.config.runtimeVersions.kAE;

  let targets: string[];
  if (args.paths && args.paths.length > 0) {
    // Explicit scope. Canonicalize and validate before touching the filesystem further.
    targets = args.paths.map((raw) => {
      const path = resolve(raw);
      if (!existsSync(path) || !statSync(path).isDirectory()) {
        throw new Error(`Path does not exist or is not a directory: ${path}`);
      }
      return path;
    });
  } else {
    discoverRepos(d.learningDb, d.clock, d.config, { force: args.force_rescan });
    targets = listDiscoveredRepos(d.learningDb).map((r) => r.path);
  }

  const scannedRepos = targets.map((repoPath) => {
    const result = scanRepo(d.learningDb, d.staticDb, d.clock, d.config, repoPath, {
      force: args.force_rescan,
    });
    return {
      repo: repoPath,
      scanned: result.scanned,
      references: result.references,
      error: result.error,
    };
  });

  const references: AuditedReference[] = [];
  const byVerdict = emptyVerdictTally();
  let slotCount = 0;
  let resolvedSlots = 0;

  // Six vendored copies of the same generated header ask for the same addresses six times over.
  // One cache per run turns ~100k lookups into a few thousand.
  const nameCache = new Map<string, ResolvedName>();
  const nameOf = (version: string, rva: number): ResolvedName => {
    const key = `${version}:${rva}`;
    let hit = nameCache.get(key);
    if (!hit) {
      hit = resolveName(d.staticDb, version, rva, ogVersion, aeVersion);
      nameCache.set(key, hit);
    }
    return hit;
  };

  for (const repoPath of targets) {
    for (const ref of listReferences(d.learningDb, { repoPath })) {
      const provenance = getProvenance(d.learningDb, repoPath, ref.filePath);

      const slots: AuditedSlot[] = ref.slots.map((slot) => {
        let name: ResolvedName = {
          name: null,
          source: null,
          ogRva: null,
          bridgeName: null,
          pdbSignature: null,
          subsystem: null,
        };
        if (slot.resolvedRva !== null) {
          name = nameOf(slot.version, slot.resolvedRva);
        }
        return {
          runtime: slot.runtime,
          version: slot.version,
          method: slot.method,
          rawValue: slot.rawValue,
          rva: slot.resolvedRva !== null ? `0x${slot.resolvedRva.toString(16)}` : null,
          va: slot.resolvedRva !== null ? formatVa(slot.resolvedRva) : null,
          name: name.name,
          nameSource: name.source,
          bridgeName: name.bridgeName,
          pdbSignature: name.pdbSignature,
          subsystem: name.subsystem,
          unresolvedReason: slot.unresolvedReason,
        };
      });

      slotCount += slots.length;
      resolvedSlots += slots.filter((s) => s.rva !== null).length;

      const { verdict, detail, suggestion } = judge(ref.form, slots, provenance, d.staticDb);

      // The audit trail and any learned finding are anchored to the OG slot, because that is the
      // only build those records describe.
      const og = ref.slots.find((s) => s.runtime === "kOG" && s.resolvedRva !== null);
      const context = og ? getStaticContext(d.staticDb, og.version, og.resolvedRva!) : null;
      const learned = og ? getCurrentFinding(d.learningDb, og.version, og.resolvedRva!) : null;

      references.push({
        repo: repoPath,
        location: `${ref.filePath}:${ref.lineNumber}`,
        kind: ref.kind,
        form: ref.form,
        rawValue: ref.rawValue,
        verdict,
        detail,
        suggestion,
        provenance,
        slots,
        staticIndex: context
          ? { name: context.staticName, confidenceVerdict: context.confidenceVerdict }
          : null,
        learnedFinding: learned
          ? {
              proposedName: learned.proposedName,
              confidence: learned.confidence,
              createdAt: learned.createdAt,
            }
          : null,
        contradiction: context && learned ? detectContradiction(context, learned) : null,
        unresolvedReason: ref.form === "vtable_symbol" ? VTABLE_REASON : null,
      });
      byVerdict[verdict]++;
    }
  }

  // A clean reference is a row nobody needs to read. Default to withholding them so the signal is
  // the report, not something to be found inside it; totals still count every one.
  const wanted = args.verdicts;
  const visible = references.filter((r) => {
    if (wanted && wanted.length > 0) return wanted.includes(r.verdict);
    return args.include_ok ? true : r.verdict !== "ok";
  });

  const groups = new Map<string, AuditGroup>();
  for (const ref of visible) {
    const key = `${ref.repo} ${ref.location.split(":")[0]} ${ref.verdict}`;
    const file = ref.location.split(":")[0];
    let group = groups.get(key);
    if (!group) {
      group = {
        repo: ref.repo,
        file,
        verdict: ref.verdict,
        count: 0,
        vendored: VENDORED.test(`/${file}`),
        generated: GENERATED.test(`/${file}`),
        provenance: ref.provenance,
        detail: ref.detail,
        // A group's suggestion is only meaningful if it is the same fix for every member, which
        // it is not once the ids differ. Kept only where the group is a single reference.
        suggestion: ref.suggestion,
        examples: [],
      };
      groups.set(key, group);
    }
    group.count++;
    if (group.count > 1) group.suggestion = null;
    if (group.examples.length < EXAMPLES_PER_GROUP) {
      group.examples.push({ location: ref.location, rawValue: ref.rawValue });
    }
  }

  const ranked = [...groups.values()].sort((a, b) => {
    const weight = (g: AuditGroup) => (g.vendored || g.generated ? 1 : 0);
    return weight(a) - weight(b) || b.count - a.count || a.file.localeCompare(b.file);
  });

  const limit = args.detail_limit ?? DEFAULT_DETAIL_LIMIT;
  const detail = visible.slice(0, limit);

  return {
    nameAuthorityVersion: ogVersion,
    runtimeVersions: d.config.runtimeVersions,
    scannedRepos,
    totals: {
      references: references.length,
      slots: slotCount,
      resolvedSlots,
      unresolvedSlots: slotCount - resolvedSlots,
      withLearnedFinding: references.filter((r) => r.learnedFinding !== null).length,
      contradictions: references.filter((r) => r.contradiction !== null).length,
      byVerdict,
      firstPartyReferences: references.filter(
        (r) => !VENDORED.test(`/${r.location.split(":")[0]}`)
      ).length,
    },
    groups: ranked,
    references: detail,
    truncated: visible.length > detail.length ? { references: visible.length - detail.length } : null,
  };
}

export { RUNTIME_ORDER };
