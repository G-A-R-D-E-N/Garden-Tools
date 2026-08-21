// Diagnostic: is this program CommonLib-named or just auto-analyzed?
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;

public class FW_Stats extends GhidraScript {
    @Override
    public void run() throws Exception {
        FunctionManager fm = currentProgram.getFunctionManager();
        long total = 0, funPrefixed = 0, scoped = 0, named = 0;
        int sample = 0;
        StringBuilder samples = new StringBuilder();
        FunctionIterator it = fm.getFunctions(true);
        while (it.hasNext()) {
            Function f = it.next();
            total++;
            String n = f.getName();
            String full = f.getName(true);
            if (n.startsWith("FUN_") || n.startsWith("sub_")) { funPrefixed++; continue; }
            named++;
            if (full.contains("::")) scoped++;
            if (sample < 40) { samples.append("    ").append(full).append("\n"); sample++; }
        }
        println("=== FUNCTION NAME STATS ===");
        println("  total functions      : " + total);
        println("  FUN_/sub_ (unnamed)  : " + funPrefixed);
        println("  named (non-FUN_)     : " + named);
        println("  scoped (Class::Meth) : " + scoped);
        println("=== SAMPLE NAMED FUNCTIONS (up to 40) ===");
        println(samples.toString());

        // Symbol table scan for our keywords (vtables/RTTI/labels live here, not just functions)
        String[] keys = {"PlayerControls", "Activate", "Compass", "Pipboy", "PlayerCharacter", "VTABLE_", "RTTI_"};
        SymbolTable st = currentProgram.getSymbolTable();
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
