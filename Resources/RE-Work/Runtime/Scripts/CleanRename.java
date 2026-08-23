// Force a function to a fully-qualified name, clearing any pre-existing colliding symbols at the
// same address first (e.g. a stray Label left by an earlier data-path apply that setName() would
// otherwise refuse to duplicate). Input CSV: address,full_qualified_name (2 cols).
// Usage: -postScript CleanRename.java [csvPath]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;

import java.io.BufferedReader;
import java.io.FileReader;

public class CleanRename extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String csvPath = (args.length > 0 && !args[0].isEmpty())
            ? args[0] : "E:\\F4SE OG\\ReverseEngineering\\Organized\\inconclusive_resolved_rename.csv";

        FunctionManager fm = currentProgram.getFunctionManager();
        SymbolTable st = currentProgram.getSymbolTable();
        int total = 0, ok = 0, fail = 0;

        try (BufferedReader r = new BufferedReader(new FileReader(csvPath))) {
            r.readLine(); // header
            String line;
            while ((line = r.readLine()) != null) {
                if (line.isEmpty()) continue;
                String[] f = line.split(",", -1);
                if (f.length < 2) continue;
                String addrStr = f[0].trim();
                String qname = f[1].trim();
                total++;

                long v;
                try { v = Long.parseUnsignedLong(addrStr.replace("0x", "").replace("0X", ""), 16); }
                catch (Exception e) { println("SKIP bad addr " + addrStr); fail++; continue; }
                Address addr = toAddr(v);
                Function fn = fm.getFunctionContaining(addr);
                if (fn == null) { println("SKIP no fn " + addrStr); fail++; continue; }

                String shortName = qname.contains("::") ? qname.substring(qname.lastIndexOf("::") + 2) : qname;
                String nsPath = qname.contains("::") ? qname.substring(0, qname.lastIndexOf("::")) : "";

                // Delete any symbol at the function's entry that isn't the function symbol itself --
                // clears stray labels (from an earlier data-path apply) that would collide with setName.
                Address ep = fn.getEntryPoint();
                for (Symbol s : st.getSymbols(ep)) {
                    if (s.getSource() == SourceType.DEFAULT) continue;
                    if (s == fn.getSymbol()) continue;
                    try { s.delete(); } catch (Exception e) { }
                }

                try {
                    if (!nsPath.isEmpty()) {
                        ghidra.program.model.symbol.Namespace ns = getNamespace(nsPath);
                        fn.setParentNamespace(ns);
                    }
                    fn.setName(shortName, SourceType.USER_DEFINED);
                    String finalName = fn.getName(true);
                    if (finalName.equals(qname)) { ok++; }
                    else { println("  " + addrStr + " -> " + finalName + " (wanted " + qname + ")"); ok++; }
                } catch (Exception e) {
                    println("FAILED " + addrStr + ": " + e.getMessage());
                    fail++;
                }
            }
        }
        println("=== CLEAN RENAME RESULT ===  total=" + total + " ok=" + ok + " fail=" + fail);
    }

    private ghidra.program.model.symbol.Namespace getNamespace(String path) {
        ghidra.program.model.symbol.Namespace current = currentProgram.getGlobalNamespace();
        for (String part : path.split("::")) {
            if (part.isEmpty()) continue;
            ghidra.program.model.symbol.Namespace next = currentProgram.getSymbolTable().getNamespace(part, current);
            if (next == null) {
                try { next = currentProgram.getSymbolTable().createNameSpace(current, part, SourceType.USER_DEFINED); }
                catch (Exception e) { next = currentProgram.getSymbolTable().getNamespace(part, current); }
            }
            if (next == null) return currentProgram.getGlobalNamespace();
            current = next;
        }
        return current;
    }
}
