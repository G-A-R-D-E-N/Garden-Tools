import Database from "better-sqlite3";
import { afterEach, describe, expect, it, vi } from "vitest";
import { fixedClock } from "../../src/clock.js";
import { loadConfig } from "../../src/config.js";
import { GhidraError } from "../../src/adapters/ghidra.js";
import { OllamaError } from "../../src/adapters/ollama.js";
import {
  AnalyzeError,
  analyzeTarget,
  detectContradiction,
  getStaticContext,
  resolveTarget,
  type AnalyzeDeps,
} from "../../src/learning/analyze.js";
import { tempLearningDb } from "../helpers/fixtures.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

function staticDb(): Database.Database {
  const db = new Database(":memory:");
  db.exec(`
    CREATE TABLE symbols (version TEXT, rva INTEGER, name TEXT, signature TEXT, sizeof INTEGER);
    CREATE TABLE address_library (version TEXT, id INTEGER, rva INTEGER);
    CREATE TABLE name_disagreements (address TEXT, rva INTEGER, existing_name TEXT, decoded_name TEXT, note TEXT, verdict_155 TEXT);
    CREATE TABLE misname_corrections (address TEXT, rva INTEGER, old_name TEXT, new_name TEXT, method TEXT, note TEXT);
    CREATE TABLE misname_reverts (address TEXT, rva INTEGER, wrongly_renamed_to TEXT, revert_to TEXT);
    CREATE TABLE subsystem_index (version TEXT, category TEXT, rva INTEGER, name TEXT);
    INSERT INTO symbols VALUES ('1.10.163', 0x749ea0, 'PlayerCharacter::Update', 'void Update(PlayerCharacter *)', 120);
    INSERT INTO symbols VALUES ('1.10.163', 0x128b0, 'Actor::SetFlag', 'void SetFlag(Actor *)', 15);
    INSERT INTO symbols VALUES ('1.10.163', 999, 'Dup::Name', 'sig', 1);
    INSERT INTO symbols VALUES ('1.10.163', 1000, 'Dup::Name', 'sig', 1);
    INSERT INTO address_library VALUES ('1.10.163', 42424, 0x749ea0);
  `);
  cleanups.push(() => db.close());
  return db;
}

function deps(overrides: Partial<AnalyzeDeps> = {}): AnalyzeDeps {
  const learning = tempLearningDb();
  cleanups.push(learning.cleanup);
  return {
    staticDb: staticDb(),
    learningDb: learning.db,
    clock: fixedClock("2026-08-13T10:00:00.000Z"),
    config: loadConfig({}),
    decompile: vi.fn(async () => ({
      version: "1.10.163",
      rva: 0x749ea0,
      entryRva: 0x749ea0,
      functionName: "PlayerCharacter::Update",
      body: "void Update(PlayerCharacter *this) { return; }",
      normalizedHash: "hash-a",
      truncated: false,
    })),
    extract: vi.fn(async () => ({
      proposedName: "PlayerCharacter::Update",
      confidence: "high" as const,
      note: "matches the static name",
    })),
    projectFingerprint: () => "proj-1",
    ...overrides,
  };
}

describe("resolveTarget", () => {
  it("resolves a hex VA, a hex RVA, and a 0x-prefixed RVA to the same target", () => {
    const db = staticDb();
    for (const input of ["0x140749ea0", "0x749ea0", "749ea0"]) {
      expect(resolveTarget(db, input, "1.10.163").rva).toBe(0x749ea0);
    }
  });

  it("resolves an exact symbol name", () => {
    const db = staticDb();
    expect(resolveTarget(db, "PlayerCharacter::Update", "1.10.163").rva).toBe(0x749ea0);
  });

  it("refuses an ambiguous name and names the candidates", () => {
    const db = staticDb();
    expect(() => resolveTarget(db, "Dup::Name", "1.10.163")).toThrow(/ambiguous/i);
  });

  it("throws unresolved_target for a name that does not exist", () => {
    const db = staticDb();
    try {
      resolveTarget(db, "Nope::Missing", "1.10.163");
      expect.unreachable("should have thrown");
    } catch (err) {
      expect((err as AnalyzeError).kind).toBe("unresolved_target");
    }
  });

  it("accepts an address with no symbol — the point is often to name it", () => {
    const db = staticDb();
    const resolved = resolveTarget(db, "0x555555", "1.10.163");
    expect(resolved.rva).toBe(0x555555);
    expect(resolved.name).toBeNull();
  });
});

