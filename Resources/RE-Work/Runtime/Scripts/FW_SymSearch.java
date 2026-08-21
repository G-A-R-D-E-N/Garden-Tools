// Search ALL symbols (not just functions) for a substring -- finds Data/vtable symbols too.
// Usage: -postScript FW_SymSearch.java term1 [term2 ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;
import ghidra.program.model.symbol.SymbolIterator;

public class FW_SymSearch extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        SymbolTable st = currentProgram.getSymbolTable();
        long imageBase = currentProgram.getImageBase().getOffset();
        println("=== IMAGE BASE: 0x" + Long.toHexString(imageBase) + " ===");

        for (String term : args) {
            println("### SYMSEARCH: " + term);
            String lt = term.toLowerCase();
            int count = 0;
            SymbolIterator it = st.getAllSymbols(true);
            while (it.hasNext()) {
                Symbol s = it.next();
                String nm = s.getName();
                if (nm.toLowerCase().contains(lt)) {
                    long addr = s.getAddress().getOffset();
                    println("  " + nm + "  @ 0x" + Long.toHexString(addr)
                            + "  (RVA 0x" + Long.toHexString(addr - imageBase) + ")"
                            + "  type=" + s.getSymbolType()
                            + "  ns=" + s.getParentNamespace().getName());
                    if (++count >= 150) { println("  ... (truncated at 150)"); break; }
                }
            }
            println("### MATCHES(" + term + "): " + count);
        }
    }
}
