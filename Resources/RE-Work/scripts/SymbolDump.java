// Dump symbols whose name contains the given substring (case-insensitive).
// usage: SymbolDump.java RTTI
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;

public class SymbolDump extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        SymbolTable st = currentProgram.getSymbolTable();
        long base = currentProgram.getImageBase().getOffset();
        String want = args.length > 0 ? args[0].toLowerCase() : "rtti";
        int max = args.length > 1 ? Integer.parseInt(args[1]) : 60;
        int n = 0;
        SymbolIterator it = st.getAllSymbols(true);
        while (it.hasNext()) {
            Symbol s = it.next();
            String nm = s.getName();
            if (nm.toLowerCase().contains(want)) {
                long rva = s.getAddress().getOffset() - base;
                println("SYM 0x" + Long.toHexString(rva) + " " + nm + " @" + s.getParentNamespace().getName());
                if (++n >= max) { println("(truncated at " + max + ")"); break; }
            }
        }
        println("total shown: " + n);
    }
}
