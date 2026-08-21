import Database from "better-sqlite3";
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { fixedClock } from "../../src/clock.js";
import type { ParsedOrganizedRow } from "../../src/organized/csvSources.js";
import {
  CSV_PAIRS,
  auditCsvCorpus,
  checkClaimDrift,
  checkPairDisagreement,
  checkPdbUncorroborated,
  checkStructural,
  checkUnresolvedAddresses,
  looksLikeSignatureText,
  type AuditCsvDeps,
} from "../../src/tools/auditCsvCorpus.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

function staticDb(): Database.Database {
  const db = new Database(":memory:");
  cleanups.push(() => db.close());
  db.exec(`CREATE TABLE symbols (version TEXT, rva INTEGER, name TEXT, signature TEXT, sizeof INTEGER);`);
  db.prepare("INSERT INTO symbols VALUES ('1.10.163', 0x724fa0, 'Actor::SetFlag', 'sig', 1)").run();
  return db;
}

function row(overrides: Partial<ParsedOrganizedRow>): ParsedOrganizedRow {
  return {
    rowNo: 1,
    address: "0x140724fa0",
    rva: 0x724fa0,
    nameA: null,
    nameB: null,
    kind: null,
    note: null,
    extra: {},
    ...overrides,
  };
}

describe("looksLikeSignatureText", () => {
  it("flags a full signature", () => {
    expect(looksLikeSignatureText("public virtual void __thiscall Foo::Bar(int x)")).toBe(true);
  });
  it("does not flag a bare qualified name", () => {
    expect(looksLikeSignatureText("Actor::SetFlag")).toBe(false);
  });
});

describe("checkStructural", () => {
  it("flags an address that failed to parse", () => {
    const findings = checkStructural("some_source", [row({ address: "not-hex", rva: null })]);
    expect(findings).toHaveLength(1);
    expect(findings[0]).toMatchObject({ findingType: "structural", severity: "error" });
  });

  it("flags a duplicate address within one file", () => {
    const findings = checkStructural("some_source", [
      row({ rowNo: 1, address: "0x1", rva: 1 }),
      row({ rowNo: 2, address: "0x1", rva: 1 }),
    ]);
    expect(findings).toHaveLength(1);
    expect(findings[0]).toMatchObject({ findingType: "structural", severity: "warning", rowNo: 2 });
  });

  it("produces nothing for clean, unique rows", () => {
    expect(
      checkStructural("some_source", [row({ address: "0x1", rva: 1 }), row({ address: "0x2", rva: 2, rowNo: 2 })])
    ).toEqual([]);
  });

  it("ignores rows with no address at all (e.g. commonlib_vtable_slots)", () => {
    expect(checkStructural("commonlib_vtable_slots", [row({ address: null, rva: null })])).toEqual([]);
  });
});

describe("checkUnresolvedAddresses", () => {
  it("flags an address with no symbol in the live index", () => {
    const findings = checkUnresolvedAddresses(staticDb(), "some_source", [
      row({ address: "0x1", rva: 1 }),
    ], "1.10.163");
    expect(findings).toHaveLength(1);
    expect(findings[0]).toMatchObject({ findingType: "unresolved_address", severity: "warning" });
  });

  it("is silent when the address resolves", () => {
    const findings = checkUnresolvedAddresses(staticDb(), "some_source", [
      row({ address: "0x140724fa0", rva: 0x724fa0 }),
    ], "1.10.163");
    expect(findings).toEqual([]);
  });

  it("skips rows with no address to check", () => {
    expect(checkUnresolvedAddresses(staticDb(), "commonlib_vtable_slots", [row({ address: null, rva: null })], "1.10.163")).toEqual([]);
  });
});

