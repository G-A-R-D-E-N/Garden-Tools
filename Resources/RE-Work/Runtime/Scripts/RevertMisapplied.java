// Revert a small number of renames from ApplyConfirmedMisnames.java that turned out to be wrong:
// a post-apply cross-check against CommonLibF4's own hand-authored headers (the most authoritative
// source in this workspace) found that these addresses' ORIGINAL name was a real, exactly-matching
// member declared on that exact class (often with an explicit vtable-slot comment) -- meaning the
// decompile-only review's CONFIRMED_MISNAME verdict was wrong for these specific entries. See
// 07-DECOMPILE-CANDIDATES.md's "Reverted" section for the full writeup.
//
// Input CSV: address,revert_to_name  (2 columns only -- the "wrongly renamed to" signature text
// isn't needed and its embedded commas aren't worth a quote-aware parser for 13 rows)
// Usage: -postScript RevertMisapplied.java [csvPath]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.SourceType;

import java.io.BufferedReader;
import java.io.FileReader;

public class RevertMisapplied extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String csvPath = (args.length > 0 && !args[0].isEmpty())
            ? args[0] : "E:\\F4SE OG\\ReverseEngineering\\Organized\\misnames_to_revert_simple.csv";

        FunctionManager fm = currentProgram.getFunctionManager();
        int total = 0, reverted = 0, failed = 0;

        try (BufferedReader r = new BufferedReader(new FileReader(csvPath))) {
            String header = r.readLine();
            String line;
            while ((line = r.readLine()) != null) {
                if (line.isEmpty()) continue;
                String[] fields = line.split(",", -1);
                if (fields.length < 2) continue;
                String addrStr = fields[0].trim();
                String correctName = fields[1].trim();
                total++;

                long addrVal;
                try {
                    addrVal = Long.parseUnsignedLong(addrStr.replace("0x", "").replace("0X", ""), 16);
                } catch (Exception e) {
                    println("SKIP bad address: " + addrStr);
                    failed++;
                    continue;
                }

                Address addr = toAddr(addrVal);
                Function fn = fm.getFunctionContaining(addr);
                if (fn == null) {
                    println("SKIP no function at: " + addrStr);
                    failed++;
                    continue;
                }

                String currentName = fn.getName(true);
                try {
                    if (correctName.contains("::")) {
                        String nsPath = correctName.substring(0, correctName.lastIndexOf("::"));
                        String shortName = correctName.substring(correctName.lastIndexOf("::") + 2);
                        ghidra.program.model.symbol.Namespace ns = getNamespace(nsPath, currentProgram.getGlobalNamespace());
                        if (ns == null) {
                            ns = currentProgram.getSymbolTable().createNameSpace(
                                currentProgram.getGlobalNamespace(), nsPath, SourceType.USER_DEFINED);
                        }
                        fn.setParentNamespace(ns);
                        fn.setName(shortName, SourceType.USER_DEFINED);
                    } else {
                        fn.setName(correctName, SourceType.USER_DEFINED);
                    }
                    String finalName = fn.getName(true);
                    println(addrStr + ": " + currentName + " -> " + finalName
                        + (finalName.equals(correctName) ? "  [OK]" : "  [MISMATCH, expected " + correctName + "]"));
                    if (finalName.equals(correctName)) reverted++; else failed++;
                } catch (Exception e) {
                    println("FAILED " + addrStr + ": " + e.getMessage());
                    failed++;
                }
            }
        }

        println("=== REVERT RESULT ===");
        println("  total    : " + total);
        println("  reverted : " + reverted);
        println("  failed   : " + failed);
    }

    private ghidra.program.model.symbol.Namespace getNamespace(String path, ghidra.program.model.symbol.Namespace root) {
        ghidra.program.model.symbol.Namespace current = root;
        for (String part : path.split("::")) {
            if (part.isEmpty()) continue;
            ghidra.program.model.symbol.Namespace next = currentProgram.getSymbolTable().getNamespace(part, current);
            if (next == null) {
                try {
                    next = currentProgram.getSymbolTable().createNameSpace(current, part, SourceType.USER_DEFINED);
                } catch (Exception e) {
                    next = currentProgram.getSymbolTable().getNamespace(part, current);
                }
            }
            if (next == null) return null;
            current = next;
        }
        return current;
    }
}
