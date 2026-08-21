import Database from "better-sqlite3";
import { basename } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { fixedClock } from "../../src/clock.js";
import { loadConfig } from "../../src/config.js";
import { recordFinding } from "../../src/learning/findings.js";
import { discoverRepos } from "../../src/learning/repos.js";
import { scanRepo } from "../../src/learning/references.js";
import type { AuditDeps } from "../../src/tools/auditHardcodedAddresses.js";
import { suggestHooks } from "../../src/tools/suggestHooks.js";
import { tempGitRepo, tempLearningDb } from "../helpers/fixtures.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

const CLOCK = fixedClock("2026-08-13T10:00:00.000Z");

function setup() {
  const learning = tempLearningDb();
  cleanups.push(learning.cleanup);

  // Repo A hooks 0x1000. Repo B hooks nothing.
  const repoA = tempGitRepo({ "src/a.cpp": "REL::Relocation<void*> h{ REL::Offset(0x1000) };" });
  const repoB = tempGitRepo({ "src/b.cpp": "int nothing = 0;" });
  cleanups.push(repoA.cleanup, repoB.cleanup);

  const sdb = new Database(":memory:");
  cleanups.push(() => sdb.close());
  sdb.exec(`
    CREATE TABLE symbols (version TEXT, rva INTEGER, name TEXT, signature TEXT, sizeof INTEGER);
    CREATE TABLE address_library (version TEXT, id INTEGER, rva INTEGER);
    CREATE TABLE name_disagreements (address TEXT, rva INTEGER, existing_name TEXT, decoded_name TEXT, note TEXT, verdict_155 TEXT);
    CREATE TABLE misname_corrections (address TEXT, rva INTEGER, old_name TEXT, new_name TEXT, method TEXT, note TEXT);
    CREATE TABLE misname_reverts (address TEXT, rva INTEGER, wrongly_renamed_to TEXT, revert_to TEXT);
    CREATE TABLE subsystem_index (version TEXT, category TEXT, rva INTEGER, name TEXT, PRIMARY KEY (version, category, rva));
    INSERT INTO subsystem_index VALUES ('1.10.163', 'AI_Combat', 0x1000, 'Combat::Start');
    INSERT INTO subsystem_index VALUES ('1.10.163', 'AI_Combat', 0x2000, 'Combat::Stop');
    INSERT INTO subsystem_index VALUES ('1.10.163', 'Quests', 0x3000, 'Quest::Advance');
    INSERT INTO symbols VALUES ('1.10.163', 0x1000, 'Combat::Start', 'sig', 8);
    INSERT INTO symbols VALUES ('1.10.163', 0x2000, 'Combat::Stop', 'sig', 8);
    INSERT INTO symbols VALUES ('1.10.163', 0x3000, 'Quest::Advance', 'sig', 8);
  `);

  const config = { ...loadConfig({}), workspaceRoots: [repoA.path, repoB.path] };
  const deps: AuditDeps = { staticDb: sdb, learningDb: learning.db, clock: CLOCK, config };

  discoverRepos(learning.db, CLOCK, config, { force: true });
  scanRepo(learning.db, sdb, CLOCK, config, repoA.path, { force: true });
  scanRepo(learning.db, sdb, CLOCK, config, repoB.path, { force: true });

  return { deps, repoA: repoA.path, repoB: repoB.path };
}

describe("suggest_hooks", () => {
  it("omits what this repo already hooks", async () => {
    const { deps, repoA } = setup();
    const result = await suggestHooks({ subsystem: "AI_Combat", repo: repoA }, deps);

    expect(result.candidates).toBe(2);
    expect(result.alreadyHooked).toBe(1);
    expect(result.suggestions.map((s) => s.name)).toEqual(["Combat::Stop"]);
  });

  it("REGRESSION: another repo's hooks never suppress a suggestion here", async () => {
    const { deps, repoB } = setup();
    const result = await suggestHooks({ subsystem: "AI_Combat", repo: repoB }, deps);

    // Repo A hooks Combat::Start. Repo B does not, so it must still be suggested for repo B.
    expect(result.alreadyHooked).toBe(0);
    expect(result.suggestions.map((s) => s.name).sort()).toEqual(["Combat::Start", "Combat::Stop"]);
  });

  it("scopes to the requested subsystem only", async () => {
    const { deps, repoB } = setup();
    const result = await suggestHooks({ subsystem: "Quests", repo: repoB }, deps);
    expect(result.suggestions.map((s) => s.name)).toEqual(["Quest::Advance"]);
  });

  it("renders both RVA and VA and carries the static verdict", async () => {
    const { deps, repoB } = setup();
    const suggestion = (await suggestHooks({ subsystem: "Quests", repo: repoB }, deps))
      .suggestions[0];
    expect(suggestion.rva).toBe("0x3000");
    expect(suggestion.va).toBe("0x140003000");
    expect(suggestion.confidenceVerdict).toBe("no_audit_trail");
  });

  it("attaches a learned finding when one exists", async () => {
    const { deps, repoB } = setup();
    recordFinding(deps.learningDb, CLOCK, {
      version: "1.10.163",
      rva: 0x3000,
      rawDecompileHash: "h",
      model: "m",
      promptVersion: "1",
      extractorVersion: "1",
      projectFingerprint: null,
      proposedName: "Quest::AdvanceStage",
      confidence: "medium",
      note: "n",
      origin: "daemon",
      ghidraVersion: null,
    });

    const suggestion = (await suggestHooks({ subsystem: "Quests", repo: repoB }, deps))
      .suggestions[0];
    expect(suggestion.learnedFinding!.proposedName).toBe("Quest::AdvanceStage");
  });

  it("accepts a repo basename as well as a full path", async () => {
    const { deps, repoA } = setup();
    const byName = await suggestHooks({ subsystem: "AI_Combat", repo: basename(repoA) }, deps);
    expect(byName.repo).toBe(repoA);
  });

  it("errors clearly on an unknown repo", async () => {
    const { deps } = setup();
    await expect(suggestHooks({ subsystem: "AI_Combat", repo: "NotARepo" }, deps)).rejects.toThrow(
      /known repos/i
    );
  });

  it("returns an empty candidate set for an unknown subsystem rather than throwing", async () => {
    const { deps, repoB } = setup();
    const result = await suggestHooks({ subsystem: "NoSuchCategory", repo: repoB }, deps);
    expect(result.candidates).toBe(0);
    expect(result.suggestions).toEqual([]);
  });
});