describe("checkClaimDrift", () => {
  it("is silent when nameB matches the live symbol", () => {
    const findings = checkClaimDrift(
      staticDb(),
      "applied_safe_gaps",
      [row({ address: "0x140724fa0", rva: 0x724fa0, nameB: "Actor::SetFlag" })],
      "1.10.163"
    );
    expect(findings).toEqual([]);
  });

  it("flags drift when nameB no longer matches the live symbol", () => {
    const findings = checkClaimDrift(
      staticDb(),
      "applied_safe_gaps",
      [row({ address: "0x140724fa0", rva: 0x724fa0, nameB: "Actor::OtherName" })],
      "1.10.163"
    );
    expect(findings).toHaveLength(1);
    expect(findings[0]).toMatchObject({ findingType: "claim_drift", severity: "error" });
    expect(findings[0].detail).toContain("Actor::SetFlag");
    expect(findings[0].detail).toContain("Actor::OtherName");
  });

  it("does not fire for a source outside CLAIM_DRIFT_SOURCES", () => {
    const findings = checkClaimDrift(
      staticDb(),
      "confirmed_misnames_to_apply",
      [row({ address: "0x140724fa0", rva: 0x724fa0, nameB: "Actor::OtherName" })],
      "1.10.163"
    );
    expect(findings).toEqual([]);
  });

  it("skips a row whose nameB is signature text rather than a bare name", () => {
    const findings = checkClaimDrift(
      staticDb(),
      "misnames_to_revert",
      [row({ address: "0x140724fa0", rva: 0x724fa0, nameB: "public virtual void __thiscall Foo::Bar()" })],
      "1.10.163"
    );
    expect(findings).toEqual([]);
  });

  it("skips a row whose address does not resolve (unresolved_address's job, not claim_drift's)", () => {
    const findings = checkClaimDrift(
      staticDb(),
      "applied_safe_gaps",
      [row({ address: "0xdead", rva: 0xdead, nameB: "Anything" })],
      "1.10.163"
    );
    expect(findings).toEqual([]);
  });
});

describe("checkPairDisagreement", () => {
  const pair = { a: "safe_gaps_apply", b: "applied_safe_gaps" };

  it("is silent when both sides agree", () => {
    const findings = checkPairDisagreement(
      pair,
      [row({ address: "0x1", rva: 1, nameB: "Actor::SetFlag" })],
      [row({ address: "0x1", rva: 1, nameB: "Actor::SetFlag" })]
    );
    expect(findings).toEqual([]);
  });

  it("flags a name mismatch between the plan and the record", () => {
    const findings = checkPairDisagreement(
      pair,
      [row({ address: "0x1", rva: 1, nameB: "Actor::SetFlag" })],
      [row({ address: "0x1", rva: 1, nameB: "Actor::DifferentName" })]
    );
    expect(findings).toHaveLength(1);
    expect(findings[0]).toMatchObject({ findingType: "pair_disagreement", severity: "warning" });
    expect(findings[0].detail).toContain("Actor::SetFlag");
    expect(findings[0].detail).toContain("Actor::DifferentName");
  });

  it("flags an address present in only one side", () => {
    const findings = checkPairDisagreement(
      pair,
      [row({ address: "0x1", rva: 1, nameB: "Actor::SetFlag" })],
      []
    );
    expect(findings).toHaveLength(1);
    expect(findings[0].detail).toContain("missing from applied_safe_gaps");
  });

  it("declares exactly the 4 pairs from the spec", () => {
    expect(CSV_PAIRS).toEqual([
      { a: "safe_gaps_apply", b: "applied_safe_gaps" },
      { a: "reassign_apply", b: "applied_reassignments" },
      { a: "std_func_sweep", b: "applied_std_func" },
      { a: "misnames_to_revert", b: "misnames_to_revert_simple" },
    ]);
  });
});

