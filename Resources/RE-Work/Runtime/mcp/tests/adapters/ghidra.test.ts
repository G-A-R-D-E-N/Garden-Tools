import { chmodSync, mkdtempSync, readlinkSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { loadConfig } from "../../src/config.js";
import {
  classifyGhidraFailure,
  createGhidraDecompiler,
  hashDecompile,
  normalizeDecompile,
  parseDecompileOutput,
  sanitizeGhidraPath,
  stripGhidraScriptLog,
} from "../../src/adapters/ghidra.js";

const cleanups: (() => void)[] = [];
afterEach(() => {
  while (cleanups.length) cleanups.pop()!();
});

function fakeHeadless(bodyJs: string): string {
  const dir = mkdtempSync(join(tmpdir(), "re-ghidra-"));
  cleanups.push(() => rmSync(dir, { recursive: true, force: true }));
  const path = join(dir, "analyzeHeadless");
  writeFileSync(path, `#!/usr/bin/env node\n${bodyJs}\n`);
  chmodSync(path, 0o755);
  return path;
}

const REAL_OUTPUT = [
  "INFO  Using log config file: jar:file:/ghidra/...",
  "INFO  Opening project: Fallout4_163_unpacked",
  "### DECOMPILE PlayerCharacter::Update @ 0x140749ea0 (RVA 0x749ea0)",
  "void Update(PlayerCharacter *this)",
  "{",
  "  this->flags |= 2;",
  "  return;",
  "}",
  "INFO  ANALYZING changes to program: Fallout4_163_unpacked",
  "INFO  Packed database cache: /tmp/ghidra",
].join("\n");

describe("parseDecompileOutput", () => {
  it("pulls the function out of surrounding Ghidra framework logging", () => {
    const parsed = parseDecompileOutput(REAL_OUTPUT)!;
    expect(parsed.functionName).toBe("PlayerCharacter::Update");
    expect(parsed.entryRva).toBe(0x749ea0);
    expect(parsed.body).toContain("void Update(PlayerCharacter *this)");
    expect(parsed.body).toContain("this->flags |= 2;");
    expect(parsed.body).not.toContain("INFO");
  });

  it("returns null when the script reported no function", () => {
    expect(parseDecompileOutput("INFO x\n### 0x749ea0: no function here\nINFO y")).toBeNull();
  });

  it("returns null when the marker never appears", () => {
    expect(parseDecompileOutput("INFO only logging here")).toBeNull();
  });
});

describe("parseDecompileOutput on real headless output", () => {
  // Captured verbatim from analyzeHeadless on the 1.10.163 project. Headless wraps every script
  // println in "INFO  <script>> ... (GhidraScript)", which the parser has to undo.
  const WRAPPED = [
    "INFO  Headless startup complete (953 ms) (AnalyzeHeadless)  ",
    "INFO  DecompileAt.java> ### DECOMPILE NavMesh::CreateGroupData @ 0x140749ea0 (RVA 0x749ea0) (GhidraScript)  ",
    "INFO  DecompileAt.java>  (GhidraScript)  ",
    "INFO  DecompileAt.java> bool NavMesh::CreateGroupData (GhidraScript)  ",
    "INFO  DecompileAt.java>               (NavMesh *this,TESObjectREFR *p0) (GhidraScript)  ",
    "INFO  DecompileAt.java> { (GhidraScript)  ",
    "INFO  DecompileAt.java>   if (p0 == (TESObjectREFR *)0x0) { (GhidraScript)  ",
    "INFO  DecompileAt.java>     return (bool)in_AL; (GhidraScript)  ",
    "INFO  DecompileAt.java>   } (GhidraScript)  ",
    "INFO  DecompileAt.java> } (GhidraScript)  ",
    "INFO  ANALYZING changes to program: Fallout4_163_unpacked (HeadlessAnalyzer)  ",
  ].join("\n");

  it("unwraps the script-log prefix and suffix", () => {
    const parsed = parseDecompileOutput(WRAPPED)!;
    expect(parsed.functionName).toBe("NavMesh::CreateGroupData");
    expect(parsed.entryRva).toBe(0x749ea0);
    expect(parsed.body).toContain("bool NavMesh::CreateGroupData");
    expect(parsed.body).toContain("if (p0 == (TESObjectREFR *)0x0) {");
    expect(parsed.body).not.toContain("GhidraScript");
    expect(parsed.body).not.toContain("INFO");
  });

  it("stops at the framework log line that follows the body", () => {
    expect(parseDecompileOutput(WRAPPED)!.body).not.toContain("ANALYZING");
  });

  it("stripGhidraScriptLog returns null for a line that is not script output", () => {
    expect(stripGhidraScriptLog("INFO  Headless startup complete (AnalyzeHeadless)  ")).toBeNull();
    expect(stripGhidraScriptLog("plain text")).toBeNull();
  });
});

describe("sanitizeGhidraPath", () => {
  it("leaves a clean path alone", () => {
    expect(sanitizeGhidraPath("/tmp/plain/path")).toBe("/tmp/plain/path");
  });

  it("routes a path containing '&' through a symlink Ghidra will accept", () => {
    const real = "/run/media/user/Games-Storage/Projects/Tools & Reverse Engineering/x";
    const sanitized = sanitizeGhidraPath(real);
    expect(sanitized).not.toBe(real);
    expect(sanitized).not.toContain("&");
    expect(readlinkSync(sanitized)).toBe(real);
    // Stable across calls so repeat runs do not churn symlinks.
    expect(sanitizeGhidraPath(real)).toBe(sanitized);
    rmSync(sanitized, { force: true });
  });
});

describe("normalizeDecompile + hashDecompile", () => {
  it("is stable across trailing whitespace and line endings", () => {
    const a = normalizeDecompile("void f()\r\n{\r\n  return;   \r\n}\r\n");
    const b = normalizeDecompile("void f()\n{\n  return;\n}");
    expect(a).toBe(b);
    expect(hashDecompile(a)).toBe(hashDecompile(b));
  });

  it("changes when the code actually changes", () => {
    expect(hashDecompile(normalizeDecompile("void f(){}"))).not.toBe(
      hashDecompile(normalizeDecompile("void g(){}"))
    );
  });

  it("produces a 64-char sha256 hex digest", () => {
    expect(hashDecompile("anything")).toMatch(/^[0-9a-f]{64}$/);
  });
});

describe("classifyGhidraFailure", () => {
  it("detects a locked project", () => {
    expect(classifyGhidraFailure("Unable to lock project! Already open?", "")).toBe("locked");
    expect(classifyGhidraFailure("", "project is locked by another process")).toBe("locked");
  });

  it("falls back to script_error", () => {
    expect(classifyGhidraFailure("java.lang.NullPointerException", "")).toBe("script_error");
  });
});

describe("createGhidraDecompiler", () => {
  const config = loadConfig({});

  it("decompiles through the child process and hashes the body", async () => {
    const path = fakeHeadless(
      `process.stdout.write(${JSON.stringify(REAL_OUTPUT)}); process.exit(0);`
    );
    const decompile = createGhidraDecompiler({ config, analyzeHeadlessPath: path });
    const result = await decompile({ version: "1.10.163", rva: 0x749ea0 });

    expect(result.functionName).toBe("PlayerCharacter::Update");
    expect(result.entryRva).toBe(0x749ea0);
    expect(result.normalizedHash).toMatch(/^[0-9a-f]{64}$/);
    expect(result.truncated).toBe(false);
  });

  it("passes the RVA as a bare argv element, never through a shell", async () => {
    // If the implementation used a shell, this argument would be interpreted rather than echoed.
    const path = fakeHeadless(
      `const args = process.argv.slice(2);
       process.stdout.write("### DECOMPILE Probe::Args @ 0x140000001 (RVA 0x1)\\n" + JSON.stringify(args) + "\\n");`
    );
    const decompile = createGhidraDecompiler({ config, analyzeHeadlessPath: path });
    const result = await decompile({ version: "1.10.163", rva: 0x749ea0 });

    const args = JSON.parse(result.body.trim());
    expect(args).toContain("749ea0");
    expect(args).toContain("-readOnly");
    expect(args).toContain("DecompileAt.java");
  });

  // The 1.11.221 project holds the real target *and* the 1.11.191 AutoVT source import. Ghidra
  // processes them alphabetically, so a bare "-process" decompiles Fallout4.exe (191) and the
  // parser takes that first marker — 191 code recorded as 1.11.221. Name the program instead.
  const ARGS_PROBE = `const args = process.argv.slice(2);
     process.stdout.write("### DECOMPILE Probe::Args @ 0x140000001 (RVA 0x1)\\n" + JSON.stringify(args) + "\\n");`;

  it.each([
    ["1.10.163", "Fallout4.exe.unpacked.exe"],
    ["1.11.221", "Fallout4.exe.unpacked.exe"],
  ])("names the program to process for %s", async (version, programName) => {
    const path = fakeHeadless(ARGS_PROBE);
    const decompile = createGhidraDecompiler({ config, analyzeHeadlessPath: path });
    const result = await decompile({ version, rva: 0x749ea0 });

    const args: string[] = JSON.parse(result.body.trim());
    expect(args[args.indexOf("-process") + 1]).toBe(programName);
  });

  it("throws no_function when the target has no function", async () => {
    const path = fakeHeadless(`process.stdout.write("### 0x1: no function here");`);
    const decompile = createGhidraDecompiler({ config, analyzeHeadlessPath: path });
    await expect(decompile({ version: "1.10.163", rva: 1 })).rejects.toMatchObject({
      kind: "no_function",
    });
  });

  it("throws locked when Ghidra could not take the project lock", async () => {
    const path = fakeHeadless(
      `process.stderr.write("Unable to lock project! Already open?"); process.exit(1);`
    );
    const decompile = createGhidraDecompiler({ config, analyzeHeadlessPath: path });
    await expect(decompile({ version: "1.10.163", rva: 1 })).rejects.toMatchObject({
      kind: "locked",
    });
  });

  it("kills the child on timeout and reports kind=timeout", async () => {
    const path = fakeHeadless(`setTimeout(() => {}, 60_000);`);
    const decompile = createGhidraDecompiler({
      config: { ...config, ghidraTimeoutMs: 300 },
      analyzeHeadlessPath: path,
    });
    const started = Date.now();
    await expect(decompile({ version: "1.10.163", rva: 1 })).rejects.toMatchObject({
      kind: "timeout",
    });
    expect(Date.now() - started).toBeLessThan(10_000);
  });

  it("rejects a version with no configured Ghidra project", async () => {
    const path = fakeHeadless(`process.stdout.write("unused");`);
    const decompile = createGhidraDecompiler({ config, analyzeHeadlessPath: path });
    await expect(decompile({ version: "9.9.9", rva: 1 })).rejects.toMatchObject({
      kind: "unknown_version",
    });
  });

  it("truncates output beyond maxDecompileBytes instead of buffering it all", async () => {
    const huge = "### DECOMPILE Big::Fn @ 0x140000001 (RVA 0x1)\n" + "x".repeat(200_000);
    const path = fakeHeadless(`process.stdout.write(${JSON.stringify(huge)});`);
    const decompile = createGhidraDecompiler({
      config: { ...config, maxDecompileBytes: 2_000 },
      analyzeHeadlessPath: path,
    });
    const result = await decompile({ version: "1.10.163", rva: 1 });
    expect(result.truncated).toBe(true);
    expect(result.body.length).toBeLessThanOrEqual(2_000);
  });
});
