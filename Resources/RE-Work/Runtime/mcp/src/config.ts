import { delimiter, join } from "node:path";
import { DEFAULT_WORKSPACE_ROOTS, PROJECTS_DIR } from "./paths.js";

export interface GhidraProject {
  projectDir: string;
  projectName: string;
  /**
   * The program inside the project to decompile. Required, not optional: a Ghidra project can
   * hold several programs, and headless with a bare "-process" runs every one of them, leaving
   * the parser to take whichever Ghidra happened to list first. Naming it is the only thing that
   * makes "which binary did this decompile come from" answerable.
   */
  programName: string;
}

// CommonLibF4 fills COMMONLIB_RUNTIMECOUNT slots per REL::ID, indexed by REX::FModule::Runtime.
// The order is the enum's, not ours: changing it silently re-points every multi-runtime literal.
export const RUNTIME_ORDER = ["kOG", "kNG", "kAE"] as const;
export type Runtime = (typeof RUNTIME_ORDER)[number];

// Which concrete build each runtime slot resolves against. The boundaries are REX::FModule's own
// (src/REX/FModule.cpp): kOG is exactly 1.10.163, kNG is (1.10.163, 1.10.984], kAE is > 1.10.984.
// A slot needs a build whose Address Library is indexed, so each maps to the newest such build.
export const RUNTIME_VERSIONS: Record<Runtime, string> = {
  kOG: "1.10.163",
  kNG: "1.10.984",
  kAE: "1.11.221",
};

export interface LearningConfig {
  defaultVersion: string;
  versions: string[];
  runtimeVersions: Record<Runtime, string>;
  ghidraProjects: Record<string, GhidraProject>;
  ollamaBaseUrl: string;
  ollamaModel: string;
  promptVersion: string;
  extractorVersion: string;
  ghidraTimeoutMs: number;
  ollamaTimeoutMs: number;
  maxDecompileBytes: number;
  maxModelResponseBytes: number;
  maxChildStderrBytes: number;
  workspaceRoots: string[];
  discoveryMaxDepth: number;
  repoDiscoveryTtlMs: number;
}

// Bump when the extraction prompt text changes; it is part of the dedup identity, so bumping it
// makes every target eligible for re-extraction.
const PROMPT_VERSION = "1";
// Bump when the parsing/validation of model output changes shape.
const EXTRACTOR_VERSION = "1";

function num(env: NodeJS.ProcessEnv, key: string, fallback: number): number {
  const raw = env[key];
  if (raw === undefined || raw === "") return fallback;
  const value = Number(raw);
  if (!Number.isFinite(value) || value <= 0) {
    throw new Error(`${key} must be a positive number, got "${raw}"`);
  }
  return value;
}

export function loadConfig(env: NodeJS.ProcessEnv = process.env): LearningConfig {
  const roots = env.RE_MCP_WORKSPACE_ROOTS
    ? env.RE_MCP_WORKSPACE_ROOTS.split(delimiter).filter(Boolean)
    : DEFAULT_WORKSPACE_ROOTS;

  return {
    // The name authority, not "the version". Curated names exist only for 1.10.163; an AE address
    // is named by bridging back to this build through og_ae_map, never by assuming AE ids are OG.
    defaultVersion: "1.10.163",
    // Versions that can be decompiled. Adding one here without adding its Ghidra project below
    // would make targets in that version unresolvable rather than silently wrong.
    versions: ["1.10.163", "1.11.221"],
    runtimeVersions: RUNTIME_VERSIONS,
    ghidraProjects: {
      "1.10.163": {
        projectDir: join(PROJECTS_DIR, "fwgh_proj_1.10.163_unpacked"),
        projectName: "Fallout4_163_unpacked",
        programName: "Fallout4.exe.unpacked.exe",
      },
      // Two programs live in this project: the 1.11.221 target and "Fallout4.exe", the 1.11.191
      // AutoVT source import. Ghidra lists that one first, so an unnamed program silently
      // decompiled 191 code and recorded it as 1.11.221 — verified 2026-08-14 against
      // NavMesh::CreateGroupData, which is 0x77f7a0 in 191 and 0x77f7d0 here.
      "1.11.221": {
        projectDir: join(PROJECTS_DIR, "fwgh_proj_1.11.221_unpacked"),
        projectName: "Fallout4_1_11_221_unpacked",
        programName: "Fallout4.exe.unpacked.exe",
      },
    },
    ollamaBaseUrl: env.RE_MCP_OLLAMA_URL || "http://localhost:11434",
    ollamaModel: env.RE_MCP_OLLAMA_MODEL || "qwen2.5-coder:7b",
    promptVersion: PROMPT_VERSION,
    extractorVersion: EXTRACTOR_VERSION,
    // A cold analyzeHeadless start on this project is tens of seconds; 6 min is generous but finite.
    ghidraTimeoutMs: num(env, "RE_MCP_GHIDRA_TIMEOUT_MS", 360_000),
    // A measured qwen2.5-coder:7b structured response took ~5 s; 120 s covers a cold model load.
    ollamaTimeoutMs: num(env, "RE_MCP_OLLAMA_TIMEOUT_MS", 120_000),
    maxDecompileBytes: num(env, "RE_MCP_MAX_DECOMPILE_BYTES", 64_000),
    maxModelResponseBytes: num(env, "RE_MCP_MAX_MODEL_RESPONSE_BYTES", 16_000),
    maxChildStderrBytes: num(env, "RE_MCP_MAX_CHILD_STDERR_BYTES", 32_000),
    workspaceRoots: roots,
    discoveryMaxDepth: num(env, "RE_MCP_DISCOVERY_MAX_DEPTH", 4),
    repoDiscoveryTtlMs: num(env, "RE_MCP_DISCOVERY_TTL_MS", 24 * 60 * 60 * 1000),
  };
}