describe("checkPdbUncorroborated", () => {
  function dbWithPdb(names: string[]): Database.Database {
    const db = staticDb();
    db.exec(`CREATE TABLE pdb_symbols (name TEXT, signature TEXT, calling_convention TEXT, sizeof INTEGER, is_fpo INTEGER, pdb_rva INTEGER, version TEXT);`);
    for (const name of names) {
      db.prepare("INSERT INTO pdb_symbols (name, signature, version) VALUES (?, 'sig', '1.10.155')").run(name);
    }
    return db;
  }

  it("is info-severity when a claimed name is absent from the PDB", () => {
    const findings = checkPdbUncorroborated(dbWithPdb([]), "applied_safe_gaps", [
      row({ nameB: "Actor::SetFlag" }),
    ]);
    expect(findings).toHaveLength(1);
    expect(findings[0].severity).toBe("info");
  });

  it("is silent when the name is present", () => {
    const findings = checkPdbUncorroborated(dbWithPdb(["Actor::SetFlag"]), "applied_safe_gaps", [
      row({ nameB: "Actor::SetFlag" }),
    ]);
    expect(findings).toEqual([]);
  });

  it("does not run for a source outside the scope list", () => {
    const findings = checkPdbUncorroborated(dbWithPdb([]), "crossref_review_candidates", [
      row({ nameB: "Actor::SetFlag" }),
    ]);
    expect(findings).toEqual([]);
  });

  it("skips a row whose nameB is signature text", () => {
    const findings = checkPdbUncorroborated(dbWithPdb([]), "applied_safe_gaps", [
      row({ nameB: "public virtual void __thiscall Foo::Bar()" }),
    ]);
    expect(findings).toEqual([]);
  });

  it("does not throw when pdb_symbols does not exist", () => {
    const db = staticDb();
    expect(() => checkPdbUncorroborated(db, "applied_safe_gaps", [row({ nameB: "X" })])).not.toThrow();
    expect(checkPdbUncorroborated(db, "applied_safe_gaps", [row({ nameB: "X" })])).toEqual([]);
  });
});

