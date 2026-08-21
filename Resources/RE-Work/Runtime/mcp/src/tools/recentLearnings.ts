import { formatVa } from "../db.js";
import { detectContradiction, getStaticContext, type Contradiction } from "../learning/analyze.js";
import { listRecentFindings } from "../learning/findings.js";
import { createAuditDeps, type AuditDeps } from "./auditHardcodedAddresses.js";

const MAX_LIMIT = 500;

export interface RecentLearning {
  createdAt: string;
  origin: string;
  version: string;
  rva: string;
  va: string;
  proposedName: string | null;
  confidence: string;
  note: string | null;
  isCurrent: boolean;
  supersedesId: number | null;
  staticName: string | null;
  contradiction: Contradiction | null;
}

export interface RecentLearningsResult {
  count: number;
  currentOnly: boolean;
  origin: string;
  learnings: RecentLearning[];
}

export async function recentLearnings(
  args: {
    limit?: number;
    since?: string;
    origin?: "interactive" | "daemon" | "all";
    current_only?: boolean;
  },
  deps?: AuditDeps
): Promise<RecentLearningsResult> {
  const d = deps ?? createAuditDeps();
  const limit = Math.min(args.limit ?? 50, MAX_LIMIT);
  const currentOnly = args.current_only ?? true;
  const origin = args.origin ?? "all";

  const findings = listRecentFindings(d.learningDb, {
    limit,
    since: args.since,
    origin,
    currentOnly,
  });

  const learnings = findings.map((finding) => {
    const context = getStaticContext(d.staticDb, finding.version, finding.rva);
    return {
      createdAt: finding.createdAt,
      origin: finding.origin,
      version: finding.version,
      rva: `0x${finding.rva.toString(16)}`,
      va: formatVa(finding.rva),
      proposedName: finding.proposedName,
      confidence: finding.confidence,
      note: finding.note,
      isCurrent: finding.isCurrent === 1,
      supersedesId: finding.supersedesId,
      staticName: context.staticName,
      contradiction: detectContradiction(context, finding),
    };
  });

  return { count: learnings.length, currentOnly, origin, learnings };
}
