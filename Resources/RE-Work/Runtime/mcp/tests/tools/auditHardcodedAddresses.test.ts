import { afterEach, describe, expect, it } from "vitest";
import { fixedClock } from "../../src/clock.js";
import { loadConfig } from "../../src/config.js";
import { recordFinding } from "../../src/learning/findings.js";
import {
  auditHardcodedAddresses,
  type AuditDeps,
} from "../../src/tools/auditHardcodedAddresses.js";
import {
  seedIdSpaces,
  seededRva,
  tempGitRepo,
  tempLearningDb,
  tempStaticDb,
} from "../helpers/fixtures.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

const CLOCK = fixedClock("2026-08-13T10:00:00.000Z");

// Enough scalar ids for the provenance test to have a sample, all authored against OG (never a
// multiple of the AE stride), plus the constructs under test.
const OG_FILLER = Array.from({ length: 40 }, (_, i) => `REL::ID(${i * 20 + 7});`).join("\n");

const SOURCE = [
  OG_FILLER,
  "REL::Relocation<void*> a{ REL::Offset(0x128B0) };",
  "REL::Relocation<void*> c{ REL::ID(340) };",
  "inline constexpr REL::VariantID paired{ 340, 100007 };",
  "inline constexpr REL::VariantID gone{ 340, 0 };",
  "REL::Relocation<uintptr_t> v{ RE::VTABLE::Actor[5] };",
].join("\n");

function setup(source = SOURCE): { deps: AuditDeps; repoPath: string } {
  const learning = tempLearningDb();
  cleanups.push(learning.cleanup);
  const repo = tempGitRepo({ "src/hooks.cpp": source });
  cleanups.push(repo.cleanup);

  const sdb = tempStaticDb();
  cleanups.push(() => sdb.close());
  seedIdSpaces(sdb, { ogMax: 2000, aeStride: 20 });
  const ogRva = seededRva("1.10.163", 340);
  const aeRva = seededRva("1.11.221", 100007);
  sdb.exec(`
    INSERT INTO symbols VALUES ('1.10.163', 0x128b0, 'Actor::SetFlag', 'sig', 15);
    INSERT INTO symbols VALUES ('1.10.163', ${ogRva}, 'Actor::Paired', 'sig', 8);
    INSERT INTO og_ae_map VALUES ('Actor::Paired(void)', ${ogRva}, 340, ${aeRva}, 100007);
    INSERT INTO pdb_symbols (name, signature) VALUES ('Actor::Paired', 'void __fastcall Actor::Paired(void)');
    INSERT INTO subsystem_index VALUES ('1.10.163', 'Actor_Character', ${ogRva}, 'Actor::Paired');
  `);

  return {
    repoPath: repo.path,
    deps: {
      staticDb: sdb,
      learningDb: learning.db,
      clock: CLOCK,
      config: { ...loadConfig({}), workspaceRoots: [repo.path] },
    },
  };
}

