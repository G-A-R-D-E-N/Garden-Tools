import Database from "better-sqlite3";
import { afterEach, describe, expect, it } from "vitest";
import { fixedClock } from "../../src/clock.js";
import { loadConfig } from "../../src/config.js";
import { discoverRepos, listDiscoveredRepos } from "../../src/learning/repos.js";
import {
  listReferences,
  parseReferences,
  resolveSlot,
  scanRepo,
  sourceFilesIn,
} from "../../src/learning/references.js";
import { tempGitRepo, tempLearningDb, tempStaticDb } from "../helpers/fixtures.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

const CLOCK = fixedClock("2026-08-13T10:00:00.000Z");

function staticDb(): Database.Database {
  const db = tempStaticDb(`
    INSERT INTO address_library VALUES ('1.10.163', 1000120, 8000);
    INSERT INTO symbols VALUES ('1.10.163', 8000, 'Some::Function', 'sig', 4);
  `);
  cleanups.push(() => db.close());
  return db;
}

const REAL_SOURCE = [
  "#include <RE/Fallout.h>",
  "REL::Relocation<RawArray*> renderers{ REL::Offset(0x5A66B58) };",
  "static REL::Relocation<GetTexture_t> g{ REL::Offset{ 0x27D61F0 } };",
  "static REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE::BSEffectShaderProperty[0] };",
  "REL::Relocation<uintptr_t> byId{ REL::ID(1000120) };",
  "// REL::Offset(0xDEADBEEF) in a comment is still a hardcoded address worth auditing",
  "int unrelated = 42;",
].join("\n");

describe("parseReferences", () => {
  it("finds both the paren and brace REL::Offset forms", () => {
    const refs = parseReferences(REAL_SOURCE).filter((r) => r.kind === "rel_offset");
    expect(refs.map((r) => r.rawValue)).toEqual(["0x5A66B58", "0x27D61F0", "0xDEADBEEF"]);
  });

  it("finds REL::ID with its decimal value", () => {
    const refs = parseReferences(REAL_SOURCE).filter((r) => r.kind === "rel_id");
    expect(refs).toHaveLength(1);
    expect(refs[0].rawValue).toBe("1000120");
  });

  it("keeps a vtable reference symbolic, class and index intact", () => {
    const refs = parseReferences(REAL_SOURCE).filter((r) => r.kind === "vtable_symbol");
    expect(refs).toHaveLength(1);
    expect(refs[0].rawValue).toBe("BSEffectShaderProperty[0]");
  });

  it("records 1-based line numbers", () => {
    const refs = parseReferences(REAL_SOURCE);
    expect(refs.find((r) => r.rawValue === "0x5A66B58")!.lineNumber).toBe(2);
    expect(refs.find((r) => r.rawValue === "1000120")!.lineNumber).toBe(5);
  });

  it("finds nothing in source with no engine references", () => {
    expect(parseReferences("int main() { return 0; }")).toEqual([]);
  });

  it("handles several references on one line", () => {
    const refs = parseReferences("a{REL::Offset(0x1)}; b{REL::Offset(0x2)};");
    expect(refs.map((r) => r.rawValue)).toEqual(["0x1", "0x2"]);
    expect(refs.every((r) => r.lineNumber === 1)).toBe(true);
  });
});

describe("resolveSlot", () => {
  const version = "1.10.163";

  it("treats a REL::Offset literal as the RVA itself", () => {
    expect(resolveSlot(staticDb(), "offset", "0x1DCB30", version).rva).toBe(0x1dcb30);
  });

  it("resolves an id through the address library of that slot's own version", () => {
    expect(resolveSlot(staticDb(), "id", "1000120", version).rva).toBe(8000);
  });

  it("returns a reason naming the version rather than guessing", () => {
    const out = resolveSlot(staticDb(), "id", "999999999", version);
    expect(out.rva).toBeNull();
    expect(out.reason).toContain("1.10.163");
  });

  it("does not fall back to another version when a slot's own library lacks the id", () => {
    // The whole defect: 1000120 exists in OG only, and an AE slot must say so rather than
    // borrowing the OG answer.
    expect(resolveSlot(staticDb(), "id", "1000120", "1.11.221").rva).toBeNull();
  });
});

describe("sourceFilesIn", () => {
  it("returns C++ sources and skips everything else", () => {
    const repo = tempGitRepo({
      "src/a.cpp": "int a;",
      "src/b.h": "int b;",
      "src/c.inl": "int c;",
      "README.md": "docs",
      "node_modules/pkg/d.cpp": "int d;",
      "build/e.cpp": "int e;",
    });
    cleanups.push(repo.cleanup);

    const files = sourceFilesIn(repo.path).sort();
    expect(files).toEqual(["src/a.cpp", "src/b.h", "src/c.inl"]);
  });

  it("skips dot-directories, which hold copies rather than new source", () => {
    // Seen live in Prisma: .claude/worktrees/<branch>/ and .release-build-2.0.1/ are full source
    // copies. Walking them reports every reference two or three times over.
    const repo = tempGitRepo({
      "src/real.cpp": "REL::Offset(0x1)",
      ".claude/worktrees/wt/src/real.cpp": "REL::Offset(0x1)",
      ".release-build-2.0.1/src/real.cpp": "REL::Offset(0x1)",
    });
    cleanups.push(repo.cleanup);

    expect(sourceFilesIn(repo.path)).toEqual(["src/real.cpp"]);
  });
});

