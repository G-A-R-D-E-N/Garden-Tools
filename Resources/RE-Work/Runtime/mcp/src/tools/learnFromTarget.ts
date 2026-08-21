import { formatVa } from "../db.js";
import {
  analyzeTarget,
  createDefaultDeps,
  type AnalyzeDeps,
  type Contradiction,
} from "../learning/analyze.js";

export interface LearnFromTargetResult {
  target: string;
  version: string;
  rva: string;
  va: string;
  resolvedName: string;
  analysisRun: boolean;
  finding: {
    proposedName: string | null;
    confidence: string;
    note: string | null;
    origin: string;
    createdAt: string;
    supersedesId: number | null;
  };
  staticIndex: {
    name: string | null;
    signature: string | null;
    addressLibraryId: number | null;
    confidenceVerdict: string;
    auditNote: string | null;
  };
  contradiction: Contradiction | null;
  warning: string | null;
}

function warningFor(contradiction: Contradiction | null): string | null {
  if (!contradiction) return null;
  const { staticName, proposedName, staticIsAudited } = contradiction;
  if (staticIsAudited) {
    return (
      `CONTRADICTION: the model proposes "${proposedName}" but the static index name ` +
      `"${staticName}" is backed by an audit trail. The audited name wins unless you verify ` +
      `otherwise by hand — this learned finding is evidence, not a correction.`
    );
  }
  return (
    `The model disagrees with the static index: proposed "${proposedName}" vs indexed ` +
    `"${staticName}". Neither is audited; treat both as unverified.`
  );
}

export async function learnFromTarget(
  args: { target: string; version?: string; force?: boolean },
  deps?: AnalyzeDeps
): Promise<LearnFromTargetResult> {
  const resolved = deps ?? createDefaultDeps();
  const outcome = await analyzeTarget(resolved, {
    target: args.target,
    version: args.version,
    origin: "interactive",
    force: args.force,
  });

  return {
    target: args.target,
    version: outcome.version,
    rva: outcome.rvaHex,
    va: formatVa(outcome.rva),
    resolvedName: outcome.resolvedName,
    analysisRun: outcome.status === "analyzed",
    finding: {
      proposedName: outcome.finding.proposedName,
      confidence: outcome.finding.confidence,
      note: outcome.finding.note,
      origin: outcome.finding.origin,
      createdAt: outcome.finding.createdAt,
      supersedesId: outcome.finding.supersedesId,
    },
    staticIndex: {
      name: outcome.staticContext.staticName,
      signature: outcome.staticContext.signature,
      addressLibraryId: outcome.staticContext.addressLibraryId,
      confidenceVerdict: outcome.staticContext.confidenceVerdict,
      auditNote: outcome.staticContext.auditNote,
    },
    contradiction: outcome.contradiction,
    warning: warningFor(outcome.contradiction),
  };
}
