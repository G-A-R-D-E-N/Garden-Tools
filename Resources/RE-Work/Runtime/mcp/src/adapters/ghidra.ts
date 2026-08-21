import { spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { mkdirSync, readlinkSync, symlinkSync, unlinkSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import type { LearningConfig } from "../config.js";
import { ANALYZE_HEADLESS, SCRIPTS_DIR } from "../paths.js";

export type GhidraFailureKind =
  | "locked"
  | "timeout"
  | "no_function"
  | "spawn_failed"
  | "script_error"
  | "unknown_version";

export class GhidraError extends Error {
  readonly kind: GhidraFailureKind;
  constructor(kind: GhidraFailureKind, message: string) {
    super(message);
    this.name = "GhidraError";
    this.kind = kind;
  }
}

export interface DecompileRequest {
  version: string;
  rva: number;
}

export interface DecompileResult {
  version: string;
  rva: number;
  entryRva: number;
  functionName: string;
  body: string;
  normalizedHash: string;
  truncated: boolean;
}

export type Decompiler = (req: DecompileRequest) => Promise<DecompileResult>;

const DECOMPILE_MARKER = /^### DECOMPILE (.+) @ 0x([0-9a-fA-F]+) \(RVA 0x([0-9a-fA-F]+)\)$/;
// Ghidra resumes its own logging after the script finishes; these prefixes end the C body.
const LOG_LINE = /^(INFO|WARN|ERROR|DEBUG|Exception|\tat )\b/;

// Headless does not pass a script's println through untouched. Every line comes back wrapped as
//   "INFO  DecompileAt.java> <the actual line> (GhidraScript)  "
// so the wrapper has to come off before the C body can be recognized at all.
const SCRIPT_LOG_LINE = /^INFO\s+\S+>\s?(.*?)\s*\(GhidraScript\)\s*$/;

export function stripGhidraScriptLog(line: string): string | null {
  const match = line.match(SCRIPT_LOG_LINE);
  return match ? match[1] : null;
}

export function parseDecompileOutput(
  stdout: string
): { entryRva: number; functionName: string; body: string } | null {
  // Normalize to (text, isFrameworkNoise) so real headless output and bare script output both work.
  const lines = stdout.split("\n").map((raw) => {
    const unwrapped = stripGhidraScriptLog(raw);
    if (unwrapped !== null) return { text: unwrapped, noise: false };
    return { text: raw, noise: LOG_LINE.test(raw) };
  });

  const start = lines.findIndex((line) => DECOMPILE_MARKER.test(line.text.trim()));
  if (start === -1) return null;

  const header = lines[start].text.trim().match(DECOMPILE_MARKER)!;
  const body: string[] = [];
  for (let i = start + 1; i < lines.length; i++) {
    const line = lines[i];
    if (line.text.trimStart().startsWith("###")) break;
    if (line.noise) break;
    body.push(line.text);
  }

  return {
    functionName: header[1],
    entryRva: parseInt(header[3], 16),
    body: body.join("\n").trim(),
  };
}

// Ghidra validates every element of a project path and rejects several characters outright
// ("Path element contains invalid character: '&'"). This workspace lives under
// "Tools & Reverse Engineering", so the real path can never be passed directly. Route it through
// a stable symlink in the OS temp dir instead of asking anyone to move the workspace.
const GHIDRA_UNSAFE_PATH_CHARS = /[&:*?"<>|]/;

export function sanitizeGhidraPath(realPath: string): string {
  if (!GHIDRA_UNSAFE_PATH_CHARS.test(realPath)) return realPath;

  const linkDir = join(tmpdir(), "f4-re-mcp-paths");
  const link = join(linkDir, createHash("sha1").update(realPath).digest("hex").slice(0, 16));

  try {
    if (readlinkSync(link) === realPath) return link;
    unlinkSync(link);
  } catch {
    // No usable link yet; fall through and create one.
  }

  try {
    mkdirSync(linkDir, { recursive: true });
    symlinkSync(realPath, link);
  } catch (err) {
    const code = (err as NodeJS.ErrnoException).code;
    if (code !== "EEXIST") {
      throw new GhidraError(
        "spawn_failed",
        `Ghidra cannot accept the path "${realPath}" (it contains a character Ghidra rejects), ` +
          `and the symlink workaround at ${link} could not be created: ${(err as Error).message}`
      );
    }
  }
  return link;
}

export function normalizeDecompile(body: string): string {
  return body
    .replace(/\r\n/g, "\n")
    .split("\n")
    .map((line) => line.replace(/\s+$/, ""))
    .join("\n")
    .trim();
}

export function hashDecompile(normalized: string): string {
  return createHash("sha256").update(normalized, "utf8").digest("hex");
}

export function classifyGhidraFailure(stderr: string, stdout: string): GhidraFailureKind {
  const combined = `${stderr}\n${stdout}`.toLowerCase();
  if (combined.includes("unable to lock") || combined.includes("locked")) return "locked";
  return "script_error";
}

interface ChildOutcome {
  stdout: string;
  stderr: string;
  code: number | null;
  timedOut: boolean;
  truncated: boolean;
}

function runHeadless(
  executable: string,
  args: string[],
  timeoutMs: number,
  maxStdoutBytes: number,
  maxStderrBytes: number
): Promise<ChildOutcome> {
  return new Promise((resolve, reject) => {
    // detached puts the child in its own process group so the timeout can kill analyzeHeadless
    // *and* the JVM it spawns, rather than orphaning the JVM.
    const child = spawn(executable, args, { detached: true, stdio: ["ignore", "pipe", "pipe"] });

    let stdout = "";
    let stderr = "";
    let truncated = false;
    let timedOut = false;
    let settled = false;

    const killTree = () => {
      if (child.pid === undefined) return;
      try {
        process.kill(-child.pid, "SIGKILL");
      } catch {
        try {
          child.kill("SIGKILL");
        } catch {
          // already gone
        }
      }
    };

    const timer = setTimeout(() => {
      timedOut = true;
      killTree();
    }, timeoutMs);

    child.stdout.on("data", (chunk: Buffer) => {
      if (stdout.length >= maxStdoutBytes) {
        truncated = true;
        return;
      }
      stdout += chunk.toString("utf8");
      if (stdout.length > maxStdoutBytes) {
        stdout = stdout.slice(0, maxStdoutBytes);
        truncated = true;
      }
    });

    child.stderr.on("data", (chunk: Buffer) => {
      if (stderr.length >= maxStderrBytes) return;
      stderr = (stderr + chunk.toString("utf8")).slice(0, maxStderrBytes);
    });

    child.on("error", (err) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      reject(new GhidraError("spawn_failed", `Could not run ${executable}: ${err.message}`));
    });

    child.on("close", (code) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      resolve({ stdout, stderr, code, timedOut, truncated });
    });
  });
}

export function createGhidraDecompiler(opts: {
  config: LearningConfig;
  analyzeHeadlessPath?: string;
  scriptsDir?: string;
}): Decompiler {
  const executable = opts.analyzeHeadlessPath ?? ANALYZE_HEADLESS;
  const scriptsDir = opts.scriptsDir ?? SCRIPTS_DIR;

  return async function decompile(req: DecompileRequest): Promise<DecompileResult> {
    const project = opts.config.ghidraProjects[req.version];
    if (!project) {
      throw new GhidraError(
        "unknown_version",
        `No Ghidra project is configured for version ${req.version}.`
      );
    }

    // argv array only. Nothing here is ever concatenated into a shell command.
    const args = [
      sanitizeGhidraPath(project.projectDir),
      project.projectName,
      // Named, never bare: a bare -process runs every program in the project and the parser then
      // takes whichever produced the first marker.
      "-process",
      project.programName,
      "-noanalysis",
      "-readOnly",
      "-scriptPath",
      sanitizeGhidraPath(scriptsDir),
      "-postScript",
      "DecompileAt.java",
      req.rva.toString(16),
    ];

    const outcome = await runHeadless(
      executable,
      args,
      opts.config.ghidraTimeoutMs,
      opts.config.maxDecompileBytes,
      opts.config.maxChildStderrBytes
    );

    if (outcome.timedOut) {
      throw new GhidraError(
        "timeout",
        `Ghidra exceeded ${opts.config.ghidraTimeoutMs} ms for RVA 0x${req.rva.toString(16)}; ` +
          `the process tree was killed.`
      );
    }

    const parsed = parseDecompileOutput(outcome.stdout);
    if (!parsed) {
      if (outcome.stdout.includes("no function here")) {
        throw new GhidraError(
          "no_function",
          `No function contains RVA 0x${req.rva.toString(16)} in ${project.projectName}.`
        );
      }
      throw new GhidraError(
        classifyGhidraFailure(outcome.stderr, outcome.stdout),
        `Ghidra produced no decompile output (exit ${outcome.code}). ` +
          `stderr: ${outcome.stderr.slice(0, 500)}`
      );
    }

    const normalized = normalizeDecompile(parsed.body);
    return {
      version: req.version,
      rva: req.rva,
      entryRva: parsed.entryRva,
      functionName: parsed.functionName,
      body: parsed.body,
      normalizedHash: hashDecompile(normalized),
      truncated: outcome.truncated,
    };
  };
}
