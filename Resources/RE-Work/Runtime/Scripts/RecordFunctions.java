// General-purpose batch decompile-and-record tool: reads a tracked CSV of targets, decompiles
// every one of them in ONE headless run against a SINGLE shared DecompInterface (the old workflow
// of one run_query.ps1 invocation per function paid ~15-30s of JVM+project-load overhead PER
// function -- this pays it once for the whole batch), and writes a persistent, git-tracked
// markdown record. This is the raw evidence log; curated narrative docs (README-*.md) synthesize
// from it, they don't replace it -- rerunning this script never loses a previously recorded
// decompile as long as the CSV still lists the target.
//
// CSV columns (header row required): target,category,note
//   target forms:
//     addr:0xRVA          -- exact RVA from image base (preferred: unambiguous)
//     Some::QualifiedName -- exact function-name match (first hit if the DB has duplicates)
//     search:substring    -- first function whose name contains this substring (case-insensitive;
//                            use only when the exact qualified name isn't known -- e.g. the DB has
//                            an unqualified placeholder name for an otherwise-known method)
//   category: freeform tag (e.g. "Quests", "AI") purely for grouping in the output.
//   note: one-line human context for why this function was recorded (shows in the output header).
//
// Usage: -postScript RecordFunctions.java [csvPath] [outMdPath]
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;

import java.io.BufferedReader;
import java.io.FileReader;
import java.io.PrintWriter;
import java.io.FileWriter;
import java.time.LocalDate;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public class RecordFunctions extends GhidraScript {

    private static String[] splitCsvLine(String line) {
        List<String> fields = new ArrayList<>();
        StringBuilder cur = new StringBuilder();
        boolean inQuotes = false;
        for (int i = 0; i < line.length(); i++) {
            char c = line.charAt(i);
            if (inQuotes) {
                if (c == '"') {
                    if (i + 1 < line.length() && line.charAt(i + 1) == '"') { cur.append('"'); i++; }
                    else inQuotes = false;
                } else cur.append(c);
            } else {
                if (c == '"') inQuotes = true;
                else if (c == ',') { fields.add(cur.toString()); cur.setLength(0); }
                else cur.append(c);
            }
        }
        fields.add(cur.toString());
        return fields.toArray(new String[0]);
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String csvPath = (args.length > 0 && !args[0].isEmpty())
            ? args[0]
            : "E:\\F4SE OG\\ReverseEngineering\\Organized\\decompile_targets.csv";
        String outPath = (args.length > 1 && !args[1].isEmpty())
            ? args[1]
            : "E:\\F4SE OG\\ReverseEngineering\\Organized\\DECOMPILE-RECORDS.md";

        FunctionManager fm = currentProgram.getFunctionManager();
        long imageBase = currentProgram.getImageBase().getOffset();

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        // category -> list of [target, note, resultBlock]
        Map<String, List<String[]>> byCategory = new LinkedHashMap<>();
        int total = 0, ok = 0, notFound = 0, failed = 0, ambiguous = 0;

        try (BufferedReader r = new BufferedReader(new FileReader(csvPath))) {
            String header = r.readLine(); // "target,category,note"
            String line;
            while ((line = r.readLine()) != null) {
                if (line.trim().isEmpty() || line.trim().startsWith("#")) continue;
                String[] f = splitCsvLine(line);
                if (f.length < 2) continue;
                String target = f[0].trim();
                String category = f[1].trim();
                String note = f.length > 2 ? f[2].trim() : "";
                total++;

                Function fn = null;
                String resolvedHow;

                if (target.startsWith("addr:")) {
                    String hex = target.substring(5).trim().replace("0x", "").replace("0X", "");
                    long rva = Long.parseUnsignedLong(hex, 16);
                    fn = fm.getFunctionContaining(toAddr(imageBase + rva));
                    resolvedHow = "by RVA";
                } else if (target.startsWith("search:")) {
                    String needle = target.substring(7).trim().toLowerCase();
                    int hits = 0;
                    FunctionIterator it = fm.getFunctions(true);
                    while (it.hasNext()) {
                        Function cand = it.next();
                        if (cand.getName().toLowerCase().contains(needle)) {
                            if (fn == null) fn = cand;
                            hits++;
                        }
                    }
                    if (hits > 1) ambiguous++;
                    resolvedHow = "by substring search (" + hits + " total matches, using first)";
                } else {
                    FunctionIterator it = fm.getFunctions(true);
                    while (it.hasNext()) {
                        Function cand = it.next();
                        if (cand.getName(true).equals(target) || cand.getName().equals(target)) { fn = cand; break; }
                    }
                    resolvedHow = "by exact name match";
                }

                StringBuilder block = new StringBuilder();
                if (fn == null) {
                    notFound++;
                    block.append("**NOT FOUND** (target `").append(target).append("`, ").append(resolvedHow).append(")\n");
                } else {
                    long ep = fn.getEntryPoint().getOffset();
                    String qname = fn.getName(true);
                    String rvaHex = "0x" + Long.toHexString(ep - imageBase);
                    block.append("### `").append(qname).append("` -- RVA `").append(rvaHex).append("`\n\n");
                    if (!note.isEmpty()) block.append(note).append("\n\n");
                    block.append("Resolved ").append(resolvedHow).append(" from target `").append(target).append("`.\n\n");
                    DecompileResults res = di.decompileFunction(fn, 45, monitor);
                    if (res != null && res.decompileCompleted()) {
                        block.append("```c\n").append(res.getDecompiledFunction().getC()).append("\n```\n");
                        ok++;
                    } else {
                        block.append("(decompile failed or timed out)\n");
                        failed++;
                    }
                }

                byCategory.computeIfAbsent(category, k -> new ArrayList<>())
                          .add(new String[]{ target, block.toString() });

                if (total % 10 == 0) {
                    println("... " + total + " processed (ok=" + ok + " notFound=" + notFound + " failed=" + failed + ")");
                }
            }
        }
        di.dispose();

        try (PrintWriter w = new PrintWriter(new FileWriter(outPath))) {
            w.println("# Decompile records");
            w.println();
            w.println("Auto-generated by `ReverseEngineering/Scripts/RecordFunctions.java` from");
            w.println("`" + csvPath.substring(csvPath.lastIndexOf('\\') + 1) + "` -- this is the raw evidence log (full decompiled C per");
            w.println("recorded function). Curated architecture docs (`README-*.md`) synthesize FROM this; this file");
            w.println("is the source of truth for \"what did the decompile actually say\", not a replacement for");
            w.println("reading it. Regenerated fresh every run -- add a row to the CSV and rerun to record a new");
            w.println("function, don't hand-edit this file.");
            w.println();
            w.println("**Stats:** " + total + " targets, " + ok + " decompiled, " + notFound + " not found, "
                       + failed + " decompile failures, " + ambiguous + " ambiguous substring searches.");
            w.println();
            for (Map.Entry<String, List<String[]>> e : byCategory.entrySet()) {
                w.println("## Category: " + e.getKey());
                w.println();
                for (String[] row : e.getValue()) {
                    w.println(row[1]);
                    w.println("---");
                    w.println();
                }
            }
        }
        println("DONE. total=" + total + " ok=" + ok + " notFound=" + notFound + " failed=" + failed
                + " ambiguous=" + ambiguous + " -> " + outPath);
    }
}
