import { formatVa } from "../db.js";
import { getStaticContext } from "../learning/analyze.js";
import { getCurrentFinding } from "../learning/findings.js";
import { listReferences } from "../learning/references.js";
import { resolveRepoArgument } from "../learning/repos.js";
import { createAuditDeps, type AuditDeps } from "./auditHardcodedAddresses.js";

export interface HookSuggestion {
  name: string;
  rva: string;
  va: string;
  confidenceVerdict: string;
  auditNote: string | null;
  learnedFinding: { proposedName: string | null; confidence: string } | null;
}

export interface SuggestHooksResult {
  subsystem: string;
  repo: string;
  version: string;
  candidates: number;
  alreadyHooked: number;
  suggestions: HookSuggestion[];
}

export async function suggestHooks(
  args: { subsystem: string; repo: string; version?: string },
  deps?: AuditDeps
): Promise<SuggestHooksResult> {
  const d = deps ?? createAuditDeps();
  const version = args.version ?? d.config.defaultVersion;
  const repo = resolveRepoArgument(d.learningDb, args.repo);

  const candidates = d.staticDb
    .prepare(
      "SELECT rva, name FROM subsystem_index WHERE version = ? AND category = ? ORDER BY rva"
    )
    .all(version, args.subsystem) as { rva: number; name: string }[];

  // Only this repo's references. Pooling across repos is the contamination bug this tool fixes.
  // A reference counts as hooking this address if ANY of its runtime slots lands on it — a site
  // reached only through its AE id is still hooked, and must not be suggested again.
  const hooked = new Set<number>();
  for (const ref of listReferences(d.learningDb, { repoPath: repo.path })) {
    for (const slot of ref.slots) {
      if (slot.version === version && slot.resolvedRva !== null) hooked.add(slot.resolvedRva);
    }
  }

  const suggestions: HookSuggestion[] = [];
  for (const candidate of candidates) {
    if (hooked.has(candidate.rva)) continue;
    const context = getStaticContext(d.staticDb, version, candidate.rva);
    const learned = getCurrentFinding(d.learningDb, version, candidate.rva);
    suggestions.push({
      name: candidate.name,
      rva: `0x${candidate.rva.toString(16)}`,
      va: formatVa(candidate.rva),
      confidenceVerdict: context.confidenceVerdict,
      auditNote: context.auditNote,
      learnedFinding: learned
        ? { proposedName: learned.proposedName, confidence: learned.confidence }
        : null,
    });
  }

  return {
    subsystem: args.subsystem,
    repo: repo.path,
    version,
    candidates: candidates.length,
    alreadyHooked: candidates.filter((c) => hooked.has(c.rva)).length,
    suggestions,
  };
}
