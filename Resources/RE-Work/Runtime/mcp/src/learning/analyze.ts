import type Database from "better-sqlite3";
import { createHash } from "node:crypto";
import { readdirSync, statSync } from "node:fs";
import { join } from "node:path";
import type { Clock } from "../clock.js";
import { systemClock } from "../clock.js";
import { loadConfig, type LearningConfig } from "../config.js";
import { getDb, parseAddressToRva } from "../db.js";
import {
  GhidraError,
  createGhidraDecompiler,
  type Decompiler,
  type GhidraFailureKind,
} from "../adapters/ghidra.js";
import {
  OllamaError,
  createOllamaExtractor,
  type Extractor,
  type OllamaFailureKind,
} from "../adapters/ollama.js";
import { openLearningDb } from "./db.js";
import { getCurrentFinding, recordFinding, type LearnedFinding, type Origin } from "./findings.js";

export type ConfidenceVerdict =
  | "confirmed_correction"
  | "reverted_correction"
  | "unresolved_disagreement"
  | "no_audit_trail";

export interface StaticContext {
  staticName: string | null;
  signature: string | null;
  addressLibraryId: number | null;
  confidenceVerdict: ConfidenceVerdict;
  auditNote: string | null;
}

export interface Contradiction {
  staticName: string;
  proposedName: string;
  staticIsAudited: boolean;
}

// Phase 2's retry classifier switches on this union. Adding members is fine; removing is not.
export type AnalyzeFailureKind =
  | GhidraFailureKind
  | OllamaFailureKind
  | "unresolved_target"
  | "wal_unsafe";

// Transient conditions worth another cycle; everything else needs a human or a code change.
const RETRYABLE: ReadonlySet<AnalyzeFailureKind> = new Set<AnalyzeFailureKind>([
  "locked",
  "timeout",
  "spawn_failed",
  "unavailable",
  "http_error",
]);

export class AnalyzeError extends Error {
  readonly kind: AnalyzeFailureKind;
  readonly retryable: boolean;
  constructor(kind: AnalyzeFailureKind, message: string) {
    super(message);
    this.name = "AnalyzeError";
    this.kind = kind;
    this.retryable = RETRYABLE.has(kind);
  }
}

export interface AnalyzeDeps {
  staticDb: Database.Database;
  learningDb: Database.Database;
  clock: Clock;
  config: LearningConfig;
  decompile: Decompiler;
  extract: Extractor;
  projectFingerprint(version: string): string | null;
}

export interface AnalyzeTargetOptions {
  target: string;
  version?: string;
  origin: Origin;
  force?: boolean;
}

export interface AnalyzeOutcome {
  status: "analyzed" | "deduplicated";
  version: string;
  rva: number;
  rvaHex: string;
  resolvedName: string;
  finding: LearnedFinding;
  staticContext: StaticContext;
  contradiction: Contradiction | null;
}

const HEX_TARGET = /^(0x)?[0-9a-fA-F]+$/;

export function resolveTarget(
  staticDb: Database.Database,
  target: string,
  version: string
): { rva: number; name: string | null } {
  const trimmed = target.trim();
  if (!trimmed) {
    throw new AnalyzeError("unresolved_target", "Target is empty.");
  }

  if (HEX_TARGET.test(trimmed)) {
    const rva = parseAddressToRva(trimmed);
    if (rva === null) {
      throw new AnalyzeError("unresolved_target", `Could not parse "${target}" as an address.`);
    }
    const row = staticDb
      .prepare("SELECT name FROM symbols WHERE version = ? AND rva = ? LIMIT 1")
      .get(version, rva) as { name: string } | undefined;
    return { rva, name: row?.name ?? null };
  }

  const exact = staticDb
    .prepare("SELECT rva, name FROM symbols WHERE version = ? AND name = ?")
    .all(version, trimmed) as { rva: number; name: string }[];
  const candidates =
    exact.length > 0
      ? exact
      : (staticDb
          .prepare("SELECT rva, name FROM symbols WHERE version = ? AND name LIKE ? LIMIT 11")
          .all(version, `%${trimmed}%`) as { rva: number; name: string }[]);

  if (candidates.length === 0) {
    throw new AnalyzeError(
      "unresolved_target",
      `No symbol matches "${target}" in version ${version}.`
    );
  }
  const distinct = new Set(candidates.map((c) => c.rva));
  if (distinct.size > 1) {
    const shown = candidates
      .slice(0, 10)
      .map((c) => `${c.name} @ 0x${c.rva.toString(16)}`)
      .join(", ");
    throw new AnalyzeError(
      "unresolved_target",
      `"${target}" is ambiguous — ${distinct.size} matches: ${shown}. Pass an address instead.`
    );
  }
  return { rva: candidates[0].rva, name: candidates[0].name };
}