describe("getStaticContext", () => {
  it("reports no_audit_trail with the static name and address-library id", () => {
    const db = staticDb();
    const context = getStaticContext(db, "1.10.163", 0x749ea0);
    expect(context.staticName).toBe("PlayerCharacter::Update");
    expect(context.addressLibraryId).toBe(42424);
    expect(context.confidenceVerdict).toBe("no_audit_trail");
  });

  it("reports an unresolved disagreement when one is recorded", () => {
    const db = staticDb();
    db.prepare(
      "INSERT INTO name_disagreements VALUES ('0x140749ea0', 0x749ea0, 'A', 'B', 'note here', 'inconclusive')"
    ).run();
    const context = getStaticContext(db, "1.10.163", 0x749ea0);
    expect(context.confidenceVerdict).toBe("unresolved_disagreement");
    expect(context.auditNote).toContain("note here");
  });

  it("a confirmed correction outranks a disagreement", () => {
    const db = staticDb();
    db.prepare(
      "INSERT INTO name_disagreements VALUES ('0x140749ea0', 0x749ea0, 'A', 'B', 'n', 'x')"
    ).run();
    db.prepare(
      "INSERT INTO misname_corrections VALUES ('0x140749ea0', 0x749ea0, 'Old::Name', 'PlayerCharacter::Update', 'decompile', 'verified')"
    ).run();
    expect(getStaticContext(db, "1.10.163", 0x749ea0).confidenceVerdict).toBe(
      "confirmed_correction"
    );
  });

  it("a revert outranks the correction it undid — matching check_name_confidence", () => {
    // The 13 reverted corrections in this project are exactly this case: a correction was applied
    // and then withdrawn, so the ORIGINAL name is the confirmed-correct one. Reading the leftover
    // misname_corrections row as authoritative would invert that.
    const db = staticDb();
    db.prepare(
      "INSERT INTO misname_corrections VALUES ('0x140749ea0', 0x749ea0, 'PlayerCharacter::Update', 'Wrong::Name', 'decompile', 'n')"
    ).run();
    db.prepare(
      "INSERT INTO misname_reverts VALUES ('0x140749ea0', 0x749ea0, 'Wrong::Name', 'PlayerCharacter::Update')"
    ).run();
    const context = getStaticContext(db, "1.10.163", 0x749ea0);
    expect(context.confidenceVerdict).toBe("reverted_correction");
    expect(context.auditNote).toContain("PlayerCharacter::Update");
  });
});

describe("detectContradiction", () => {
  const base = {
    staticName: "Actor::SetFlag",
    signature: null,
    addressLibraryId: null,
    auditNote: null,
  };

  it("is null when the names agree", () => {
    expect(
      detectContradiction(
        { ...base, confidenceVerdict: "no_audit_trail" },
        { proposedName: "Actor::SetFlag" }
      )
    ).toBeNull();
  });

  it("is null when there is no static name to contradict", () => {
    expect(
      detectContradiction(
        { ...base, staticName: null, confidenceVerdict: "no_audit_trail" },
        { proposedName: "Anything" }
      )
    ).toBeNull();
  });

  it("flags a mismatch and marks an audited static name as authoritative", () => {
    const contradiction = detectContradiction(
      { ...base, confidenceVerdict: "confirmed_correction" },
      { proposedName: "Actor::SetCombatFlag" }
    );
    expect(contradiction).toEqual({
      staticName: "Actor::SetFlag",
      proposedName: "Actor::SetCombatFlag",
      staticIsAudited: true,
    });
  });

  it("treats a reverted correction as audited too", () => {
    const contradiction = detectContradiction(
      { ...base, confidenceVerdict: "reverted_correction" },
      { proposedName: "Actor::Other" }
    );
    expect(contradiction!.staticIsAudited).toBe(true);
  });
});

