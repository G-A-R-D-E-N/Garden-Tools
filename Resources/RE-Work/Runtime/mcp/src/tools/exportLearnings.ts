import { join } from "node:path";
import { formatVa } from "../db.js";
import { detectContradiction, getStaticContext } from "../learning/analyze.js";
import { listRecentFindings } from "../learning/findings.js";
import { ORGANIZED_GENERATED_DIR } from "../paths.js";
import { writeCsv } from "../organized/writeCsv.js";
import { createAuditDeps, type AuditDeps } from "./auditHardcodedAddresses.js";

export interface ExportLearningsResult {
  count: number;
  wrote: string | null;
}

const EXPORT_LIMIT = 100_000;
const HEADER = [
  "address", "rva", "version", "proposed_name", "confidence", "note",
  "origin", "model", "created_at", "static_name", "contradiction",
];

export async function exportLearnings(
  args: { write?: boolean },
  deps?: AuditDeps,
  generatedDir: string = ORGANIZED_GENERATED_DIR
): Promise<ExportLearningsResult> {
  const d = deps ?? createAuditDeps();
  const findings = listRecentFindings(d.learningDb, {
    limit: EXPORT_LIMIT,
    currentOnly: true,
    origin: "all",
  });

  let wrote: string | null = null;
  if (args.write) {
    const rows = findings.map((finding) => {
      const context = getStaticContext(d.staticDb, finding.version, finding.rva);
      const contradiction = detectContradiction(context, finding);
      return [
        formatVa(finding.rva),
        `0x${finding.rva.toString(16)}`,
        finding.version,
        finding.proposedName,
        finding.confidence,
        finding.note,
        finding.origin,
        finding.model,
        finding.createdAt,
        context.staticName,
        contradiction ? `${contradiction.staticName} vs ${contradiction.proposedName}` : null,
      ];
    });

    const path = join(generatedDir, "learned_findings.csv");
    writeCsv(path, HEADER, rows);
    wrote = path;
  }

  return { count: findings.length, wrote };
}
