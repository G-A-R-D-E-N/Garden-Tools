import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = dirname(fileURLToPath(import.meta.url));

// The .mcpb bundle unpacks into a user-config dir, so a bundle-relative RE_ROOT would miss the
// real workspace's 03-FINDINGS.md / 05-LABEL-RECOVERY.md / etc. RE_MCP_ROOT (set via the bundle's
// user_config) points it at the real ReverseEngineering directory instead.
export const RE_ROOT = process.env.RE_MCP_ROOT || join(__dirname, "..", "..");

// The .mcpb bundle ships without the ~1GB index; RE_MCP_DB_PATH (set via the bundle's
// user_config) points it at the real file instead of whatever folder it got unpacked into.
export const DB_PATH = process.env.RE_MCP_DB_PATH || join(__dirname, "..", "re-index.sqlite");

// Durable learned state. Deliberately a different file from DB_PATH: build-index.ts unlinks and
// recreates DB_PATH on every rebuild, which would destroy learned findings stored beside it.
export const LEARNING_DB_PATH =
  process.env.RE_MCP_LEARNING_DB_PATH || join(__dirname, "..", "re-learning.sqlite");

export const GHIDRA_DIR = join(RE_ROOT, "Ghidra", "ghidra_12.1_PUBLIC");
export const ANALYZE_HEADLESS = join(GHIDRA_DIR, "support", "analyzeHeadless");
export const PROJECTS_DIR = join(RE_ROOT, "Projects");
export const SCRIPTS_DIR = join(RE_ROOT, "Scripts");

// RE_ROOT is <workspace>/Tools & Reverse Engineering/ReverseEngineering, so two levels up is the
// workspace root that holds every consumer repo.
export const DEFAULT_WORKSPACE_ROOTS = [join(RE_ROOT, "..", "..")];

export const BINS_DIR = join(RE_ROOT, "Bins");
export const ORGANIZED_DIR = join(RE_ROOT, "Organized");
export const SYMBOLS_DIR = join(RE_ROOT, "Symbols");

export const IDA_FUNCTIONS_OG_AE_CSV = join(RE_ROOT, "IDA_Functions_OG_AE.csv");
export const FINDINGS_MD = join(RE_ROOT, "03-FINDINGS.md");

// Only 1.10.163 (OG) is indexed - the 1.10.155 PDB dump has a different, harder-to-parse
// grammar and wrong addresses anyway; its name cross-check value is already in verdict_155.
export const SYMBOL_DUMPS = [
  { version: "1.10.163", path: join(SYMBOLS_DIR, "1.10.163", "Fallout4_163_functions.txt") },
];

// The 1.10.155 PDB dump. Names/signatures only — its addresses are for a DIFFERENT game version
// and are never used as 1.10.163 hook offsets. See pdb_symbols' isolation rules in build-index.ts.
export const PDB_DUMP_PATH = join(SYMBOLS_DIR, "Fallout4_functions.txt");

export const ORGANIZED_GENERATED_DIR = join(ORGANIZED_DIR, "generated");

// Havok's own class declarations for the version the game is built on. Fallout 4 ships
// hk_2014.1.0-r1 and this is 2014.1.1, one point release later; no class layout patch set exists
// for 2014, the newest being 2013_3, so field meanings carry over. Field *offsets* still come from
// HavokClassTypes.json and the game binary, never from here.
//
// Reference only. The material is Havok's and is marked confidential, so nothing from it is copied
// into any repository. Read it to check an understanding, then write the conclusion in your own
// words from what a file can be shown to do.
export const HAVOK_DIR = join(RE_ROOT, "Havok");
export const HAVOK_SDK_DIR = join(HAVOK_DIR, "SDK-2014.1.1");

// Where the declarations live, most specific first, so a class search stops at the behaviour tree
// rather than dragging in every physics header that mentions the same word.
export const HAVOK_SOURCE_DIRS = [
  join(HAVOK_SDK_DIR, "Source", "Behavior"),
  join(HAVOK_SDK_DIR, "Source", "Animation"),
  join(HAVOK_SDK_DIR, "Source", "Common"),
  join(HAVOK_SDK_DIR, "Tools", "FBXImporter"),
];