export function getStaticContext(
  staticDb: Database.Database,
  version: string,
  rva: number
): StaticContext {
  const symbol = staticDb
    .prepare("SELECT name, signature FROM symbols WHERE version = ? AND rva = ? LIMIT 1")
    .get(version, rva) as { name: string; signature: string } | undefined;

  const idRow = staticDb
    .prepare("SELECT id FROM address_library WHERE version = ? AND rva = ? LIMIT 1")
    .get(version, rva) as { id: number } | undefined;

  const revert = staticDb
    .prepare("SELECT revert_to FROM misname_reverts WHERE rva = ? LIMIT 1")
    .get(rva) as { revert_to: string | null } | undefined;
  const correction = staticDb
    .prepare("SELECT new_name, note FROM misname_corrections WHERE rva = ? LIMIT 1")
    .get(rva) as { new_name: string | null; note: string | null } | undefined;
  const disagreement = staticDb
    .prepare("SELECT note, verdict_155 FROM name_disagreements WHERE rva = ? LIMIT 1")
    .get(rva) as { note: string | null; verdict_155: string | null } | undefined;

  // Same precedence as check_name_confidence: revert first, then correction, then disagreement.
  // A revert means an applied correction was later withdrawn, so the ORIGINAL name is the
  // confirmed-correct one — reading the leftover correction row as authoritative inverts that.
  let confidenceVerdict: ConfidenceVerdict = "no_audit_trail";
  let auditNote: string | null = null;
  if (revert) {
    confidenceVerdict = "reverted_correction";
    auditNote = `A correction here was reverted; the confirmed name is ${revert.revert_to}`;
  } else if (correction) {
    confidenceVerdict = "confirmed_correction";
    auditNote = correction.note;
  } else if (disagreement) {
    confidenceVerdict = "unresolved_disagreement";
    auditNote = [disagreement.note, disagreement.verdict_155].filter(Boolean).join(" / ") || null;
  }

  return {
    staticName: symbol?.name ?? null,
    signature: symbol?.signature ?? null,
    addressLibraryId: idRow?.id ?? null,
    confidenceVerdict,
    auditNote,
  };
}

export function detectContradiction(
  context: StaticContext,
  finding: { proposedName: string | null }
): Contradiction | null {
  if (!context.staticName || !finding.proposedName) return null;
  if (context.staticName === finding.proposedName) return null;
  return {
    staticName: context.staticName,
    proposedName: finding.proposedName,
    staticIsAudited:
      context.confidenceVerdict === "confirmed_correction" ||
      context.confidenceVerdict === "reverted_correction",
  };
}

// A runaway guard, not a real bound: a .rep tree is one directory per program plus its database
// files, which is dozens of entries. Blowing past this means the path is not a Ghidra project.
const MAX_FINGERPRINT_ENTRIES = 10_000;

// Ghidra rotates its directory indexes on every project open, .dat to .bak and a fresh .dat, at
// constant size — measured on 1.10.163, two read-only decompiles in a row moved all four mtimes
// while db.1012.gbf sat untouched. Including them would make the fingerprint self-invalidating:
// it is computed just before the decompile that rotates them, so the next call could never match.
// The cost is that a program renamed inside the project tree, which lives only in these indexes,
// reads as no change — that moves no function name or address, so no finding goes stale from it.
const INDEX_FILE = /^~index\.(dat|bak)$/;

/**
 * Identity of the Ghidra project's *contents*, so a re-analysis or renaming pass invalidates
 * findings that were extracted from the older state.
 *
 * The .gpr is a zero-byte marker whose size and mtime never move when a function is renamed —
 * fingerprinting it made the identity a constant, and every stale finding looked current forever.
 * The program database lives in the sibling .rep tree, so that is what gets walked. Metadata only:
 * db.<n>.gbf is gigabytes, and Ghidra cannot save without moving a size, an mtime, or a filename.
 *
 * Returns null if the tree cannot be read in full. That direction is deliberate — an unknown
 * identity forces re-extraction rather than silently confirming a stale finding.
 */