describe("auditCsvCorpus (integration, fixture CSVs on disk)", () => {
  function fixtureCorpus(): { organizedDir: string; generatedDir: string } {
    const organizedDir = mkdtempSync(join(tmpdir(), "re-organized-"));
    cleanups.push(() => rmSync(organizedDir, { recursive: true, force: true }));
    const generatedDir = join(organizedDir, "generated");

    // A claim_drift case: applied_safe_gaps says 0x724fa0 -> Actor::WrongName, but the live
    // index (set up below) says Actor::SetFlag.
    writeFileSync(
      join(organizedDir, "applied_safe_gaps.csv"),
      "address,old_name,new_name,kind,note\n0x140724fa0,Old::Name,Actor::WrongName,reassign,n\n"
    );
    // Its plan-side pair, disagreeing with the record above.
    writeFileSync(
      join(organizedDir, "safe_gaps_apply.csv"),
      "address,existing_name,decoded_name\n0x140724fa0,Old::Name,Actor::SetFlag\n"
    );
    // An unresolved address.
    writeFileSync(
      join(organizedDir, "crossref_review_candidates.csv"),
      "address,existing_name,decoded_name\n0x14deadbe0,A,B\n"
    );

    return { organizedDir, generatedDir };
  }

  function deps(organizedDir: string, generatedDir: string): AuditCsvDeps {
    const db = staticDb();
    db.prepare("INSERT INTO symbols VALUES ('1.10.163', 0x724fa0, 'Actor::SetFlag', 'sig', 1)").run();
    return { staticDb: db, organizedDir, generatedDir };
  }

  it("audits only the files present, reporting the rest as file-not-found only when scoped elsewhere", () => {
    const { organizedDir, generatedDir } = fixtureCorpus();
    // Scope to just the 3 fixture files so the other 21 declared-but-absent files don't drown the
    // assertions in "file not found" noise unrelated to this test's purpose.
    const result = auditCsvCorpus(
      { source: "applied_safe_gaps" },
      deps(organizedDir, generatedDir)
    );
    expect(result.sourcesAudited).toBe(1);
    expect(result.findings.some((f) => f.findingType === "claim_drift")).toBe(true);
  });

  it("reports unresolved_address for the crossref fixture row", () => {
    const { organizedDir, generatedDir } = fixtureCorpus();
    const result = auditCsvCorpus(
      { source: "crossref_review_candidates" },
      deps(organizedDir, generatedDir)
    );
    expect(result.findings).toHaveLength(1);
    expect(result.findings[0].findingType).toBe("unresolved_address");
  });

  it("reports pair_disagreement only on a whole-corpus run", () => {
    const { organizedDir, generatedDir } = fixtureCorpus();
    const scoped = auditCsvCorpus({ source: "applied_safe_gaps" }, deps(organizedDir, generatedDir));
    expect(scoped.findings.some((f) => f.findingType === "pair_disagreement")).toBe(false);

    const full = auditCsvCorpus({}, deps(organizedDir, generatedDir));
    expect(full.findings.some((f) => f.findingType === "pair_disagreement")).toBe(true);
  });

  it("throws a clear error for an unknown source", () => {
    const { organizedDir, generatedDir } = fixtureCorpus();
    expect(() => auditCsvCorpus({ source: "NotARealSource" }, deps(organizedDir, generatedDir))).toThrow(
      /Unknown source/
    );
  });

  it("a missing declared file becomes a structural error, not a thrown exception", () => {
    const { organizedDir, generatedDir } = fixtureCorpus();
    // commonlib_vtable_slots.csv is declared in CSV_SOURCES but absent from this fixture dir.
    const result = auditCsvCorpus({ source: "commonlib_vtable_slots" }, deps(organizedDir, generatedDir));
    expect(result.findings).toHaveLength(1);
    expect(result.findings[0]).toMatchObject({ findingType: "structural", severity: "error" });
  });

  it("writes nothing when write is not set", () => {
    const { organizedDir, generatedDir } = fixtureCorpus();
    const result = auditCsvCorpus({ source: "applied_safe_gaps" }, deps(organizedDir, generatedDir));
    expect(result.wrote).toBeNull();
    expect(existsSync(generatedDir)).toBe(false);
  });

  it("writes exactly three files under generated/ when write is true", () => {
    const { organizedDir, generatedDir } = fixtureCorpus();
    const result = auditCsvCorpus({ write: true }, deps(organizedDir, generatedDir));

    expect(result.wrote).not.toBeNull();
    expect(existsSync(result.wrote!.csvAuditReportCsv)).toBe(true);
    expect(existsSync(result.wrote!.csvAuditReportMd)).toBe(true);
    expect(result.wrote!.csvAuditReportCsv.startsWith(generatedDir)).toBe(true);

    const csvContent = readFileSync(result.wrote!.csvAuditReportCsv, "utf8");
    expect(csvContent).toContain("claim_drift");

    const mdContent = readFileSync(result.wrote!.csvAuditReportMd, "utf8");
    expect(mdContent).toContain("## Counts by severity");
  });

  it("the full report on disk is not truncated even when the response limit caps it", () => {
    const { organizedDir, generatedDir } = fixtureCorpus();
    const result = auditCsvCorpus({ write: true, limit: 0 }, deps(organizedDir, generatedDir));
    expect(result.findings).toHaveLength(0);
    expect(result.truncated).toBe(true);
    const csvContent = readFileSync(result.wrote!.csvAuditReportCsv, "utf8");
    expect(csvContent.split("\n").filter(Boolean).length).toBeGreaterThan(1); // header + real findings
  });

  it("pdbCrosscheckCsv is null when pdb_symbols is not indexed", () => {
    const { organizedDir, generatedDir } = fixtureCorpus();
    const result = auditCsvCorpus({ write: true }, deps(organizedDir, generatedDir));
    expect(result.wrote!.pdbCrosscheckCsv).toBeNull();
  });

  it("writes a real pdb crosscheck row when pdb_symbols has a matching name", () => {
    const { organizedDir, generatedDir } = fixtureCorpus();
    const d = deps(organizedDir, generatedDir);
    d.staticDb.exec(`CREATE TABLE pdb_symbols (name TEXT, signature TEXT, calling_convention TEXT, sizeof INTEGER, is_fpo INTEGER, pdb_rva INTEGER, version TEXT);`);
    d.staticDb
      .prepare("INSERT INTO pdb_symbols VALUES ('Actor::SetFlag', 'void Actor::SetFlag()', '__cdecl', 1, 0, 999, '1.10.155')")
      .run();

    const result = auditCsvCorpus({ write: true }, d);
    expect(result.wrote!.pdbCrosscheckCsv).not.toBeNull();
    const content = readFileSync(result.wrote!.pdbCrosscheckCsv!, "utf8");
    expect(content).toContain("Actor::SetFlag");
  });

  it("timestamps the report via the injected clock, not new Date() (clock.ts invariant)", () => {
    const { organizedDir, generatedDir } = fixtureCorpus();
    const d = deps(organizedDir, generatedDir);
    d.clock = fixedClock("2026-08-13T10:00:00.000Z");
    const result = auditCsvCorpus({ write: true }, d);
    const mdContent = readFileSync(result.wrote!.csvAuditReportMd, "utf8");
    expect(mdContent).toContain("Generated by audit_csv_corpus at 2026-08-13T10:00:00.000Z.");
  });
});