describe("scanRepo", () => {
  function setup(files: Record<string, string>) {
    const learning = tempLearningDb();
    cleanups.push(learning.cleanup);
    const repo = tempGitRepo(files);
    cleanups.push(repo.cleanup);
    const config = { ...loadConfig({}), workspaceRoots: [repo.path] };
    discoverRepos(learning.db, CLOCK, config);
    return { db: learning.db, repo, config, staticDb: staticDb() };
  }

  it("caches parsed references with their resolutions", () => {
    const { db, repo, config, staticDb: sdb } = setup({ "src/a.cpp": REAL_SOURCE });
    const result = scanRepo(db, sdb, CLOCK, config, repo.path);

    expect(result.scanned).toBe(true);
    expect(result.error).toBeNull();

    const refs = listReferences(db, { repoPath: repo.path });
    expect(refs.length).toBe(5);

    const byId = refs.find((r) => r.kind === "rel_id")!;
    // One literal, three runtime slots — resolving only where that runtime's library has the id.
    expect(byId.slots.map((s) => s.runtime)).toEqual(["kOG", "kNG", "kAE"]);
    expect(byId.slots.find((s) => s.runtime === "kOG")!.resolvedRva).toBe(8000);
    expect(byId.slots.find((s) => s.runtime === "kAE")!.resolvedRva).toBeNull();

    const vtable = refs.find((r) => r.kind === "vtable_symbol")!;
    expect(vtable.slots).toEqual([]);

    expect(refs.every((r) => r.filePath === "src/a.cpp")).toBe(true);
  });

  it("skips a rescan when the fingerprint is unchanged", () => {
    const { db, repo, config, staticDb: sdb } = setup({ "src/a.cpp": REAL_SOURCE });
    scanRepo(db, sdb, CLOCK, config, repo.path);
    const second = scanRepo(db, sdb, CLOCK, config, repo.path);
    expect(second.scanned).toBe(false);
  });

  it("rescans after a working-tree change", () => {
    const { db, repo, config, staticDb: sdb } = setup({ "src/a.cpp": REAL_SOURCE });
    scanRepo(db, sdb, CLOCK, config, repo.path);
    repo.write("src/a.cpp", "REL::Relocation<int> x{ REL::Offset(0xAAAA) };");

    const second = scanRepo(db, sdb, CLOCK, config, repo.path);
    expect(second.scanned).toBe(true);

    const refs = listReferences(db, { repoPath: repo.path });
    expect(refs).toHaveLength(1);
    expect(refs[0].rawValue).toBe("0xAAAA");
  });

  it("force rescans even when the fingerprint is unchanged", () => {
    const { db, repo, config, staticDb: sdb } = setup({ "src/a.cpp": REAL_SOURCE });
    scanRepo(db, sdb, CLOCK, config, repo.path);
    expect(scanRepo(db, sdb, CLOCK, config, repo.path, { force: true }).scanned).toBe(true);
  });

  it("removed references disappear only after a successful replacement scan", () => {
    const { db, repo, config, staticDb: sdb } = setup({ "src/a.cpp": REAL_SOURCE });
    scanRepo(db, sdb, CLOCK, config, repo.path);
    expect(listReferences(db, { repoPath: repo.path }).length).toBe(5);

    repo.write("src/a.cpp", "int nothing_here = 0;");
    scanRepo(db, sdb, CLOCK, config, repo.path);
    expect(listReferences(db, { repoPath: repo.path })).toEqual([]);
  });

  it("a failed scan keeps the last known-good cache and records the error", () => {
    const { db, repo, config, staticDb: sdb } = setup({ "src/a.cpp": REAL_SOURCE });
    scanRepo(db, sdb, CLOCK, config, repo.path);
    const before = listReferences(db, { repoPath: repo.path });
    expect(before.length).toBe(5);

    // Simulate the repo vanishing mid-cycle: the scan must fail without erasing anything.
    repo.cleanup();
    const failed = scanRepo(db, sdb, CLOCK, config, repo.path, { force: true });

    expect(failed.scanned).toBe(false);
    expect(failed.error).toBeTruthy();
    expect(listReferences(db, { repoPath: repo.path }).length).toBe(5);
    expect(listDiscoveredRepos(db)[0].lastScanError).toBeTruthy();
  });
});
