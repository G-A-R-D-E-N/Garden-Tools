// Diagnostic for the PDB-named FO4 1.10.155 project: how much of the binary did the PDB actually
// name? Covers both functions AND data/global symbols (the 1.10.163 pipeline's FW_Stats.java only
// covers functions, since that DB is CommonLib-RTTI-named, not PDB-named).
import ghidra.app.script.GhidraScript;
import ghidra.app.util.pdb.PdbProgramAttributes;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;

public class PDB_Stats extends GhidraScript {
    private static boolean isDefaultName(String n) {
        return n.startsWith("FUN_") || n.startsWith("sub_") || n.startsWith("DAT_")
            || n.startsWith("s_") || n.startsWith("u_") || n.startsWith("ptr_") || n.startsWith("PTR_")
            || n.startsWith("off_") || n.startsWith("ARRAY_") || n.startsWith("STRUCT_")
            || n.startsWith("LAB_") || n.startsWith("switchdata") || n.startsWith("caseD_")
            || n.startsWith("AddrTable") || n.startsWith("JTable");
    }

    @Override
    public void run() throws Exception {
        // --- PDB load status flag on the program itself ---
        PdbProgramAttributes attrs = new PdbProgramAttributes(currentProgram);
        println("=== PDB PROGRAM ATTRIBUTES ===");
        println("  isPdbLoaded()        : " + attrs.isPdbLoaded());
        println("  PDB symbol path      : " + attrs.getPdbFile());
        println("  PDB age              : " + attrs.getPdbAge());
        println("  PDB GUID             : " + attrs.getPdbGuid());
        println("  PDB version          : " + attrs.getPdbVersion());

        // --- Function name stats ---
        FunctionManager fm = currentProgram.getFunctionManager();
        long fnTotal = 0, fnDefault = 0, fnNamed = 0, fnScoped = 0;
        int sample = 0;
        StringBuilder samples = new StringBuilder();
        FunctionIterator fit = fm.getFunctions(true);
        while (fit.hasNext()) {
            Function f = fit.next();
            fnTotal++;
            String n = f.getName();
            String full = f.getName(true);
            if (isDefaultName(n)) { fnDefault++; continue; }
            fnNamed++;
            if (full.contains("::")) fnScoped++;
            if (sample < 40) { samples.append("    ").append(full).append("\n"); sample++; }
        }
        println("=== FUNCTION NAME STATS ===");
        println("  total functions        : " + fnTotal);
        println("  FUN_/sub_ (unnamed)     : " + fnDefault);
        println("  named (PDB-resolved)    : " + fnNamed);
        println("  scoped (Class::Method)  : " + fnScoped);
        println("=== SAMPLE NAMED FUNCTIONS (up to 40) ===");
        println(samples.toString());

        // --- Data/global symbol stats ---
        long dataTotal = 0, dataDefault = 0, dataNamed = 0;
        StringBuilder dataSamples = new StringBuilder();
        int dsample = 0;
        DataIterator dit = currentProgram.getListing().getDefinedData(true);
        while (dit.hasNext()) {
            Data d = dit.next();
            dataTotal++;
            Symbol s = d.getPrimarySymbol();
            String n = s != null ? s.getName() : "";
            if (n.isEmpty() || isDefaultName(n)) { dataDefault++; continue; }
            dataNamed++;
            if (dsample < 40) { dataSamples.append("    ").append(n)
                    .append("  @ ").append(d.getAddress())
                    .append("  :: ").append(d.getDataType().getName()).append("\n"); dsample++; }
        }
        println("=== DEFINED DATA / GLOBAL SYMBOL STATS ===");
        println("  total defined data items : " + dataTotal);
        println("  default-named (DAT_/etc) : " + dataDefault);
        println("  PDB-named                : " + dataNamed);
        println("=== SAMPLE NAMED DATA (up to 40) ===");
        println(dataSamples.toString());

        // --- Whole symbol table breakdown by type ---
        // Note: SymbolType is not a Java enum (no values()) - it's a class of static instances,
        // so tally by its toString() in a single pass instead of iterating once per type.
        SymbolTable st = currentProgram.getSymbolTable();
        java.util.Map<String, Long> typeCounts = new java.util.TreeMap<>();
        SymbolIterator allSyms = st.getSymbolIterator();
        while (allSyms.hasNext()) {
            Symbol s = allSyms.next();
            String t = s.getSymbolType().toString();
            typeCounts.merge(t, 1L, Long::sum);
        }
        println("=== SYMBOL TABLE — COUNTS BY TYPE ===");
        for (java.util.Map.Entry<String, Long> e : typeCounts.entrySet()) {
            println("  " + e.getKey() + " : " + e.getValue());
        }

        // --- Keyword sampling, mirrors FW_Stats.java's keyword scan ---
        String[] keys = {"PlayerCharacter", "TESForm", "BGSKeyword", "NiNode", "BSTArray",
                          "ScriptCompiler", "NavMesh", "Papyrus", "bhkWorld", "RTTI_", "vftable"};
        println("=== SYMBOL-TABLE KEYWORD COUNTS ===");
        for (String k : keys) {
            int c = 0; String first = "";
            SymbolIterator si = st.getSymbolIterator();
            while (si.hasNext()) {
                Symbol s = si.next();
                if (s.getName().contains(k)) { if (c == 0) first = s.getName() + " @ " + s.getAddress(); c++; }
            }
            println("  " + k + " : " + c + (c > 0 ? ("   (first: " + first + ")") : ""));
        }
    }
}
