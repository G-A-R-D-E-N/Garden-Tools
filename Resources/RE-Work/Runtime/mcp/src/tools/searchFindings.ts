import { readFileSync } from "node:fs";
import { join } from "node:path";
import { RE_ROOT } from "../paths.js";

const SEARCHABLE_DOCS = [
  "03-FINDINGS.md",
  "05-LABEL-RECOVERY.md",
  "06-FUNCTION-INDEX.md",
  "07-DECOMPILE-CANDIDATES.md",
];

const SNIPPET_RADIUS = 200;
const MAX_MATCHES_PER_DOC = 10;

export interface FindingsMatch {
  doc: string;
  snippet: string;
}

export function searchFindings(query: string): FindingsMatch[] {
  const results: FindingsMatch[] = [];
  const needle = query.toLowerCase();

  for (const doc of SEARCHABLE_DOCS) {
    let content: string;
    try {
      content = readFileSync(join(RE_ROOT, doc), "utf8");
    } catch {
      continue;
    }
    const haystack = content.toLowerCase();
    let idx = 0;
    let matches = 0;
    while (matches < MAX_MATCHES_PER_DOC) {
      const found = haystack.indexOf(needle, idx);
      if (found === -1) break;
      const start = Math.max(0, found - SNIPPET_RADIUS);
      const end = Math.min(content.length, found + needle.length + SNIPPET_RADIUS);
      const prefix = start > 0 ? "..." : "";
      const suffix = end < content.length ? "..." : "";
      results.push({
        doc,
        snippet: prefix + content.slice(start, end).replace(/\s+/g, " ").trim() + suffix,
      });
      idx = found + needle.length;
      matches++;
    }
  }

  return results;
}