describe("audit_hardcoded_addresses", () => {
  it("discovers, scans, and audits with no arguments", async () => {
    const { deps } = setup();
    const result = await auditHardcodedAddresses({}, deps);

    expect(result.nameAuthorityVersion).toBe("1.10.163");
    expect(result.runtimeVersions).toEqual({
      kOG: "1.10.163",
      kNG: "1.10.984",
      kAE: "1.11.221",
    });
    expect(result.scannedRepos).toHaveLength(1);
    expect(result.totals.references).toBe(45);
  });

  it("expands one literal into a slot per runtime", async () => {
    const { deps } = setup();
    const result = await auditHardcodedAddresses({ include_ok: true }, deps);
    const paired = result.references.find((r) => r.form === "variant" && r.rawValue === "{340,100007}")!;
    expect(paired.slots.map((s) => s.runtime)).toEqual(["kOG", "kNG", "kAE"]);
    // { og, ae } pads: kNG takes the AE value too.
    expect(paired.slots.map((s) => s.rawValue)).toEqual(["340", "100007", "100007"]);
  });

  it("names an AE address by bridging to the curated OG name, not by reading OG at that RVA", async () => {
    const { deps } = setup();
    const result = await auditHardcodedAddresses({ include_ok: true }, deps);
    const paired = result.references.find((r) => r.rawValue === "{340,100007}")!;
    const ae = paired.slots.find((s) => s.runtime === "kAE")!;

    expect(ae.rva).toBe(`0x${seededRva("1.11.221", 100007).toString(16)}`);
    expect(ae.name).toBe("Actor::Paired");
    expect(ae.nameSource).toBe("symbols");
    expect(ae.bridgeName).toBe("Actor::Paired(void)");
    expect(ae.pdbSignature).toBe("void __fastcall Actor::Paired(void)");
    expect(ae.subsystem).toBe("Actor_Character");
  });

  it("proves one id handed to every runtime from the bridge pairing alone", async () => {
    const { deps } = setup();
    const result = await auditHardcodedAddresses({ verdicts: ["single_id_all_runtimes"] }, deps);

    const bare = result.references.find((r) => r.rawValue === "340")!;
    expect(bare.verdict).toBe("single_id_all_runtimes");
    // The bridge pairs 340 with 100007, which settles it without any statistical argument.
    expect(bare.detail).toMatch(/pairs OG id 340 .*with AE id 100007/);
    expect(bare.suggestion).toBe("REL::VariantID{ 340, 100007 }");
  });

  it("falls back to the file's measured provenance when the bridge has no pairing", async () => {
    // Ids the bridge does not carry, so only the population argument is available — the situation
    // in every generated table.
    const { deps } = setup();
    const result = await auditHardcodedAddresses({ verdicts: ["single_id_all_runtimes"] }, deps);
    const unpaired = result.references.find((r) => r.rawValue === "27")!;
    expect(unpaired.provenance!.verdict).toBe("og_native");
    expect(unpaired.detail).toMatch(/og-native/);
    expect(unpaired.detail).toMatch(/by chance/);
  });

  it("does not flag a properly slotted VariantID", async () => {
    const { deps } = setup();
    const result = await auditHardcodedAddresses({ verdicts: ["single_id_all_runtimes"] }, deps);
    expect(result.references.map((r) => r.rawValue)).not.toContain("{340,100007}");
  });

  it("treats the 0 sentinel as declared-absent rather than inventing an address", async () => {
    const { deps } = setup();
    const result = await auditHardcodedAddresses({ include_ok: true }, deps);
    const gone = result.references.find((r) => r.rawValue === "{340,0}")!;
    const ae = gone.slots.find((s) => s.runtime === "kAE")!;
    expect(ae.rva).toBeNull();
    expect(ae.unresolvedReason).toMatch(/0 sentinel/);
  });

  it("reports repo-relative file:line locations", async () => {
    const { deps } = setup();
    const result = await auditHardcodedAddresses({ include_ok: true }, deps);
    expect(result.references.map((r) => r.location)).toContain("src/hooks.cpp:41");
  });

  it("attaches the static-index verdict to a resolved reference", async () => {
    const { deps } = setup();
    const result = await auditHardcodedAddresses({ include_ok: true }, deps);
    const known = result.references.find((r) => r.rawValue === "0x128B0")!;
    const og = known.slots.find((s) => s.runtime === "kOG")!;
    expect(og).toMatchObject({ rva: "0x128b0", va: "0x1400128b0" });
    expect(known.staticIndex!.name).toBe("Actor::SetFlag");
  });

  it("reports a vtable symbol as unresolvable with a reason, not as an RVA", async () => {
    const { deps } = setup();
    const result = await auditHardcodedAddresses({}, deps);
    const vtable = result.references.find((r) => r.kind === "vtable_symbol")!;
    expect(vtable.verdict).toBe("unresolvable_symbol");
    expect(vtable.slots).toEqual([]);
    expect(vtable.unresolvedReason).toMatch(/CommonLibF4/i);
    expect(vtable.rawValue).toBe("Actor[5]");
  });

  it("withholds clean references by default but still counts them", async () => {
    const { deps } = setup();
    const quiet = await auditHardcodedAddresses({}, deps);
    const loud = await auditHardcodedAddresses({ include_ok: true }, deps);
    expect(loud.references.length).toBeGreaterThan(quiet.references.length);
    expect(quiet.totals.references).toBe(loud.totals.references);
    expect(quiet.references.every((r) => r.verdict !== "ok")).toBe(true);
  });

  it("surfaces a learned finding and its contradiction", async () => {
    const { deps } = setup();
    recordFinding(deps.learningDb, CLOCK, {
      version: "1.10.163",
      rva: 0x128b0,
      rawDecompileHash: "h",
      model: "m",
      promptVersion: "1",
      extractorVersion: "1",
      projectFingerprint: null,
      proposedName: "Actor::ClearFlag",
      confidence: "medium",
      note: "n",
      origin: "daemon",
      ghidraVersion: null,
    });

    const result = await auditHardcodedAddresses({ include_ok: true }, deps);
    const known = result.references.find((r) => r.rawValue === "0x128B0")!;
    expect(known.learnedFinding!.proposedName).toBe("Actor::ClearFlag");
    expect(known.contradiction).toMatchObject({
      staticName: "Actor::SetFlag",
      proposedName: "Actor::ClearFlag",
    });
    expect(result.totals.contradictions).toBe(1);
  });

  it("reuses the cache on a second call instead of rescanning", async () => {
    const { deps } = setup();
    await auditHardcodedAddresses({}, deps);
    const second = await auditHardcodedAddresses({}, deps);
    expect(second.scannedRepos[0].scanned).toBe(false);
    expect(second.totals.references).toBe(45);
  });

  it("force_rescan rescans", async () => {
    const { deps } = setup();
    await auditHardcodedAddresses({}, deps);
    const forced = await auditHardcodedAddresses({ force_rescan: true }, deps);
    expect(forced.scannedRepos[0].scanned).toBe(true);
  });

  it("an explicit path limits the audit to that scope", async () => {
    const { deps, repoPath } = setup();
    const result = await auditHardcodedAddresses({ paths: [repoPath] }, deps);
    expect(result.scannedRepos).toHaveLength(1);
    expect(result.totals.references).toBe(45);
  });

  it("errors on a path that does not exist rather than returning nothing", async () => {
    const { deps } = setup();
    await expect(auditHardcodedAddresses({ paths: ["/no/such/place"] }, deps)).rejects.toThrow(
      /\/no\/such\/place/
    );
  });
});
