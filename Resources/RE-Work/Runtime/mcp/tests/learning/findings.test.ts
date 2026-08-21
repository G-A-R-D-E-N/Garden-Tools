import { afterEach, describe, expect, it } from "vitest";
import { fixedClock } from "../../src/clock.js";
import {
  getCurrentFinding,
  getFindingHistory,
  listRecentFindings,
  recordFinding,
  sameExtractionIdentity,
} from "../../src/learning/findings.js";
import { tempLearningDb, type TempDb } from "../helpers/fixtures.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

function fixture(): TempDb {
  const f = tempLearningDb();
  cleanups.push(f.cleanup);
  return f;
}

const BASE = {
  version: "1.10.163",
  rva: 0x128b0,
  rawDecompileHash: "hash-a",
  model: "qwen2.5-coder:7b",
  promptVersion: "1",
  extractorVersion: "1",
  projectFingerprint: "proj-1",
  proposedName: "Actor::SetFlag",
  confidence: "high" as const,
  note: "sets bit 1",
  origin: "interactive" as const,
  ghidraVersion: "12.1",
};

const CLOCK = fixedClock("2026-08-13T10:00:00.000Z");
const LATER = fixedClock("2026-08-13T11:00:00.000Z");

describe("extraction identity", () => {
  const identity = {
    version: "1.10.163",
    rva: 1,
    rawDecompileHash: "h",
    model: "m",
    promptVersion: "1",
    extractorVersion: "1",
    projectFingerprint: "p",
  };

  it("is equal to itself", () => {
    expect(sameExtractionIdentity(identity, { ...identity })).toBe(true);
  });

  it("differs when any one component differs", () => {
    for (const key of Object.keys(identity) as (keyof typeof identity)[]) {
      const changed = { ...identity, [key]: key === "rva" ? 2 : "changed" };
      expect(sameExtractionIdentity(identity, changed)).toBe(false);
    }
  });
});

describe("recordFinding", () => {
  it("inserts the first finding as current", () => {
    const { db } = fixture();
    const { finding, deduplicated } = recordFinding(db, CLOCK, BASE);
    expect(deduplicated).toBe(false);
    expect(finding.isCurrent).toBe(1);
    expect(finding.supersedesId).toBeNull();
    expect(finding.createdAt).toBe("2026-08-13T10:00:00.000Z");
  });

  it("deduplicates an identical extraction identity without writing a row", () => {
    const { db } = fixture();
    recordFinding(db, CLOCK, BASE);
    const second = recordFinding(db, LATER, { ...BASE, proposedName: "Different::Name" });

    expect(second.deduplicated).toBe(true);
    // The stored row is the original, untouched — a repeat extraction is not a new observation.
    expect(second.finding.proposedName).toBe("Actor::SetFlag");
    expect(second.finding.createdAt).toBe("2026-08-13T10:00:00.000Z");
    const n = (db.prepare("SELECT COUNT(*) AS n FROM learned_findings").get() as { n: number }).n;
    expect(n).toBe(1);
  });

  it("supersedes when the decompile hash changes", () => {
    const { db } = fixture();
    const first = recordFinding(db, CLOCK, BASE);
    const second = recordFinding(db, LATER, {
      ...BASE,
      rawDecompileHash: "hash-b",
      proposedName: "Actor::SetCombatFlag",
    });

    expect(second.deduplicated).toBe(false);
    expect(second.finding.supersedesId).toBe(first.finding.id);
    expect(second.finding.isCurrent).toBe(1);
    expect(getCurrentFinding(db, BASE.version, BASE.rva)!.id).toBe(second.finding.id);
  });

  it("re-extracts when the model, prompt, extractor, or project changes", () => {
    for (const patch of [
      { model: "other-model" },
      { promptVersion: "2" },
      { extractorVersion: "2" },
      { projectFingerprint: "proj-2" },
    ]) {
      const { db } = fixture();
      recordFinding(db, CLOCK, BASE);
      const next = recordFinding(db, LATER, { ...BASE, ...patch });
      expect(next.deduplicated, JSON.stringify(patch)).toBe(false);
    }
  });

  it("force re-records even when the identity is unchanged", () => {
    const { db } = fixture();
    recordFinding(db, CLOCK, BASE);
    const forced = recordFinding(db, LATER, { ...BASE, force: true });
    expect(forced.deduplicated).toBe(false);
    expect(getFindingHistory(db, BASE.version, BASE.rva)).toHaveLength(2);
  });

  it("keeps exactly one current row per (version, rva)", () => {
    const { db } = fixture();
    recordFinding(db, CLOCK, BASE);
    recordFinding(db, LATER, { ...BASE, rawDecompileHash: "hash-b" });
    const currents = (
      db
        .prepare(
          "SELECT COUNT(*) AS n FROM learned_findings WHERE version = ? AND rva = ? AND is_current = 1"
        )
        .get(BASE.version, BASE.rva) as { n: number }
    ).n;
    expect(currents).toBe(1);
  });

  it("preserves supersession history newest-first", () => {
    const { db } = fixture();
    recordFinding(db, CLOCK, BASE);
    recordFinding(db, LATER, { ...BASE, rawDecompileHash: "hash-b" });
    const history = getFindingHistory(db, BASE.version, BASE.rva);
    expect(history).toHaveLength(2);
    expect(history[0].isCurrent).toBe(1);
    expect(history[1].isCurrent).toBe(0);
  });

  it("a failed replacement leaves the previous row current", () => {
    const { db } = fixture();
    const first = recordFinding(db, CLOCK, BASE);
    expect(() =>
      recordFinding(db, LATER, {
        ...BASE,
        rawDecompileHash: "hash-b",
        // CHECK constraint violation mid-transaction, after the old row has been demoted.
        confidence: "bogus" as unknown as "high",
      })
    ).toThrow();

    const current = getCurrentFinding(db, BASE.version, BASE.rva);
    expect(current!.id).toBe(first.finding.id);
    expect(current!.isCurrent).toBe(1);
    expect(getFindingHistory(db, BASE.version, BASE.rva)).toHaveLength(1);
  });
});

describe("listRecentFindings", () => {
  it("filters by origin", () => {
    const { db } = fixture();
    recordFinding(db, CLOCK, BASE);
    recordFinding(db, CLOCK, { ...BASE, rva: 0x200, origin: "daemon" });

    const daemon = listRecentFindings(db, { limit: 50, origin: "daemon", currentOnly: true });
    expect(daemon).toHaveLength(1);
    expect(daemon[0].rva).toBe(0x200);

    expect(listRecentFindings(db, { limit: 50, origin: "all", currentOnly: true })).toHaveLength(2);
  });

  it("currentOnly=false exposes superseded rows", () => {
    const { db } = fixture();
    recordFinding(db, CLOCK, BASE);
    recordFinding(db, LATER, { ...BASE, rawDecompileHash: "hash-b" });

    expect(listRecentFindings(db, { limit: 50, currentOnly: true })).toHaveLength(1);
    expect(listRecentFindings(db, { limit: 50, currentOnly: false })).toHaveLength(2);
  });

  it("honours the limit and the since cutoff", () => {
    const { db } = fixture();
    recordFinding(db, CLOCK, BASE);
    recordFinding(db, LATER, { ...BASE, rva: 0x300 });

    expect(listRecentFindings(db, { limit: 1, currentOnly: true })).toHaveLength(1);
    expect(
      listRecentFindings(db, { limit: 50, since: "2026-08-13T10:30:00.000Z", currentOnly: true })
    ).toHaveLength(1);
  });
});
