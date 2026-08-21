#!/usr/bin/env node
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { checkNameConfidence } from "./tools/checkNameConfidence.js";
import { pdbSignature } from "./tools/pdbSignature.js";
import { getVtableSlots } from "./tools/getVtableSlots.js";
import { lookupByAddress } from "./tools/lookupByAddress.js";
import { lookupByName } from "./tools/lookupByName.js";
import { mapOgAe } from "./tools/mapOgAe.js";
import { searchFindings } from "./tools/searchFindings.js";
import { havokClass, searchHavokSource } from "./tools/havokSource.js";
import { learnFromTarget } from "./tools/learnFromTarget.js";
import { auditCsvCorpus } from "./tools/auditCsvCorpus.js";
import { auditHardcodedAddresses } from "./tools/auditHardcodedAddresses.js";
import { suggestHooks } from "./tools/suggestHooks.js";
import { recentLearnings } from "./tools/recentLearnings.js";
import { exportLearnings } from "./tools/exportLearnings.js";
import { searchOrganized } from "./tools/searchOrganized.js";

const server = new McpServer({
  name: "f4-re-mcp",
  version: "1.0.0",
});

function textResult(text: string) {
  return { content: [{ type: "text" as const, text }] };
}

function errorResult(message: string) {
  return { content: [{ type: "text" as const, text: message }], isError: true };
}

function run(fn: () => unknown) {
  try {
    return textResult(JSON.stringify(fn(), null, 2));
  } catch (err) {
    return errorResult(err instanceof Error ? err.message : String(err));
  }
}

async function runAsync(fn: () => Promise<unknown>) {
  try {
    return textResult(JSON.stringify(await fn(), null, 2));
  } catch (err) {
    return errorResult(err instanceof Error ? err.message : String(err));
  }
}

server.tool(
  "lookup_by_address",
  "Resolve a Fallout 4 engine address (hex, VA or RVA, with or without 0x) to its symbol name, " +
    "signature, and Address Library id for a given runtime version (default 1.10.163 OG).",
  {
    address: z.string().describe('Hex address, e.g. "0x140724fa0" or "724fa0".'),
    version: z.string().optional().describe('Runtime version, default "1.10.163".'),
  },
  async ({ address, version }) => run(() => lookupByAddress(address, version))
);

server.tool(
  "lookup_by_name",
  "Find symbols by name (substring match) for a given runtime version (default 1.10.163 OG). " +
    "Use this when you know the class/method name but not the address.",
  {
    name: z.string().describe('Name or substring, e.g. "BSGeometry" or "Actor::Update".'),
    version: z.string().optional().describe('Runtime version, default "1.10.163".'),
  },
  async ({ name, version }) => run(() => lookupByName(name, version))
);

server.tool(
  "check_name_confidence",
  "Check whether a symbol name is trustworthy before hooking it. Cross-references the confirmed " +
    "misname corrections, reverted-correction, and unresolved-disagreement audit trails built up " +
    "from Ghidra label-recovery and decompile verification, plus independent corroboration " +
    "against the 1.10.155 Microsoft PDB where available. Pass either address or name.",
  {
    address: z.string().optional().describe("Hex address to check."),
    name: z.string().optional().describe("Symbol name to check (substring match)."),
  },
  async ({ address, name }) => run(() => checkNameConfidence({ address, name }))
);

server.tool(
  "pdb_signature",
  "Look up a name in the 1.10.155 private Microsoft PDB dump for its real signature, calling " +
    "convention, and size — an independent ground truth for what a function is actually called, " +
    "from neither CommonLibF4 RTTI nor the community labels. The PDB's own addresses are for a " +
    "different game version and are never returned or usable as hook offsets.",
  {
    name: z.string().describe('Name or substring, e.g. "AddShared" or "BSCullingProcess".'),
    limit: z.number().optional().describe("Max matches, default 25, capped at 50."),
  },
  async ({ name, limit }) => run(() => pdbSignature({ name, limit }))
);

server.tool(
  "get_vtable_slots",
  "Get CommonLibF4's expected vtable layout for a class, joined with the live Ghidra audit " +
    "verdict per slot (match / MISMATCH / not audited) where available.",
  { className: z.string().describe('Exact class name, e.g. "ActorPackageData".') },
  async ({ className }) => run(() => getVtableSlots(className))
);

