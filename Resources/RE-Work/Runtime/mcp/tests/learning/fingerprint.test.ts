import { mkdirSync, mkdtempSync, rmSync, utimesSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { loadConfig, type LearningConfig } from "../../src/config.js";
import { ghidraProjectFingerprint } from "../../src/learning/analyze.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

const VERSION = "1.10.163";
const PROJECT_NAME = "Fallout4_163_unpacked";

interface Project {
  config: LearningConfig;
  repDir: string;
  /** Write a file inside the .rep tree, creating parent directories. */
  write(relative: string, contents: string): void;
  fingerprint(): string | null;
}

/**
 * A minimal stand-in for a real Ghidra project directory: a zero-byte .gpr next to the .rep
 * tree that actually holds the program database. Both facts are load-bearing for the fix.
 */
function tempProject(): Project {
  const projectDir = mkdtempSync(join(tmpdir(), "re-ghidra-proj-"));
  cleanups.push(() => rmSync(projectDir, { recursive: true, force: true }));

  writeFileSync(join(projectDir, `${PROJECT_NAME}.gpr`), "");
  const repDir = join(projectDir, `${PROJECT_NAME}.rep`);
  mkdirSync(join(repDir, "idata", "00", "~00000000.db"), { recursive: true });

  const config: LearningConfig = {
    ...loadConfig(),
    ghidraProjects: { [VERSION]: { projectDir, projectName: PROJECT_NAME } },
  };

  const project: Project = {
    config,
    repDir,
    write(relative, contents) {
      const path = join(repDir, relative);
      mkdirSync(dirname(path), { recursive: true });
      writeFileSync(path, contents);
    },
    fingerprint: () => ghidraProjectFingerprint(config, VERSION),
  };

  project.write("project.prp", "<FILE_INFO/>");
  project.write("idata/00/00000000.prp", "<FILE_INFO/>");
  project.write("idata/00/~00000000.db/db.1012.gbf", "program database v1012");
  project.write("idata/~index.dat", "idata index");
  project.write("idata/~index.bak", "idata index");
  project.write("user/~index.dat", "user index");
  project.write("versioned/~index.dat", "versioned index");
  project.write("versioned/~index.bak", "versioned index");
  return project;
}

/** Move mtime without touching size or contents — how Ghidra's index rotation looks on disk. */
function touch(path: string, secondsAhead: number): void {
  const when = new Date(Date.now() + secondsAhead * 1000);
  utimesSync(path, when, when);
}

/** The four files a read-only headless open rewrites every time. Measured, not assumed. */
const OPEN_CHURN = [
  "idata/~index.dat",
  "idata/~index.bak",
  "versioned/~index.dat",
  "versioned/~index.bak",
];

describe("ghidraProjectFingerprint", () => {
  it("is stable when the project is untouched", () => {
    const project = tempProject();
    expect(project.fingerprint()).toBe(project.fingerprint());
  });

  it("changes when Ghidra writes a new program database version", () => {
    const project = tempProject();
    const before = project.fingerprint();

    // What a save actually looks like: a new db.<n>.gbf appears in the .db directory.
    project.write("idata/00/~00000000.db/db.1013.gbf", "program database v1013");

    expect(project.fingerprint()).not.toBe(before);
  });

  it("changes when an existing .rep file grows", () => {
    const project = tempProject();
    const before = project.fingerprint();

    project.write("idata/00/~00000000.db/db.1012.gbf", "program database v1012 plus renames");

    expect(project.fingerprint()).not.toBe(before);
  });

  it("changes when a program file is rewritten at the same size", () => {
    const project = tempProject();
    const before = project.fingerprint();

    touch(join(project.repDir, "idata", "00", "00000000.prp"), 10);

    expect(project.fingerprint()).not.toBe(before);
  });

  it("ignores the index files Ghidra rotates on every project open", () => {
    const project = tempProject();
    const before = project.fingerprint();

    // Measured against the real 1.10.163 project: a read-only headless decompile rewrites
    // these four every time, size unchanged, mtime moved — it opened the project, nothing more.
    // Counting that as a content change makes the fingerprint self-invalidating: the decompile
    // that runs just after it is computed would guarantee the next call never dedups.
    for (const relative of OPEN_CHURN) touch(join(project.repDir, relative), 10);

    expect(project.fingerprint()).toBe(before);
  });

  it("still sees a save that happens alongside the index rotation", () => {
    const project = tempProject();
    const before = project.fingerprint();

    // A real save does both: the index rotates AND a new database version lands. Excluding the
    // churn must not swallow the signal buried in it.
    for (const relative of OPEN_CHURN) touch(join(project.repDir, relative), 10);
    project.write("idata/00/~00000000.db/db.1013.gbf", "program database v1013");

    expect(project.fingerprint()).not.toBe(before);
  });

  it("changes when a program is added to the project", () => {
    const project = tempProject();
    const before = project.fingerprint();

    project.write("idata/00/00000001.prp", "<FILE_INFO/>");
    project.write("idata/00/~00000001.db/db.1.gbf", "second program");

    expect(project.fingerprint()).not.toBe(before);
  });

  it("does not change when only the zero-byte .gpr is rewritten", () => {
    const project = tempProject();
    const before = project.fingerprint();

    // The .gpr never carries content, so it can never be the signal — but rewriting it must
    // not manufacture a false change either.
    const gpr = join(project.config.ghidraProjects[VERSION]!.projectDir, `${PROJECT_NAME}.gpr`);
    writeFileSync(gpr, "");
    const later = new Date(Date.now() + 10_000);
    utimesSync(gpr, later, later);

    expect(project.fingerprint()).toBe(before);
  });

  it("identifies the project it came from", () => {
    const project = tempProject();
    expect(project.fingerprint()).toContain(PROJECT_NAME);
  });

  it("returns null for a version with no configured project", () => {
    const project = tempProject();
    expect(ghidraProjectFingerprint(project.config, "9.9.9")).toBeNull();
  });

  it("returns null when the project directory is gone", () => {
    const project = tempProject();
    rmSync(project.config.ghidraProjects[VERSION]!.projectDir, { recursive: true, force: true });
    expect(project.fingerprint()).toBeNull();
  });
});
