import { mkdirSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { basename, join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { fixedClock } from "../../src/clock.js";
import { loadConfig } from "../../src/config.js";
import {
  discoverRepos,
  findGitRepos,
  fingerprintRepo,
  isDiscoveryStale,
  listDiscoveredRepos,
  markRepoScanned,
  resolveRepoArgument,
} from "../../src/learning/repos.js";
import { tempGitRepo, tempLearningDb } from "../helpers/fixtures.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

const CLOCK = fixedClock("2026-08-13T10:00:00.000Z");

function workspace(): string {
  const dir = mkdtempSync(join(tmpdir(), "re-workspace-"));
  cleanups.push(() => rmSync(dir, { recursive: true, force: true }));
  return dir;
}

describe("fingerprintRepo", () => {
  it("is stable for an unchanged repo", () => {
    const repo = tempGitRepo({ "src/a.cpp": "int a;" });
    cleanups.push(repo.cleanup);
    expect(fingerprintRepo(repo.path)).toBe(fingerprintRepo(repo.path));
  });

  it("changes when a commit is made", () => {
    const repo = tempGitRepo({ "src/a.cpp": "int a;" });
    cleanups.push(repo.cleanup);
    const before = fingerprintRepo(repo.path);
    repo.write("src/b.cpp", "int b;");
    repo.commitAll("add b");
    expect(fingerprintRepo(repo.path)).not.toBe(before);
  });

  it("changes on an uncommitted working-tree edit", () => {
    const repo = tempGitRepo({ "src/a.cpp": "int a;" });
    cleanups.push(repo.cleanup);
    const before = fingerprintRepo(repo.path);
    repo.write("src/a.cpp", "int a; int c;");
    expect(fingerprintRepo(repo.path)).not.toBe(before);
  });

  it("returns null for a directory that is not a git repo", () => {
    expect(fingerprintRepo(workspace())).toBeNull();
  });
});

describe("findGitRepos", () => {
  it("finds repos under the given roots", () => {
    const outer = tempGitRepo();
    const nested = tempGitRepo();
    cleanups.push(outer.cleanup, nested.cleanup);

    const found = findGitRepos([outer.path, nested.path], 4);
    expect(found).toContain(outer.path);
    expect(found).toContain(nested.path);
  });

  it("ignores node_modules and build output", () => {
    const root = workspace();
    mkdirSync(join(root, "node_modules", "pkg", ".git"), { recursive: true });
    mkdirSync(join(root, "dist", ".git"), { recursive: true });
    expect(findGitRepos([root], 4)).toEqual([]);
  });

  it("respects the depth limit", () => {
    const root = workspace();
    mkdirSync(join(root, "a", "b", "c", "d", "e", ".git"), { recursive: true });
    expect(findGitRepos([root], 2)).toEqual([]);
  });

  it("tolerates a root that does not exist", () => {
    expect(findGitRepos(["/definitely/not/here"], 4)).toEqual([]);
  });
});

describe("discoverRepos", () => {
  it("records every discovered repo with its fingerprint", () => {
    const f = tempLearningDb();
    cleanups.push(f.cleanup);
    const repo = tempGitRepo({ "src/a.cpp": "int a;" });
    cleanups.push(repo.cleanup);

    const config = { ...loadConfig({}), workspaceRoots: [repo.path] };
    const result = discoverRepos(f.db, CLOCK, config);

    expect(result.discovered).toBe(1);
    const repos = listDiscoveredRepos(f.db);
    expect(repos[0].path).toBe(repo.path);
    expect(repos[0].repoFingerprint).toBeTruthy();
    expect(repos[0].lastDiscoveredAt).toBe("2026-08-13T10:00:00.000Z");
  });

  it("re-running does not duplicate rows and preserves scan state", () => {
    const f = tempLearningDb();
    cleanups.push(f.cleanup);
    const repo = tempGitRepo();
    cleanups.push(repo.cleanup);
    const config = { ...loadConfig({}), workspaceRoots: [repo.path] };

    discoverRepos(f.db, CLOCK, config);
    markRepoScanned(f.db, CLOCK, repo.path, "fp-1", null);
    discoverRepos(f.db, fixedClock("2026-08-14T10:00:00.000Z"), config, { force: true });

    const repos = listDiscoveredRepos(f.db);
    expect(repos).toHaveLength(1);
    expect(repos[0].lastScannedAt).toBe("2026-08-13T10:00:00.000Z");
    expect(repos[0].lastDiscoveredAt).toBe("2026-08-14T10:00:00.000Z");
  });

  it("skips the walk when discovery is fresh and force is not set", () => {
    const f = tempLearningDb();
    cleanups.push(f.cleanup);
    const repo = tempGitRepo();
    cleanups.push(repo.cleanup);
    const config = { ...loadConfig({}), workspaceRoots: [repo.path] };

    discoverRepos(f.db, CLOCK, config);
    const second = discoverRepos(f.db, fixedClock("2026-08-13T10:30:00.000Z"), config);
    expect(second.skipped).toBe(true);
  });

  it("does not skip once the TTL has elapsed", () => {
    const f = tempLearningDb();
    cleanups.push(f.cleanup);
    const repo = tempGitRepo();
    cleanups.push(repo.cleanup);
    const config = { ...loadConfig({}), workspaceRoots: [repo.path] };

    discoverRepos(f.db, CLOCK, config);
    const later = fixedClock("2026-08-15T10:00:00.000Z");
    expect(isDiscoveryStale(f.db, later, config.repoDiscoveryTtlMs)).toBe(true);
    expect(discoverRepos(f.db, later, config).skipped).toBe(false);
  });

  it("reports stale on an empty cache", () => {
    const f = tempLearningDb();
    cleanups.push(f.cleanup);
    expect(isDiscoveryStale(f.db, CLOCK, 86_400_000)).toBe(true);
  });
});

describe("resolveRepoArgument", () => {
  it("resolves by absolute path and by basename", () => {
    const f = tempLearningDb();
    cleanups.push(f.cleanup);
    const repo = tempGitRepo();
    cleanups.push(repo.cleanup);
    discoverRepos(f.db, CLOCK, { ...loadConfig({}), workspaceRoots: [repo.path] });

    expect(resolveRepoArgument(f.db, repo.path).path).toBe(repo.path);
    expect(resolveRepoArgument(f.db, basename(repo.path)).path).toBe(repo.path);
  });

  it("throws a helpful error listing known repos when nothing matches", () => {
    const f = tempLearningDb();
    cleanups.push(f.cleanup);
    const repo = tempGitRepo();
    cleanups.push(repo.cleanup);
    discoverRepos(f.db, CLOCK, { ...loadConfig({}), workspaceRoots: [repo.path] });

    expect(() => resolveRepoArgument(f.db, "NoSuchRepo")).toThrow(/known repos/i);
  });
});