export function ghidraProjectFingerprint(config: LearningConfig, version: string): string | null {
  const project = config.ghidraProjects[version];
  if (!project) return null;

  const hash = createHash("sha256");
  let entries = 0;

  const walk = (dir: string, prefix: string): boolean => {
    let dirents;
    try {
      dirents = readdirSync(dir, { withFileTypes: true });
    } catch {
      return false;
    }
    // readdir order is filesystem-dependent; sort so the hash is a property of the tree itself.
    dirents.sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));

    for (const dirent of dirents) {
      if (INDEX_FILE.test(dirent.name)) continue;
      if (++entries > MAX_FINGERPRINT_ENTRIES) return false;
      const relative = prefix + dirent.name;

      if (dirent.isDirectory()) {
        hash.update(`d ${relative}\n`);
        if (!walk(join(dir, dirent.name), `${relative}/`)) return false;
      } else if (dirent.isFile()) {
        let stat;
        try {
          stat = statSync(join(dir, dirent.name));
        } catch {
          return false;
        }
        hash.update(`f ${relative} ${stat.size} ${Math.trunc(stat.mtimeMs)}\n`);
      } else {
        // Symlinks and sockets have no business in a .rep tree, but their appearance or
        // disappearance is still a change worth recording.
        hash.update(`? ${relative}\n`);
      }
    }
    return true;
  };

  if (!walk(join(project.projectDir, `${project.projectName}.rep`), "")) return null;
  return `${project.projectName}:${hash.digest("hex")}`;
}

export function createDefaultDeps(overrides: Partial<AnalyzeDeps> = {}): AnalyzeDeps {
  const config = overrides.config ?? loadConfig();
  return {
    staticDb: overrides.staticDb ?? getDb(),
    learningDb: overrides.learningDb ?? openLearningDb(),
    clock: overrides.clock ?? systemClock,
    config,
    decompile: overrides.decompile ?? createGhidraDecompiler({ config }),
    extract: overrides.extract ?? createOllamaExtractor({ config }),
    projectFingerprint:
      overrides.projectFingerprint ?? ((version) => ghidraProjectFingerprint(config, version)),
  };
}

export async function analyzeTarget(
  deps: AnalyzeDeps,
  opts: AnalyzeTargetOptions
): Promise<AnalyzeOutcome> {
  const version = opts.version ?? deps.config.defaultVersion;
  if (!deps.config.versions.includes(version)) {
    throw new AnalyzeError(
      "unresolved_target",
      `Version ${version} is not configured for learning. Configured: ${deps.config.versions.join(", ")}.`
    );
  }

  const resolved = resolveTarget(deps.staticDb, opts.target, version);
  const staticContext = getStaticContext(deps.staticDb, version, resolved.rva);
  const fingerprint = deps.projectFingerprint(version);

  let decompiled;
  try {
    decompiled = await deps.decompile({ version, rva: resolved.rva });
  } catch (err) {
    if (err instanceof GhidraError) throw new AnalyzeError(err.kind, err.message);
    throw err;
  }

  // Short-circuit before Ollama: an unchanged identity means there is nothing new to extract.
  const existing = getCurrentFinding(deps.learningDb, version, resolved.rva);
  if (
    existing &&
    !opts.force &&
    existing.rawDecompileHash === decompiled.normalizedHash &&
    existing.model === deps.config.ollamaModel &&
    existing.promptVersion === deps.config.promptVersion &&
    existing.extractorVersion === deps.config.extractorVersion &&
    existing.projectFingerprint === fingerprint
  ) {
    return {
      status: "deduplicated",
      version,
      rva: resolved.rva,
      rvaHex: `0x${resolved.rva.toString(16)}`,
      resolvedName: decompiled.functionName,
      finding: existing,
      staticContext,
      contradiction: detectContradiction(staticContext, existing),
    };
  }

  let extracted;
  try {
    extracted = await deps.extract({
      functionName: decompiled.functionName,
      version,
      rva: resolved.rva,
      decompiled: decompiled.body,
      staticName: staticContext.staticName,
      staticNote: staticContext.auditNote,
    });
  } catch (err) {
    if (err instanceof OllamaError) throw new AnalyzeError(err.kind, err.message);
    throw err;
  }

  const { finding, deduplicated } = recordFinding(deps.learningDb, deps.clock, {
    version,
    rva: resolved.rva,
    rawDecompileHash: decompiled.normalizedHash,
    model: deps.config.ollamaModel,
    promptVersion: deps.config.promptVersion,
    extractorVersion: deps.config.extractorVersion,
    projectFingerprint: fingerprint,
    proposedName: extracted.proposedName,
    confidence: extracted.confidence,
    note: extracted.note,
    origin: opts.origin,
    ghidraVersion: null,
    force: opts.force,
  });

  return {
    status: deduplicated ? "deduplicated" : "analyzed",
    version,
    rva: resolved.rva,
    rvaHex: `0x${resolved.rva.toString(16)}`,
    resolvedName: decompiled.functionName,
    finding,
    staticContext,
    contradiction: detectContradiction(staticContext, finding),
  };
}