server.tool(
  "map_og_ae",
  "Map a function between Old-Gen (1.10.163) and AE (1.11.221) address spaces, by name or by " +
    "address on either side. Many entries are partial (only one side known). The AE address is " +
    "resolved through the Address Library id, not read from the bridge's own AE column, which " +
    "holds 1.11.191 addresses.",
  { nameOrAddress: z.string().describe("Function name (substring) or hex address on either side.") },
  async ({ nameOrAddress }) => run(() => mapOgAe(nameOrAddress))
);

server.tool(
  "search_findings",
  "Keyword search over the RE findings docs (struct offsets, vtable notes, bug write-ups) that " +
    "only exist as prose, not structured data - e.g. searching a class name or field description.",
  { query: z.string().describe("Search term or short phrase.") },
  async ({ query }) => run(() => searchFindings(query))
);

server.tool(
  "havok_class",
  "Read a Havok class declaration from the 2014.1.1 SDK source, with its members and the notes " +
    "attached to them. Use this to find out what an hkb/hka/hkp field actually means, what an enum " +
    "value does, or which fields disable each other. Bethesda's BS* classes are not in it. " +
    "Reference only: field offsets come from HavokClassTypes.json or the game binary, and Havok's " +
    "wording must never be copied into a repository.",
  {
    name: z.string().describe('Exact class name, e.g. "hkbClipGenerator" or "hkaSkeleton".'),
  },
  async ({ name }) => run(() => havokClass(name))
);

server.tool(
  "search_havok_source",
  "Keyword search across the Havok 2014.1.1 SDK source for the questions a class name does not " +
    "answer: what a flag bit means, which classes mention a field, how a behaviour is described. " +
    "Same reference-only rule as havok_class.",
  { query: z.string().describe('Search term or short phrase, e.g. "extracted motion" or "FLAG_SYNC".') },
  async ({ query }) => run(() => searchHavokSource(query))
);

server.tool(
  "learn_from_target",
  "Decompile one Fallout 4 engine function with Ghidra headless and extract a proposed name, " +
    "confidence, and note with the local Ollama model, then store it as a durable learned finding. " +
    "Learned findings never overwrite the static index; a disagreement is reported as a " +
    "contradiction. Repeat calls on unchanged code are deduplicated rather than re-analyzed.",
  {
    target: z.string().describe('Symbol name or hex address, e.g. "Actor::SetFlag" or "0x749ea0".'),
    version: z.string().optional().describe('Runtime version, default "1.10.163".'),
    force: z
      .boolean()
      .optional()
      .describe("Re-run extraction even if nothing about the target changed."),
  },
  async ({ target, version, force }) => runAsync(() => learnFromTarget({ target, version, force }))
);

server.tool(
  "audit_hardcoded_addresses",
  "Audit hardcoded Fallout 4 engine addresses across the workspace's consumer repos. Every " +
    "REL::ID / REL::VariantID / REL::Offset fills all three CommonLibF4 runtime slots (kOG " +
    "1.10.163, kNG 1.10.984, kAE 1.11.221), so each is resolved against its own Address Library " +
    "and named — AE addresses through og_ae_map back to the curated 1.10.163 names. Flags the " +
    "case that used to pass silently: one id handed to every runtime when the file's own id " +
    "population shows it was authored against only one. Repo scans are cached by git fingerprint.",
  {
    paths: z
      .array(z.string())
      .optional()
      .describe("Limit the audit to these directories. Omit to audit all discovered git repos."),
    force_rescan: z.boolean().optional().describe("Rescan even if repo fingerprints are unchanged."),
    verdicts: z
      .array(
        z.enum([
          "ok",
          "single_id_all_runtimes",
          "slot_unresolved",
          "slot_name_mismatch",
          "unbridged",
          "declared_unavailable",
          "unresolvable_symbol",
          "stale_scan",
        ])
      )
      .optional()
      .describe("Return only these verdicts. Omit for everything except ok."),
    include_ok: z
      .boolean()
      .optional()
      .describe("Include clean references in the listing. Totals always count them."),
    detail_limit: z
      .number()
      .int()
      .positive()
      .optional()
      .describe("Cap on per-reference rows. Read `groups` first — it is the report."),
  },
  async ({ paths, force_rescan, verdicts, include_ok, detail_limit }) =>
    runAsync(() =>
      auditHardcodedAddresses({ paths, force_rescan, verdicts, include_ok, detail_limit })
    )
);