describe("analyzeTarget", () => {
  it("runs the pipeline and persists a current finding", async () => {
    const d = deps();
    const outcome = await analyzeTarget(d, { target: "0x749ea0", origin: "interactive" });

    expect(outcome.status).toBe("analyzed");
    expect(outcome.rvaHex).toBe("0x749ea0");
    expect(outcome.finding.isCurrent).toBe(1);
    expect(outcome.finding.origin).toBe("interactive");
    expect(outcome.finding.rawDecompileHash).toBe("hash-a");
    expect(outcome.finding.projectFingerprint).toBe("proj-1");
    expect(outcome.contradiction).toBeNull();
  });

  it("skips Ollama entirely when the extraction identity is unchanged", async () => {
    const d = deps();
    await analyzeTarget(d, { target: "0x749ea0", origin: "interactive" });
    const callsAfterFirst = (d.extract as ReturnType<typeof vi.fn>).mock.calls.length;

    const second = await analyzeTarget(d, { target: "0x749ea0", origin: "interactive" });

    expect(second.status).toBe("deduplicated");
    expect((d.extract as ReturnType<typeof vi.fn>).mock.calls.length).toBe(callsAfterFirst);
  });

  it("force re-runs extraction even on an unchanged identity", async () => {
    const d = deps();
    await analyzeTarget(d, { target: "0x749ea0", origin: "interactive" });
    const forced = await analyzeTarget(d, {
      target: "0x749ea0",
      origin: "interactive",
      force: true,
    });
    expect(forced.status).toBe("analyzed");
    expect((d.extract as ReturnType<typeof vi.fn>).mock.calls.length).toBe(2);
  });

  it("surfaces a contradiction without overwriting the static name", async () => {
    const d = deps({
      extract: vi.fn(async () => ({
        proposedName: "PlayerCharacter::Tick",
        confidence: "medium" as const,
        note: "looks like a per-frame update",
      })),
    });
    const outcome = await analyzeTarget(d, { target: "0x749ea0", origin: "interactive" });

    expect(outcome.contradiction).toEqual({
      staticName: "PlayerCharacter::Update",
      proposedName: "PlayerCharacter::Tick",
      staticIsAudited: false,
    });
    // The static index is untouched.
    const stillStatic = d.staticDb
      .prepare("SELECT name FROM symbols WHERE rva = 0x749ea0")
      .get() as { name: string };
    expect(stillStatic.name).toBe("PlayerCharacter::Update");
  });

  it("passes the static name into the extractor as context", async () => {
    const d = deps();
    await analyzeTarget(d, { target: "0x749ea0", origin: "interactive" });
    const [req] = (d.extract as ReturnType<typeof vi.fn>).mock.calls[0];
    expect(req.staticName).toBe("PlayerCharacter::Update");
    expect(req.decompiled).toContain("void Update");
  });

  it("writes nothing when Ghidra fails, and preserves the failure kind", async () => {
    const d = deps({
      decompile: vi.fn(async () => {
        throw new GhidraError("locked", "Unable to lock project");
      }),
    });
    await expect(analyzeTarget(d, { target: "0x749ea0", origin: "daemon" })).rejects.toMatchObject({
      kind: "locked",
      retryable: true,
    });

    const n = (
      d.learningDb.prepare("SELECT COUNT(*) AS n FROM learned_findings").get() as { n: number }
    ).n;
    expect(n).toBe(0);
  });

  it("writes nothing when Ollama is unavailable", async () => {
    const d = deps({
      extract: vi.fn(async () => {
        throw new OllamaError("unavailable", "ECONNREFUSED");
      }),
    });
    await expect(analyzeTarget(d, { target: "0x749ea0", origin: "daemon" })).rejects.toMatchObject({
      kind: "unavailable",
      retryable: true,
    });
    expect(
      (d.learningDb.prepare("SELECT COUNT(*) AS n FROM learned_findings").get() as { n: number }).n
    ).toBe(0);
  });

  it("marks invalid model output as non-retryable", async () => {
    const d = deps({
      extract: vi.fn(async () => {
        throw new OllamaError("invalid_output", "not json");
      }),
    });
    await expect(analyzeTarget(d, { target: "0x749ea0", origin: "daemon" })).rejects.toMatchObject({
      kind: "invalid_output",
      retryable: false,
    });
  });

  it("rejects a version outside the configured set before spawning anything", async () => {
    const d = deps();
    await expect(
      analyzeTarget(d, { target: "0x749ea0", version: "1.10.984", origin: "interactive" })
    ).rejects.toMatchObject({ kind: "unresolved_target" });
    expect((d.decompile as ReturnType<typeof vi.fn>).mock.calls.length).toBe(0);
  });

  it("defaults to version 1.10.163", async () => {
    const d = deps();
    const outcome = await analyzeTarget(d, { target: "0x749ea0", origin: "interactive" });
    expect(outcome.version).toBe("1.10.163");
  });
});
