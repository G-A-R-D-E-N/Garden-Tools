// Batch-decompile the 313 "genuine misname candidate" functions flagged by organize_functions.py
// (ReverseEngineering\Organized\crossref_review_candidates.csv) in ONE headless run, instead of
// 313 separate analyzeHeadless invocations (each pays ~30-60s of JVM+project-load overhead --
// batching this into one run is the difference between minutes and hours).
//
// For each row (address, existing_name, decoded_name-claim), writes
// ReverseEngineering\Decompiles\<addr>.txt containing: the address, the CSV's claimed
// existing/decoded names, Ghidra's OWN current name+signature for whatever function actually
// contains that address (a sanity check -- confirms the address still resolves to what the CSV
// thinks it does), and the full decompiled C. A short 25s-per-function timeout bounds the total
// worst case; most of these are small leaf/thunk functions and complete in well under a second.
//
// Usage: -postScript DecompileCandidates.java [csvPath] [outDir]
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.FileReader;
import java.io.FileWriter;
import java.util.ArrayList;
import java.util.List;

public class DecompileCandidates extends GhidraScript {

    // Minimal CSV line splitter: handles the one quoted-field-with-commas case this file has
    // (decoded_name sometimes contains commas inside a quoted field, RFC4180-ish).
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
            : "E:\\F4SE OG\\ReverseEngineering\\Organized\\crossref_review_candidates.csv";
        String outDir = (args.length > 1 && !args[1].isEmpty())
            ? args[1]
            : "E:\\F4SE OG\\ReverseEngineering\\Decompiles";

        FunctionManager fm = currentProgram.getFunctionManager();
        long imageBase = currentProgram.getImageBase().getOffset();

        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);

        int total = 0, decompiled = 0, failed = 0, noFunc = 0;

        try (BufferedReader r = new BufferedReader(new FileReader(csvPath))) {
            String header = r.readLine(); // skip "address,existing_name,decoded_name"
            String line;
            while ((line = r.readLine()) != null) {
                if (line.isEmpty()) continue;
                String[] fields = splitCsvLine(line);
                if (fields.length < 3) continue;
                String addrStr = fields[0].trim();
                String existingClaim = fields[1];
                String decodedClaim = fields[2];
                total++;

                long addrVal;
                try {
                    addrVal = Long.parseUnsignedLong(addrStr.replace("0x", "").replace("0X", ""), 16);
                } catch (Exception e) {
                    println("SKIP (bad address): " + addrStr);
                    continue;
                }

                Address addr = toAddr(addrVal);
                Function f = fm.getFunctionContaining(addr);

                String outPath = outDir + "\\" + addrStr + ".txt";
                try (BufferedWriter w = new BufferedWriter(new FileWriter(outPath))) {
                    w.write("ADDRESS: " + addrStr + " (RVA 0x" + Long.toHexString(addrVal - imageBase) + ")\n");
                    w.write("CLAIMED existing (CommonLib) name : " + existingClaim + "\n");
                    w.write("CLAIMED decoded (155-confirmed) name: " + decodedClaim + "\n");

                    if (f == null) {
                        w.write("\n### NO FUNCTION CONTAINS THIS ADDRESS (may be data, or address stale) ###\n");
                        noFunc++;
                        continue;
                    }

                    long ep = f.getEntryPoint().getOffset();
                    w.write("ACTUAL current Ghidra name       : " + f.getName(true) + "\n");
                    w.write("ACTUAL function entry            : 0x" + Long.toHexString(ep)
                            + " (RVA 0x" + Long.toHexString(ep - imageBase) + ")\n");
                    try {
                        w.write("ACTUAL signature                 : " + f.getSignature().getPrototypeString() + "\n");
                    } catch (Exception e) { /* best effort */ }
                    w.write("\n--- DECOMPILED C ---\n");

                    DecompileResults res = di.decompileFunction(f, 25, monitor);
                    if (res != null && res.decompileCompleted()) {
                        w.write(res.getDecompiledFunction().getC());
                        decompiled++;
                    } else {
                        w.write("(decompile failed or timed out)\n");
                        failed++;
                    }
                }

                if (total % 25 == 0) {
                    println("... processed " + total + "/313 (decompiled=" + decompiled
                            + " failed=" + failed + " noFunc=" + noFunc + ")");
                }
            }
        }

        di.dispose();
        println("DONE. total=" + total + " decompiled=" + decompiled + " failed=" + failed + " noFunc=" + noFunc);
    }
}