describe("checkPairDisagreement signature-text guard (real-data finding)", () => {
  const pair = { a: "safe_gaps_apply", b: "applied_safe_gaps" };

  it("does not emit a name-mismatch finding when either side is signature text", () => {
    const findings = checkPairDisagreement(
      pair,
      [row({ address: "0x1", rva: 1, nameB: "private static unsigned int NiRefObject::ms_uiObjects" })],
      [row({ address: "0x1", rva: 1, nameB: "NiRefObject::ms_uiObjects" })]
    );
    expect(findings).toEqual([]);
  });

  it("still flags an address missing from one side even when names are signature text", () => {
    const findings = checkPairDisagreement(
      pair,
      [row({ address: "0x1", rva: 1, nameB: "private static unsigned int NiRefObject::ms_uiObjects" })],
      []
    );
    expect(findings).toHaveLength(1);
    expect(findings[0].detail).toContain("missing from applied_safe_gaps");
  });
});

describe("checkClaimDrift with documented reversion records", () => {
  // A reversion record: hknpShape_rename.csv / misnames_to_revert_simple.csv map an address to the
  // name it was reverted to. When the live index shows exactly that reverted name, the drift is
  // explained and documented — not an error.
  const reverts = new Map<number, { name: string; file: string; rowNo: number }>([
    [0x724fa0, { name: "Actor::SetFlag", file: "hknpShape_rename.csv", rowNo: 3 }],
  ]);

  it("downgrades to info when a documented reversion matches the live name", () => {
    const findings = checkClaimDrift(
      staticDb(),
      "applied_hknpShape",
      [row({ address: "0x140724fa0", rva: 0x724fa0, nameB: "Actor::OtherName" })],
      "1.10.163",
      reverts
    );
    expect(findings).toHaveLength(1);
    expect(findings[0]).toMatchObject({ findingType: "claim_drift", severity: "info" });
    expect(findings[0].detail).toContain("Actor::OtherName");
    expect(findings[0].detail).toContain("Actor::SetFlag");
    expect(findings[0].detail).toContain("hknpShape_rename.csv");
    expect(findings[0].detail).toContain("documented reversion");
  });

  it("stays an error when the revert record does not explain the live name", () => {
    const conflicting = new Map<number, { name: string; file: string; rowNo: number }>([
      // The revert says Actor::YetAnother, but the live index shows Actor::SetFlag — that mismatch
      // is a genuine three-way disagreement and must stay loud.
      [0x724fa0, { name: "Actor::YetAnother", file: "misnames_to_revert_simple.csv", rowNo: 7 }],
    ]);
    const findings = checkClaimDrift(
      staticDb(),
      "applied_misname_corrections",
      [row({ address: "0x140724fa0", rva: 0x724fa0, nameB: "Actor::OtherName" })],
      "1.10.163",
      conflicting
    );
    expect(findings).toHaveLength(1);
    expect(findings[0]).toMatchObject({ findingType: "claim_drift", severity: "error" });
  });

  it("is unchanged when no reversion records are supplied", () => {
    const findings = checkClaimDrift(
      staticDb(),
      "applied_safe_gaps",
      [row({ address: "0x140724fa0", rva: 0x724fa0, nameB: "Actor::OtherName" })],
      "1.10.163"
    );
    expect(findings[0]).toMatchObject({ findingType: "claim_drift", severity: "error" });
  });
});