server.tool(
  "suggest_hooks",
  "For one subsystem and one repo, list the engine functions that repo does not yet reference. " +
    "Scoped to a single repo on purpose: a function hooked in another project must not stop being " +
    'suggested here. Subsystem names are the Organized category names, e.g. "AI_Combat", ' +
    '"Pathing_Navmesh", "Quests".',
  {
    subsystem: z.string().describe('Category name, e.g. "AI_Combat".'),
    repo: z.string().describe("Repo path or basename. Required — suggestions are per-project."),
    version: z.string().optional().describe('Runtime version, default "1.10.163".'),
  },
  async ({ subsystem, repo, version }) => runAsync(() => suggestHooks({ subsystem, repo, version }))
);

server.tool(
  "search_organized",
  "Search across all 24 Organized/*.csv files at once (the full human audit-trail corpus, not " +
    "just the five files with dedicated tools). Pass a name substring, or a hex address to find " +
    "every row that mentions it. Results cite source:row_no so a claim can be traced back to its file.",
  {
    query: z.string().describe('Name substring or hex address, e.g. "SetFlag" or "0x749ea0".'),
    source: z.string().optional().describe('Limit to one CSV, e.g. "applied_safe_gaps".'),
    limit: z.number().optional().describe("Max rows, default 50, capped at 200."),
  },
  async ({ query, source, limit }) => run(() => searchOrganized({ query, source, limit }))
);

server.tool(
  "audit_csv_corpus",
  "Re-read all 24 Organized/*.csv files from disk right now and cross-check their claims against " +
    "the live index: does an applied rename still hold (claim_drift), does the address even exist " +
    "(unresolved_address), do a plan file and its record file agree (pair_disagreement), and does " +
    "a claimed real name show up in the independent 1.10.155 PDB (pdb_uncorroborated, info-only). " +
    "Pass write:true to also write Organized/generated/csv_audit_report.csv/.md and a PDB " +
    "name-crosscheck CSV.",
  {
    source: z.string().optional().describe('Limit to one CSV, e.g. "applied_safe_gaps". Omit to audit all 24.'),
    write: z.boolean().optional().describe("Write the generated reports under Organized/generated/."),
    limit: z.number().optional().describe("Max findings in the response, default/cap 500. The written report is never truncated."),
  },
  async ({ source, write, limit }) => run(() => auditCsvCorpus({ source, write, limit }))
);

server.tool(
  "recent_learnings",
  "List recent learned findings, newest first. Use origin='daemon' to see specifically what " +
    "unattended analysis found since you last looked, and current_only=false to see superseded " +
    "history rather than only current truth.",
  {
    limit: z.number().optional().describe("Max rows, default 50, capped at 500."),
    since: z.string().optional().describe("UTC ISO-8601 cutoff, e.g. 2026-08-13T00:00:00.000Z."),
    origin: z
      .enum(["interactive", "daemon", "all"])
      .optional()
      .describe('Filter by how the finding was produced. Default "all".'),
    current_only: z
      .boolean()
      .optional()
      .describe("Default true. False includes findings that were later superseded."),
  },
  async ({ limit, since, origin, current_only }) =>
    runAsync(() => recentLearnings({ limit, since, origin, current_only }))
);

server.tool(
  "export_learnings",
  "Export current learned findings from re-learning.sqlite to a CSV in the same address,name " +
    "shape as the human audit-trail CSVs, with the static name and any contradiction attached. " +
    "Makes machine findings reviewable next to human ones; does not promote them into the static " +
    "index.",
  {
    write: z.boolean().optional().describe("Write Organized/generated/learned_findings.csv."),
  },
  async ({ write }) => runAsync(() => exportLearnings({ write }))
);

async function main() {
  const transport = new StdioServerTransport();
  await server.connect(transport);
}

main().catch((err) => {
  console.error("f4-re-mcp failed to start:", err);
  process.exit(1);
});
